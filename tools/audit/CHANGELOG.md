# Changelog

All notable changes to the Audit Tool are documented in this file.

## [2.6.0] - 2026-04-14

### Added (D4 — Tier 6: Feature Coverage)
- **New `lib/tier6_coverage.py` module.** Heuristic sweep that flags
  engine subsystems lacking test coverage. A subsystem (a top-level
  `engine/<name>/` directory) is considered "covered" when at least one
  test file under `tests/` references it via either:
  - `#include "engine/<name>/..."` / `<engine/<name>/...>`, or
  - A filename starting with `test_<name>`

  Coverage thresholds:
  - **0 test files** → MEDIUM finding (`tier6_no_coverage`)
  - **1 to (thin_threshold-1) test files** → INFO finding (`tier6_thin_coverage`)
  - **≥ thin_threshold** → silent (adequately covered)

  This is a *breadth* signal, not line/branch coverage. Its value is
  surfacing subsystems that nobody has written a single test for —
  closing another rigour gap versus the manual audit prompt, which asks
  reviewers to walk every subsystem and check for tests.

### Config
- New `tier6` section in `audit_config.yaml` / `DEFAULTS`:
  ```yaml
  tier6:
    enabled: true
    engine_dir: "engine"
    tests_dir: "tests"
    thin_threshold: 3
    excluded_subsystems: []   # on top of built-in ["testing"]
  ```
- Default `tiers` list now `[1, 2, 3, 4, 5, 6]`.

### Pipeline integration
- Tier 6 joins the parallel-tier thread pool (with tiers 2, 3, 4) since
  it's filesystem-only and independent of other tiers' findings.
- `Finding.source_key` fallback (`f"tier{N}"`) already correctly maps
  tier-6 findings to `tier6`, so they don't cross-corroborate with
  other tiers. No changes to the corroboration layer.

### Rendering
- New `_build_tier6_section()` in `lib/report.py`. Produces a compact
  two-table section: uncovered subsystems (MEDIUM) and thin coverage
  (INFO). Section header documents the heuristic so readers understand
  that visible but unused tests (math mirrored from shaders, etc.)
  legitimately won't be detected.

### Tests
- `tests/test_tier6_coverage.py` — 24 tests covering subsystem
  discovery, coverage counting (via include, via filename prefix,
  both signals), and the `run()` integration (disabled, no engine
  dir, zero/thin/adequate coverage, custom exclusions, threshold
  tuning, source_tier/source_key correctness).

Totals: 604 (2.4.0) → 642 (2.5.0) → **666 tests passing** (2.6.0).

### Migration
Existing configs that pre-date Tier 6 pick up the new defaults
automatically. To disable the new tier:
```yaml
tier6:
  enabled: false
```
Or drop `6` from the `tiers` list. No behaviour change to tiers 1-5.

## [2.5.0] - 2026-04-14

### Added (D3 — Human-review verification layer)
- **`.audit_verified` file + `[VERIFIED]` tag.** Maintainers who review a
  report and confirm a finding is real can now persist that decision
  across runs. Matching findings render with a `[VERIFIED]` prefix in
  tier tables and surface as `verified: true` in JSON/SARIF output.

  Verification is a **tag, not a filter** — verified findings stay
  visible. The tag lets reviewers distinguish "reviewed and real, still
  needs fixing" from "not yet looked at" without losing the finding.
  Suppression (`.audit_suppress`) is the existing filter; a finding can
  legitimately be both verified (yes, real) and suppressed (but hide it
  anyway), in which case suppression still wins.

  Pipeline ordering:
  1. deduplicate
  2. corroborate  (D2, 2.4.0)
  3. severity_overrides
  4. **verified tagging**  (D3, 2.5.0)
  5. suppression filter

### CLI
- `--verified-show` — list currently verified keys and exit
- `--verified-add KEY` — add a dedup_key to `.audit_verified` and exit
- `--verified-remove KEY` — remove a key from `.audit_verified` and exit

Mirrors the existing `--suppress-show` / `--suppress-add` pattern.

### Changed
- `lib/findings.py`:
  - `Finding` gained a `verified: bool = False` field.
  - `to_dict()` surfaces `verified: true` only when True (compact output
    preserved for the common unverified case).
  - `deduplicate()` merges the `verified` flag across same-dedup_key
    entries so a verified tag isn't silently lost on a tier-ordering
    swap (same shape as the `corroborated_by` merge from 2.4.0).
- `lib/report.py`:
  - `_corr_prefix()` now renders `[VERIFIED][CORROB]` combinations,
    with `[VERIFIED]` first because human review is the stronger
    signal than mere multi-tool agreement.
  - Executive Summary JSON now carries a `verified` count alongside
    the existing `corroborated` count.
- `lib/runner.py`: added verified-tagging step between
  severity_overrides and suppression.

### New files
- `lib/verified.py` — `load_verified`, `apply_verified_tags`,
  `save_verified`, `remove_verified`. Mirrors `lib/suppress.py`.
- `tests/test_verified.py` — 25 tests covering file I/O, tagging,
  dedup flag preservation.

### Tests
- `tests/test_findings.py`: +3 tests for the `verified` field on
  Finding / `to_dict()`.
- `tests/test_report.py`: +4 tests for `[VERIFIED]` rendering and the
  new `verified` executive-summary count.

### Migration
No config changes required. The feature is dormant until you start
adding keys to `.audit_verified` (or use `--verified-add`). Existing
`.audit_suppress` workflows are unchanged.

## [2.4.1] - 2026-04-14

### Changed (open-source prep — personal-path scrub)
- `lib/agent_playbook.py`, `web/app.py`, `AUDIT_TOOL_STANDARDS.md`: removed
  hardcoded absolute paths (`/mnt/Storage/...`) from docstrings, comments,
  and example code. No behaviour change — the runtime code in
  `web/app.py` was already portable via `AUDIT_ROOT.parent.parent`; only
  the trailing explanatory comment leaked the path.
- `CHANGELOG.md`: rewrote the 2.4.0 `/mnt/Storage/Scripts/audit_prompt.md`
  reference to describe the manual audit prompt generically, so the
  historical entry no longer leaks the maintainer's filesystem layout.

Part of the pre-open-source audit checklist (§3 Personal / Machine-Specific
Data). See `docs/PRE_OPEN_SOURCE_AUDIT.md`.

## [2.4.0] - 2026-04-14

