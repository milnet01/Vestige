# FIXPLAN — 2026-04-13 full audit

**Status:** DRAFT — awaiting your approval before any code change.

**Ordering principle:** smallest blast radius first, one logical change per commit. Security-critical scrubs precede larger refactors. Infrastructure items (CI, formatter, changelog) are last and non-blocking.

**Legend:**
- 🔒 Security-critical (reach CRITICAL / HIGH severity)
- ⚠️ Hard-to-reverse or requires data/process work — confirm before executing
- 🧪 Needs visual or runtime verification before committing
- 🪓 Touches >5 files — review diff carefully
- 🟢 Small, mechanical, low-risk

For each item: **commit #**, **estimated scope (files)**, **severity source**, **summary**, **rollback**, **dependencies**, **verification**.

---

## Batch A — Safety scrubs (ship immediately, independent)

### A1. 🔒 🟢 Rotate + scrub NVD API key
- **Severity:** C2 (Critical)
- **Files:** `tools/audit/audit_config.yaml` (1 line)
- **Change:** replace literal UUID with `api_key: null`; keep `api_key_env` path. Simultaneously request a new key from NVD (manual step — external to repo).
- **Rollback:** restore YAML line from git. Rotation cannot be undone (nor should it be).
- **Depends on:** nothing in repo; user must request new key via <https://nvd.nist.gov/developers/request-an-api-key> before we have a replacement. Activation window is 7 days.
- **Verify:** `git grep bcb87fe8` → empty in working tree; `NVD_API_KEY=… python3 tools/audit/audit.py -t 5` still produces research results after activation.
- **Note:** old key remains in git history. If you want to purge history too, that's a separate `git filter-repo` operation — confirm before running.

### A2. 🟢 Add `--inline-suppr` to audit tool's cppcheck invocation
- **Severity:** AT-A1 (tool false-positive)
- **Files:** `tools/audit/audit_config.yaml` (1 line in `static_analysis.cppcheck.args`), `tools/audit/lib/auto_config.py` (1 default)
- **Change:** append `--inline-suppr` to default cppcheck args.
- **Rollback:** remove the flag.
- **Depends on:** nothing.
- **Verify:** re-run `python3 tools/audit/audit.py -t 1`; confirm the 6 HIGH cppcheck findings at `engine/formula/node_graph.cpp:495`, `engine/scene/entity.cpp:60,79`, `tests/test_command_history.cpp:244-246` **disappear**.
- **Also:** per `CLAUDE.md`, update `tools/audit/CHANGELOG.md` and VERSION in the same commit (patch bump → 2.0.1).

### A3. 🟢 Add `NodeEditor.json` to `.gitignore`
- **Severity:** L8 / AT-D1
- **Files:** `.gitignore` (1 line)
- **Change:** add `NodeEditor.json` (and/or `/NodeEditor.json` if we want to scope to root only).
- **Rollback:** remove the line.
- **Depends on:** nothing.
- **Verify:** `git status` — file no longer shows as untracked.

---

## Batch B — Web UI path-traversal fixes (single atomic commit)

### B1. 🔒 Lock down `GET /api/report`, `GET /api/config`, `POST /api/init` to allowed roots
- **Severity:** H1, H2, H3
- **Files:** `tools/audit/web/app.py` (3 route handlers + 1 helper)
- **Change:** extract the `allowed_roots` + `resolve().is_relative_to()` check already present in the PUT handler (`app.py:220-226`) into a private helper `_is_safe_path(p, allowed_roots)`. Apply to all 3 read/write endpoints. Reject with 403 on failure.
- **Rollback:** revert commit.
- **Depends on:** nothing.
- **Verify:**
  ```bash
  curl -s 'http://127.0.0.1:5800/api/report?path=/etc/passwd'   # expect 403
  curl -s 'http://127.0.0.1:5800/api/config?path=/etc/shadow'   # expect 403
  curl -s -X POST 'http://127.0.0.1:5800/api/init' \
    -d '{"output_path":"/tmp/foo.yaml","project_root":"/tmp"}'   # expect 403
  ```
