# Formula Workbench Changelog

All notable changes to the Formula Workbench are documented in this file.

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
