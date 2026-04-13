# Phase 9E-1 / 9E-2 Audit Findings

**Date:** 2026-04-13
**Scope:** commits 4799b66 (9E-1) and 4928acb (9E-2) — visual scripting subsystem (~7,152 LOC)
**Method:** 5-tier audit per AUDIT_STANDARDS.md (automated tool + 6 parallel subagents on changed files)

---

## Summary

| Severity | Count |
|----------|-------|
| Critical | 1 |
| High     | 9 |
| Medium   | 14 |
| Low      | 6 |

**Tier 1 automated:** build clean (0 warnings), 1695/1695 tests pass, 0 tool-detected critical findings in scripting/.

All semantic findings below come from the Tier 4 subagent sweep (A=bugs, B=memory, C=perf, D=quality, E=security, F=docs/tests). Each has been cross-checked by reading the cited lines.

---

## Critical Findings

### C1. `script_graph.cpp:313-335` — Unbounded graph deserialization (DoS)
`ScriptGraph::loadFromFile()` wraps `fromJson()` in try/catch for JSON errors, but `fromJson()` imposes no caps on `nodes.size()`, `connections.size()`, `variables.size()`, or individual string lengths. A crafted `.vscript` file with 10M nodes exhausts memory. **Tier:** 4E. **Fix:** cap nodes ≤ 10,000, connections ≤ 100,000, variable/name strings ≤ 256 bytes; reject and log if exceeded. **Complexity:** small.

---

## High Findings

### H1. `scripting_system.cpp:31, 277` — EventBus lambda captures `ScriptInstance&` by reference
`subscribeOneEventNode<T>` and the `EntityDestroyedEvent` handler capture `&instance` by reference in long-lived subscriptions. If a `ScriptInstance` is destroyed without `unregisterInstance()` being called first, the stored lambda holds a dangling reference — next event fire → UAF. Current registered paths (`onSceneUnload`, explicit `unregisterInstance`) unsubscribe correctly, but the invariant is undefended by the type system. **Tiers:** 4A, 4B. **Fix:** capture by `ScriptInstance*` (pointer), look up liveness via `m_activeInstances` lookup in lambda body. Apply same pattern to `engine`/`registry` captures for defense-in-depth. **Complexity:** medium.

### H2. `script_instance.h:120` — `m_graph` raw pointer could dangle if graph asset unloaded
`ScriptInstance::m_graph` is `const ScriptGraph*` pointing to the asset. No guarantee the graph outlives instances referencing it. **Tier:** 4B. **Fix:** either (a) document caller obligation to destroy instances before their graph, (b) share ownership via `shared_ptr<const ScriptGraph>`, or (c) store graph by ID and resolve at use. **Complexity:** medium.

### H3. `scripting_system.cpp:334` — `findNodesByType("OnUpdate")` is O(N) per frame per instance
Every frame, for every active instance, iterates all nodes to find OnUpdates. At 50 entities × 10 OnUpdate nodes each = 500+ scans/frame. **Tier:** 4C. **Fix:** cache OnUpdate node IDs per instance at `subscribeEventNodes()` / `initialize()` time. **Complexity:** small.

### H4. `script_context.cpp:272-293` — `findOutputConnection` / `findInputConnection` linear-scan connections every read
Called on every execution trigger and every data-pin read. Inside a 100-iteration loop reading 5 pins = 500 linear scans. **Tier:** 4C. **Fix:** build a `(nodeId, pinName) → connection*` index map at graph compile / context construction. **Complexity:** small.

### H5. `scripting_system.cpp:407-415` — O(N²) latent action removal per frame
Collects completed actions then does `std::find_if + erase` for each, creating quadratic behavior with many pending actions. **Tier:** 4C. **Fix:** single-pass partition (`std::remove_if` with predicate), or swap-and-pop. **Complexity:** trivial.

### H6. Unbounded `ScriptValue::fromJson` called outside try/catch
`script_graph.cpp:274` calls `ScriptValue::fromJson(val)` during `fromJson(graph)` — the `val[0]..val[3]` array accesses (`script_value.cpp:304-314`) throw `nlohmann::json::out_of_range` on short arrays. Only caught at the outermost `loadFromFile()`; any direct caller of `fromJson` on a sub-object is unprotected. **Tier:** 4E. **Fix:** size-check inside `ScriptValue::fromJson` before indexing; return `ScriptValue(0.0f)` or a validation error on mismatch. **Complexity:** small.

### H7. `ScriptGraph::fromJson` does not call `validate()` on load
Dangling connections (`targetNode` references a non-existent node id) silently load, and only fail opaquely at execution. **Tier:** 4E. **Fix:** call `validate()` after `fromJson()`; on failure, log and return an empty graph (or surface the error to the caller). **Complexity:** trivial.

### H8. NaN / Inf propagation in pure math nodes
`CompareEqual` with NaN yields IEEE-correct `false` (correct) but comparison nodes don't sanitize; `VectorNormalize` with zero-length input returns `(0,0,0)` silently; `MathDiv` already guards divide-by-zero but does not mark the value as invalid. **Tier:** 4E. **Fix:** add `std::isfinite` guards at node boundaries; convert NaN/Inf results to `0.0f` with a debug warning. **Complexity:** small.

