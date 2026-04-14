# Changelog

All notable changes to the Audit Tool are documented in this file.

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