### Added (D2 — Cross-source corroboration layer)
- **Findings flagged by two or more independent sources at the same
  `(file, line)` are now tagged `[CORROB]` and, by default, promoted
  one severity level.** Closes the biggest remaining rigour gap versus
  the manual audit prompt, which explicitly weights findings by how
  many independent signals converge on the same location.

  Conversely, *solo* (uncorroborated) hits whose `pattern_name` is in
  the configured `demoted_patterns` list are demoted to INFO — these
  are the known-noisy tier-2 patterns (`std_endl`, `push_back_loop`,
  `todo_fixme`, `dead_code_markers`, `shared_ptr`) where a single match
  in isolation is rarely actionable and drowns out real issues.

  Severity adjustments run **before** `severity_overrides` and
  `suppressions`, so an explicit user override or a SUPPRESS.yaml entry
  still has the final say.

### Changed
- `lib/findings.py`:
  - `Finding` gained two new fields:
    - `corroborated_by: list[str]` — sorted list of other `source_key`
      values that flagged the same `(file, line)`. Empty list = solo
      finding.
    - `source_key` (computed property) — stable identifier used by the
      corroboration layer to decide whether two findings came from
      *independent* mechanisms. Mapping: tier 1 cppcheck → `cppcheck`,
      tier 1 clang-tidy → `clang_tidy`, other tier 1 → `build`, tier 2
      (all patterns) → `pattern_scan` (same grep-based mechanism,
      intentionally not self-corroborating), tier 3 → `tier3_diff`,
      tier 4 → `tier4_{category}` (each submodule distinct), tier 5 →
      `tier5_research`.
  - `to_dict` surfaces `corroborated_by` only when non-empty, keeping
    the JSON sidecar and SARIF output compact for solo findings.
  - `deduplicate` now merges `corroborated_by` across collapsed
    duplicates so dedup-ordering swaps don't lose signal.
- `lib/corroboration.py` (new module):
  - `corroborate(findings, config)` groups findings by `(file, line)`,
    tags any with ≥2 distinct `source_key` values, applies
    `promote_level` severity bump (clamped at CRITICAL), and demotes
    solo `demoted_patterns` hits to INFO.
  - `promote_level=0` → tag-only mode (no severity change).
  - `enabled: false` → full no-op.
  - Findings without a line number can't be positionally corroborated
    and skip both promotion and demotion.
- `lib/runner.py`:
  - Pipeline reordered to `dedup → corroborate → overrides → suppress`
    so user overrides remain the final arbiter of severity.
  - `ReportBuilder.build()` still calls `deduplicate()` (idempotent —
    protects future callers that bypass the runner pipeline).
- `lib/report.py`:
  - Tier 1 cppcheck/clang-tidy tables, Tier 2 pattern table, and Tier 3
    diff-security table all render `[CORROB]` on corroborated entries
    (short tag to minimise token-cost inflation).
  - Executive Summary JSON gained `findings.corroborated` count so the
    downstream agent can see the multi-source-signal total at a glance.
- `audit_config.yaml`:
  - New `corroboration:` block: `enabled` (default `true`),
    `promote_level` (default `1`), `demoted_patterns` (seeded with the
    five tier-2 patterns above).

### Tests
- `tests/test_findings.py` — 14 new cases covering:
  - `source_key` mapping for all five tiers (cppcheck, clang_tidy,
    build default, pattern_scan, tier3_diff, per-category tier 4,
    tier5_research, unknown-tier fallback).
  - `corroborated_by` omission when empty / inclusion when populated.
  - `deduplicate` merging `corroborated_by` when swapping the kept
    finding and when keeping the first finding.
- `tests/test_corroboration.py` (new file) — 23 cases covering:
  - Solo / same-source / different-line / different-file / `None`-line
    findings do NOT corroborate.
  - cppcheck + pattern, three-source, and tier-4 submodule corroboration.
  - Severity promotion (`MEDIUM→HIGH`, `LOW→MEDIUM`, `promote_level=2`,
    CRITICAL clamping, `promote_level=0` tag-only, solo not promoted).
  - Demotion of noisy solo patterns, non-demotion when corroborated,
    non-touching of non-noisy patterns or already-INFO findings.
  - `enabled=false` no-op, `None` config defaults to enabled, empty
    list, in-place identity, merging with preexisting tags.

### Migration notes
- Existing `audit_config.yaml` files without a `corroboration:` block
  pick up the default behaviour (enabled, `promote_level=1`, empty
  `demoted_patterns` — so no automatic demotion, but corroboration
  still tags+promotes). To opt out entirely, set `corroboration:
  {enabled: false}`.

## [2.3.0] - 2026-04-14

### Added (D6 — Version-scoped NVD queries / `affects_pinned` tag)
- **Each NVD CVE is now tagged with whether the project's pinned
  dependency version actually falls into its affected-version range.**
  Turns Tier 5 from "here are CVEs that mention the keyword GLFW" into
  "here are CVEs that *actually affect* GLFW 3.4 as pinned in
  `external/CMakeLists.txt`" — the biggest signal-to-noise win in
  Tier 5 since cache-backed research landed.

  Three-state tag (`True`/`False`/`None`):
  - `[AFFECTS PINNED 3.4]` title prefix — the pinned version falls in
    at least one vulnerable cpeMatch range; review is warranted.
  - `[unaffected@3.4]` title prefix — configurations parsed cleanly and
    the pinned version is *not* in any vulnerable range; keyword-match
    noise, de-emphasised.
  - No prefix — unknown (no pinned version supplied, or no parseable
    CPE configurations in the CVE).

  CVEs are sorted so `affects_pinned=True` leads each NVD query,
  secondary-sorted by CVSS descending.

