# Audit Tool

Automated code audit tool with LLM-optimized reporting. Runs 5 audit tiers mechanically and produces a condensed report (~3K tokens) that an LLM can consume in one shot, cutting audit token usage by ~80-90%.

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
| 1 | Build warnings, test suite, cppcheck (XML), clang-tidy | ~11 min |
| 2 | Configurable regex pattern scanning (memory safety, GL state, performance, code quality, shaders) | ~1s |
| 3 | Git diff analysis (changed files, functions modified, subsystems touched) | ~1s |
| 4 | LOC stats, Rule-of-Five audit, event lifecycle check, large files, deferred work markers | ~1s |
| 5 | Web research via DuckDuckGo (CVEs, driver bugs, best practices) with file-based caching | ~10s |

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
    compile_commands: "build/compile_commands.json"  # recommended

patterns:
  memory_safety:
    - name: "raw_new"
      pattern: "\\bnew\\s+\\w+"
      file_glob: "*.cpp,*.h"
      severity: "high"
      skip_comments: true       # ignore matches in comments

research:
  topics:
    - query: "CVE MyDep 1.2.3 vulnerability"

report:
  output_path: "docs/AUTOMATED_AUDIT_REPORT.md"

tiers: [1, 2, 3, 4, 5]         # override with --tiers CLI flag
```

### Pattern Options

Each pattern supports:

| Field | Description |
|-------|-------------|
| `name` | Identifier for the pattern |
| `pattern` | Python regex to search for |
| `file_glob` | Comma-separated file patterns (e.g., `*.cpp,*.h`) |
| `severity` | `critical`, `high`, `medium`, `low`, or `info` |
| `description` | Human-readable description shown in report |
| `exclude_pattern` | Regex to exclude from matches (e.g., `placement new`) |
| `skip_comments` | Set `true` to skip `//` and `/* */` comment lines |

## CLI Reference

```
usage: audit.py [-h] [--config PATH] [--tiers N [N ...]] [--output PATH]
                [--base-ref REF] [--project-root DIR] [--no-research]
                [--verbose] [--dry-run] [--json] [--list-patterns] [--init]

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
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Audit completed, no critical findings |
| 1 | Config error or infrastructure failure |
| 2 | Audit completed with critical findings (useful for CI) |

## Web UI

Start with `python3 tools/audit/web/app.py` (runs at http://127.0.0.1:5800).

### Features

- **Config panel** with project root dropdown (remembers recent projects), config file path, base ref, tier checkboxes
- **Run/Stop** buttons with cooperative cancellation between tiers
- **Init Config** button to auto-generate config for any project
- **Real-time progress** bars with percentage indicators per tier
- **Overall progress** shown in the status badge
- **Tabbed results:**
  - **Summary** — severity count cards, build/test status, duration, LOC
  - **Findings** — sortable and filterable table with severity color badges
  - **Changes** — changed files with subsystem badges, line deltas, function names
  - **Stats** — LOC bar chart by subsystem, Rule-of-Five audit, event lifecycle, deferred markers
  - **Research** — web search results as cards with snippets and links
- **Live log viewer** with auto-scroll and level color-coding
- **View Report** button to open the generated markdown in a new tab
- **Dark theme** optimized for developer use

### API Endpoints

| Method | Route | Purpose |
|--------|-------|---------|
| GET | `/` | Web UI |
| POST | `/api/run` | Start an audit |
| POST | `/api/stop` | Cancel running audit |
| GET | `/api/events` | SSE stream (real-time progress) |
| GET | `/api/status` | Current session state |
| POST | `/api/init` | Auto-generate config |
| GET | `/api/report` | Generated markdown report |
| GET | `/api/config` | Current YAML config |

## Dependencies

### Required (already in Python stdlib)
- `pathlib`, `subprocess`, `re`, `json`, `threading`, `queue`, `logging`

### Required (pip)
- `PyYAML` — config file parsing
- `Flask` — web UI (only needed if using the web interface)

### Optional
- `xmltodict` — cppcheck XML parsing (falls back to text parsing without it)
- `duckduckgo-search` — Tier 5 web research (gracefully skipped if not installed)

### System Tools
- `cppcheck` — Tier 1 static analysis (auto-detected, skipped if missing)
- `clang-tidy` — Tier 1 static analysis (auto-detected, skipped if missing)
- `git` — Tier 3 change analysis (skipped if not a git repo)

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
audit.py (CLI)              web/app.py (Flask)
    |                            |
    v                            v
lib/runner.py  <-- progress_callback --> web/audit_bridge.py
    |                                        |
    +-- tier1_build.py                       +-- AuditSession (threading)
    +-- tier1_cppcheck.py                    +-- QueueLogHandler (log capture)
    +-- tier1_clangtidy.py                   +-- SSE event streaming
    +-- tier2_patterns.py
    +-- tier3_changes.py
    +-- tier4_stats.py
    +-- tier5_research.py
    |
    v
lib/report.py --> docs/AUTOMATED_AUDIT_REPORT.md
```

The CLI and web UI share the same `AuditRunner`. The web UI adds a `progress_callback` for real-time SSE streaming and a `cancel_event` for cooperative cancellation. The CLI is completely unaffected by the web UI code.