- **Also:** bump audit tool to 2.0.2, update CHANGELOG.

---

## Batch C — Subprocess hardening (multi-step, commit by commit)

### C1a. 🔒 ⚠️ Validate user-reachable config values before passing to shell
- **Severity:** Part of C1 — bounds the blast radius BEFORE the full refactor lands.
- **Files:** `tools/audit/web/app.py` (1 validator), `tools/audit/lib/config.py` (schema check for `*_ref`, `binary:`, `build_dir` fields).
- **Change:** add `re.fullmatch(r"[A-Za-z0-9._/~^-]{1,64}", value)` to `base_ref` and any `_ref` suffixed field. Reject binaries paths with shell metachars (`;`, `&`, `|`, backtick). Reject `build_dir` with shell metachars.
- **Rollback:** revert commit.
- **Depends on:** nothing.
- **Verify:**
  ```bash
  curl -s -X POST http://127.0.0.1:5800/api/run \
    -d '{"base_ref":"HEAD; touch /tmp/pwn"}' -H 'Content-Type: application/json'
  # Expect 400 with validation error; /tmp/pwn must NOT exist after.
  ```
- **Risk:** some legitimate base-refs with unusual characters may be rejected. The regex allows `/`, `.`, `_`, `^`, `~`, `-` which covers all standard git ref forms.
- **Confirmation needed:** this commit reduces (but does not eliminate) the injection surface. **Full fix is C1b below**.

### C1b. 🔒 ⚠️ 🪓 Refactor `run_cmd` to `shell=False` by default
- **Severity:** C1 (Critical), fixes the root cause
- **Files:** `tools/audit/lib/utils.py` (rewrite wrapper), plus callers:
  - `tier1_build.py`, `tier1_cppcheck.py`, `tier1_clangtidy.py`
  - `tier3_changes.py`
  - `tier5_research.py`, `tier5_nvd.py`
- **Change:** `run_cmd(cmd: list[str], ...)` default. For callers that legitimately need a shell (user-authored `build_cmd: "cmake --build build 2>&1"`, `test_cmd: "cd build && ctest …"`), add an explicit `run_cmd(cmd: str, ..., shell: bool = True)` variant gated behind per-call opt-in. Refactor internal callers to `shlex.split(config_value)` + list form. For `cd build && …` patterns: use `cwd=` and move redirection to Python file handling.
- **Rollback:** revert commit (single atomic change). Keep the old wrapper name as an alias for one release, deprecated.
- **Depends on:** C1a (for reducing the attack surface while this lands).
- **Verify:**
  - Full audit tool test suite passes (`cd tools/audit && python3 -m pytest tests/ -v`)
  - `python3 tools/audit/audit.py -t 1 2 3 4 5` completes without errors
  - Test with `build.build_dir: "build; touch /tmp/pwn_c1"` → `/tmp/pwn_c1` does NOT exist
- **Risk:** moderate. Must ship atomically to avoid partial-refactor regressions. The test suite provides a safety net. Suggest a branch + PR before merging.
- **⚠️ Confirm before executing** — this is the biggest audit-tool refactor in the plan.

### C1c. 🔒 🟢 Validate NVD API key shape
- **Severity:** H4
- **Files:** `tools/audit/lib/tier5_nvd.py` (1 guard in `_resolve_api_key`)
- **Change:** `re.fullmatch(r"[A-Za-z0-9-]{16,64}", key)` before `req.add_header`. Refuse with warning log otherwise.
- **Rollback:** revert commit.
- **Depends on:** nothing.
- **Verify:** `NVD_API_KEY=$'foo\r\nX-Admin: 1' python3 tools/audit/audit.py -t 5` — expect "invalid API key shape" warning, no HTTP request made.

---

## Batch D — Engine scripting correctness