### Changed
- `lib/tier5_nvd.py`:
  - New helpers: `parse_semver` (handles `3.4`, `v2.9.4`, `VER-2-13-3`,
    `1.24.1`; returns `None` for `master`/`docking`), `version_in_range`
    (mirrors NVD's four-bound `cpeMatch` shape — `versionStartIncluding`,
    `versionStartExcluding`, `versionEndIncluding`, `versionEndExcluding`,
    with fallthrough to exact-pin), `_extract_cpe_version` (field 5 of
    CPE 2.3 URI), and `cve_affects_version` (tri-state decision over a
    CVE's full `configurations` tree).
  - `CVEResult` gained `affects_pinned: bool | None` and
    `pinned_version: str` fields. `to_dict` surfaces both the title
    prefix and the raw fields (so downstream consumers can filter).
  - `query_nvd` accepts `pinned_version`, parses CVE `configurations`,
    computes `affects_pinned` per result, and returns the list
    pre-sorted (affects-pinned first, then by CVSS descending).
  - `run_nvd_queries` accepts either string-form deps (legacy) or
    dict-form `{name, version}`. Malformed entries (int/list/None,
    dicts without a name) are logged and skipped — no crash. Cache
    key incorporates the pinned version so a version bump busts the
    cache cleanly.
- `audit_config.yaml`:
  - Vestige's NVD dependency list switched to dict form with pinned
    versions tracking `external/CMakeLists.txt` as of 2026-04-14:
    GLFW 3.4, FreeType 2.13.3 (from GIT_TAG `VER-2-13-3`), OpenAL 1.24.1,
    nlohmann/json 3.12.0, JoltPhysics 5.2.0, Recast 1.6.0, tinygltf 2.9.4,
    googletest 1.15.2.
  - Added the four newly-scanned deps (JoltPhysics, Recast, tinygltf,
    googletest) — previously only keyword-matched via web search; now
    CPE-filtered for pinned-version relevance.
  - `stb image` kept as a legacy string entry (header-only, no
    published version to pin against).

### Tests
- `tests/test_tier5_nvd.py` — 47 new cases covering:
  - `parse_semver` across all observed GIT_TAG formats + unparseables.
  - `version_in_range` over all four bound combinations plus
    start-excluding / end-including / exact-only / unparseable-bound
    edge cases.
  - `_extract_cpe_version` shape handling (wildcards, short strings).
  - `cve_affects_version` tri-state over: in/above/below range, no
    pinned version, unparseable pinned, no configurations, non-
    vulnerable cpeMatch nodes ignored, exact CPE-version match,
    multiple-nodes any-match wins.
  - `CVEResult.to_dict` title-prefix rendering for True/False/None +
    edge case (affects_pinned=True with empty pinned_version → no
    trailing-space artefact).
  - `query_nvd` sort order: affects-pinned first even when secondary
    CVSS is lower; no-pinned-version falls through to CVSS-descending.
  - `run_nvd_queries`: dict form passes pinned_version through; string
    form passes None; mixed forms all run; malformed entries skipped;
    dicts without `name` skipped; cache key changes with version
    (version bump busts cache).
- Full audit suite: 568 passed.

### Design notes
- Auto-detection of pinned versions from `external/CMakeLists.txt`
  `FetchContent_Declare GIT_TAG` was explicitly kept out of scope for
  D6. That detector belongs in `lib/auto_config.py` (where project
  scanning already lives) and would ship as a separate follow-up;
  D6 just gives the audit config the shape it needs to accept the
  data.
- Tri-state `affects_pinned` (`True`/`False`/`None`) is deliberate.
  Conflating False with None (the "couldn't decide" case) would mask
  missing-data situations and let a potentially-relevant CVE slip past
  review unflagged. The title-prefix renderer uses only confident
  verdicts; None gets no prefix, preserving the caller's visibility.
- `parse_semver` is intentionally permissive (extracts digit runs) so
  it handles FreeType's `VER-N-N-N` tag style without a custom parser,
  while still keeping the resulting tuple comparable. Pre-release
  suffixes (`-rc1`) keep their numeric prefix; this is fine for NVD
  range filtering which always uses plain numeric versions.

## [2.2.0] - 2026-04-14

### Added (D5 — Baseline Comparison)
- **Run-over-run delta block at the top of every report.** New
  `_build_baseline_section` in `lib/report.py` answers "what changed
  since the last audit?" without forcing reviewers to diff JSON sidecars
  by hand. Rendered automatically on any run with at least one prior
  trend snapshot — no `--diff` flag required, no opt-in config knob.

  Section content (only non-zero deltas rendered, to keep noise low):
  - **Build** — the previous→current transition (`OK→FAILED`, `FAILED→OK`)
    or a single label when unchanged.
  - **Tests** — passed/failed pass-count deltas.
  - **LOC / Files / Duration** — codebase metric deltas.
  - **Findings** — total delta + by-severity breakdown
    (critical → high → medium → low → info).
  - **Top movers** — up to 5 categories with the largest absolute change.

  When the previous snapshot is a legacy (pre-2.2.0) record without
  baseline metadata, build/test/LOC lines are suppressed so we don't
  report a misleading "+100 tests passed" diff against zeros. The build
  status, when known, still surfaces with a "no prior baseline" caveat.

### Changed
- `lib/trends.py`:
  - `TrendSnapshot` extended with optional baseline fields: `build_ok`
    (`bool | None` — None means "not captured"), `build_warnings`,
    `build_errors`, `tests_passed`, `tests_failed`, `tests_skipped`,
    `total_loc`, `file_count`, `duration_seconds`. All defaulted; older
    snapshot files deserialize cleanly via `from_dict`.
  - `save_snapshot` accepts new optional kwargs: `tier1_summary`,
    `audit_data`, `duration`. Legacy callers (no kwargs) still work; the
    snapshot just won't carry baseline info, which the comparator
    detects.
  - New `BaselineComparison` dataclass + `compute_baseline_comparison`
    function. Compares newest vs second-newest snapshot (immediate
    run-over-run delta), distinct from the existing `compute_trends`
    which does newest-vs-oldest (long-horizon trajectory).
- `lib/runner.py`:
  - `run()` now passes `tier1_summary`, `audit_data`, and `duration`
    into `save_snapshot` so each snapshot captures baseline metadata.
  - `build_report()` loads snapshots, computes the comparison, and
    forwards it to `ReportBuilder.build()`.
- `lib/report.py`:
  - `ReportBuilder.build()` accepts a new optional `baseline_comparison`
    parameter; the renderer is a no-op when None (first run case).
  - Helpers `_signed`/`_signed_float` for delta formatting.

### Tests
- `tests/test_trends.py` — 16 new cases covering snapshot baseline-field
  round-trip, save_snapshot capture, missing-key safety, and
  `compute_baseline_comparison` over six scenarios (single snapshot,
  finding delta, severity deltas, category movers, build transitions,
  legacy previous, test/LOC deltas).
- `tests/test_report.py` — 10 new cases on the rendered section: absent
  on first run, present + headers when comparison provided, build
  transition rendering, test/LOC delta rendering, no-op suppression,
  legacy-previous caveat path, severity ordering, top-mover cap, and
  total-report token budget.

### Design notes
- The `Differential Report` (opt-in, finding-level new/resolved) and
  `Finding Trends` (long-horizon trajectory) sections remain unchanged.
  The new Baseline Comparison fills the immediate-delta gap that used
  to require visual inspection of two consecutive reports.
- Section position: directly after Executive Summary, before Diff. The
  ordering reads as "current state → what changed since last → which
  findings are new specifically".
- Backwards compatibility is preserved both ways: legacy snapshots load
  with neutral defaults, and legacy callers of `save_snapshot` keep
  working without recompile.

## [2.1.0] - 2026-04-14

### Added (D1 — Agent Playbook)
- **Inline 5-phase audit prompt at the top of every report.** New
  `lib/agent_playbook.py` renders a "Read This First" section above the
  Executive Summary that instructs the downstream LLM consumer (a Claude
  Code session handed this report) to follow Baseline → Verify → Cite →
  Approval Gate → Implement+Test rather than jumping straight to fixes.
  Closes the largest rigour gap between the automated report output and
  the manual-audit prompt used when running audits by hand.

  The playbook is parameterised by:
  - `tier1_summary` — if build failed or tests failed > 0, a
    **Baseline is already broken** callout opens the section so the agent
    surfaces pre-existing breakage before running Phase 2.
  - Approval-gate threshold — controls the "wait for user approval at
    {severity} or higher" sentence in Phase 4.

  Size: ~600 tokens, well inside the 800-token budget asserted in
  `tests/test_agent_playbook.py::TestBuildTokenBudget`. 25 new tests
  cover threshold parsing, phase rendering, baseline-broken branching,
  parametrised threshold phrasing, and the token ceiling.

### New config knobs (default both enabled)
- `report.include_playbook: true` — set to `false` to skip the playbook
  (e.g. for downstream consumers that already have the instructions).
- `report.approval_gate_threshold: "medium"` — one of `critical`, `high`,
  `medium`, `low`, `info`. Findings below this severity may be auto-fixed
  by the agent without waiting for user approval. Default `medium` mirrors
  the manual audit's implicit threshold.

### Design notes
- Renderer is pure (no I/O, no mutation) so unit tests are trivial.
- Import in `lib/report.py` is `from . import agent_playbook` so the
  renderer is called as `agent_playbook.build(...)` — mirrors the
  existing tier module style.
- Findings list is accepted but not inlined — counts already live in the
  Executive Summary below the playbook, so duplication is avoided.
- Pinned-version CVE scoping and finding corroboration (planned D2 / D6)
  will build on this scaffold: the playbook content is a pure function of
  inputs, so later tiers can pass a richer `tier1_summary`-like dict
  without changing the callers.

## [2.0.12] - 2026-04-14

### Fixed
- **CI `audit-tool-tier1` job reported 4 Timer test failures** after the
  2.0.11 parallelism fix let ctest complete: `TimerTest.InitialState`,
  `.UpdateReturnsDeltaTime`, `.DeltaTimeIsClamped`, `.ElapsedTimeIncreases`.
  Root cause: `test_timer.cpp` calls `glfwInit()` in SetUp, which fails
  in a headless environment. `linux-build-test` avoids this by wrapping
  ctest with `xvfb-run --auto-servernum`; the audit job never got the
  same treatment. Previous job failures (timeout, cppcheck HIGHs) were
  tripping earlier, so this regression didn't surface until now. See CI
  run 24395499747.

### Changed
- `.github/workflows/ci.yml` `audit-tool-tier1` job:
  1. Apt-installs `xvfb` alongside other build deps.
  2. Wraps the `python3 tools/audit/audit.py -t 1 --ci --no-color`
     invocation in `xvfb-run --auto-servernum`, matching linux-build-test.
  The audit tool itself / `audit_config.yaml` stays untouched — the
  display requirement is Vestige-test-specific, handled in the workflow
  rather than leaked into the generic audit config.

### Follow-up (out of scope)
- `engine/core/timer.{h,cpp}` uses `glfwGetTime()` for delta/FPS. Swap
  to `std::chrono::steady_clock` so `test_timer.cpp` doesn't need a
  display. Behaviour is identical on Linux (both ultimately read
  `clock_gettime(CLOCK_MONOTONIC)`). Deferred to avoid scope creep.

## [2.0.11] - 2026-04-14

### Fixed
- **Tier 1 `test_cmd` ran ctest serially**, taking ~9-10 min on GH free
  runners (1768 tests × ~0.3 s each) and tripping the 600 s audit-internal
  subprocess timeout. Tests reported as `0 passed, 0 failed, 0 skipped`
  because ctest was killed before it could produce any result. The fix
  is an invocation change, not a ceiling bump: pass `-j $(nproc)` so
  tests run in parallel (they complete in ~42 s locally). Fixtures are
  parallel-safe after the 2026-04-14 tmpdir fix (`test_scene_serializer.cpp`,
  `test_file_menu.cpp`). See CI run 24394450313.

### Changed
- `static_analysis.clang_tidy.timeout`: `600` → `1200` s. 50 files × 5
  heavy check categories (bugprone-*, performance-*, modernize-*,
  readability-*, cppcoreguidelines-*) finishes in ~3 min locally but
  takes ~10+ min on GH free runners. The old ceiling was tripping
  mid-run. Legitimate bump per CLAUDE.md rule #10 — the work is real,
  not redundant, and not masking a bug.

## [2.0.10] - 2026-04-14

### Changed
- `audit_config.yaml` — updated the `--inline-suppr` comment to reflect
  the current false-positive sites after engine-side fixes landed:
  `engine/scene/entity.cpp` (return-ptr-after-std::move) and
  `engine/formula/node_graph.cpp` (post-resize access). Previous comment
  referenced stale line numbers.

### Notes
- The `--ci` exit-code contract (0 clean / 1 HIGH / 2 CRITICAL) did its
  job this release: it surfaced three pre-existing HIGH cppcheck findings
  in engine source that inline suppressions were supposed to silence but
  weren't attaching to the right lines. Engine-side fix shipped in the
  same commit as this bump (see repo history). No tool change required.

## [2.0.9] - 2026-04-14

### Fixed
- **CI `audit-tool-tier1` job hit its 20-min job ceiling before clang-tidy
  could finish**, cancelling the run. The 2.0.8 fix (explicit Build step +
  FetchContent cache) did its job — audit-internal `cmake --build` was a
  warm 5-second no-op, cppcheck completed in ~1.5 min — but the cold
  Debug build itself takes ~12 min on GH free runners and clang-tidy over
  50 files × 5 heavy check categories (bugprone-*, performance-*,
  modernize-*, readability-*, cppcoreguidelines-*) needs ~8-10 min on
  top. Total cold-start cost ~22 min; 20-min ceiling was never going to
  hold. See milnet01/Vestige CI run 24391001628 / job.

### Changed
- `.github/workflows/ci.yml` `audit-tool-tier1.timeout-minutes`:
  `20` → `45` (matching `linux-build-test`). Legitimate ceiling bump per
  CLAUDE.md rule #10 — the work inside the job is real (cold build +
  heavy static analysis), not redundant or masking a bug. Warm runs
  finish much faster thanks to the `build/_deps` cache added in 2.0.8.

