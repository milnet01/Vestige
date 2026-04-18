# Formula Workbench Changelog

All notable changes to the Formula Workbench are documented in this file.

## [1.9.0] - 2026-04-18

### Added — small-scope roadmap items (W4, W5, W7, W8)

Four close-outs from `docs/SELF_LEARNING_ROADMAP.md`, grouped into
one release because they touch disjoint surfaces and each is small
on its own. W1 (async-worker pattern), W2 (§3.5 GUI), W3 (markdown
rendering), and W6 (confidence-weighted meta-features) remain
deferred — they each need more design work than a single commit.

**W5 — two more reference cases**
- `reference_cases/hooke_spring.json` — linear Hooke spring
  (F = -k · (x - restLength), k = 100). Relative-tolerance
  assertion on k.
- `reference_cases/exponential_fog.json` — saturating exp-family
  fog factor (f = 1 - exp(-density · distance), density = 0.01).
  Complements `beer_lambert` (same exp-family, inverted).
- Reference-case suite now covers 5 formulas (beer_lambert,
  exponential_fog, fresnel_schlick, hooke_spring, stokes_drag).
  All 5 auto-discovered by `test_reference_harness.cpp` at build
  time — no CMake change needed.

**W8 — `--self-benchmark` consults `.fit_history.json`**
- `runBenchmark` now seeds each formula's LM initial coefficients
  from `lastExportedCoeffsFor(formula_name)` when history exists.
  Matches the GUI's §3.2 behaviour so the CLI leaderboard isn't
  systematically penalising formulas the user has previously fit.
  Missing / corrupt history silently falls back to library
  defaults.

**W4 — pin-this-fit toggle** (§3.2 history-poisoning mitigation)
- Checkbox "Remember for future seeding" next to the Export
  button in the fitting-controls panel, defaults to on.
- When unchecked, the fit is still appended to `.fit_history.json`
  but with `user_action: "discarded"` instead of `"exported"`.
  `lastExportedCoeffsFor` skips non-exported entries, so a
  discarded fit is visible to auditors but can't bias future
  seeding.
- Use case: export an outlier / experimental fit without
  polluting the seed well for that formula. Replaces the
  scorched-earth alternative of deleting the history file.

**W7 — LLM per-call cost log**
- `scripts/llm_rank.py` appends one JSONL entry to `.llm_calls.log`
  per Anthropic API call: timestamp, model, CSV path, n_points,
  input/output/total tokens, estimated USD cost (best-effort from
  local pricing constants; authoritative token counts come from
  `resp.usage`), and stop_reason.
- `.llm_calls.log` added to `.gitignore` (developer-local spend
  audit, not committed).
- Append is best-effort — silent on `OSError` so an unwritable cwd
  can't break the ranking pipeline.
- Pricing constants cover Haiku 4.5, Sonnet 4.6, Opus 4.7; match
  by model-family prefix so specific snapshots
  (claude-haiku-4-5-20251001) inherit the family's pricing.

### Changed

- **`WORKBENCH_VERSION`** 1.8.0 → 1.9.0.
- **`workbench.h`** adds `m_rememberFitForSeeding` member (default true).
- **`workbench.cpp::exportFormula`** sets `user_action` from the
  flag.
- **`renderFittingControls`** renders the checkbox with a tooltip
  explaining the two states.
- **`benchmark.cpp::runBenchmark`** loads `.fit_history.json` once
  at the start of the call and consults it per formula.

### Verified

- 1809 tests pass (+2 from 1807 — the two new reference cases).
- Full build clean for both `formula_workbench` and `vestige_tests`.
- Manual inspection: the pin toggle appears next to Export with
  tooltip; defaults on; checkbox state round-trips through
  `exportFormula`.

## [1.8.0] - 2026-04-18

### Added (§3.6 GUI — in-Workbench Suggestions panel)

Follow-up to the 1.7.0 CLI work: the LLM-ranked formula shortlist is
now discoverable inside the Workbench itself, not just via
`--suggest-formulas` on the command line. New "Suggestions (LLM)"
panel docked alongside the template browser with a single-click
"Suggest Formulas" button that operates on the currently-loaded
dataset and displays the ranked markdown shortlist inline.