### D1. 🪓 Replace `Engine&` with `Engine*` in scripting; remove UB null-guards
- **Severity:** H5
- **Files:** `engine/scripting/script_context.{cpp,h}`, `action_nodes.cpp`, `pure_nodes.cpp`, plus any construction site in tests.
- **Change:** `Engine& m_engine` → `Engine* m_engine`; call sites use `if (m_engine) m_engine->…`. Remove `reinterpret_cast<uintptr_t>(&m_engine) == 0` checks.
- **Rollback:** revert.
- **Depends on:** nothing; independent of other scripting fixes.
- **Verify:** `ctest -R Scripting` passes; run `build/bin/vestige_tests --gtest_filter="Script*"`; build with `-O2 -fno-inline` does not crash in any existing test path.
- **Risk:** moderate — affects every scripting call site. Touched by 30+ existing tests; those exercise the path.

### D2. 🟢 `Blackboard::fromJson` enforces `MAX_KEYS`
- **Severity:** H6
- **Files:** `engine/scripting/blackboard.cpp` (`fromJson` routes through `set()`)
- **Change:** replace direct `m_values[k] = v` with `bb.set(k, v)`; additionally `clampString(k, MAX_STRING_BYTES)` on the key.
- **Rollback:** revert.
- **Depends on:** D1 (not strictly — can ship separately).
- **Verify:** new unit test feeds JSON with 2000 keys / 1 MB key name; asserts `bb.size() <= MAX_KEYS` and each key length `<= MAX_STRING_BYTES`.