## [2.0.8] - 2026-04-14

### Fixed
- **CI `audit-tool-tier1` job was failing with false Build/Test
  failures.** The job configured `build/` but never ran a build step.
  When `audit.py -t 1` subsequently invoked `cmake --build build 2>&1`
  (per `audit_config.yaml` `build.build_cmd`), it hit a cold build of
  the engine + FetchContent deps from scratch inside its own 300 s
  subprocess timeout. The timeout tripped, `Build: FAILED, 0 warnings,
  0 errors` was reported (though the build would succeed given more
  time), and the subsequent test step saw only 3 tests (0/1/2) because
  the test binary was never produced — masking the fact that the
  project has 1768 tests. Clang-tidy then ran against 50 files cold
  and also hit its 600 s ceiling. See milnet01/Vestige CI run
  24385438965 / job 71218095739.

### Changed
- `.github/workflows/ci.yml` `audit-tool-tier1` job now:
  1. Restores the `build/_deps` FetchContent cache (same pattern
     `linux-build-test` already uses) so GLFW / glm / JoltPhysics /
     etc. don't re-download from scratch every run.
  2. Runs `cmake --build build -j $(nproc)` as an explicit native CI
     step **before** invoking the audit tool. The native step has no
     internal subprocess timeout, so cold builds finish. When
     `audit.py` subsequently runs its own `cmake --build build`, the
     tree is warm and the invocation is a no-op, leaving budget for
     cppcheck + clang-tidy.

  Root-cause fix, not a timeout bump — per CLAUDE.md rule #10,
  raising the 300 s ceiling would have masked the real issue
  (redundant cold build inside the audit job).

