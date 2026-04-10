# Audit Tool Standards

This document defines the mandatory standards for developing and maintaining the Audit Tool. All code, configuration, patterns, reports, and tests must conform to these rules.

---

## 1. Code Quality

### 1.1 Language & Version

- **Python 3.11+** — use modern features (`match`, `|` union types, `from __future__ import annotations`)
- **No external frameworks** in `lib/` — only standard library + PyYAML + xmltodict
- **Flask only** in `web/` — no additional web frameworks or JS build tools

### 1.2 File Naming

| Type | Convention | Example |
|------|-----------|---------|
| Python modules | `snake_case.py` | `tier2_patterns.py` |
| Tier modules | `tier{N}_{name}.py` | `tier1_cppcheck.py` |
| Config files | `snake_case.yaml` | `audit_config.yaml` |
| Templates | `snake_case.html` | `index.html` |
| Standards docs | `UPPER_SNAKE_CASE.md` | `AUDIT_TOOL_STANDARDS.md` |

### 1.3 Naming Conventions

```python
# Modules and variables: snake_case
audit_data = collect_stats()

# Classes: PascalCase
class AuditRunner:
class AuditSession:

# Constants: UPPER_SNAKE_CASE
MAX_DETAIL_PER_TOOL = 20
DEFAULT_ROOT = "/mnt/Storage/Scripts/Linux/3D_Engine"

# Private methods/attributes: _leading_underscore
def _emit(self, event, **data):
self._progress_cb = callback

# Type aliases: PascalCase
ProgressCallback = Callable[[str, dict[str, Any]], None]
```

### 1.4 Type Annotations

- **All function signatures** must have parameter and return type annotations
- **Dataclass fields** must have type annotations
- Use `from __future__ import annotations` for deferred evaluation
- Use `X | None` syntax, not `Optional[X]`

```python
# Correct
def run(self, cancel_event: threading.Event | None = None) -> AuditResults:

# Wrong
def run(self, cancel_event=None):
```

### 1.5 Docstrings

- Every module gets a one-line module docstring
- Every public class and function gets a docstring
- Use imperative mood: "Run the build" not "Runs the build"
- Document return types in docstring when non-obvious

```python
"""Tier 1: cppcheck static analysis with XML output parsing."""

def run(config: Config) -> list[Finding]:
    """Run cppcheck and parse results into findings."""
```

### 1.6 Imports

Order (separated by blank lines):
1. `__future__` imports
2. Standard library
3. Third-party packages (yaml, flask, xmltodict)
4. Local imports (relative: `from .config import Config`)

```python
from __future__ import annotations

import logging
import re
from pathlib import Path

import yaml

from .config import Config
from .findings import Finding, Severity
```

### 1.7 Error Handling

- **Never crash the audit.** Catch exceptions per-tier and per-tool, log the error, continue
- **Never silently swallow errors.** Always `log.warning()` or `log.error()` when catching
- **Validate inputs early.** Check file existence, config keys, tool availability before running
- **Use specific exception types.** Catch `OSError`, `subprocess.TimeoutExpired`, `json.JSONDecodeError` — not bare `except Exception`
- **Bare `except:` is forbidden** — always specify the exception type

```python
# Correct
try:
    content = path.read_text(errors="replace")
except OSError as e:
    log.warning("Cannot read %s: %s", path, e)
    return []

# Wrong
try:
    content = path.read_text()
except:
    pass
```

### 1.8 Logging

| Level | When to use |
|-------|-------------|
| `DEBUG` | Tool command strings, cache hits, file-level details |
| `INFO` | Tier start/end, tool start/end, summary counts |
| `WARNING` | Missing tools, missing files, degraded functionality |
| `ERROR` | Tool failures, parse errors, config errors |

Rules:
- Always use the `"audit"` logger: `log = logging.getLogger("audit")`
- Never use `print()` in `lib/` modules — use `log.info()` instead
- Format messages with `%s` placeholders, not f-strings (lazy evaluation)
- Include counts in summary messages: `"Tier 2: %d total findings"`

