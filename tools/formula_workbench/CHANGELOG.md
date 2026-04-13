# Formula Workbench Changelog

All notable changes to the Formula Workbench are documented in this file.

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