## [2.0.7] - 2026-04-14

### Fixed
- **CI Tier 1 job was failing on every push to `main`** with
  `audit.py: error: unrecognized arguments: --no-color`. The
  `.github/workflows/ci.yml` "Audit tool Tier 1 (static)" step invokes
  `python3 tools/audit/audit.py -t 1 --no-color`, but argparse didn't
  define `--no-color`, so the step exited non-zero before any audit ran
  (and the artifact upload subsequently reported "No files were found",
  masking the root cause as a missing-report issue). Run IDs on
  milnet01/Vestige: 24368940403, 24362882238, and every push since the
  workflow landed the flag.

### Added
- `--no-color` CLI flag implementing the
  [NO_COLOR](https://no-color.org) convention. When set (or when
  `NO_COLOR` is already present in the environment, or stdout is not
  attached to a TTY), the flag writes `NO_COLOR=1` into `os.environ`
  so that child subprocesses — cppcheck, clang-tidy, git — inherit
  the preference and suppress their own ANSI escape sequences. The
  audit tool itself currently emits no ANSI; suppression is delegated
  to children, which is the useful behaviour for CI log readability.
- `apply_no_color(explicit, env, stdout_is_tty)` helper in `audit.py`,
  exposed for unit testing. Kept as a free function (not a class
  method) so tests can inject `env` and `stdout_is_tty` without
  constructing a full argparse namespace.
- `tests/test_audit_cli.py` — 9 tests covering explicit flag, env-var
  inheritance, non-TTY auto-detection, empty-string edge case, and an
  end-to-end `subprocess` check that the exact failing CI invocation
  (`-t 1 --no-color`) now parses without `unrecognized arguments`.

### Changed
- `.github/workflows/ci.yml` Tier 1 step now invokes
  `audit.py -t 1 --ci --no-color` (previously `-t 1 --no-color`).
  Adding `--ci` aligns the workflow with the design in
  `AUDIT.md §M25`, `EXPERIMENTAL.md`, and `FIXPLAN.md:248`: per-finding
  `::error::` / `::warning::` annotations, a step summary written to
  `$GITHUB_STEP_SUMMARY`, and severity-keyed exit codes (0 clean / 1
  HIGH / 2 CRITICAL). As a side-benefit, `--ci` always invokes
  `runner.build_report`, so the artifact-upload step no longer logs
  `No files were found with the provided path:
  docs/AUTOMATED_AUDIT_REPORT.md`.

## [2.0.6] - 2026-04-13

### Audit fallout (AUDIT.md §L7, §M19, §M21, §M23, §L9 / FIXPLAN J + I5)

- **§L7**: NVD response body capped at 16 MB. `tier5_nvd.query_keyword`
  was previously unbounded; a compromised proxy or gzip bomb could wedge
  the process.
- **§M19 / §J5**: subprocess output capped at 64 MB per stream. cppcheck
  on large codebases used to be able to emit 100+ MB of XML into stderr
  and blow past `capture_output`'s default memory. Helper `_capped()`
  in `utils.py` logs a warning when truncation occurs.
- **§M21 / §J7**: pattern-scanner false-positive fixes:
  - `raw_new` Qt-widget exclude now lists the actual Qt classes instead
    of `new Q\\w+\\(` (which swallowed ordinary types starting with 'Q').
  - `predictable_temp` anchored with `\\b` so `my_mktemp_wrapper(` no
    longer false-positives.
  - `null_macro` excludes `#define FOO_NULL 0` and `*_NULL` tokens.
- **§M23 / §J4**: SARIF output now emits `run.originalUriBaseIds` with
  a `SRCROOT` binding (uriBaseId: `SRCROOT`, not `%SRCROOT%`). Fixes
  GitHub Advanced Security and VS Code SARIF viewer rejection.
- **§L9 / §I5**: new `--keep-snapshots N` CLI flag. When set, the trend
  snapshot saver retains only the N most recent `trend_snapshot_*.json`
  files in `docs/`. Default behaviour is unchanged (keep all).

## [2.0.5] - 2026-04-13

