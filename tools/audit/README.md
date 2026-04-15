# Audit Tool

Automated code audit tool with LLM-optimized reporting. Runs 5 audit tiers mechanically and produces a condensed report (~3K tokens) that an LLM can consume in one shot, cutting audit token usage by ~80-90%.

Current version is in `CHANGELOG.md`; major releases are tagged in the file.

## Project relationship

This tool lives inside the Vestige engine repository but is designed to run against **any** C++, Python, Rust, Go, Java, JavaScript, or TypeScript project (see `--init` auto-detection). It is not engine-specific — the Vestige repo is just its primary user today.

| Concern | Engine | Audit Tool |
|---------|--------|------------|
| Version source | (follows phase milestones) | `CHANGELOG.md` + version badge in web UI |
| Changelog | `ROADMAP.md` phases | `tools/audit/CHANGELOG.md` |
| Release cadence | Phase-gated | Feature-gated (see CHANGELOG) |
| External users | one (this repo) | designed for N; Vestige is user #1 |

When the tool gains a second active user, it's a candidate to move to its own repository (git submodule or standalone install). Until then: monorepo with a clean seam — the tool has no `#include` into the engine source tree, the engine has no runtime dependency on the tool.

## Quick Start

### CLI

```bash
# Full audit with default config
python3 tools/audit/audit.py

# Quick check (build + patterns only)
python3 tools/audit/audit.py -t 1 2

# Verbose output
python3 tools/audit/audit.py -v

# Skip web research (offline mode)
python3 tools/audit/audit.py --no-research

# Raw JSON output (pipe to jq)
python3 tools/audit/audit.py --json | jq '.findings[] | select(.severity == "high")'

# Compare against previous audit
python3 tools/audit/audit.py --diff

# CI mode (GitHub Actions annotations)
python3 tools/audit/audit.py --ci

# Only HIGH+ severity patterns
python3 tools/audit/audit.py --patterns relaxed

# Disable ANSI colour (CI-friendly; auto-enabled on non-TTY stdout)
python3 tools/audit/audit.py --no-color
```

### Web UI

```bash
python3 tools/audit/web/app.py
# Open http://127.0.0.1:5800
```

### New Project Setup

```bash
# Auto-detect language, build system, source dirs, dependencies
python3 tools/audit/audit.py --init -r /path/to/your/project

# Review the generated audit_config.yaml, then run
python3 tools/audit/audit.py -c /path/to/your/project/audit_config.yaml
```

## Audit Tiers

| Tier | What it does | Time (Vestige) |
|------|-------------|----------------|
| 1 | Build warnings, test suite, cppcheck (XML), clang-tidy (auto-generates compile_commands.json if missing) | ~11 min |
| 2 | Configurable regex pattern scanning with smart comment/string detection and multiline support | ~1s |
| 3 | Git diff analysis (changed files, functions modified, subsystems touched) | ~1s |
| 4 | LOC stats, Rule-of-Five audit, event lifecycle, uniform-shader sync, include analysis, cyclomatic complexity | ~2s |
| 5 | Web research via DuckDuckGo + NVD CVE database with file-based caching | ~10s |
| 6 | Feature coverage sweep — flags engine subsystems with no/thin test coverage (D4, 2.6.0; D5 keyword map, 2.7.0) | <1s |

Tiers 2, 3, 4, and 6 run in parallel via `ThreadPoolExecutor`. Tier 1
must complete first (build before analysis); Tier 5 runs last
(needs findings for context).

### D2 — Cross-source corroboration (2.4.0)

Findings flagged by **two or more independent sources** at the same
`(file, line)` are tagged `[CORROB]` and (by default) promoted one
severity level. Conversely, solo hits whose `pattern_name` is in the
configured `demoted_patterns` list (`std_endl`, `push_back_loop`,
`todo_fixme`, etc.) are demoted to INFO. Closes the biggest rigour
gap versus the manual audit prompt, which weights findings by
multi-source agreement. See `corroboration:` in the config.

### D3 — Human-review verification (2.5.0)

