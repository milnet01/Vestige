# Changelog

All notable changes to the Audit Tool are documented in this file.

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