```python
# Correct
log.info("Tier 2: scanning %d files against %d categories", len(files), len(patterns))

# Wrong
print(f"Scanning {len(files)} files")
```

---

## 2. Configuration

### 2.1 Schema Rules

- Every config field must have a sensible default in `config.py:DEFAULTS`
- A minimal config (`project.root` only) must work — all other fields fall back to defaults
- Config values are never modified after loading (treat `Config` as immutable during a run)
- CLI overrides are applied to `config.raw` before the run starts, not during

### 2.2 Required Fields

These fields must exist in every `audit_config.yaml`:

| Field | Type | Required | Default |
|-------|------|----------|---------|
| `project.name` | string | No | Directory name |
| `project.root` | string | Yes | `"."` |
| `project.language` | string | No | Auto-detected |
| `project.source_dirs` | list | No | Auto-detected |
| `project.exclude_dirs` | list | No | Language-specific |
| `tiers` | list[int] | No | `[1, 2, 3, 4, 5]` |

### 2.3 Path Handling

- All paths in config are **relative to `project.root`** unless absolute
- `project.root` itself is resolved to an absolute path at load time
- Never hard-code paths in Python modules — always read from config
- Use `pathlib.Path` everywhere, never string concatenation for paths

### 2.4 Config Validation

- Missing optional sections must not cause errors — use `.get()` with defaults
- Invalid regex patterns in `patterns:` must log a warning and skip, not crash
- Unknown config keys are silently ignored (forward-compatibility)
- Tool binary paths are auto-detected via `shutil.which()` if not specified

### 2.5 Auto-Config (`--init`)

When auto-detecting a project:
- **Language detection** must check marker files in priority order (most specific first)
- **Source directory detection** must exclude common non-source directories (build, vendor, node_modules, etc.)
- **Dependency detection** must handle malformed manifest files gracefully
- **Generated config** must include a header comment explaining it was auto-generated
- **Generated config** must be valid YAML that loads without errors

---

## 3. Pattern Library

### 3.1 Pattern Definition Rules

Every pattern must have:

| Field | Required | Purpose |
|-------|----------|---------|
| `name` | Yes | Unique identifier within its category |
| `pattern` | Yes | Valid Python regex |
| `file_glob` | Yes | Comma-separated file patterns |
| `severity` | Yes | One of: `critical`, `high`, `medium`, `low`, `info` |
| `description` | Yes | Human-readable, starts with the thing found |

Optional fields:

| Field | Purpose |
|-------|---------|
| `exclude_pattern` | Regex to filter out known-good matches |
| `skip_comments` | Set `true` to skip `//` and `/* */` lines |

### 3.2 Severity Assignment

Severity must be assigned based on actual impact, not pattern complexity:

| Severity | Criteria | Examples |
|----------|----------|----------|
| **critical** | Crash, data loss, security breach | Buffer overflow, SQL injection, use-after-free |
| **high** | Bug, security risk, resource leak | Raw new/delete, unsafe C strings, unbounded GPU loops |
| **medium** | Performance issue, bad practice | C-style casts, using namespace in headers, empty catch |
| **low** | Style, minor efficiency, informational | std::endl, shared_ptr where unique_ptr suffices |
| **info** | Markers, notes, review-needed items | TODO/FIXME, discard in shaders, warning suppressions |

### 3.3 False Positive Management

- Every pattern that matches English words (`new`, `delete`) **must** have `skip_comments: true`
- Common false positive sources must be handled via `exclude_pattern`
- Pattern library must be validated against the target codebase before release — aim for <10% false positive rate on HIGH+ findings
- If a pattern consistently produces >30% false positives, lower its severity or add exclusions

### 3.4 Language-Specific Patterns