### Security
- **CRITICAL: Subprocess shell=False refactor** (AUDIT.md §C1 / FIXPLAN C1b). `run_cmd` previously defaulted to `shell=True` and took a single command string; callers built that string with f-strings from YAML-supplied values, giving any reader of `audit_config.yaml` an injection vector through `binary:`, `args:`, and `build_dir:` — and any caller of `POST /api/run` an injection vector through `base_ref`.
  - Split into two explicit functions: `run_cmd(cmd: list[str], …)` is now the type-enforced default (refuses strings via `TypeError`), and `run_shell_cmd(cmd: str, …)` is the opt-in for user-authored strings that legitimately need shell interpretation (`build_cmd: "cd build && ctest"`, `test_cmd`).
  - Updated all internal callers to use argv lists: `tier1_cppcheck`, `tier1_clangtidy` (both cmake auto-gen and the main call), and `tier3_changes` (four git invocations).
  - `tier1_build` routes user-authored strings through `run_shell_cmd` explicitly — combined with the C1a validation guards, shell=True is still used for `build_cmd`/`test_cmd` but the paths reaching it are named, auditable, and non-attacker-steerable.
- Added 7 tests in `tests/test_utils_subprocess.py` covering both positives and the specific negative case of "metacharacter in argument does not spawn a subshell". The `touch /tmp/pwn_c1b_refactor_test` probe confirms shell metachars are literal strings under the new contract.

## [2.0.4] - 2026-04-13

### Security
- **HIGH: Input-validation guards at web UI `/api/run`** (AUDIT.md §C1 / FIXPLAN C1a). `base_ref` is now matched against `^[A-Za-z0-9._/~^-]{1,128}$`, `project_root` and `config_path` checked via `_is_safe_path()`, and `tiers` shape-validated (must be list[int] in [1,5]). This narrows the command-injection surface that `shell=True` in `run_cmd` exposes; the full `shell=False` refactor is deferred to the upcoming C1b.
- **HIGH: NVD API key shape validation** (AUDIT.md §H4 / FIXPLAN C1c). `_resolve_api_key()` now rejects keys that don't match `^[A-Za-z0-9-]{16,64}$`, preventing CRLF header injection via a malicious `NVD_API_KEY` env var. Existing `TestResolveApiKey` tests updated to use realistic UUID-shaped keys; three new tests cover CRLF, too-short, and space-containing keys.
- Added 6 new tests (`test_api_run_rejects_injection_in_base_ref`, `test_api_run_rejects_backtick_in_base_ref`, `test_api_run_accepts_normal_git_refs`, `test_api_run_rejects_bad_tiers`, and NVD rejection cases). Full suite: 453 passing.

## [2.0.3] - 2026-04-13

### Security
- **HIGH: Path-traversal fix in web UI GET/POST endpoints** (AUDIT.md §H1-H3). Three routes accepted a user-supplied path without containment: `GET /api/report`, `GET /api/config`, and `POST /api/init`. The sibling PUT `/api/config` was hardened in 2.0.0 but its three siblings were overlooked — an attacker on 127.0.0.1:5800 (e.g. a malicious local site via CSRF) could read arbitrary files (`/etc/passwd`, SSH keys) and overwrite files outside the project tree. Introduced `_is_safe_path()` helper that canonicalises via `Path.resolve()` + `is_relative_to()` against the allowed-root list, and refactored all four endpoints to use it. Added 11 tests in `tests/test_web_app.py`.

## [2.0.2] - 2026-04-13

### Fixed
- **False-positive flood**: cppcheck now runs with `--inline-suppr` (in both the existing `audit_config.yaml` and the `--init` template via `auto_config.py`). In-source `// cppcheck-suppress <rule>` comments are now honoured, eliminating ~6 HIGH findings per run at `engine/formula/node_graph.cpp:495`, `engine/scene/entity.cpp:60,79`, and `tests/test_command_history.cpp:244-246` that were correctly suppressed in source but re-reported every audit (AUDIT.md finding AT-A1).

## [2.0.1] - 2026-04-13

### Security
- **CRITICAL: Scrubbed committed NVD API key** from `audit_config.yaml:272` (audit finding C2). Literal `api_key:` field is now `null` by default; use the `NVD_API_KEY` env var only. `_resolve_api_key` emits a warning if a future committer re-adds a literal.

## [2.0.0] - 2026-04-11

### Added
- **Cognitive complexity analysis (Tier 4)**: New `tier4_cognitive.py` module implementing SonarSource's cognitive complexity algorithm — per-function scoring with nesting penalty, logical operator tracking, and control flow break counting for C/C++, Python, Rust, Go, Java, JS, TS
- **SARIF 2.1.0 output**: New `sarif_output.py` module and `--sarif` CLI flag — generates industry-standard SARIF JSON for GitHub Code Scanning and VS Code integration
- **Buffer overflow detection (Tier 2)**: 8 new patterns — `gets` (CWE-242), `strcpy`/`strcat`/`sprintf`/`vsprintf` (CWE-120), `scanf` family (CWE-120), format string injection (CWE-134), off-by-one `<= .size()` (CWE-193)
- **Memory leak detection (Tier 2)**: `malloc`/`calloc`/`realloc` (CWE-401), `fopen` without RAII (CWE-772), use-after-`std::move` (CWE-416)
- **Modern C++ best practices (Tier 2)**: 8 new patterns — deprecated `auto_ptr`/`random_shuffle`/`bind1st`, deprecated throw spec, throw in destructor, catch by value (slicing), raw mutex lock (CWE-667), non-const mutable statics (CWE-362)
- **Python best practices (Tier 2)**: 5 new patterns — mutable default arguments, wildcard imports, builtin shadowing, `== None` (PEP 8), assert in production
- **Secret/credential detection (Tier 2)**: 5 patterns — hardcoded passwords (CWE-798), private keys (CWE-321), AWS access keys, GitHub tokens, generic API keys
- **Performance anti-patterns (Tier 2)**: `std::string`/`std::vector` passed by value, `.size() == 0` vs `.empty()`
- **Include ordering analysis (Tier 4)**: Checks C++ source files for Google-style include order (corresponding header → C system → C++ stdlib → third-party → project)
- **Deep circular dependency detection (Tier 4)**: Upgraded from direct A↔B detection to full Tarjan's SCC algorithm for multi-file cycles (A→B→C→A)
- **Configurable cognitive complexity threshold** in audit_config.yaml (`tier4.cognitive.threshold`, default 15)
- Test suites: `test_tier4_cognitive.py`, `test_sarif_output.py`, `test_tier4_includes_extended.py`

### Fixed
- **Path traversal in `/api/config` PUT** — replaced `str.startswith()` with `Path.is_relative_to()` for proper canonicalization
- **Unbounded event queue** — `Queue(maxsize=1000)` prevents memory growth on SSE disconnect
- **Regex compilation in Tier 2** — patterns pre-compiled once before file loop (~15% faster)