**Added**
- **`Workbench::renderSuggestionsPanel()`** — ImGui panel with
  a Run button, status line, and a read-only multi-line text area
  that holds the driver's markdown output. Errors surface in red
  inside the panel so the user doesn't have to check the terminal.
- **`Workbench::runLlmSuggestions()`** — writes the current
  `m_dataPoints` to a `/tmp/workbench_suggest_<pid>.csv` (union of
  variable names across all points, stable alphabetical order so
  the CSV is deterministic), dumps the library to JSON via the
  existing `libraryToJsonString`, and pipes both through to
  `scripts/llm_rank.py` via `runDriverCaptured`. Blocking call —
  Haiku 4.5 responses are a second or two, and a modal-free
  blocking wait is simpler than a worker-thread path for this
  latency budget.
- **`Vestige::runDriverCaptured`** (in `benchmark.{h,cpp}`) — new
  public helper that forks a Python driver with both stdin and
  stdout pipes attached, captures stdout into a string, inherits
  stderr (so import errors remain visible in the launching
  terminal). Complement to the existing `runDriver` which
  inherits both streams (right for CLI passthrough, wrong for
  GUI capture).
- **`Vestige::libraryToJsonString`** / **`findDriverScriptPath`**
  — public wrappers around the anonymous-namespace helpers
  previously used only by the CLI path, so the GUI can reach the
  same script-locator logic without duplicating it.

**Changed**
- **`Workbench::render()`** — calls `renderSuggestionsPanel()`
  after the preset browser. Panel is dockable alongside everything
  else in the workbench layout.
- **`workbench.h`** adds `m_suggestionsOutput`, `m_suggestionsError`,
  `m_suggestionsPending` members; `runLlmSuggestions` /
  `renderSuggestionsPanel` method declarations.
- **`WORKBENCH_VERSION`** 1.7.0 → 1.8.0.

**Pre-flight checks.** Before spawning the driver, the panel:
1. Refuses when `m_dataPoints.empty()` with a clear message —
   silent fallback to synthetic data would produce misleading
   suggestions.
2. Checks the driver exists at one of the known locations; if
   not, surfaces the expected path so the user can diagnose a
   broken install without leaving the Workbench.
3. Forwards driver exit codes verbatim with a hint about the two
   common causes (missing `ANTHROPIC_API_KEY`, missing
   `anthropic` SDK).

**Known trade-off.** The panel blocks the UI thread while the
driver runs. Haiku 4.5's typical 1–2 s latency keeps this bearable,
but if the user runs `--model claude-opus-4-7` via a config tweak,
the Workbench will freeze for the duration. A worker-thread path
is tracked but deliberately deferred — the pattern we'd want
(async I/O on a separate std::thread, poll from the render loop)
is worth building once and reusing for the §3.5 GUI too.

**Not yet done** (tracked in design doc):
- §3.5 GUI ("Discover via PySR" button + auto-import of discovered
  expressions into the library). Still deferred because PySR runs
  take minutes; needs the async-worker pattern mentioned above.
- Markdown rendering inside the panel (currently shows the markdown
  source verbatim — fine for a table of ≤10 rows, would be
  prettier rendered).

## [1.7.0] - 2026-04-17

**Completes Phase 2–3 of the self-learning design**
(`docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md`). Three new CLI
tiers land together: reference-case regression harness (§3.4), PySR
symbolic regression shell-out (§3.5), and LLM-guided formula ranking
(§3.6). Phases 1 and 2 of the design are now entirely live; Phase 3's
two tiers (§3.5, §3.6) ship as optional Python-side drivers so
PySR/anthropic-SDK remain optional dependencies that don't touch
the default build.

### Added — §3.4 reference-case regression harness

- **`tools/formula_workbench/reference_cases/`** — three seed JSON
  specs: `beer_lambert.json`, `stokes_drag.json`,
  `fresnel_schlick.json`. Each names a library formula, a canonical
  coefficient set, an input-sweep spec, and an expected envelope
  (R² min, RMSE max, per-coefficient tolerance).
- **`tools/formula_workbench/reference_harness.{h,cpp}`** — parses
  spec JSON, synthesizes a dataset from the canonical coefficients,
  runs the LM fitter starting from the library defaults (never the
  canonical values — that would be tautological), compares the
  recovered coefficients against the envelope.