- C/C++ patterns go in the `memory_safety`, `opengl_state`, `performance`, `code_quality`, and `shader` categories
- Python patterns go in `security` and `code_quality`
- Each language must have at least a `code_quality` category with `todo_fixme` pattern
- Patterns must not assume file content encoding — always use `errors="replace"` when reading

### 3.5 Adding New Patterns

When adding a new pattern:
1. Test the regex against the target codebase: `grep -rn 'PATTERN' source/`
2. Count matches — if >200, the severity should be `low` or `info`
3. Review first 10 matches manually for false positives
4. Add `exclude_pattern` for any systematic false positives found
5. Add `skip_comments: true` if the pattern matches natural language

---

## 4. Report Standards

### 4.1 Format

- Reports are **Markdown** with embedded **fenced JSON blocks** for structured data
- The executive summary JSON block is always the first content section
- Finding tables use compact format: `| Severity | File | Line | Issue |`
- No source code is included in reports — only `file:line` references

### 4.2 Token Efficiency

The report's primary consumer is an LLM. Token efficiency is a hard requirement:

| Metric | Target |
|--------|--------|
| Full 5-tier report on 100K LOC codebase | < 5,000 tokens |
| Findings per tool in detail | Max 20 (HIGH+ only) |
| Findings per pattern category in detail | Max 15 |
| JSON blocks | Indent 2, no trailing whitespace |
| Tier 3 changed files | Table format, not JSON |
| Tier 4 LOC data | Single JSON block |

### 4.3 Content Rules

- **Executive Summary** must contain: finding counts by severity, build status, test results, LOC, duration
- **Tier 1** shows severity breakdown + only HIGH+ findings in detail. MEDIUM/LOW/INFO are counted, not listed
- **Tier 2** shows per-category pattern counts + only HIGH+ matches in detail. Remaining are counted
- **Tier 3** always uses a table (compact) not JSON (verbose)
- **Tier 4** shows only incomplete Rule-of-Five classes and event lifecycle leaks. OK items are counted
- **Tier 5** shows only queries that returned results. Empty/error queries are summarized
- Every section ends with a count of omitted items: `*(N more)*`

### 4.4 Deduplication

- Findings are deduplicated by `file:line:category:title` hash
- When the same issue is found by multiple tools (e.g., cppcheck + Tier 2 pattern), the richer finding (lower tier number = more detail) is kept
- Findings are sorted by: severity (ascending), then file path, then line number

---

## 5. Testing

### 5.1 Test Framework

- Use **pytest** for all tests
- Tests live in `tools/audit/tests/` (to be created)
- Test files: `test_{module_name}.py`
- Use fixtures for shared config and sample data

### 5.2 Required Test Coverage

Every tier module must have tests covering:

| Module | Required Tests |
|--------|---------------|
| `findings.py` | Severity ordering, deduplication, Finding.to_dict(), ChangeSummary, AuditData |
| `config.py` | Load defaults, load YAML, merge override, missing file handling, tool detection |
| `tier1_build.py` | Parse clean build, parse warnings, parse errors, parse test results, timeout |
| `tier1_cppcheck.py` | Parse XML output, parse text fallback, filter externals, handle missing tool |
| `tier1_clangtidy.py` | Parse text output, dedup same-file findings, handle missing compile_commands |
| `tier2_patterns.py` | Match positive, match with exclude, skip_comments, file_glob filtering, cap limit |
| `tier3_changes.py` | Parse name-status, parse numstat, extract functions, subsystem mapping, no git |
| `tier4_stats.py` | LOC counting, Rule-of-Five detection, event lifecycle, deferred markers |
| `tier5_research.py` | Cache hit, cache miss, cache expired, no library, no network |
| `report.py` | Token estimate under target, all sections present, JSON validity, dedup applied |
| `runner.py` | Tier selection, progress callback fires, cancel between tiers, report generation |
| `auto_config.py` | Detect C++, detect Python, detect Rust, detect deps, generate valid YAML |

### 5.3 Test Data

