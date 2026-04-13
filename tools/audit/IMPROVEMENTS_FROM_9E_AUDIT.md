# Audit Tool Improvement Ideas — Observations from Phase 9E-1/9E-2 Audit

Recorded: 2026-04-13. Context: first real post-phase audit using the tool after it matured to v2.0.0. Six Tier-4 subagents found 30 actionable findings (1 Critical, 9 High, 14 Medium, 6 Low) against ~7,152 LOC of new scripting code. The automated tool, running against the same codebase, reported **zero** criticals in scripting/ and 49 highs — all of which were either outside the scripting subsystem or were false positives. This gap motivates the following improvements.

---

## 1. Severity calibration — the "medium" flood

**Problem:** 4,652 medium findings is effectively noise. Breakdown:
- clang-tidy: 4,443 medium
- `shared_ptr` pattern: 100 medium
- `framebuffer_no_check`: 62 medium
- `global_gl_state`: 36 medium
- `c_style_cast`: 18 medium

Most of these are pre-existing across the full codebase, not introduced by the audited phase, and many are pure style opinions.

**Proposed upgrades:**

### 1a. Per-check severity overrides for clang-tidy
Today: a single `medium` bucket for every clang-tidy rule. Add a config section that maps clang-tidy check IDs to custom severities. Bugprone-* stays medium/high, modernize-use-auto drops to info.

```yaml
static_analysis:
  clang_tidy:
    severity_overrides:
      modernize-use-auto: info
      readability-braces-around-statements: info
      bugprone-use-after-move: critical
      cppcoreguidelines-pro-type-reinterpret-cast: high
```

### 1b. Demote `shared_ptr` pattern to `info`
It's a preference, not a bug. Keeping it as `high` or even `medium` creates alert fatigue. Only flag `shared_ptr` when paired with an ownership smell (e.g., never moved, never copied) — which needs semantic analysis, not a regex.

### 1c. `framebuffer_no_check` context-aware
Current pattern: any `glBindFramebuffer(...)` without nearby `glCheckFramebufferStatus`. This fires on `glBindFramebuffer(GL_FRAMEBUFFER, 0)` (UNBIND, no check needed) — a dozen of those are in the current report. Skip unbinds (target=0 or target=default) in the pattern.

### 1d. Regex-pattern false-positive filter for `c_style_cast`
17 of the 18 matches are `/*deltaTime*/` in parameter names, matched by the `*/ ` sequence. The regex needs a `skip_in_comments: true` upgrade that understands `/* ... */` block comments (not just `//` line comments).

---

## 2. Phase-scoped findings vs. full-codebase debt

**Problem:** the tool treats "findings introduced by this phase" and "pre-existing debt in files not touched" identically. The 9E audit isn't meaningfully improved by re-surfacing `c_style_cast` warnings in `engine/audio/audio_clip.cpp` — that's pre-9E code.

**Proposed upgrades:**

### 2a. `--only-new` flag
Filter Tier 2/3/4 findings to files in the Tier 3 diff. Separate report section "Pre-existing debt (not introduced by this phase)".

### 2b. Regression detection
When a finding in a changed file is new (didn't appear in the previous audit's JSON sidecar), tag it "regression". Today `--diff` tells you new/resolved/persistent, but doesn't cross-reference against changed files.

### 2c. Phase-anchored base-ref
Store the last audit's HEAD in `.last_audit_commit`. Default Tier 3 base-ref to that commit rather than a heuristic. This prevents the base-ref drift I saw (two runs four minutes apart produced 35 vs. 19 changed files because the audit tool's auto-detect landed on different refs).

---

## 3. Semantic gaps — what subagents caught and the tool missed

The tool missed **every single Critical/High from 9E** because those are semantic, not syntactic:

| Finding | Why tool missed it |
|---|---|
| C1 — unbounded JSON deserialization | Requires understanding that a loop body calls `.push_back` on user-controlled size |
| H1 — lambda ref-capture of parameter bound to external object in long-lived callback | Requires call-graph + lifetime reasoning |
| H6 — `val[i].get<float>()` without prior size check | Requires understanding nlohmann's throwing semantics + control flow |
| H8 — NaN propagation in math nodes | Requires tracking a value through multiple nodes |

**Proposed upgrades:**

### 3a. Lambda capture-by-ref detector
Add a pattern: any `[&...]` inside a call to a function whose name matches `subscribe|register|connect|bind|addCallback|on...` (i.e., long-lived callbacks). Severity: high. False positives are acceptable here — the fix is almost always safer.

### 3b. JSON deserialization size-cap detector
A scanning rule: look for patterns `j["key"].is_array()` or `for (... : j["key"])` where the preceding N lines don't contain a size cap (`.size() < X`, `.size() < MAX_*`). Flag as medium-high.

### 3c. Integer overflow on boundary inputs
For any `int32_t count = X - Y` pattern where X and Y are read from user input, flag for review. Rules engine could recommend `int64_t count` as fix. This is more speculative but well-defined.