Maintainers reviewing a report can mark a finding as confirmed-real
via `--verified-add <dedup_key>`. The key is persisted to
`.audit_verified` and matching findings carry a `[VERIFIED]` prefix
on subsequent runs. Verification is a *tag*, not a filter — verified
findings stay visible. Lets reviewers distinguish "reviewed-real-
still-needs-fixing" from "not-yet-looked-at" without losing the
finding.

## Configuration

All project-specific settings live in `audit_config.yaml`. The `--init` flag auto-generates one by detecting:

- **Language** (C++, C, Python, Rust, Go, Java, JavaScript, TypeScript)
- **Build system** (CMake, Meson, Make, Cargo, npm, pip, Gradle, Maven)
- **Source directories** and shader directories
- **Dependencies** (from CMakeLists.txt, Cargo.toml, package.json, requirements.txt) for CVE research
- **Pattern library** (language-appropriate regex patterns)

### Key Config Sections

```yaml
project:
  name: "My Project"
  language: "cpp"
  source_dirs: ["src/", "lib/"]
  exclude_dirs: ["external/", "build/"]

build:
  build_cmd: "cmake --build build 2>&1"
  test_cmd: "cd build && ctest --output-on-failure 2>&1"

static_analysis:
  cppcheck:
    enabled: true
  clang_tidy:
    enabled: true
    compile_commands: "build/compile_commands.json"  # auto-generated if missing

patterns:
  memory_safety:
    - name: "raw_new"
      pattern: "=\\s*new\\s+\\w+"
      file_glob: "*.cpp,*.h"
      severity: "high"
      skip_comments: true       # smart detection: skips comments AND string literals
      multiline: false          # set true for cross-line patterns

tier4:
  uniform_analysis:
    enabled: true               # cross-reference GLSL uniforms with C++ setters
  include_analysis:
    enabled: true               # detect heavy headers, forward-decl candidates
    heavy_header_threshold: 15
  complexity:
    enabled: true               # cyclomatic complexity via lizard
    threshold: 15

research:
  enabled: true
  nvd:
    enabled: true               # NVD CVE database queries
    api_key: null               # optional, improves rate limits
    dependencies: ["GLFW", "FreeType"]
  topics:
    - query: "CVE MyDep 1.2.3 vulnerability"

report:
  output_path: "docs/AUTOMATED_AUDIT_REPORT.md"

tiers: [1, 2, 3, 4, 5]
```

### Pattern Options

| Field | Description |
|-------|-------------|
| `name` | Identifier for the pattern |
| `pattern` | Python regex to search for |
| `file_glob` | Comma-separated file patterns (e.g., `*.cpp,*.h`) |
| `severity` | `critical`, `high`, `medium`, `low`, or `info` |
| `description` | Human-readable description shown in report |
| `exclude_pattern` | Regex to exclude from matches (e.g., `placement new`) |
| `skip_comments` | Skip comments (`//`, `/* */`) AND string literals (`"..."`) |
| `multiline` | Enable cross-line matching with `re.DOTALL` (1MB file cap) |

## CLI Reference

