# Changelog

All notable changes to the Audit Tool are documented in this file.

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