### 3d. "Lifetime contract" docs
For patterns like `&m_engine`, `&instance` captured in lambdas: if the function doc or annotation says "must be called while X is alive", a checker can verify unsubscribe is paired. Less ambitious: emit an info-level note every time a lambda captures by ref so the reviewer is nudged.

---

## 4. Tier 5 research quality

**Problem:** Tier 5 returned largely off-topic cached results. Example hits on `Dear ImGui best practices`: Cambridge Dictionary's "DEAR" entry and Nationwide Cash ISA rates. These don't help.

**Proposed upgrades:**

### 4a. Domain whitelist per topic class
For C++ best practices: `site:github.com OR site:stackoverflow.com OR site:cppreference.com OR site:isocpp.org`. For CVEs: keep NVD. For library-specific: pin `site:<library-name>/discussions`.

### 4b. Query sanitization — reject dictionary hits
Filter out results whose URL contains `dictionary`, `meaning`, `translate`, `nationwide`, `moneysavingexpert`, etc. Simple deny-list of domains that bleed into generic-keyword searches.

### 4c. Make Tier 5 optional-but-smart
Today `--no-research` is all-or-nothing. Add `--research=cve` to run only NVD queries (high-signal) and skip best-practice keyword searches (low-signal). Default to this.

---

## 5. Uniform-shader sync finding quality

Tier 4 reported 6 "declared-not-set" uniforms in SSR and bloom shaders. Likely false positives because those uniforms are set via `glUniformBlockBinding` or similar non-direct paths. Need to:

### 5a. Parse uniform blocks and SSBOs
Today the scanner looks for `uniform <type> <name>`. It should also parse `layout(std140) uniform Block { ... }` and skip blocks that are bound with `glUniformBlockBinding` rather than individual `glUniform*`.

### 5b. Cross-reference against shader programs that are unused
If a shader is declared-not-set *because* the whole program is never loaded (e.g., SSR is behind a feature flag that's off), flag the whole program as unused rather than each uniform individually.

---

## 6. Cognitive complexity — weight registration boilerplate down

`registerActionNodeTypes` flagged at cognitive-complexity 29. It's a 500-line function of ~15 sequential `registry.registerNode({...})` calls. Each call contains a lambda. The tool counts each lambda + each `if` statement inside each lambda toward the outer function's complexity.

### 6a. Treat sequentially-called functions as non-nesting
If a function is just a long run of `foo(...); foo(...); foo(...);` with no control flow between, its cognitive complexity should be roughly linear in call count, not exponential in depth. Lizard handles this better than the custom scorer.

### 6b. Detect and deweight "registry" / "descriptor" patterns
Functions whose name matches `register.*` or `populate.*` or whose body is dominated by a single repeated call pattern get a weight reduction. Doesn't eliminate the signal — long registration functions do eventually need refactoring — but tempers the score.

---

## 7. Dedup & correlation across findings

The automated tool reported 5,577 findings. Many are the same underlying issue surfacing from multiple tiers (e.g., a raw `new` shows up in both `memory_safety` pattern scan AND clang-tidy's `cppcoreguidelines-owning-memory`). Today the `dedup_key` is per-tool; true cross-tool dedup would help.

### 7a. Semantic dedup
Hash `(file, line, category)` — collapse matches across tools pointing at the same line-level issue.

### 7b. Findings clustering by root cause
When 12 findings all resolve the same underlying issue (e.g., "all 6 event-subscription lambdas capture by reference"), cluster them. Report one root-cause entry with per-finding detail collapsed.

---

## 8. Formula workbench observations

I only skimmed the workbench output in this run. Observations:

- The tool treats `engine/formula/node_graph.cpp` as a **god file** (88 functions, 982 lines). That's accurate and should stay flagged.
- One cppcheck HIGH: `containerOutOfBounds` at `engine/formula/node_graph.cpp:495`. Real bug, separate from 9E.
- No obvious false positives observed in formula-specific findings; the workbench issues look like pre-existing debt worth a dedicated audit pass.

**Formula workbench improvements (audit-adjacent):**

### 8a. Symbolic execution for expression validation
The expression evaluator could pre-compute and warn on inputs that would divide-by-zero, produce NaN, or overflow. Today these only surface at runtime.

### 8b. Curve-fitter degenerate-input detection
If the user supplies fewer data points than the target polynomial order, the fitter currently falls back quietly. Should flag in the UI.

### 8c. Shader codegen round-trip validation
Generate GLSL from an expression, parse it back, diff the AST. Catches codegen bugs that only surface when the shader is compiled.

These are all small QoL upgrades — they're not blocking 9E-3 or the engine at large.

---

## Priority ranking

If only some of the above can be implemented, I'd do them in this order:

1. **1c + 1d + 2a** — immediately cuts report noise by ~70%. Small changes.
2. **3a + 3b** — real new detections that would have caught the 9E issues. Medium changes.
3. **4a + 4b** — improves Tier 5 signal. Small changes.
4. **7a + 7b** — structural upgrade to how findings are grouped. Medium-large change.
5. **5a** — specialized, but high-signal when it fires. Medium change.
6. **6a + 6b** — fine-tuning, not urgent. Small changes.
7. **8a–c** — formula workbench QoL, lowest priority. Small changes.