### H9. Test coverage gaps — 45+ node types have no dedicated tests
Event nodes (OnSceneLoaded, OnWeatherChanged, OnKeyReleased, OnMouseButton, OnCollision/Trigger*, OnAudioFinished, OnVariableChanged), action nodes (almost all 14), several pure nodes (GetPosition, GetRotation, FindEntityByName, MathMul, DotProduct, CrossProduct, Raycast, HasVariable) — only registration is exercised. Also no tests for re-entrancy, Blackboard Entity/Graph/Global scope precedence, serialization edge cases (empty/single-node/malformed), or pure-node re-evaluation inside loops. **Tier:** 4F. **Fix:** add targeted unit tests per node category and the four missing interpreter-level tests. **Complexity:** large.

---

## Medium Findings

### M1. `scripting_system.cpp:245-270` — OnCustomEvent filter is leaky
Lambda sets `_filtered = true` on mismatch but still returns, then `subscribeOneEventNode` unconditionally calls `triggerOutput` afterwards. Comment at line 268 explicitly flags the design issue. **Fix:** thread the filter decision through a return value so the trigger is suppressed.

### M2. `flow_nodes.cpp:148` — ForLoop count uses int32 arithmetic (UB on boundary inputs)
`count = last - first + 1` is signed-int UB at `first = INT32_MIN, last = INT32_MAX`. In practice wraps to 0 (loop no-ops silently). **Fix:** compute in `int64_t`, clamp to `[0, MAX_FOR_ITERATIONS]`.

### M3. `flow_nodes.cpp:152` — ForLoop silently clamps iteration count
Designer sees a warning log but no runtime indication the loop was truncated. **Fix:** set a "Clamped" output pin or publish a `ScriptError` event.

### M4. `latent_nodes.cpp:388-392` — Timeline progress div-by-zero fragile
Guard checks `totalDuration > 0.0f`, but a NaN-valued `remainingTime` would still produce NaN progress. **Fix:** `std::clamp(std::isfinite(p) ? p : 0.0f, 0.0f, 1.0f)`.

### M5. `flow_nodes.cpp:314` — FlipFlop state edge case on first call
Default-`true` branch before toggle is correct, but state mutation path is fragile if `runtimeState` is cleared externally. **Fix:** make `nextIsA` default explicit and guard.

### M6. `script_context.cpp:212, 223` — scheduleDelay accepts negative / huge seconds
No validation. Malicious script can schedule 1e30 seconds (effectively forever-pending). **Fix:** clamp to `[0.0f, 3600.0f]` and log if clamped.

### M7. `script_graph.cpp:318` — `loadFromFile` path traversal not validated
Opens user-supplied path directly. **Fix:** resolve against assets root via `std::filesystem::canonical()`; reject if outside.

### M8. `blackboard.cpp:68-70` — No per-scope variable count cap
Unbounded `m_values.emplace(key, ...)`. **Fix:** cap at e.g. 1024 keys per scope.

### M9. `script_instance.cpp:60-71` — `findNodesByType` allocates `std::vector<uint32_t>` per call
Called in `tickUpdateNodes` after H3 fix still runs once per instance per call. **Fix (after H3):** cache ids; obviated if H3 is fixed with pre-cached ids.

### M10. `script_context.cpp:59, 93` — String-keyed `properties` / `outputValues` maps on hot path
Per pin read / write. **Fix (follow-on):** intern pin names to small integer IDs; defer to 9E-3 if invasive.

### M11. Pure node output caching scope
Pure nodes re-evaluate on each read; inside a large loop body this multiplies. Design document calls this out, but there's no per-execution memoization. **Fix:** cache pure outputs for duration of one `executeNode` chain.

### M12. `ScriptValue` single-arg ctors missing `explicit`
Eight ctors (bool/int32/float/string/vec2/vec3/vec4/quat) permit implicit conversions. Violates CODING_STANDARDS §7. **Fix:** add `explicit` to all single-arg ctors.

### M13. Public API missing Doxygen `@brief`
~18 public methods in `script_value.h`, `blackboard.h`, and others lack `@brief`. **Fix:** add brief tags.

### M14. `docs/PHASE9E_DESIGN.md` status says "Proposal", ROADMAP.md marks 9E-1/2 complete
Drift. **Fix:** set status line to "Implemented (9E-1, 9E-2)".

---

## Low Findings

### L1. `pure_nodes.cpp:163` — MathDiv silently returns 0 on div-by-zero (no warning).
### L2. `script_context.cpp:116-123` — Call-depth counter accumulates across latent re-triggers (false-positive risk for MAX_NODES_PER_CHAIN over long timelines).
### L3. Cognitive complexity 29 on `registerActionNodeTypes` — boilerplate lambda pattern. Can be factored with helper builders post-9E-3.
### L4. Three large `switch` chains in `script_value.cpp` (11 cases each) for `ScriptDataType` dispatch — expected for variant type, flag for awareness only.
### L5. `script_context.h:109-110` — `MAX_CALL_DEPTH = 256` and `MAX_NODES_PER_CHAIN = 1000` magic numbers lack inline justification.
### L6. `flow_nodes.cpp:254` TODO scoped to Phase 9E-3 (entry-pin multiplexing) — acceptable for now.

