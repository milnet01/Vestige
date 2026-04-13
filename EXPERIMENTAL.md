# Experimental features / tools worth trialing

Surfaced during the 2026-04-13 full audit. **Not** part of the fix plan — these are forward-looking ideas with rationale + risk, to be considered independently after FIXPLAN ships.

---

## E1. Valgrind / AddressSanitizer pass on the editor test-play cycle

- **What:** run `build/bin/vestige` (and especially the Script Editor Panel open → close cycle that triggers H16) under valgrind or ASan. The engine already has `asan_suppressions.txt` for known GLFW/fontconfig leaks; a full run would catch the imgui-node-editor shutdown SEGV stack frame.
- **Why:** H16 is a known SEGV whose workaround (empty settings path) masks the symptom. Root-cause is unknown; a one-shot ASan run is the cheapest way to find it.
- **Risk:** low — purely diagnostic. Binary size ~ 2–3× under ASan; frame rate drops.
- **Effort:** ~2 hours: rebuild with `-fsanitize=address -fno-omit-frame-pointer`, run until crash, inspect stack.

## E2. Tree-sitter-based intra-function flow analysis in the audit tool

- **What:** replace string-regex-only scans with a tree-sitter parse in Tier 2/4. Enables catching AT-A4 (Flask `request.*` → `Path()` without `resolve().relative_to()`) and AT-A2 (`subprocess.*shell=True` with dynamic args) with near-zero false positives.
- **Why:** the automated tool currently produces 4652 medium-severity findings — most noise. The 9E audit caught 4 critical + 9 high that the tool missed because they're flow-sensitive. Tree-sitter is cheap to run, has bindings for all 8 languages the tool supports, and enables a whole class of new high-signal rules.
- **Risk:** medium — adds a tree-sitter dependency; grammar versioning across languages; ~5–10% run-time increase.
- **Effort:** 1–2 weeks for MVP covering Python + C++. Start with AT-A4 as the proof of value.

## E3. Formula Workbench JIT parity testing

- **What:** link `libgccjit` or `tcc` into the workbench. On every fit completion, JIT-compile the codegen-C++ output and cross-check 32 random points against the evaluator's safe-math path. Flag any divergence.
- **Why:** direct fix for the H12 class (evaluator uses safe math, codegen emits raw math → fit ≠ runtime). Even after H12 is patched, parity testing guards against future drift.
- **Risk:** medium — `libgccjit` is GPL (license impact); `tcc` is LGPL but less reliable on modern C++. GLSL parity is harder (need real GL context for compile — but workbench already has one).
- **Effort:** 2–4 weeks. Prototype first with C++ only.

## E4. Shader reflection + automatic uniform-binding validation

- **What:** at shader compile time, parse reflection output (glslang / SPIRV-Cross provide JSON) and cross-check against C++ setUniform call sites. Flag declared-not-set and set-not-declared. The audit tool's `tier4_uniforms.py` does a regex approximation today; shader reflection would be ground-truth.
- **Why:** would have caught H14 (SH basis constant typo) if the uniform were even typed — C++ writes `L[0..8]`, shader reads `L[0..8]`, all present; but reflection-driven parity would at least surface the nine-term expansion pattern and let the audit tool scan the expansion against a known-good table.
- **Risk:** low-to-medium — SPIRV-Cross is well-maintained; just adds a build step.
- **Effort:** 1 week.

## E5. Motion-vector pass unification

- **What:** fix H15 properly by adding per-object prev-model motion vector output. Once implemented, consolidate the current `motion_vectors.frag.glsl` pass into the geometry G-buffer pass and write a vec2 motion attachment.
- **Why:** H15 blocks dynamic-object correctness for TAA. Unifying with the G-buffer pass saves one draw pass overall (perf win) and matches modern AAA engines.
- **Risk:** medium — touches the forward render loop. Must be verified against the full Tabernacle walkthrough + any animated-object demos.
- **Effort:** ~1 week.

## E6. SARIF 2.1.0 upload to GitHub Code Scanning

- **What:** fix M23 (missing `originalUriBaseIds`), then wire CI to upload `audit.sarif` to GitHub Code Scanning on every push. Would give inline PR annotations automatically.
- **Why:** maximizes the value of the existing SARIF output. The feature exists; just not plumbed to GH.
- **Risk:** low — read-only feature.
- **Effort:** half a day after M23 fix. Depends on M25 (CI being set up) first.

## E7. Formula-graph → GLSL DAG lowering (instead of current textual codegen)

- **What:** build a proper IR between `ExprNode` and emitted GLSL/C++. Currently the codegen is a recursive string-concatenation pass — a DAG representation would enable:
  - Common-subexpression elimination
  - Symbolic domain probes (FW-I1) without running the evaluator
  - Vectorization (compute bands in parallel)
  - Clean separation of identifier validation from emission
- **Why:** paves the way for real optimization passes. Today every formula evaluation walks the AST twice (safe-math evaluator + codegen), and any identifier-level security (H11) requires bolting on validation.
- **Risk:** high — significant refactor of the formula subsystem. Deferred from this audit scope.
- **Effort:** 2–3 weeks.

## E8. Engine-level `VERSION` + `CHANGELOG.md`

- **What:** add `VERSION` file + `CHANGELOG.md` at repo root, matching the mandatory-same-commit rule already applied to `tools/audit/` and `tools/formula_workbench/`. Bump via a pre-commit hook that refuses any commit touching `engine/` without a changelog entry.
- **Why:** M27 flags the gap. Tools have it; engine doesn't. Helps reviewers / future contributors / release tagging. Cheap.
- **Risk:** low — pre-commit hook needs tuning to avoid blocking legitimate merge commits.
- **Effort:** 2 hours.

## E9. Minimal CI (GitHub Actions)

- **What:** add `.github/workflows/ci.yml` — Linux-Debug + Linux-Release matrix runs build + ctest + `python3 tools/audit/audit.py -t 1 --ci`. Protect `main` with this as a required check.
- **Why:** M25 — no automated gate. Every audit is manual; the automated tool already supports CI annotations.
- **Risk:** low. GitHub Actions free-tier covers this.
- **Effort:** ~4 hours including tuning and secret management for NVD_API_KEY.

---

**Note on prioritization:** E1 and E9 are the fastest wins. E2 unlocks a whole class of audit-tool improvements (cross-references AT-A4). E3–E7 are larger investments with clearer payoff only after the Critical/High fixes in FIXPLAN land.
