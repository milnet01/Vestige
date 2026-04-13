# AUDIT — Phase 2 (2026-04-13)

**Baseline:** commit `01079ac` + uncommitted Phase 9E-3 Step 4 WIP.
**Scope:** Entire tree minus vendored (`external/glad|stb|dr_libs`), `build/`, `.git/`, caches, worktrees.
**Method:** Manual pass. Spot-verified the 4 pre-existing cppcheck HIGHs by hand; dispatched 4 parallel semantic-audit subagents against scripting, formula + workbench, renderer + shaders, and audit-tool Python; did cross-cutting greps myself for TODO/FIXME, threading, git history, repo hygiene, editor Step-4 WIP.

---

## Severity summary

| Severity | Count | Notes |
|---|---:|---|
| **Critical** | 2 | Both in audit tool web surface |
| **High** | 16 | 4 audit tool, 5 scripting, 4 formula/workbench, 2 renderer, 1 known Step-4 SEGV |
| **Medium** | 27 | Split across scripting, workbench, renderer, audit tool, build/CI gaps |
| **Low** | 9 | Minor |
| **Known false-positives** | 4 | Already inline-suppressed; tool config is the bug (see AT-A1) |

Build + tests baseline confirmed clean from last automated run (0 warnings, 1695 tests passing; current tree has 1738 after Step-3 additions — haven't re-run but no structural reason to expect regressions).

---

## Critical

### C1 — Command injection via `run_cmd(shell=True)` chain, reachable from web UI
- **Location:** `tools/audit/lib/utils.py:13-30` (default `shell=True`), plus every call site that builds the command string with f-strings from YAML-controlled values.
- **Evidence:**
  - `tools/audit/lib/tier1_cppcheck.py:30` — `f"{binary} --xml --xml-version=2 {args} {targets}"`
  - `tools/audit/lib/tier1_clangtidy.py:34,95,97` — cmake + clang-tidy commands concatenated from config
  - `tools/audit/lib/tier3_changes.py:54,64,153,188` — `f"git diff … {base_ref}"` where `base_ref` comes directly from `POST /api/run` body via `app.py:60`.
- **Impact:** A malicious `audit_config.yaml` (or a `base_ref` payload on the localhost web UI) executes arbitrary shell. Any browser-based CSRF or open tab on 127.0.0.1:5800 can reach `/api/run` and exfiltrate/trash user data.
- **Proposed fix:** Default `run_cmd` to `shell=False`, accept `list[str]`; where a real shell is needed (user-authored `build_cmd: "cmake --build build 2>&1"`, `test_cmd: "cd build && ctest"`) gate behind explicit `shell: true` per-command and refactor stray `cd`/redirection out. Add `re.fullmatch(r"[A-Za-z0-9._/~^-]{1,64}", base_ref)` validation at the `/api/run` boundary.
- **Verification:** set `build.build_dir: "build; touch /tmp/pwn"` in a test config, run Tier 1, confirm `/tmp/pwn` is NOT created. Set `base_ref: "HEAD; touch /tmp/pwn2"` via `/api/run` body, confirm nothing happens.
- **Risk of fix:** Moderate. Every shell-using caller must be updated; `cd`-into-build patterns must become `cwd=`; output redirection must move to Python file handling. Has to ship atomically to avoid partial-refactor regressions.

### C2 — NVD API key committed in plaintext
- **Location:** `tools/audit/audit_config.yaml:272`.
- **Evidence:**
  ```yaml
  api_key: "<REDACTED-rotated-2026-04-13>"  # Pending activation (up to 7 days from 2026-04-11)
  ```
- **Impact:** Key is in git history permanently (even if removed now). Rotate before push. Memory notes the key is "pending activation" — treat as active-once-pushed regardless.
- **Proposed fix:** Remove literal; keep `api_key_env: "NVD_API_KEY"` path only. Loader warns when `api_key` is a non-null literal. Rotate the key at NVD.
- **Verification:** `git grep bcb87fe8` → empty (current working tree); `NVD_API_KEY=… python3 audit.py -t 5` still works.
- **Risk of fix:** Low. One-line scrub + rotation. Must happen before any public push.

---

## High

### Audit tool web surface

### H1 — `GET /api/report?path=…` has no containment check
- **Location:** `tools/audit/web/app.py:183-196`
- **Evidence:** `custom = request.args.get("path"); report_path = Path(custom); return Response(report_path.read_text(), …)`. Sibling `api_report_file` DOES validate via `relative_to`; this one does not.
- **Impact:** Arbitrary file read (`/etc/passwd`, SSH keys, git creds) by any browser that can hit 127.0.0.1:5800.
- **Fix:** Mirror `api_report_file`'s allowed-roots check.
- **Verify:** `curl 'http://127.0.0.1:5800/api/report?path=/etc/passwd'` → 403.

### H2 — `GET /api/config?path=…` has no containment check
- **Location:** `tools/audit/web/app.py:199-208`
- **Evidence:** Same pattern as H1 — PUT handler added containment in 2.0.0 but GET was missed.
- **Impact:** Same as H1.
- **Fix:** Apply the same `allowed_roots` check as PUT (lines 220-226).

### H3 — `POST /api/init` allows arbitrary file overwrite
- **Location:** `tools/audit/web/app.py:118-133`
- **Evidence:** `output_path` from request JSON is `Path(...)` and written to without containment.
- **Impact:** Overwrite `~/.ssh/authorized_keys`, crontab, `.bashrc` — anywhere the server process can write.
- **Fix:** Validate inside allowed roots AND require `.yaml`/`.yml` suffix.

### H4 — NVD `api_key` header injection via CRLF
- **Location:** `tools/audit/lib/tier5_nvd.py:52-60`
- **Evidence:** `req.add_header("apiKey", api_key)` with no validation. A value containing `\r\n` would CRLF-inject response headers.
- **Impact:** Low-likelihood but real HTTP request smuggling on self-hosted proxies.
- **Fix:** `re.fullmatch(r"[A-Za-z0-9-]{16,64}", key)` before use.

### Scripting / engine

### H5 — `reinterpret_cast<uintptr_t>(&m_engine) == 0` is UB, optimizable to `false`
- **Location:** `engine/scripting/script_context.cpp:293-298`, repeated in `action_nodes.cpp:98,472`, `pure_nodes.cpp:479`.
- **Evidence:**
  ```cpp
  if (reinterpret_cast<uintptr_t>(&m_engine) == 0) return nullptr;
  return m_engine.getSceneManager().getActiveScene();
  ```
  `m_engine` is `Engine&` (reference). Taking its address can never legally be 0; the standard allows the compiler to fold this branch to `false` under `-O2`.
- **Impact:** Tests (and any path) that construct `ScriptContext` with a null engine reference rely on this guard. Release build will segfault in those paths.
- **Fix:** Change `Engine& m_engine` to `Engine*` (nullable), update call sites; OR require tests to provide a stub engine and remove the guards.
- **Verify:** compile with `-O2`; inspect disassembly for the 4 sites; confirm branches are preserved after refactor.

### H6 — `Blackboard::fromJson` bypasses `MAX_KEYS` cap
- **Location:** `engine/scripting/blackboard.cpp:124-135`
- **Evidence:** `fromJson` writes directly into `bb.m_values[key] = value` instead of routing through `set()`, which enforces the 1024-key cap and length limits. Header docstring at `blackboard.h:86` claims the cap applies — **API-contract violation**.
- **Impact:** A crafted save-file or embedded-graph JSON inserts unbounded keys; defeats the memory-bound commitment from prior 9E audit (C1).
- **Fix:** Route every insert through `set()`, or mirror the caps inside `fromJson`; `clampString(key, MAX_STRING_BYTES)`.
- **Verify:** test feeding JSON with 2000 keys / 1MB key names → assert `bb.size() <= MAX_KEYS`.

### H7 — `m_pureCache` memoizes mutable-state reads (`GetVariable`, `FindEntityByName`)
- **Location:** `engine/scripting/script_context.cpp:346-347` cache; pure-node callers in `pure_nodes.cpp`.
- **Evidence:** Cache key is `(nodeId << 32) | pinId` — no inputs. `GetVariable("X")` is classified as pure, but its result depends on the blackboard state at call-time. After `SetVariable("X", new)` in the same chain, subsequent `GetVariable("X")` returns the **cached first value**.
- **Impact:** Silent correctness bug — any loop body that reads a variable it mutates returns stale values.
- **Fix:** Either (a) mark nodes that read mutable state non-memoizable (add `isReallyPure` to descriptor), or (b) invalidate cache entries that read blackboards after any `setVariable()`/impure call in the chain.
- **Verify:** Graph: `SetVariable("X",1)` → `ForLoop` body `SetVariable("X", Index); PrintToScreen(GetVariable("X"))` — expect `0,1,2,3`; currently gets `0,0,0,0`.

### H8 — `WhileLoop.Condition` is memoized — loop is semantically broken
- **Location:** `engine/scripting/flow_nodes.cpp:221-241`
- **Evidence:** Condition input is pulled via `evaluatePureNode` every iteration, but the first evaluation is cached. Subsequent iterations see the same condition value → loop either exits immediately or runs to `MAX_WHILE_ITERATIONS`.
- **Impact:** Same root cause as H7; flagged separately because WhileLoop is visibly unusable today.
- **Fix:** Same as H7 (once H7 is fixed, H8 goes away).
- **Verify:** Counter-based while loop — must exit at the intended count, not the cap.

### H9 — `latent_nodes` captures `ScriptInstance*` across hot-reload
- **Location:** `engine/scripting/latent_nodes.cpp:103, 124-137`
- **Evidence:** Timeline lambda captures `inst` by raw pointer + `nodeId`; stored in `m_pendingActions` for duration of the latent action. If the scripting system re-initializes mid-tick (editor's test-play cycle per `scripting_system.cpp:175` comment), `nodeId` may refer to a different node in the new graph.
- **Impact:** Use-after-reinit; wrong pin written or crash if pending actions weren't cleared.
- **Fix:** Generation token on the instance; validate before deref; or unconditionally drop `onTick` callbacks when `initialize()` is re-run.
- **Verify:** Test that starts a Timeline, re-inits mid-tick, asserts no writes to the rebuilt map.

### Formula / workbench

### H10 — Formula Workbench residual plot — `m_residuals` / `m_dataX` size mismatch
- **Location:** `tools/formula_workbench/workbench.cpp:1934-1938` vs `:1860-1867`
- **Evidence:** `m_dataX` filtered by `m_plotVariable` presence; `m_residuals` populated for every data point. Different sizes → plot correlates residual[i] to wrong X[i] when any point lacks the plot variable.
- **Impact:** Silent correctness — exactly the class of bug the workbench is supposed to diagnose.
- **Fix:** Filter residuals identically, or build one filtered vector and reuse.
- **Verify:** Import CSV where one row lacks a variable; residuals must align with displayed X.

### H11 — Codegen injection via `ExprNode.name` / `ExprNode.op`
- **Location:** `engine/formula/codegen_cpp.cpp:33, 70, 87`; `codegen_glsl.cpp:32, 69, 86`
- **Evidence:** Node names/ops emitted verbatim into generated C++/GLSL. A JSON-supplied variable name like `x); system("rm -rf /"); float y(` gets spliced into compiled source.
- **Impact:** Supply-chain vector when `.json` preset files are shared/imported. Not live RCE because compile step is required, but a classic codegen injection.
- **Fix:** Validate identifiers against `[A-Za-z_][A-Za-z0-9_]*` in `ExprNode::fromJson`; codegen throws on unknown op rather than raw-splicing.
- **Verify:** Unit test: crafted JSON → rejected on load; unknown op in in-memory tree → throws in codegen.

### H12 — Evaluator uses safe-math; codegen emits raw math
- **Location:** `engine/formula/expression_eval.cpp:54-58, 77, 80` vs `codegen_cpp.cpp:47-48, 87` / `codegen_glsl.cpp:46-47, 86`
- **Evidence:** Evaluator guards: `/0 → 0`, `sqrt(x) → sqrt(|x|)`, `log(x≤0) → 0`. Codegen emits bare `/`, `std::sqrt`, `std::log`. LM fitter uses evaluator → finds coefficients valid under safe-math. Emitted shader runs unsafe math → NaN / black pixels.
- **Impact:** "Ships something different than what was validated." Fitter reports R² = 0.99; runtime shader is black.
- **Fix:** Pick one semantics. Prefer: codegen emits safe wrappers (`Vestige::safeDiv`, `Vestige::safeLog`, `safeSqrt`) in a shared helper + GLSL prelude. Alternative: strict eval mode — evaluator returns NaN, fitter refuses non-finite fits.
- **Verify:** Test `log(a*x)` fit with `x` crossing zero → assert emitted C++ produces same output as evaluator for all training points.

### H13 — Curve fitter has no NaN/Inf guard on residuals or Jacobian
- **Location:** `engine/formula/curve_fitter.cpp:206-210, 289-291, 336-343`
- **Evidence:** `currentError` / `trialError` / `sumSqResid` are `float`; no `isfinite` check before `trialError < currentError`. NaN comparisons are always false → NaN-initial-error → no step is accepted → loop runs to `max_iterations` reporting `converged=false` with garbage coefficients.
- **Impact:** Degenerate data (all-zero x for log-formula, singular Jacobian, etc.) silently wastes time and returns meaningless coefficients.
- **Fix:** Scan residuals + Jacobian after each compute; bail with explicit "non-finite — check input domain" message. Accumulate errors in `double`. 1.3.0 already fixed workbench-side accumulators; fitter itself missed.
- **Verify:** Fit `log(a*x)` with all-zero x → expect error, not silent max-iterations.

### Renderer / shaders

### H14 — SH basis constant wrong for L[8] band-2 (x²−y²) term
- **Location:** `assets/shaders/scene.frag.glsl:553`
- **Evidence:** `c3 * L[8] * (n.x*n.x - n.y*n.y)`. The canonical Ramamoorthi-Hanrahan Eq. 13 uses `c1 = 0.429043` for the L_22 (m=+2) term, not `c3 = 0.743125`. C++ side (`engine/renderer/sh_probe_grid.cpp:29-40`) matches canonical ordering, so the bug is shader-side only.
- **Impact:** ~1.73× overweight on band-2 (x²−y²) → wrong chromatic tilt in indoor ambient, most visible on horizontal surfaces. Violates GI-roadmap correctness target.
- **Fix:** Change `c3` to `c1` on that line.
- **Verify:** Uniform grey wall + asymmetric sky; compare irradiance on ±X-facing vs ±Y-facing walls against reference cubemap irradiance integral.

### H15 — Motion vectors ignore per-object previous model matrix
- **Location:** `assets/shaders/motion_vectors.frag.glsl:13-43` + entire motion pass
- **Evidence:** Motion is computed from camera matrices + depth alone. No `previousModel` uniform exists anywhere (grepped).
- **Impact:** TAA reprojection of dynamic / skinned objects shows only camera motion → heavy ghosting trails on anything that moves or animates. OK for the Tabernacle walkthrough (mostly static), not OK for the "beyond exploration" roadmap items.
- **Fix:** Per-draw `u_prevModel`; write vec2 motion from the geometry pass: `motion = currUV - prevUV` with `prevClip = u_prevViewProjection * u_prevModel * position`.
- **Verify:** RenderDoc capture of a rotating cube under TAA — motion buffer must show non-zero object motion, not just camera motion.

### Editor Step 4 WIP

### H16 — Known imgui-node-editor shutdown SEGV (workaround in place, root cause unresolved)
- **Location:** `engine/editor/editor.cpp:119-121`
- **Evidence:**
  ```cpp
  // Pass empty settings path while we chase a shutdown SEGV — the
  // imgui-node-editor settings save path is the leading suspect.
  m_scriptEditorPanel.initialize({});
  ```
- **Impact:** NodeEditor layout can't be persisted across sessions (the settingsFile feature is disabled); Step 4 acceptance criteria ("save/load via menu") is effectively unmet for canvas layout state. `NodeEditor.json` at repo root is stale/ignored by the editor.
- **Fix:** Root-cause the SEGV — likely ed::DestroyEditor fires an ImGui callback after ImGui::DestroyContext has run. Candidate orderings: (a) explicit shutdown sequence (already partially done at line 142-146); (b) suppress settings I/O during shutdown; (c) keep-alive the ImGui context until after all ed contexts are destroyed.
- **Verify:** Enable `settingsFile` path, exit editor with panel open, no SEGV; layout restored on next launch.

---

## Medium

Grouped by area.

### Step 4 WIP (script editor panel + node editor widget)

- **M1 — `makePinId` bit-pack collision risk.** `script_editor_panel.cpp:22-27` packs `(nodeId<<16) | (pinIndex<<1) | isInput`. Collides when `nodeId ≥ 65536` or `pinIndex ≥ 32768`. Unlikely for human-authored graphs but plausible for generated/procedural graphs. **Fix:** use a `(nodeId, pinIdx, isInput)` → opaque-id map; assert on overflow.
- **M2 — `ScriptEditorPanel::open()` contract mismatch.** Header (`script_editor_panel.h:62`) says "Does nothing if the load fails (the existing graph is preserved)". Implementation (`script_editor_panel.cpp:241-251`) replaces the in-memory graph with an empty one on failure. Contract violation; also the `bool` return value is dead (always true). **Fix:** either preserve on failure (match header) or update header to describe current behavior; fix return.
- **M3 — `renderGraph` per-frame O(N×M) pin-name lookup.** `script_editor_panel.cpp:202-222` iterates every connection, does a linear scan of src/tgt node's declared pins to resolve index-by-name. At large graphs this is hot. **Fix:** cache `name → index` maps on `NodeTypeDescriptor` at registration time.
- **M4 — Pin-name mismatch silently renders to pin 0.** Same location as M3 — if the lookup loop finds no match, `srcIdx`/`tgtIdx` stay at default 0; the connection draws to whatever pin 0 is (visually wrong). **Fix:** skip the connection + log warning when a pin isn't found.

### Scripting

- **M5 — `addConnection` doesn't `clampString` pin names.** `engine/scripting/script_graph.cpp:114-157` — on-disk load path caps strings, but `addConnection` called from the editor does not. Possible unbounded pin-name growth via editor actions.
- **M6 — `isPathTraversalSafe` incomplete.** `engine/scripting/script_graph.cpp:33-44` only rejects `..` components — accepts absolute paths (`/etc/passwd`) and symlinked paths. Gives false confidence. **Fix:** require/verify resolved path under caller-supplied asset root via `std::filesystem::canonical` + prefix check; reject absolute unless explicitly allowed.
- **M7 — `subscribeEventNodes` silent typo on unknown `eventTypeName`.** `engine/scripting/scripting_system.cpp:289-391` — unknown types return `subId = 0`; `addSubscription(0)` is skipped; node is present but never fires, no warning. **Fix:** log a `Logger::warning` when a non-empty eventTypeName doesn't match any branch.
- **M8 — Quat JSON order not documented.** `engine/scripting/script_value.cpp:350-352` — JSON `{w,x,y,z}` happens to match `glm::quat(w,x,y,z)` constructor; symmetric only by coincidence. **Fix:** comment the convention on both serializer and deserializer.

### Formula / node graph

- **M9 — `NodeGraph::fromJson` doesn't guard against duplicate / out-of-range node IDs.** `engine/formula/node_graph.cpp:1017` — `m_nodes[node.id] = std::move(node)` silently overwrites collisions; restored `m_nextNodeId` may be ≤ existing ids → future `addNode` collides. **Fix:** assert no duplicates; recompute `m_nextNodeId = max(id)+1` regardless of JSON.
- **M10 — `fromExprTree` silently drops conditionals.** `engine/formula/node_graph.cpp:667-682` — CONDITIONAL replaced with `literal(0.0f)`; sub-trees built then orphaned. Round-trip loses logic. **Fix:** either reject the import with a clear error, or add a conditional node type.
- **M11 — Workbench CSV parser lacks RFC 4180 quoted-field support.** `tools/formula_workbench/workbench.cpp:1156` — `getline(ss, cell, ',')` breaks on any Excel-exported CSV with quoted fields.
- **M12 — `ExprNode::toJson` dereferences potentially-null children.** `engine/formula/expression.cpp:133-136, 139-149` — crash on save path for malformed in-memory trees.

### Renderer

- **M13 — BRDF LUT integrates to 0 at `NdotV=0` edge.** `assets/shaders/brdf_lut.frag.glsl:67-101` — left column of LUT is black instead of Fresnel-peaked white. Rim shows dark on rough dielectrics under IBL. **Fix:** `NdotV = max(NdotV, 1e-4)` at function entry (matches UE4 reference).
- **M14 — Probe capture may leak skybox pass under forward-Z state.** `engine/renderer/renderer.cpp:1755, 1871` — capture paths set forward-Z and depth-clear 1.0; if `renderScene(geometryOnly=true)` ever stops skipping skybox, skybox's `gl_FragDepth = 0` would pass `GL_LESS` and fill foreground. Currently safe; worth a hardening note.
- **M15 — Bloom bright extraction fireflies on saturated-hue HDR.** `assets/shaders/bloom_bright.frag.glsl:25` — `color * contribution / (luminance + 0.0001)` amplifies deep-blue/deep-red up to 10000×. **Fix:** conservative epsilon + `min(color, vec3(MAX_BLOOM))` clamp.
- **M16 — SSR forward-diff normals → silhouette halos.** `assets/shaders/ssr.frag.glsl:59-64` — `cross(ddx, ddy)` from forward-only samples; at depth discontinuities the normal is garbage. **Fix:** central differences with gradient-disagreement rejection.
- **M17 — Point shadow bias 0.05 too large for Tabernacle-scale interiors.** `assets/shaders/scene.frag.glsl:437` — bias in world units causes Peter-Panning. **Fix:** scale by `farPlane`.
- **M18 — Contact shadow normals — same forward-diff issue.** `assets/shaders/contact_shadows.frag.glsl:55-59` — edge artifacts. **Fix:** same as M16 (four-tap cross).

### Audit tool

- **M19 — cppcheck stderr unbounded → OOM on large codebases.** `tier1_cppcheck.py:48` — `subprocess.run(capture_output=True)` + `xmltodict.parse(stderr)`. **Fix:** stream via `Popen`, or cap at ~64 MB with truncation warning.
- **M20 — Tier 2 regex can ReDoS on user-supplied patterns.** `tier2_patterns.py:48, 57`. On the web UI this hangs the session. **Fix:** subprocess with wall-clock timeout, OR `regex` lib with `timeout=`.
- **M21 — Several pattern false-positives** (details):
  - `raw_new` exclude `new Q\w+\(` matches `new Qux` as if a Qt widget.
  - `predictable_temp` missing `\b` anchor — matches `my_mktemp_wrapper(`.
  - `null_macro` flags `#define FOO_NULL 0`.
  - These compound with **AT-A1** (cppcheck `--inline-suppr` missing) as the main noise sources.
- **M22 — 64-bit dedup_key theoretical collision risk.** `findings.py:54` — `sha256(...)[:16]`. Deliberate adversary could collide to hide a real finding via a sacrificial suppression.
- **M23 — SARIF output uses SARIF-1 `%SRCROOT%` without `originalUriBaseIds`.** `sarif_output.py:72`. GitHub Advanced Security + VS Code SARIF viewers reject. **Fix:** add `originalUriBaseIds` entry; use `uriBaseId: "SRCROOT"` without `%…%`.
- **M24 — DOM-XSS chain via inline `onclick` in history list.** `web/templates/index.html:717` — filenames from `docs/` glob go into attribute without quote escaping. Requires prior filesystem write (which H3 allows) → chain.

### Build / process / hygiene

- **M25 — No CI.** No `.github/`, `.gitlab-ci.yml`, Jenkinsfile, or similar. Every audit is manual. **Fix:** minimal GitHub Actions matrix (Linux-Debug, Linux-Release) running build + ctest + `python3 tools/audit/audit.py -t 1 --ci`. Protect main.
- **M26 — No `.clang-format` at repo root.** Coding standards doc specifies Allman 4-space, but there's no enforcement. **Fix:** commit a `.clang-format` matching `CODING_STANDARDS.md` + pre-commit hook running `clang-format --dry-run`.
- **M27 — No engine-level VERSION file.** Engine version lives only in `CMakeLists.txt:3`. Memory (`feedback_changelog_mandatory.md`) requires VERSION + CHANGELOG in same commit for tools; engine itself has neither. **Fix:** create `VERSION` (0.1.0) + `CHANGELOG.md` at root; apply same mandatory-same-commit rule via pre-commit hook.

---

## Low

- **L1 — `ScriptingSystem::initialize()` idempotency not documented.** `engine/scripting/scripting_system.cpp:139-149`.
- **L2 — `popen` buffer truncation at 512 B in workbench file dialog.** `workbench.cpp:1248-1252`.
- **L3 — Curve fitter uses `float` for RMSE accumulator.** `curve_fitter.cpp:343`. Workbench 1.3.0 fixed its accumulators; fitter wasn't updated.
- **L4 — SH probe 3D texture units not rebound after capture.** `engine/renderer/renderer.cpp:1680-1683, 1786-1793`. Second capture could read stale.
- **L5 — TAA: `max(..., 0.0)` missing on final result for cheap NaN safety.** `taa_resolve.frag.glsl:122-124`.
- **L6 — Web UI has no CSRF tokens on POST/PUT.** `app.py:299`. Localhost-only, but combined with H1-H3 becomes meaningful.
- **L7 — NVD response body has no size cap.** `tier5_nvd.py:61`.
- **L8 — `NodeEditor.json` untracked at repo root; should be gitignored.** (AT-D1 in improvements file.)
- **L9 — 14 MB of untracked `docs/AUTOMATED_AUDIT_REPORT_*_results.json` on disk.** Hygiene only — already gitignored. User may wish to clean up periodically; audit tool could `--keep-runs N` on generation (AT-D2).

---

## Pre-existing tool false positives — not source bugs

All four have correct inline `// cppcheck-suppress` annotations in source. The audit tool re-reports them because its cppcheck invocation is missing `--inline-suppr`. See **AT-A1** in `AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS.md`. Fix is a one-line config change — not source code.

| File:Line | Rule | Why false-positive |
|---|---|---|
| `engine/formula/node_graph.cpp:495` | `containerOutOfBounds` | Short-circuit AND: `nodeId < visited.size() &&` guards the deref. |
| `engine/scene/entity.cpp:60, 79` | `returnDanglingLifetime` (×2) | `child.get()` saved BEFORE `std::move` into `m_children`; pointer remains valid (new owner). |
| `tests/test_command_history.cpp:244-246` | `containerOutOfBounds` (×3) | `ASSERT_EQ(log.size(), 3u)` on line 243 aborts the test if empty; subsequent `EXPECT_EQ(log[0..2])` is unreachable when empty. |

---

## Features verified (spot-checks)

| Feature | Verified via | Result |
|---|---|---|
| Build clean | `docs/AUTOMATED_AUDIT_REPORT_2026-04-13_091404.md` | 0 warnings, 0 errors |
| Tests pass | Same + `ctest -N` | 1738 registered, 1695 passed at last audit; Step-3 added 43 |
| Entity ownership in scene graph | Manual read of `engine/scene/entity.cpp:54-80` | Correct — suppressed cppcheck noise is cosmetic |
| ScriptGraph cycle detection | Manual read of `engine/formula/node_graph.cpp:475-485, 491-510` | Correct — `visited` resized before indexing |
| Editor shutdown ordering (Step 4) | `engine/editor/editor.cpp:142-146` diff + comment | Correctly sequenced; underlying SEGV (H16) still open |
| NodeEditor canvas minimal render | Reading Step-4 WIP source | Compiles; rendering path depends on `NodeTypeRegistry` being set — not verified at runtime (didn't launch `vestige`) |
| Formula Workbench fit round-trip | Not verified at runtime (did not launch) | Code paths reviewed; H10 size-mismatch is a real bug |

**Not verified at runtime** (user explicitly authorized, but I kept Phase 2 observation-only to avoid confounding the clean baseline): `./build/bin/vestige` launch, `./build/bin/formula_workbench` CSV-import + fit cycle. Can run in a follow-up on demand.

---

## Categories marked N/A

- **i18n / accessibility (UX):** N/A for this engine today — no shipping UI text catalog, no screen-reader targets. Revisit when editor gets polish / localization pass.
- **API & data migrations:** N/A — no network API, no database. JSON scene/script formats exist but no deployed users yet, so "breaking change" means "edit docs."

---

## Phase 3 preview (research required for Critical/High)

- **C1:** shlex safe-split patterns for Python subprocess; pip `shlex` stdlib docs.
- **C2:** NVD API key rotation procedure (vs. revocation): <https://nvd.nist.gov/developers/request-an-api-key>.
- **H1-H3:** Flask + Path traversal canonical pattern; check cheatsheetseries.owasp.org for Python file-path handling.
- **H4:** CRLF injection in `urllib.request.add_header` — stdlib does it automatically refuse some but not all; needs confirmation.
- **H5:** C++ standard citation that forbids `&ref == nullptr` being useful under optimization (`[basic.compound]` + `[expr.reinterpret.cast]`).
- **H14:** Ramamoorthi + Hanrahan 2001 paper; cross-check at learnopengl.com IBL tutorial.
- **H15:** TAA per-object motion technique — standard in UE/Unity/Frostbite; real-time rendering 4e ch.10.

**Stop here** pending your confirmation to begin Phase 3 research + Phase 4 fix-plan drafting. No code changes will be made until Phase 4's `FIXPLAN.md` is reviewed and approved.