```
usage: audit.py [-h] [--config PATH] [--tiers N [N ...]] [--output PATH]
                [--base-ref REF] [--project-root DIR] [--no-research]
                [--verbose] [--dry-run] [--json] [--list-patterns] [--init]
                [--diff] [--ci] [--patterns PRESET] [--html] [--sarif]
                [--keep-snapshots N] [--no-color]
                [--suppress-show] [--suppress-add KEY]
                [--verified-show] [--verified-add KEY] [--verified-remove KEY]
                [--nvd-test]

Options:
  --config, -c PATH       Path to audit_config.yaml
  --tiers, -t N [N ...]   Run only specific tiers (e.g., -t 1 2)
  --output, -o PATH       Override report output path
  --base-ref, -b REF      Git ref for Tier 3 diff (e.g., v0.9.0, HEAD~5)
  --project-root, -r DIR  Override project root directory
  --no-research           Skip Tier 5 (web research)
  --verbose, -v           Print progress and timing to stderr
  --dry-run               Show what would run without executing
  --json                  Output raw JSON instead of markdown report
  --list-patterns         Print all configured grep patterns and exit
  --init                  Auto-detect project and generate audit_config.yaml
  --diff                  Compare findings against previous audit run
  --ci                    CI mode: emit GitHub Actions annotations
  --patterns PRESET       Use a pattern preset: strict, relaxed, security, performance
  --html                  Also generate a self-contained HTML report
  --sarif                 Also generate a SARIF 2.1.0 report
  --keep-snapshots N      Retain only the N most recent trend snapshots
  --no-color              Disable ANSI colour in output and in child tools
                          (cppcheck, clang-tidy, git). Sets NO_COLOR=1 in
                          the environment. Auto-enabled when stdout is not
                          a TTY or NO_COLOR is already set. See
                          https://no-color.org.
  --suppress-show         Print current suppressions from .audit_suppress
  --suppress-add KEY      Add a dedup_key to .audit_suppress
  --verified-show         Print current verified keys from .audit_verified (D3)
  --verified-add KEY      Mark a finding's dedup_key as reviewed-and-real (D3)
  --verified-remove KEY   Revoke a verified-key entry (D3)
  --nvd-test              Validate NVD API key with a single test query and exit
                          (exit 0 = OK, 1 = no key, 2 = key rejected)
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Audit completed, no critical findings |
| 1 | Config error or infrastructure failure |
| 2 | Audit completed with critical findings (useful for CI) |

### Pattern Presets

| Preset | What it includes |
|--------|-----------------|
| `strict` | All patterns from the language library |
| `relaxed` | Only HIGH and CRITICAL severity patterns |
| `security` | Memory safety and security categories only |
| `performance` | Performance category only |

### Finding Suppression

Suppress known false positives without modifying source code:

```bash
# View current suppressions
python3 tools/audit/audit.py --suppress-show

# Add a finding's dedup_key to the suppress list
python3 tools/audit/audit.py --suppress-add a1b2c3d4e5f6g7h8

# The dedup_key is shown in JSON output and the web UI findings table
python3 tools/audit/audit.py --json | jq '.findings[] | .dedup_key'
```

Suppressions are stored in `.audit_suppress` (one key per line, with comments).

### Finding Verification (D3 — 2.5.0)

Mark a finding as reviewed-and-confirmed-real so future runs surface
it with a `[VERIFIED]` prefix. Distinguishes "real bug, still needs
fixing" from "not yet looked at" — without filtering it out the way
suppression does.

```bash
# View current verified keys
python3 tools/audit/audit.py --verified-show

# Mark a finding as confirmed-real (annotation auto-added)
python3 tools/audit/audit.py --verified-add a1b2c3d4e5f6g7h8

