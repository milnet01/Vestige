# Formula Workbench Changelog

All notable changes to the Formula Workbench are documented in this file.

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