- Use small, self-contained fixtures (not the full Vestige codebase)
- Mock subprocess calls for tool tests (cppcheck, clang-tidy, git)
- Provide sample XML/text outputs as string constants in test files
- Never depend on network access — mock `duckduckgo_search` in Tier 5 tests

### 5.4 Test Naming

```python
def test_severity_ordering():
def test_dedup_keeps_lower_tier():
def test_cppcheck_xml_parsing():
def test_pattern_skip_comments():
def test_config_missing_file_exits():
```

### 5.5 Running Tests

```bash
# All tests
cd tools/audit && python3 -m pytest tests/

# Single module
python3 -m pytest tests/test_findings.py -v

# With coverage
python3 -m pytest tests/ --cov=lib --cov-report=term-missing
```

---

## 6. Performance & Token Efficiency

### 6.1 Execution Time Targets

| Tier | Target (100K LOC) | Hard Limit |
|------|-------------------|------------|
| Tier 1: Build | Depends on project | N/A |
| Tier 1: cppcheck | < 60s | 600s timeout |
| Tier 1: clang-tidy | < 300s (50 files) | 600s timeout |
| Tier 2: Patterns | < 5s | 30s |
| Tier 3: Changes | < 5s | 30s |
| Tier 4: Stats | < 5s | 30s |
| Tier 5: Research | < 30s (with cache) | 120s |

### 6.2 Memory Efficiency

- Never load entire large files into memory for pattern scanning — read line by line
- Use generators for file enumeration, not lists that collect all paths first
- Cap findings per category (`max_findings_per_category`) to prevent unbounded growth
- Clear subprocess output after parsing — don't hold stdout/stderr in memory

### 6.3 Subprocess Management

- All subprocess calls must have a `timeout` parameter
- Default timeout: 300s for builds/tests, 600s for static analysis
- Capture both stdout and stderr via `capture_output=True`
- Always use `text=True` for string output (not bytes)
- Never use `shell=True` unless the command requires shell features (pipes, cd)

### 6.4 Caching

- Tier 5 research results are cached in `.audit_cache/` with a configurable TTL (default: 7 days)
- Cache keys are SHA-256 hashes of the query string (first 16 hex chars)
- Cache files are JSON with `query`, `timestamp`, and `results` fields
- Expired cache entries are overwritten on next query, not cleaned up proactively

---

## 7. Web UI

### 7.1 Architecture Rules

- **Single-page application** — one HTML template, no page navigation
- **Inline CSS and JS** — no separate build step, no bundler, no npm
- **Vanilla JavaScript only** — no frameworks (React, Vue, etc.)
- **Flask serves everything** — templates, static files, API endpoints
- **Single-user design** — one `AuditSession` singleton, no multi-user support

### 7.2 API Design

- All API routes under `/api/` prefix
- POST for state-changing operations (run, stop, init)
- GET for read operations (status, events, report, config)
- JSON request/response bodies on all API routes
- HTTP status codes: 200 OK, 400 bad request, 404 not found, 409 conflict, 500 error
- Error responses always include `{"error": "message"}`

### 7.3 SSE (Server-Sent Events)

- All events are JSON objects with a `type` field and `timestamp`
- Event types: `audit_start`, `tier_start`, `step_start`, `step_end`, `tier_end`, `audit_end`, `log`, `complete`, `error`, `cancelled`
- Keepalive comments (`: keepalive\n\n`) sent every 30 seconds to prevent timeout
- The SSE stream terminates on `complete`, `error`, or `cancelled` events
- Frontend uses `EventSource` API with auto-reconnect fallback

### 7.4 Threading

- Audit runs in a **daemon thread** (killed on Flask exit)
- Events flow through a **`queue.Queue`** (thread-safe)
- Cancel uses a **`threading.Event`** (cooperative, checked between tiers)
- Only one audit can run at a time — `/api/run` returns 409 if busy
- The `AuditSession.running` flag is protected by a `threading.Lock`

### 7.5 Frontend Standards