- **`tests/test_reference_harness.cpp`** — parameterized Google
  Test that discovers every `.json` under `reference_cases/` at
  compile time (via a `VESTIGE_REFERENCE_CASES_DIR` macro passed
  from CMake) and runs each as a separate test case. Dropping a
  new spec into the directory automatically gains a test on the
  next build. 7 tests total (3 cases + 4 harness meta-tests). Full
  suite: 1807 passing (+7 from 1800).

### Added — §3.5 PySR symbolic-regression tier

- **`--symbolic-regression <csv>`** CLI flag shells out to
  `scripts/pysr_driver.py`. Optional: install with
  `pip install pysr` (pulls Julia ~300 MB on first run). Without
  PySR, the driver prints a clear install hint and exits 2.
- **`scripts/pysr_driver.py`** — reads the CSV (last column = obs),
  runs PySR's multi-objective evolutionary search, emits a
  markdown leaderboard of discovered equations + a machine-readable
  JSON block. `--niterations`, `--binary-ops`, `--unary-ops`,
  `--max-complexity` knobs for search tuning.
- Complexity cap defaults to 20 — guards against the
  nested-sin-of-log mutants PySR sometimes evolves (per the design
  doc's §3.5 risk note).

### Added — §3.6 LLM-guided formula ranking

- **`--suggest-formulas <csv>`** CLI flag pipes the built-in
  FormulaLibrary as JSON to `scripts/llm_rank.py`, which
  constructs a prompt from library metadata + dataset summary
  (n_points, variable list, min/max/mean/stdev of observations,
  first 10 rows) and calls Anthropic's Claude for a ranked
  shortlist of plausible formulas. Defaults to Haiku 4.5
  (fastest/cheapest model accurate enough for shortlisting).
- **`--dump-library`** CLI flag — emits the FormulaLibrary as JSON
  to stdout. Also the stdin feed for `llm_rank.py`.
- **`scripts/llm_rank.py`** — reads CSV + library JSON, calls
  Anthropic, prints ranked markdown. Needs `ANTHROPIC_API_KEY` env
  var; missing key or missing `anthropic` SDK → exit 2 with
  actionable install/config message.
- Per the design doc §3.6 safety note, the LLM stays advisory —
  it ranks plausibility, the fitter still does the actual fitting.

### Changed

- **`workbench.h`** `WORKBENCH_VERSION` 1.6.0 → 1.7.0.
- **`main.cpp`** — three new branch points before GLFW init:
  `--symbolic-regression`, `--suggest-formulas`, `--dump-library`.
  All CLI tiers continue to branch before GLFW so they stay
  headless-safe.
- **`benchmark.{h,cpp}`** — new `runSymbolicRegressionCli`,
  `runSuggestFormulasCli`, `runDumpLibraryCli`, `dumpLibraryJson`,
  plus internal `findDriverScript` (locates Python drivers in
  install-dir / source-tree / cwd) and `runDriver` (fork+exec with
  optional stdin pipe, inherits parent stdout/stderr).
- **`tests/CMakeLists.txt`** — wires `test_reference_harness.cpp`
  + `reference_harness.cpp` into the test target; passes
  `VESTIGE_REFERENCE_CASES_DIR` as a compile-time path so the
  test doesn't depend on cwd.

### Verified

- Full build + 1807 tests pass.
- `--symbolic-regression` emits the PySR install hint when PySR
  isn't installed (graceful degrade, exit 2).
- `--suggest-formulas` emits the `ANTHROPIC_API_KEY` guidance when
  the key isn't set (graceful degrade, exit 2).
- `--dump-library` emits the full FormulaLibrary as formatted JSON.

### Not yet done (tracked in design doc)

- §3.5's graft-PySR-output-into-template-browser flow — the CLI
  tier works, the GUI integration that lets the workbench import a
  PySR-discovered expression as a new library entry is still the
  next step.
- §3.6 UI panel — right now the LLM is CLI-only. A "Suggestions"
  panel inside the workbench that displays the ranked shortlist
  alongside the template browser would make the feature
  discoverable for non-CLI users.

## [1.6.0] - 2026-04-17

### Added (self-learning Phase 1 §3.3 — `--self-benchmark` CLI)

Closes the Phase 1 trio. The Workbench can now batch-fit every
library formula to the same dataset and emit a markdown leaderboard
ranked by AIC ascending — the decision-ready comparison statisticians
actually use (Burnham & Anderson 2004; ΔAIC < 2 indistinguishable,
> 10 decisive). Previously the user had to click through each formula
individually, which in practice meant they stopped after 2 or 3.

**Usage**

```
formula_workbench --self-benchmark data.csv               # stdout
formula_workbench --self-benchmark data.csv --output r.md # file
formula_workbench                                         # GUI (unchanged)
formula_workbench --help
```

The CLI branches before GLFW/ImGui init, so it runs headless —
works over SSH, in CI, no display required.

**Output shape**

Markdown with a leaderboard table (rank | formula | R² | RMSE |
AIC | BIC | ΔAIC | iter | converged) and a separate "Skipped"
section listing formulas that couldn't be attempted (dataset lacked
the required input variables, formula has no coefficients to fit,
etc.). ΔAIC is computed relative to the best (first-ranked) fittable
entry so the user can read "this formula is within 2 AIC of the
winner" at a glance.