### Changed
- Web UI security headers added: `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`, `Referrer-Policy`
- C-style cast pattern expanded to include GL types (`GLuint`, `GLint`, `GLfloat`, etc.)
- Report builder adds Cognitive Complexity and Include Order Violations sections
- AuditData extended with `cognitive_complexity` field

## [1.9.0] - 2026-04-11

### Added
- **Code duplication detection (Tier 4)**: New `tier4_duplication.py` module using Rabin-Karp rolling hash to detect Type-1 (exact) and Type-2 (renamed variable) code clones across all supported languages — pure Python, no external dependencies
- **Refactoring opportunity analysis (Tier 4)**: New `tier4_refactoring.py` module detects 5 code smells:
  - **Long parameter lists**: functions with >5 params (configurable), per-language regex with Python `self`/`cls`/`*args` exclusion
  - **God files**: files with >15 function definitions (configurable)
  - **Large switch/if-else chains**: >7 case labels or >5 elif/else-if branches (configurable)
  - **Deep nesting**: >4 levels of brace/indentation nesting (configurable), brace-counting for C-family, indentation for Python
  - **Similar function signatures**: cross-file functions with identical parameter type lists
- Per-language detection for C/C++, Python, Rust, Go, Java, JavaScript, TypeScript
- Configurable thresholds for all detectors (`tier4.duplication.*`, `tier4.refactoring.*`)
- Test suites: `test_tier4_duplication.py` (23 tests) and `test_tier4_refactoring.py` (38 tests)

### Changed
- AuditData extended with `duplication` and `refactoring` fields
- Report builder adds Code Duplication and Refactoring Opportunities sections
- Tier 4 log summary includes clone pair and refactoring smell counts

## [1.8.0] - 2026-04-11

### Added
- **Security pattern category (Tier 2)**: 9 new patterns covering path traversal, command injection, missing O_CLOEXEC, decompression bombs, socket permissions, unsafe deserialization, unbounded buffers, predictable temp files, missing null checks — all with CWE references
- **Dead code detection (Tier 4)**: New `tier4_deadcode.py` module with per-language analyzers:
  - **C/C++**: unused function declarations, unreferenced definitions, unused `#include` directives (Qt-aware: skips signals, slots, virtual overrides)
  - **Python**: unused private functions, unused `import` statements
  - **Rust**: unreferenced `fn` definitions (skips trait impls, test functions)
  - **JS/TS**: unused `import` statements (named and default imports)
- **Build system audit (Tier 4)**: New `tier4_build_audit.py` module checks CMakeLists.txt for 8 security hardening flags (stack protector, FORTIFY_SOURCE, RELRO, PIE, etc.), 3 warning flags (-Wall, -Wextra, -Wpedantic), and 3 CMake best practices — with CWE references for each missing flag
- **Diff-aware security scanning (Tier 3)**: Scans added lines in git diffs for 9 security-sensitive patterns (new system calls, shell command concatenation, path traversal, SQL injection, deserialization, fork without FD cleanup)
- **Domain-specific security research (Tier 5)**: Auto-detects application domain (terminal emulator, Qt app, Lua scripting, network/IPC, web server, crypto, database, game engine) and generates targeted security research queries (terminal CVEs, PTY security, Qt injection, Lua sandbox escapes, etc.)
- **Search result quality filtering (Tier 5)**: Filters non-English results (Chinese, Japanese, Korean, Arabic, Cyrillic), spam, and irrelevant domains; uses `region=us-en` for DuckDuckGo queries; requests 2x results to compensate for filtering
- Security patterns added to auto-config defaults for C++, Python, JavaScript/TypeScript

### Fixed
- **raw_new false positives**: Qt parent-child allocations (`new QWidget(parent)`, layouts, actions, labels) now excluded from raw_new pattern — eliminates ~100 false positives on Qt projects
- Tier 5 research returning non-English results (Chinese zhihu.com, Japanese qiita.com, etc.)
- Tier 5 research returning irrelevant results (caching StackOverflow posts for CVE queries)

### Changed
- Tier 3 `run()` now returns `tuple[ChangeSummary, list[Finding]]` (was just `ChangeSummary`) to include diff security findings
- AuditData extended with `dead_code` and `build_audit` fields
- Report builder adds Dead Code, Build System Audit, and Diff Security Scan sections

## [1.7.0] - 2026-04-11

### Added
- **Parallel Tier 1**: build, cppcheck, and clang-tidy now run concurrently via ThreadPoolExecutor(max_workers=3), matching tiers 2-4 parallelization
- **NVD API key support**: config-driven API key with environment variable fallback (`NVD_API_KEY`), key validation on startup, adjusted rate limits (0.6s authenticated vs 7.0s unauthenticated)
- **Configurable severity overrides**: per-project `severity_overrides` in config to promote/demote finding severities by pattern_name
- **Finding trend tracking**: `lib/trends.py` module saves snapshots after each audit, computes improving/worsening/stable trends per category across runs
- **HTML report output**: `--html` flag generates a self-contained HTML report with dark theme, sortable findings table, severity badges, tier breakdown cards, and SVG trend chart
- **Expanded test suite**: 9 new test files (187+ new tests) covering suppress, ci_output, diff_report, tier5_nvd, tier5_research, tier5_improvements, auto_config, trends, html_report

### Changed
- `severity_overrides` applied after all tiers run but before suppression filtering
- Report builder accepts optional `trend_report` parameter for trend section rendering
- NVD rate limiting reduced from 0.7s to 0.6s with API key (safely under 50 req/30s)

## [1.6.1] - 2026-04-10

### Fixed
- **Path traversal in /api/reports**: used `relative_to()` check instead of string prefix matching
- **Arbitrary file write in /api/config PUT**: restricted writes to project root, YAML files only
- **Workbench CSV bare catch(...)**: replaced with specific `std::invalid_argument` + `std::out_of_range`
- **NVD API rate limiting**: increased delay to 7s unauthenticated (safely under 5 req/30s limit), added HTTP 429 detection
- **Suppress file robustness**: added defensive check for malformed lines

### Changed
- **Workbench performance**: moved VariableMap allocation outside curve sampling loop (100x fewer allocations), added `reserve()` for synthetic data vectors
- **Removed dead code**: unused `#include <cstring>`, `#include "formula/lut_generator.h"` from workbench.cpp, unused `POINTER_REF_RE`/`TEMPLATE_USE_RE` from tier4_includes.py
- **Type annotation**: fixed `tier4_stats.run()` return type to `tuple[AuditData, list]`
- **NVD error handling**: specific exception types (`urllib.error.HTTPError`, `URLError`) instead of generic `Exception`