**CSS:**
- Dark theme with CSS custom properties (`--bg`, `--surface`, `--text`, etc.)
- Severity colors: critical=`#f85149`, high=`#d29922`, medium=`#e3b341`, low=`#58a6ff`, info=`#8b949e`
- Monospace font for code/log content: `JetBrains Mono, Fira Code, monospace`
- All sizing in `px` for consistency (not em/rem) since this is a fixed-layout dev tool
- Maximum content width: 1400px, centered

**JavaScript:**
- State managed in a single `state` object at module scope
- DOM queries via `$()` and `$$()` helper functions
- Event handling via `addEventListener`, not inline `onclick`
- Tables render max 500 rows (paginate or cap for performance)
- Log viewer auto-scrolls unless user has scrolled up manually
- Recent projects/configs stored in `localStorage` (max 10 entries)

**Accessibility:**
- All form inputs have `<label>` elements
- Color is not the only indicator — severity badges include text
- Interactive elements are focusable (buttons, inputs, links)
- Log viewer is scrollable via keyboard

### 7.6 Favicon

- SVG format (scales to any size, works in all modern browsers)
- Must include the audit tool's visual identity (checkmark + magnifying glass)
- Background color: `#0d1117` (matches app theme)
- Accent colors: `#58a6ff` (blue) and `#3fb950` (green)

---

## 8. Security

### 8.1 Input Validation

- **Config file paths** are resolved to absolute paths and checked for existence before use
- **Project root** must be an existing directory
- **Base ref** is passed directly to `git diff` — no shell injection risk since `subprocess` uses argument lists where possible
- **Pattern regexes** from config are compiled in a try/except — invalid regexes are skipped with a warning
- **File reading** always uses `errors="replace"` to handle binary/corrupted files

### 8.2 Subprocess Safety

- Prefer argument lists over shell strings where possible
- When `shell=True` is needed (e.g., `cd build && ctest`), inputs come from config, not user HTTP requests
- All subprocess calls have timeouts to prevent hangs
- Subprocess output is capped by natural limits (stdout/stderr captured as strings)

### 8.3 Web UI Safety

- Flask runs on `127.0.0.1` only (not `0.0.0.0`) — not network-accessible by default
- No authentication required (single-user local tool)
- User-supplied text is escaped with `textContent` assignment in JS, never `innerHTML` with raw data
- API endpoints validate JSON input and return structured errors
- No file upload capabilities — paths are entered as text

### 8.4 Research Cache Safety

- Cache files are stored locally in `.audit_cache/` (gitignored)
- Cache filenames are hex hashes (no path traversal risk)
- Cache content is JSON only — no executable data
- Network errors in Tier 5 are caught and logged, never propagated

---

## 9. Extensibility

### 9.1 Adding a New Language

To add support for a new language:

1. **In `auto_config.py`:**
   - Add a marker entry to `LANGUAGE_SIGNATURES`
   - Add a `_newlang_patterns()` function returning language-specific patterns
   - Add the language to `_get_language_defaults()` with extensions, excludes, and patterns
   - Add a dependency detection function if applicable

2. **In `audit_config.yaml` documentation:**
   - Add the language to the supported languages table
   - Provide an example config snippet

3. **No changes needed** to `runner.py`, `report.py`, `tier2_patterns.py`, or any tier module — they are language-agnostic.

### 9.2 Adding a New Tier

To add a new tier (e.g., Tier 6: Complexity Analysis):

1. Create `lib/tier6_complexity.py` with a `run(config: Config) -> list[Finding]` function
2. Add the tier to `runner.py`:
   - Add entry to `tier_dispatch` dict
   - Add `_run_tier6` method
3. Add config section to `DEFAULTS` in `config.py`
4. Add tier checkbox to `web/templates/index.html`
5. Update `TIER_NAMES` dict in the frontend JavaScript
6. Add tests in `tests/test_tier6_complexity.py`
7. Update `README.md` and this standards document