**Added**
- `tools/formula_workbench/benchmark.{h,cpp}` — pure non-GUI
  module with `computeAicBic`, `loadCsvDataset`, `runBenchmark`,
  `renderBenchmarkMarkdown`, `runBenchmarkCli`. No ImGui, no GLFW —
  uses only the engine's formula/curve_fitter libraries.
- CSV loader factored out of `workbench.cpp::importCsv` (same
  RFC 4180 rules — quoted commas, `""` escapes). Benchmark mode
  doesn't need to drag the GUI along just to read a file.
- Degeneracy guards in `computeAicBic` — returns
  `{degenerate: true}` when `n ≤ k+1` or `SSE ≤ 0` rather than
  producing NaN/Inf that would pollute the leaderboard.
- `tests/test_benchmark.cpp` — 14 Google Test cases covering
  the AIC/BIC closed form, degeneracy guards, CSV happy path +
  4 error cases, ranking order (fittable-first, AIC ascending),
  ΔAIC=0 for the winner, and the markdown renderer's structural
  shape (header, two-section grouping). Full suite: 1800 passing
  (+14 from 1786).

**Changed**
- `tools/formula_workbench/main.cpp` — takes `(argc, argv)`,
  calls `runBenchmarkCli` before `glfwInit()`. Returns early with
  the CLI's exit code when the flag is handled; otherwise falls
  through to the usual GUI path.
- `tools/CMakeLists.txt` — adds `benchmark.cpp` to the workbench
  target.
- `tests/CMakeLists.txt` — adds the benchmark test pair (source
  + test).

**Verified end-to-end** against a hand-crafted sin(x) dataset —
the CLI emits a leaderboard with `aces_tonemap` ranked #1 (the
only library formula whose sole float variable is `x`), every other
formula honestly reported as "skipped: dataset lacks required input
variables". Exactly the behaviour you want: transparent about which
formulas couldn't even be attempted.

**Closes Phase 1** of the design. Phase 2 (§3.4 regression harness)
and Phase 3 (§3.5 PySR tier) remain tracked in
`docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md`.

## [1.5.0] - 2026-04-17

### Added (self-learning Phase 1 §3.2 — learned initial guesses)

Second mechanism of the Workbench self-learning loop. When the user
selects a formula, `selectFormula()` now consults
`.fit_history.json` (written by §3.1 since 1.4.0) and seeds the
Levenberg-Marquardt starting point from the most recent exported
fit for that formula. LM convergence is notoriously sensitive to
initial guesses — a starting point near the previous converged
minimum typically reaches R² >= 0.99 in a fraction of the iterations
the library's static default would need.

**Behaviour**
- When `FitHistory::lastExportedCoeffsFor(name)` returns a non-empty
  map, matching coefficient names are overwritten with the
  historical values. Coefficients without a match stay at the
  library default — so a library evolution that *adds* a coefficient
  degrades gracefully rather than erroring.
- The UI surfaces a blue "(seeded from fit @ TIMESTAMP)" badge next
  to "Initial Coefficients:" so the user can always see when the
  tool is using remembered values vs. library defaults. Silent
  seeding would make convergence behaviour feel non-deterministic
  across sessions.