# Revoke verification (e.g. if added in error)
python3 tools/audit/audit.py --verified-remove a1b2c3d4e5f6g7h8
```

Verified keys are stored in `.audit_verified` (same format as
`.audit_suppress`). If a finding is both verified and suppressed,
suppression wins (the finding is removed from the report — you've
explicitly asked to hide a known-real finding, which is a valid
choice).

Pipeline ordering: dedup → corroborate → severity_overrides →
verified-tag → suppress.

### NVD API key smoke test (2.6.1)

Validate the NVD API key without running a full audit. Useful at key
activation time and as a fail-fast CI step.

```bash
$ export NVD_API_KEY='your-key-here'
$ python3 tools/audit/audit.py --nvd-test
NVD API key: OK (authenticated access confirmed).
Authenticated rate limit: 50 requests per 30 seconds (vs 5 unauthenticated).
```

Exit codes: `0` = OK, `1` = no key in env, `2` = key shape invalid
or server-rejected (not yet activated, revoked, quota exhausted).

### Differential Reporting

Compare the current audit against the previous one:

```bash
python3 tools/audit/audit.py --diff
```

This shows:
- **New findings** — present now but not in the previous audit
- **Resolved findings** — present before but not now
- **Persistent count** — unchanged between runs

Requires at least two previous audit runs (JSON sidecars are saved alongside reports).

## Web UI

Start with `python3 tools/audit/web/app.py` (runs at http://127.0.0.1:5800).

### Features

- **Version badge** in header — click to view full changelog in a modal
- **Config panel** with project root dropdown (remembers recent projects via localStorage), config file path, base ref, tier checkboxes
- **Run/Stop** buttons with cooperative cancellation between tiers
- **Init Config** button to auto-generate config for any project
- **Real-time progress** bars with percentage indicators per tier (Tier 1 shows sub-step progress: build/cppcheck/clang-tidy)
- **Overall progress** shown in the status badge ("Running 40%")
- **Tabbed results:**
  - **Summary** — severity count cards, build/test status, duration, LOC
  - **Findings** — sortable (click column headers) and filterable (severity, category, tier, text search) table with severity color badges, capped at 500 rows
  - **Changes** — changed files with subsystem badges, line deltas, function names
  - **Stats** — LOC bar chart, Rule-of-Five audit, event lifecycle, uniform-shader sync, include analysis, complexity hotspots
  - **Research** — web search results and NVD CVE results as cards
  - **History** — browse all timestamped audit reports with View buttons
  - **Config** — inline YAML editor with Save (validates YAML, creates .bak backup) and Reload
- **Live log viewer** with auto-scroll (pauses when scrolled up) and level color-coding
- **View Report** button to open the generated markdown in a new tab
- **Dark theme** (#0d1117 background) optimized for developer use
- **SVG favicon** for browser tab / taskbar identification
- **Page reload recovery** — refreshing restores last audit results without re-running

### API Endpoints

| Method | Route | Purpose |
|--------|-------|---------|
| GET | `/` | Web UI |
| POST | `/api/run` | Start an audit (409 if already running) |
| POST | `/api/stop` | Cancel running audit (cooperative, between tiers) |
| GET | `/api/events` | SSE stream (real-time progress, log, completion events) |
| GET | `/api/status` | Current session state (for page reload recovery) |
| POST | `/api/init` | Auto-generate config for a project |
| GET | `/api/report` | Latest generated markdown report |
| GET | `/api/config` | Current YAML config content |
| PUT | `/api/config` | Save edited YAML config (validates, creates .bak) |
| GET | `/api/reports` | List all timestamped reports (filename, date, size) |
| GET | `/api/reports/<file>` | Fetch a specific timestamped report |
| GET | `/api/version` | Version string and full changelog |

## Tier 4 Analysis Details

### Uniform-Shader Sync
Cross-references GLSL `uniform` declarations with C++ `setUniform`/`glUniform` calls:
- **Declared-not-set**: uniforms in GLSL that no C++ code ever sets
- **Set-not-declared**: C++ code setting uniforms not declared in any shader
- Handles array uniforms with string concatenation patterns

### Include Dependency Analysis
Parses `#include` directives in headers to build an include graph:
- **Heavy headers**: files with more includes than the threshold (default 15)
- **Forward-declaration candidates**: includes where the type is only used as pointer/reference
- **Circular includes**: pairs of headers that include each other

### Cyclomatic Complexity
Uses the `lizard` library for per-function cyclomatic complexity:
- Flags functions above the threshold (default CC >= 15)
- Reports top hotspots with function name, file, line, and CC score
- Gracefully skips if lizard is not installed

## Dependencies

### Required (Python stdlib)
- `pathlib`, `subprocess`, `re`, `json`, `threading`, `queue`, `logging`, `concurrent.futures`, `urllib.request`

### Required (pip)
- `PyYAML` — config file parsing
- `Flask` — web UI (only needed for the web interface)

### Optional (pip)
- `xmltodict` — cppcheck XML parsing (falls back to text parsing without it)
- `duckduckgo-search` — Tier 5 web research (gracefully skipped if not installed)
- `lizard` — Tier 4 complexity analysis (gracefully skipped if not installed)