### 9.3 Adding a New Static Analysis Tool

To add a new tool to Tier 1 (e.g., `clangtidy` → `clanganalyzer`):

1. Create `lib/tier1_newool.py` following the existing `tier1_cppcheck.py` pattern:
   - `run(config: Config) -> list[Finding]`
   - Check `config.get("static_analysis", "newtool", "enabled")`
   - Auto-detect binary via `shutil.which()`
   - Parse output into `Finding` objects
   - Handle missing tool, timeouts, parse errors gracefully
2. Add to `runner.py:_run_tier1()` with `_emit("step_start/end")` calls
3. Add config section under `static_analysis:` in `audit_config.yaml`
4. Add tool to auto-detection in `config.py:_detect_tools()`
5. Update Tier 1 step percentages in frontend `T1_STEPS`

### 9.4 Adding New Report Sections

To add a new section to the report:

1. Add a `_build_new_section()` method to `ReportBuilder` in `report.py`
2. Call it from `build()` gated on the relevant tier
3. Follow the compactness rules: tables for listings, JSON only for structured data, count omitted items
4. Verify the overall report stays under the 5,000 token target

---

## 10. Release Checklist

Before any release or significant update:

- [ ] All tests pass: `python3 -m pytest tests/ -v`
- [ ] CLI works: `python3 audit.py --dry-run`
- [ ] Web UI starts: `python3 web/app.py` → loads at http://127.0.0.1:5800
- [ ] Full audit completes without errors on at least one real project
- [ ] Report is under 5,000 tokens for a 100K LOC codebase
- [ ] `--init` generates valid, loadable config for a C++ project
- [ ] `--init` generates valid, loadable config for a Python project
- [ ] `--no-research` works (offline mode)
- [ ] `--json` output is valid JSON parseable by `jq`
- [ ] No `print()` statements in `lib/` modules
- [ ] All public functions have docstrings
- [ ] All functions have type annotations
- [ ] No bare `except:` clauses
- [ ] `README.md` is up to date
- [ ] `AUDIT_TOOL_STANDARDS.md` is up to date
- [ ] `.gitignore` includes `.audit_cache/`, `__pycache__/`, generated reports

---

## 11. File Structure Reference

```
tools/audit/
    audit.py                    # CLI entry point
    audit_config.yaml           # Project-specific config (Vestige)
    README.md                   # User documentation
    AUDIT_TOOL_STANDARDS.md     # This document
    lib/
        __init__.py
        auto_config.py          # Project auto-detection and config generation
        config.py               # Config loading, validation, defaults
        findings.py             # Finding, Severity, ChangeSummary, AuditData, ResearchResult
        report.py               # ReportBuilder — markdown + JSON report assembly
        runner.py               # AuditRunner — tier orchestration with callbacks
        tier1_build.py          # Tier 1: Build warnings, test suite
        tier1_cppcheck.py       # Tier 1: cppcheck XML parsing
        tier1_clangtidy.py      # Tier 1: clang-tidy text parsing
        tier2_patterns.py       # Tier 2: Configurable regex scanning
        tier3_changes.py        # Tier 3: Git diff analysis
        tier4_stats.py          # Tier 4: LOC, Rule-of-Five, event lifecycle
        tier5_research.py       # Tier 5: Web research with caching
        utils.py                # Subprocess helpers, file enumeration, token estimation
    web/
        __init__.py
        app.py                  # Flask server and API routes
        audit_bridge.py         # AuditSession, QueueLogHandler, threading bridge
        static/
            favicon.svg         # App icon (SVG)
        templates/
            index.html          # Single-page frontend (inline CSS/JS)
    tests/                      # Test suite (to be created)
        __init__.py
        test_findings.py
        test_config.py
        test_tier1_build.py
        test_tier2_patterns.py
        test_tier3_changes.py
        test_tier4_stats.py
        test_tier5_research.py
        test_report.py
        test_runner.py
        test_auto_config.py
```