- When no history exists for the selected formula, behaviour is
  unchanged from 1.3.x — library defaults are used.

**Added**
- `Workbench::m_seededFromHistory` (bool) + `m_seededFromTimestamp`
  (string) — UI state flagging whether the current coefficient set
  came from history.

**Changed**
- `workbench.cpp::selectFormula` — merges any matching coefficients
  from `FitHistory::lastExportedCoeffsFor(name)` into
  `m_coefficients` right after copying the library defaults. Clears
  the seed state on every reselect so stale badges can't linger.
- `renderFittingControls` — renders the seed badge when
  `m_seededFromHistory` is set.

**Coverage.** The seeding path uses `FitHistory::lastExportedCoeffsFor`
end-to-end; that method has comprehensive tests under
`TestFitHistoryLastExport` in `tests/test_fit_history.cpp`. The GUI
hookup is straightforward wiring.

**Not yet done** (tracked):
- §3.3 `--self-benchmark` CLI mode — batch-fits every library
  formula against a dataset and emits a ΔAIC leaderboard. Needs
  the fit loop extracted from `runFit()` into a reusable function
  first.

## [1.4.0] - 2026-04-17

### Added (self-learning Phase 1 §3.1 — fit history persistence)

First mechanism of the self-learning loop sketched in
`docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md`. Every exported fit
now lands in `.fit_history.json` at the working directory, so future
sessions have cross-session memory of what fit what. Phase 1 §3.2
(learned initial guesses — seed LM from the most recent exported
fit) and §3.3 (`--self-benchmark` leaderboard) build on this
storage layer; landing them independently means the history starts
accumulating now, with real data ready when §3.2/§3.3 land.

**Added**
- **`tools/formula_workbench/fit_history.{h,cpp}`** — `FitHistory`
  class with load/save/record, a 64-bit FNV-1a dataset hash
  (`hashDataset`), and a meta-feature extractor (`computeMeta` →
  n_points, per-variable domain, variance-of-observed). Schema
  versioned (v1); corrupt or unknown-schema files clear the
  in-memory history rather than misparse.
- **Per-formula entry cap** — `MAX_ENTRIES_PER_FORMULA = 20`. When
  recording would exceed the cap, the oldest entry for that formula
  is evicted; other formulas are untouched. Keeps the file size
  bounded even across months of daily use.
- **`lastExportedCoeffsFor(name)`** — returns the coefficient map
  from the most recent `exported` fit for the named formula (or
  empty). Phase 1 §3.2 uses this to seed LM next time the user
  picks the same formula.