### D3. 🪓 Pure-node memoization opt-out for impure-but-classified-pure nodes
- **Severity:** H7, H8 (same fix)
- **Files:** `engine/scripting/node_type_registry.{cpp,h}` (add `bool isReallyPure` / or `noMemoize` flag to `NodeTypeDescriptor`); `engine/scripting/script_context.cpp` (check flag before cache lookup/store); `engine/scripting/pure_nodes.cpp` (mark `GetVariable`, `FindEntityByName`, `HasVariable` as non-memoizable).
- **Rollback:** revert.
- **Depends on:** nothing.
- **Verify:** new test (matching audit's H7 verification): `SetVariable("X",1)` → `ForLoop` body `SetVariable("X", Index); PrintToScreen(GetVariable("X"))` — expect `0,1,2,3`. Also new WhileLoop test with Counter-based Condition.
- **Risk:** mild perf hit — previously-memoized getters now re-evaluate per-pull. Verify fibonacci-like pure chains remain cached.

### D4. Generation token on `ScriptInstance` for latent action validity
- **Severity:** H9
- **Files:** `engine/scripting/script_instance.{cpp,h}` (add `uint32_t m_generation`, bumped on each `initialize()`); `engine/scripting/latent_nodes.cpp` (capture generation + validate before deref).
- **Rollback:** revert.
- **Depends on:** nothing.
- **Verify:** new test — start a Timeline, call `initialize()` mid-tick, assert no writes to rebuilt map, no crash.

---

## Batch E — Formula / workbench correctness

### E1. 🟢 Curve fitter NaN/Inf guards
- **Severity:** H13
- **Files:** `engine/formula/curve_fitter.cpp` (add `std::isfinite` scans after `computeResiduals`, `computeJacobian`; switch `currentError`/`trialError`/`sumSqResid` to `double`).
- **Rollback:** revert.
- **Depends on:** nothing.
- **Verify:** new test — fit `log(a*x)` with all-zero x → expect fitter returns `converged=false` with `error_reason="non-finite residuals"`, not silent max-iterations.

### E2. 🟢 Workbench residual-plot filter consistency
- **Severity:** H10
- **Files:** `tools/formula_workbench/workbench.cpp` (`rebuildVisualizationCache`: filter residuals identically to dataX, OR build one filtered pair).
- **Rollback:** revert.
- **Depends on:** nothing.
- **Verify:** new test — import CSV where row 3 lacks the plot variable; assert `m_residuals.size() == m_dataX.size()` and ordering correlates.
- **Also:** bump workbench VERSION + CHANGELOG (1.3.1).

### E3. Identifier allowlist in ExprNode::fromJson + codegen whitelist
- **Severity:** H11
- **Files:** `engine/formula/expression.cpp` (`fromJson` validates identifiers against `[A-Za-z_][A-Za-z0-9_]*`; reject otherwise), `engine/formula/codegen_cpp.cpp` + `codegen_glsl.cpp` (throw on unknown op instead of raw-splicing).
- **Rollback:** revert.
- **Depends on:** nothing.
- **Verify:** new tests — malicious JSON with `name: "x); system(\"rm -rf /\")"` → rejected on load; crafted op → throws in codegen.

### E4. Safe-math helpers for evaluator ↔ codegen parity
- **Severity:** H12
- **Files:** new `engine/formula/safe_math.h` (C++ helpers: `safeDiv`, `safeLog`, `safeSqrt`); `engine/formula/codegen_cpp.cpp` (emit `Vestige::safeDiv(a, b)` instead of `a / b`); GLSL prelude string (top of emitted GLSL codegen) with equivalent `safeDiv` / `safeLog` / `safeSqrt` functions; `engine/formula/expression_eval.cpp` uses the same helpers (so behavior is one source of truth).
- **Rollback:** revert.
- **Depends on:** E3 (ship together or E3 first; E4 adds surface area on a codegen path that should already be validated).
- **Verify:** new test — fit `log(a*x)` with x crossing zero, generate C++ codegen output, compile + link, assert emitted function produces same value as evaluator for all training points.

---

## Batch F — Shader correctness

### F1. 🟢 🧪 SH basis constant fix — `c3` → `c1` on L[8]·(x²−y²) term
- **Severity:** H14
- **Files:** `assets/shaders/scene.frag.glsl:553` (one-character change: `c3` → `c1`)
- **Rollback:** revert (one character).
- **Depends on:** nothing.
- **Verify (visual):** bake an SH probe with a distinctive asymmetric sky HDRI; render a uniform-grey wall in both ±X and ±Y orientations. Before fix: 1.73× over-weighted (x²−y²) term yields visibly wrong chromatic tilt. After fix: matches reference cubemap irradiance integral within 2%.
- **Regression:** compare F11 screenshots of a bake-test scene (user memory notes F11 captures both image + objective frame analysis).

### F2. 🧪 Shader micro-fixes (BRDF LUT edge, bloom firefly, SSR/contact-shadow normals, point-shadow bias)
- **Severity:** M13, M15, M16, M17, M18 (bundle — related class)
- **Files:** `assets/shaders/brdf_lut.frag.glsl`, `bloom_bright.frag.glsl`, `ssr.frag.glsl`, `contact_shadows.frag.glsl`, `scene.frag.glsl` (point shadow bias).
- **Changes:**
  - `brdf_lut.frag.glsl:67` — `NdotV = max(NdotV, 1e-4);` at function entry
  - `bloom_bright.frag.glsl:25` — conservative epsilon `max(luminance, 1e-4)` + `min(color, vec3(MAX_BLOOM))`
  - `ssr.frag.glsl:59` / `contact_shadows.frag.glsl:55` — central differences + gradient-disagreement rejection
  - `scene.frag.glsl:437` — point-shadow bias scaled by `farPlane`
- **Rollback:** revert shader files individually if visual regression.
- **Depends on:** F1 (ship in same sprint to minimize shader-rebake cycles).
- **Verify (visual):** side-by-side comparison per shader — see AUDIT.md per-item verification steps.
- **⚠️ Confirm before executing** — each shader tweak can shift visuals in subtle ways; regression testing is visual.

---

## Batch G — Motion vector per-object pass (H15)

### G1. 🪓 🧪 ⚠️ Add per-object previous-model matrix to motion vector pass
- **Severity:** H15
- **Files:** `engine/renderer/renderer.cpp` (geometry pass), `engine/scene/entity.{cpp,h}` or similar (store `m_prevModelMatrix` per entity, updated end-of-frame), `assets/shaders/motion_vectors.frag.glsl` + `.vert.glsl` (rewrite to use `u_prevModel * vec4(pos, 1)`).
- **Rollback:** revert commit. Keep the old depth-based path as a fallback for static-only scenes.
- **Depends on:** none logically, but wait until after Batch F (shader-change window).
- **Verify (visual):** RenderDoc capture of rotating cube under TAA. Before fix: ghost trail. After fix: clean reconstruction. Also verify the Tabernacle walkthrough (mostly-static) is unchanged within tolerance.
- **Effort:** ~1 week of work, per EXPERIMENTAL.md E5. This is the largest single engine-side change in the plan.
- **⚠️ Confirm before executing** — significant surface area, benefits only dynamic content. Currently the only "dynamic" content is the editor camera. May be deferrable until Phase 10 begins non-static gameplay work.

---

## Batch H — Editor Step 4 completion

### H1. Root-cause imgui-node-editor shutdown SEGV (H16)
- **Severity:** H16
- **Files:** `engine/editor/widgets/node_editor_widget.{cpp,h}`, `engine/editor/editor.cpp`.
- **Change:** blocked on diagnosis. Per `EXPERIMENTAL.md E1`, do an ASan run on open-close cycle first. Then implement one of:
  - (a) `ed::Config::SaveSettings` callback that no-ops during shutdown
  - (b) keep `m_settingsFile` std::string stable (currently does — possibly a different cause)
  - (c) file an upstream issue at `thedmd/imgui-node-editor` referencing issue #57 (EditorContext pointer invalidation)
- **Rollback:** if diagnosis fails, leave the empty-settings-path workaround from `editor.cpp:119` as-is.
- **Depends on:** ASan run (E1).
- **⚠️ Confirm before executing** — this is investigative; scope depends on root cause.

### H2. ScriptEditorPanel Step-4 cleanup (M1–M4)
- **Severity:** M1, M2, M3, M4
- **Files:** `engine/editor/panels/script_editor_panel.{cpp,h}`
- **Changes:**
  - M1 — replace bit-packed pin IDs with a map: `std::unordered_map<std::tuple<uint32_t, size_t, bool>, uintptr_t> m_pinIdMap` + reverse map.
  - M2 — `open(path)`: either preserve existing graph on load failure (match header) or update header + fix return value.
  - M3 — cache pin-name → index maps on `NodeTypeDescriptor` at registration; replace linear scans.
  - M4 — skip the connection + `Logger::warning` when a pin isn't found by name.
- **Rollback:** revert.
- **Depends on:** nothing.
- **Verify:** open + save + reload a large graph; confirm no pin-id collisions; confirm orphan connections logged.

---

## Batch I — Infrastructure / process (non-blocking; ship any time)

### I1. 🟢 Minimal GitHub Actions CI (M25)
- **Files:** `.github/workflows/ci.yml` (new), repo settings (branch protection).
- **Change:** Linux-Debug + Linux-Release matrix. Steps: checkout → apt install deps → cmake → build → ctest → `python3 tools/audit/audit.py -t 1 --ci`. Cache apt + build.
- **Rollback:** delete file.
- **Depends on:** A1 (don't leak NVD key in CI logs; env var required).
- **Verify:** CI green on this PR. Required status check before merge to `main`.

### I2. 🟢 `.clang-format` at repo root (M26)
- **Files:** `.clang-format` (new)
- **Change:** match `CODING_STANDARDS.md`: BasedOnStyle: Google + Allman braces + 4-space indent + line length 100.
- **Rollback:** delete file.
- **Depends on:** nothing.
- **Verify:** `find engine -name "*.cpp" | xargs clang-format --dry-run --Werror` — some files may need reformatting; scope the commit to the config only, defer reformatting.

### I3. 🟢 Engine-level `VERSION` + `CHANGELOG.md` (M27)
- **Files:** `VERSION` (new, contents: `0.1.0`), `CHANGELOG.md` (new, back-filled from ROADMAP.md highlights).
- **Rollback:** delete.
- **Depends on:** nothing.

### I4. 🟢 Pre-commit hook for mandatory CHANGELOG updates
- **Files:** `.pre-commit-config.yaml` (new), hook script.
- **Change:** for any commit touching `tools/audit/`, require `tools/audit/CHANGELOG.md` also touched (same commit). Same for `tools/formula_workbench/` and engine-level (if engine CHANGELOG lands).
- **Rollback:** delete.
- **Depends on:** I3 (engine CHANGELOG exists).

### I5. 🟢 `docs/` trend-snapshot cleanup (AT-D2 / L9)
- **Files:** audit tool — add `--keep-snapshots N` flag (default 5) that prunes old snapshots on run.
- **Additionally:** one-time `git rm docs/trend_snapshot_202604*.json` if user wants to purge the 5 currently-tracked ones. **Confirm before executing.**

---

## Batch J — Audit tool improvements (tool-internal; ship anytime)

### J1. Tier 2 Python-specific `shell=True` detection (AT-A2)
- Per `AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS.md §AT-A2`.

### J2. UUID api_key scan pattern (AT-A3)
- Per `§AT-A3`.

### J3. Git-ref validator + binary path validator (AT-A8)
- Part of C1a — covered there.

### J4. SARIF `originalUriBaseIds` + schema validation (AT-A6, M23)
- Per `§AT-A6`.

### J5. Subprocess output-size cap (AT-A7, M19)
- Per `§AT-A7`.

### J6. ReDoS heuristic or timeout (AT-A5, M20)
- Per `§AT-A5`.

### J7. Pattern tightening (M21)
- Per `AUDIT.md §M21` (5 pattern-specific tweaks).

### J8. CWE tagging (AT-A9)
- Per `§AT-A9`.

### J9. Web UI XSS + CSRF hardening (M24, L6)
- Per `AUDIT.md §M24, §L6`.

---

## Deferred to EXPERIMENTAL (not in this fix plan)

- E1 ASan run (investigative pre-work for H16)
- E2 Tree-sitter flow analysis
- E3 Workbench JIT parity testing
- E4 Shader reflection
- E5 Motion-vector pass unification (post G1)
- E6 SARIF upload to GitHub Code Scanning (post J4 + I1)
- E7 Formula IR refactor
- E8/E9 already promoted to I3/I1

---

## Proposed merge / release plan

**Ship in this order unless you override:**

1. **Batch A** (3 commits, hot-fix style): rotate NVD key, cppcheck suppressions, gitignore.
2. **Batch B** (1 commit): web UI path traversal.
3. **Batch C** in two parts: C1a (validation) → merge → C1b (shell refactor) → merge → C1c (NVD header).
4. **Batch D** (4 commits, one per finding).
5. **Batch E** (4 commits, one per finding).
6. **Batch F** (2 commits: F1 first, F2 bundled).
7. **Batch H** (after ASan run from E1).
8. **Batch G** — deferred unless you want it sooner. Largest single engine change.
9. **Batch I** (any time; I1 first to gate future PRs).
10. **Batch J** — audit-tool maintenance; ship as individual commits over time.

---

## ⚠️ Items needing explicit confirmation before execution

| Item | Reason |
|---|---|
| **A1** NVD rotation | External side effect (key invalidation / new key request) |
| **C1b** subprocess refactor | Touches all tiers; atomic ship |
| **F2** shader micro-fixes | Visual regression risk; each needs before/after screenshot |
| **G1** motion-vector pass | Large engine change; may be deferrable |
| **H1** imgui-node-editor SEGV | Investigative scope depends on ASan output |
| **I5** trend-snapshot purge | Removes tracked files; reversible via git but destructive to history UX |

---

**Stop here pending your review.** I will not start Phase 5 until you approve this plan (or an edited version of it).

When you approve, indicate:
1. Any items to drop, add, or re-order
2. Which ⚠️ items you want me to execute vs. leave for you
3. Whether to batch the batches (e.g., "ship A + B together as a hot-fix branch") or merge in order as individual PRs

I have NOT modified any source code so far. All artifacts (DISCOVERY, AUDIT, PHASE3_RESEARCH, EXPERIMENTAL, FIXPLAN, AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS) are at repo root ready for your review.