---

## ARCHITECTURE.md gap (not severity-tagged — documentation debt)

The 538-line ARCHITECTURE.md never mentions ScriptingSystem. Subsystem section stops at ResourceManager. Add a "Scripting System" section describing ISystem integration, node-type registry, and EventBus bridge pattern.

---

## Findings Downgraded / Closed After Verification

- **Subagent A/B flagged latent-action lambda captures of `ScriptInstance*` as Critical.** Verified safe: latent actions are stored inside the instance's `pendingActions()`; they cannot outlive the instance. Retained as Low-risk defensive note.
- **Subagent E flagged `script_value.cpp:304-314` Vec/Quat access as Critical.** `nlohmann::json::operator[]` throws on out-of-range, caught by `loadFromFile`. Downgraded to H6 (still a hazard for direct `fromJson` callers).
- **Subagent E flagged `flow_nodes.cpp:148` ForLoop overflow as Critical.** UB in theory, but wraps to 0-iteration loop in practice. Downgraded to M2.

---

## Experimental Feature Opportunities (Tier 5)

Tier 5 research queries hit cache but returned mostly off-topic results. Useful follow-ups for 9E-3:
- imgui-node-editor v0.9.1+ (ocornut/imgui-node-editor) — current recommended lib.
- Blueprint VM memoization strategies (Unreal) — informs M11 pure-node cache.
- Bytecode compilation for release builds (deferred to future phase per design §14).

---

## Proposed Fix Plan (for approval before implementation)

Ordered by severity, then by files-touched (big sweeps first):

**Batch 1 — Security & Safety (Critical + safety-critical High)**
- C1: cap graph deserialization sizes
- H6: size-check inside `ScriptValue::fromJson`
- H7: call `validate()` after `fromJson()`
- H1: switch EventBus lambda captures to pointer + liveness lookup
- H8: NaN/Inf guards in pure math nodes
- M7: path traversal check in `loadFromFile`
- M6: clamp `scheduleDelay` seconds
- M8: blackboard per-scope key cap
- M2: `int64_t` for ForLoop count

**Batch 2 — Performance (remaining High)**
- H3: cache OnUpdate node IDs per instance
- H4: build connection index map per context
- H5: single-pass latent action removal

**Batch 3 — Code Quality & Docs**
- M12: `explicit` on ScriptValue single-arg ctors
- M13: Doxygen `@brief` on public APIs
- M14: design doc status line
- ARCHITECTURE.md: add ScriptingSystem section
- H2: document `m_graph` lifetime obligation (or share-ownership)
- M1: fix OnCustomEvent filter leakage
- M3/M4/M5: targeted small fixes

**Batch 4 — Test Coverage (High, largest)**
- H9: tests for 45+ node types, re-entrancy, scope resolution, serialization edge cases

**Batch 5 — Lows (resolved in this audit cycle)**
- L1 (MathDiv warning), L2 (false-positive, documented), L3 (transform-setter helper),
  L4 (switch-chain design note), L5 (MAX_CALL_DEPTH / MAX_NODES_PER_CHAIN justification),
  L6 (Gate TODO retagged to PHASE9E3_DESIGN.md).

**Carried into Phase 9E-3 scope (mandatory, not optional)**
- **M9**: generalize `findNodesByType` caching (same pattern as the OnUpdate
  cache applied in H3, but for the broader type→IDs lookup). Small.
- **M10**: pin-name interning (string-keyed hot-path lookups). Requires a
  pass through every node-registration site; best done when 9E-3's UI
  rework is already touching node definitions.
- **M11**: per-execution pure-node output memoization (the pure-node
  re-evaluation pattern documented in the design doc).

These three MUST land before 9E-3 is considered complete and are enumerated
in the Phase 9E-3 design document as acceptance criteria.

After each batch: compile + `ctest` + verify. Do not proceed if regressions appear.

---

## Resolution Status (2026-04-13)

| Batch | Finding count | Status |
|-------|---------------|--------|
| 1 — Security & safety | C1 + 4 High + 4 Medium | ✓ Complete |
| 2 — Performance | 3 High | ✓ Complete |
| 3 — Quality & docs | H2 + 7 Medium | ✓ Complete (M5 verified false positive) |
| 4 — Test coverage | H9 | ✓ Complete (35 new tests; exposed + fixed a 5th Critical re-entrancy bug not in original report) |
| 5 — Lows | 6 Low | ✓ Complete (L2/L4 verified false positives / intentional) |
| 9E-3 scope | M9, M10, M11 | ⏳ Committed as acceptance criteria |

Audit cycle concludes with all reported findings resolved or explicitly
tracked into the next phase's scope. Build clean; 1730/1730 tests pass.