- **`tests/test_fit_history.cpp`** — 16 Google Test cases covering
  load/save round-trip, corrupt + wrong-schema recovery, eviction
  policy (cap + don't-evict-others), `lastExportedCoeffsFor`
  selection, hash determinism/sensitivity, and meta-feature
  correctness (population-variance form).

**Changed**
- **`workbench.cpp::exportFormula`** — at the end of a successful
  export, constructs a `FitHistoryEntry` (ISO-8601 UTC timestamp,
  formula name, dataset hash, meta, fitted coefficients,
  R²/RMSE/AIC/BIC, LM convergence info, `user_action:"exported"`)
  and appends it to `.fit_history.json`. Only exported fits are
  persisted — ephemeral in-session tweaking doesn't pollute the
  history. Failures are logged but don't block the export.

**File location + gitignore.** `.fit_history.json` is
developer-local (per-user fitting decisions, per-machine datasets),
not committed. The gitignore change lands in the same commit.

**Not yet done** (tracked for next round):
- §3.2 — seed LM with `lastExportedCoeffsFor(formula_name)` when
  the user reselects a formula with history. Trivial follow-up
  once this layer is stable.
- §3.3 — `--self-benchmark` CLI mode. Needs the fit loop extracted
  from `runFit()` into a reusable function first.

## [1.3.2] - 2026-04-13

### Fixed
- **§M11 RFC 4180 CSV support.** `importCsv` previously split cells with
  `getline(ss, cell, ',')`, breaking on any Excel-exported CSV with
  quoted fields (e.g. `"1,234.56",5.0` split into three cells). The
  new `splitCsvLine` lambda handles quoted comma literals and `""`-
  escaped quotes inline; embedded newlines inside quotes remain
  out-of-scope but short rows are dropped by the existing length
  guard so the fit dataset stays consistent.
- **§L2 File-dialog popen truncation.** Previously read up to 512
  bytes from the kdialog/zenity output. Linux PATH_MAX is 4096; long
  paths were silently truncated and the fit import would fail with a
  cryptic file-not-found. Now loops `fgets` to EOF into a
  `std::string`, trimming trailing whitespace at the end.

## [1.3.1] - 2026-04-13

### Fixed
- **HIGH: Residual plot filter drift** (AUDIT.md §H10). `rebuildVisualizationCache` populated `m_dataX` only for data points whose `variables` map contained `m_plotVariable`, but populated `m_residuals` for *every* data point. When any row lacked the plot variable, the residual plot correlated `residuals[i]` against a mismatched `X[i]` — showing correct-looking but silently wrong data. Fix: filter the residual loop identically; add an `assert(m_residuals.size() == m_dataX.size())` invariant to guard against re-drift.

## [1.3.0] - 2026-04-11

### Fixed
- **CRITICAL: string::npos undefined behavior** in CSV import and file dialog — `find_last_not_of()` / `find_first_not_of()` results now checked before use, preventing undefined behavior on all-whitespace strings
- **HIGH: Empty container dereference** — added `m_dataX.empty()` guard before `min_element`/`max_element` in `rebuildVisualizationCache()`
- **HIGH: Silent CSV data corruption** — unparseable cells now tracked with counter, user warned with status message instead of silent 0.0f insertion
- **MEDIUM: Float precision loss** — statistical accumulators (`sumSqResid`, `sumObs`, `ssTot`) changed from `float` to `double` for proper precision in validation
- **LOW: Unused member** — removed `m_firstFrame` (set but never read)

### Added
- **Adjusted R-squared** — penalizes extra parameters, displayed in validation panel
- **AIC/BIC model selection** — Akaike and Bayesian Information Criteria computed after fitting, displayed in validation and used for batch fit ranking
- **Residual plot** — scatter plot of residuals vs fitted values with zero line, toggleable via checkbox
- **Build security hardening** — added `-fstack-protector-strong`, `-D_GLIBCXX_ASSERTIONS`, `-Wformat=2`, `-Werror=format-security` to CMakeLists.txt

### Changed
- Batch fit results now sorted by AIC (ascending = better model)

## [1.2.0] - 2026-04-11

### Added
- Version constant (`WORKBENCH_VERSION`) in workbench.h
- Version displayed in window title and menu bar
- About dialog shows version number
- This changelog

### Changed
- Window title now includes version: "Vestige FormulaWorkbench v1.2.0"

## [1.1.0] - 2026-04-11

### Added
- Sensitivity analysis module (`sensitivity_analysis.h/.cpp`)
- Performance benchmarking module (`formula_benchmark.h/.cpp`)
- Documentation generator (`formula_doc_generator.h/.cpp`)
- Node graph data structure for Phase 9E node editor (`node_graph.h/.cpp`)
- 3 new template categories: post-processing (6 templates), camera (4 templates), terrain (4 templates)
- 27 total formula templates across all categories

### Changed
- Phase 9E node editor groundwork: NodeGraph with cycle detection and JSON serialization

## [1.0.0] - 2026-04-10

### Added
- File dialog for CSV import (native kdialog/zenity)
- Quality tier comparison visualization (approximate vs full curve overlay)
- Coefficient bounds (upper/lower limits for Levenberg-Marquardt)
- Convergence history visualization (residual vs iteration plot)
- Multi-variable synthetic data generation (sweep all variables)
- Batch fitting across formula categories
- Undo/redo for coefficients (50-level stack)
- Numeric stability warnings (NaN, Inf, extreme values)
- Export to C++/GLSL code snippets
- Export to FormulaLibrary JSON

## [0.1.0] - 2026-04-10

### Added
- Initial release: FormulaWorkbench standalone ImGui application
- Template browser with search/filtering
- Data editor with manual entry and CSV import
- Levenberg-Marquardt curve fitting
- ImPlot visualization
- Train/test validation (R², RMSE)
- Preset library management (environment, style, material presets)