### System Tools
- `cppcheck` — Tier 1 static analysis (auto-detected, skipped if missing)
- `clang-tidy` — Tier 1 static analysis (auto-detected, skipped if missing)
- `git` — Tier 3 change analysis (skipped if not a git repo)
- `cmake` — auto-generates compile_commands.json for clang-tidy if missing

## Supported Languages

The `--init` auto-detection supports:

| Language | Build Systems | Static Analysis | Pattern Library |
|----------|--------------|-----------------|-----------------|
| C++ | CMake, Meson, Make | cppcheck, clang-tidy | Memory safety, performance, OpenGL state, code quality, shaders |
| C | CMake, Make | cppcheck, clang-tidy | Memory safety, code quality |
| Python | pip, pyproject | - | Security (eval/exec, shell injection, pickle), code quality |
| Rust | Cargo | - | Unsafe blocks, unwrap/expect, code quality |
| Go | go mod | - | Error handling, code quality |
| Java | Maven, Gradle | - | SQL injection, code quality |
| JavaScript | npm | - | eval, innerHTML XSS, code quality |
| TypeScript | npm | - | any type, ts-ignore, plus JS patterns |

## Architecture

```
audit.py (CLI)              web/app.py (Flask v1.3.0)
    |                            |
    v                            v
lib/runner.py  <-- progress_callback --> web/audit_bridge.py
    |                                        |
    +-- tier1_build.py                       +-- AuditSession (threading)
    +-- tier1_cppcheck.py                    +-- QueueLogHandler (log capture)
    +-- tier1_clangtidy.py                   +-- SSE event streaming
    |                                        +-- Report history API
    +-- tier2_patterns.py    (parallel) -+
    +-- tier3_changes.py     (parallel) -|-- ThreadPoolExecutor
    +-- tier4_stats.py       (parallel) -|
    |     +-- tier4_uniforms.py          |
    |     +-- tier4_includes.py          |
    |     +-- tier4_complexity.py        |
    +-- tier6_coverage.py    (parallel) -+   (D4, 2.6.0)
    |
    +-- tier5_research.py
    |     +-- tier5_nvd.py (NVD API)
    |
    +-- corroboration.py (D2 — multi-source [CORROB] tagging)
    +-- verified.py      (D3 — human-review [VERIFIED] tagging)
    +-- suppress.py      (finding suppression)
    +-- diff_report.py   (differential reporting)
    +-- ci_output.py     (GitHub Actions annotations)
    |
    v
Pipeline (post-tiers): dedup → corroborate → overrides → verified → suppress
    |
    v
lib/report.py --> docs/AUTOMATED_AUDIT_REPORT_{timestamp}.md
                  docs/AUTOMATED_AUDIT_REPORT_{timestamp}_results.json
                  docs/AUTOMATED_AUDIT_REPORT.md (latest copy)
                  docs/trend_snapshot_{timestamp}.json
```

**Parallel execution**: Tiers 2, 3, 4, and 6 run concurrently via `ThreadPoolExecutor`. Tier 1 runs first (build must complete). Tier 5 runs last (needs findings for context).

**Corroboration tagging (D2)**: `.audit_config.yaml` `corroboration:` block controls how findings flagged by ≥2 independent sources at the same `(file, line)` are promoted/demoted. `[CORROB]` tag in report tables, `corroborated_by` list in JSON output.

**Verification tagging (D3)**: `.audit_verified` file marks reviewer-confirmed findings with a `[VERIFIED]` prefix without filtering them out.

**Suppression**: `.audit_suppress` file filters known false positives by dedup_key before report generation.

**Differential reporting**: JSON sidecar files enable `--diff` to compare findings between audit runs.

## Testing

```bash
# Run the test suite (~700 tests, <2s)
cd tools/audit && python3 -m pytest tests/ -v

# Run with coverage
python3 -m pytest tests/ --cov=lib --cov-report=term-missing
```

## Version History

See [CHANGELOG.md](CHANGELOG.md) for the full release history.