## [1.6.0] - 2026-04-10

### Added
- **Formula Workbench improvements** (10 enhancements):
  - **#2 File dialog**: native KDE/GNOME file picker for CSV import via kdialog/zenity
  - **#3 Quality tier comparison**: overlay FULL vs APPROXIMATE curves in the visualizer
  - **#4 Coefficient bounds**: min/max constraints on LM optimizer to prevent unphysical values
  - **#5 Convergence history**: ImPlot chart showing R-squared vs iteration during fitting
  - **#6 Multi-variable synthetic data**: sweep all variables in a grid with configurable count/noise
  - **#8 Batch fitting**: fit all formulas in a category at once with aggregate statistics
  - **#9 Undo/redo**: 50-level undo stack for coefficient changes, with Edit menu and buttons
  - **#10 Stability warnings**: detects NaN, exp overflow, div-by-zero, sqrt-of-negative risks
  - **#11 Export to C++/GLSL**: copy fitted formulas as inline code snippets to clipboard
- **Phase 9E roadmap**: added Formula Node Editor section for visual formula composition

### Changed
- **workbench.cpp** grew from 1043 to 1802 lines with all improvements
- **workbench.h** updated with new data members for bounds, convergence, undo, stability

## [1.5.0] - 2026-04-10

### Added
- **Technology detection**: auto-scans codebase for 35+ known techniques (SSAO, PBR, XPBD, motion matching, cloth simulation, etc.) and generates targeted improvement research queries
- **Improvement research**: Tier 5 now searches for better approaches, newer algorithms, and improved implementations for detected technologies
- **Best practices research**: auto-generates "best practices / common mistakes / pitfalls" queries for the top 8 most-used technologies in the codebase
- **Report split**: Tier 5 report separates "Improvement Opportunities" from "Security & CVE Research" for clarity
- **Versioning & Changelog standards**: section 10 in AUDIT_TOOL_STANDARDS.md mandates changelog updates with every code change

### Changed
- **Tier 5 research** now runs both configured queries AND auto-detected improvement + best practices queries
- **Config**: new `research.improvements.enabled` and `research.improvements.max_queries` settings

## [1.4.0] - 2026-04-10

### Added
- **Copy Claude Prompt** button: generates a ready-to-paste prompt with timestamped report path, finding summary, date/time, and step-by-step fix instructions
- **Auto-detect on project change**: changing the Project Root field triggers automatic project detection (language, build system, source dirs, tools, dependencies)
- **Visual Config Builder**: Config tab replaced with a categorized visual editor showing auto-detected settings with toggles, dropdowns, and threshold inputs; "Edit Raw YAML" button for full control
- **/api/detect** endpoint: returns structured project detection results for the config builder
- **Version control**: clickable version badge in header opens changelog modal
- **CHANGELOG.md**: full release history viewable from the web UI
- **/api/version** endpoint: returns version string and changelog content

### Changed
- Config path auto-updates when switching projects (finds existing config or generates new path)
- Claude prompt now references the timestamped report filename and includes generation date/time
- Report filenames always include date-time (`AUTOMATED_AUDIT_REPORT_2026-04-10_150340.md`)

## [1.3.0] - 2026-04-10

### Added
- **17 improvements** implemented across 5 batches
- **Test suite**: 120 tests across 7 files (findings, config, tier2, report, runner)
- **Suppress file**: `.audit_suppress` with `--suppress-show` and `--suppress-add` CLI flags
- **Auto-generate compile_commands.json**: detects missing file, runs cmake automatically for clang-tidy
- **Differential reporting**: `--diff` flag compares against previous audit, shows new/resolved findings
- **Smart comment/string detection**: state machine handles mid-line `/* */`, string literals, escapes
- **Cross-line pattern matching**: `multiline: true` config flag with `re.DOTALL` and 1MB cap
- **Uniform-shader sync check**: cross-references GLSL `uniform` declarations with C++ `setUniform` calls
- **Include dependency analysis**: detects heavy headers, forward-declaration candidates, circular includes
- **Complexity metrics**: cyclomatic complexity via lizard (per-function, threshold-based)
- **NVD API integration**: queries NIST CVE database directly with structured results and caching
- **CI mode**: `--ci` flag emits GitHub Actions `::error`/`::warning` annotations
- **Pattern presets**: `--patterns strict|relaxed|security|performance`
- **Web UI report history**: History tab lists all timestamped reports
- **Web UI config editor**: Config tab with inline YAML editor, validation, backup-on-save
- **Parallel tier execution**: Tiers 2+3+4 run concurrently via ThreadPoolExecutor

## [1.2.0] - 2026-04-10

### Added
- Comprehensive `AUDIT_TOOL_STANDARDS.md` covering 11 sections (code quality, config, patterns, reports, testing, performance, web UI, security, extensibility, release checklist)
- Recent projects dropdown with localStorage persistence (last 10 paths remembered)
- Full `README.md` documentation (CLI, Web UI, config, patterns, languages, architecture)

## [1.1.0] - 2026-04-10

### Added
- Web UI (Flask) at `http://127.0.0.1:5800` with real-time SSE progress streaming
- Dark theme single-page app with config panel, per-tier progress bars
- Tabbed results: Summary cards, sortable/filterable findings table, Changes, Stats, Research
- Live log viewer with auto-scroll and level color-coding
- SVG favicon for browser tab / taskbar identification
- Page reload recovery (restores last results)
- Progress callback and cancel_event support in AuditRunner

### Changed
- Port changed from 5000 to 5800
- Percentage progress indicators on each tier and overall badge

## [1.0.0] - 2026-04-10

### Added
- Initial release of the automated audit tool
- 5-tier audit system matching AUDIT_STANDARDS.md:
  - Tier 1: Build warnings, tests, cppcheck (XML), clang-tidy
  - Tier 2: Configurable regex pattern scanning with comment skipping
  - Tier 3: Git diff analysis (changed files, functions, subsystems)
  - Tier 4: LOC stats, Rule-of-Five audit, event lifecycle, large files
  - Tier 5: Web research via DuckDuckGo with file-based caching
- `--init` flag auto-detects language, build system, source dirs, dependencies
- Support for C++, C, Python, Rust, Go, Java, JavaScript, TypeScript
- Condensed markdown report (~3K tokens for 95K LOC codebase)
- Timestamped report filenames to preserve audit history
- YAML-based configuration with sensible defaults
- Graceful degradation when tools or network unavailable
