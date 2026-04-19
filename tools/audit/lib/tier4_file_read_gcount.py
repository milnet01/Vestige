# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: ``stream.read(buf, N)`` without a follow-up ``.gcount()`` check.

Closes out idea #10 from AUDIT_TOOL_IMPROVEMENTS.md.

Binary stream reads on ``std::istream`` / ``std::ifstream`` set failbit
and return short reads on truncation; the only reliable way to know
how many bytes were actually read is ``stream.gcount()``. Code that
reads into a fixed-size buffer and then uses the buffer as if the full
N bytes were delivered is a latent bug — it will silently operate on
stale memory past the real end of the stream.

The detector scans .cpp files for ``.read(`` call-sites and looks
forward up to ``window_lines`` lines for a ``.gcount(`` reference. If
none is found, the site is flagged.
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")


# Match ".read(" with any whitespace.
_READ_RE = re.compile(r"\.read\s*\(")

# Match ".gcount(" — the evidence we want in the lookahead window.
# Also accept stream-state checks (.good() / .fail() / .eof()) because
# istream::read() sets failbit when fewer than N bytes are delivered, so a
# post-read .good()/.fail() check catches short reads just as reliably as
# gcount() does. The intent of the rule is "did the caller verify the read
# succeeded in full?" — any of these patterns satisfies it.
_GCOUNT_RE = re.compile(
    r"\.gcount\s*\("
    r"|\.good\s*\(\s*\)"
    r"|\.fail\s*\(\s*\)"
    r"|\.eof\s*\(\s*\)"
)

# Lines that mention any of these substrings are not interesting — they're
# either Python/stdlib filesystem helpers, or the kind of read that doesn't
# have a short-read failure mode (a string-returning read can't "half"
# succeed).
_EXCLUDE_SUBSTRINGS: tuple[str, ...] = (
    ".read_text",       # Python / filesystem helpers
    ".read_bytes",
    "fs::read",         # Rust-style filesystem API
    "std::filesystem",  # fs::path objects
    "readString",       # custom string reads (fixed-size)
    "readLine",         # line readers return the line length
    "boost::asio",      # networking reads with their own error code path
)

# Basenames of files that should never be flagged even if they match the
# regex — typically the audit tool's own rule-definition files which
# describe the pattern in docstrings/strings.
_EXEMPT_BASENAMES: frozenset[str] = frozenset({
    "auto_config.py",
    "tier4_file_read_gcount.py",   # this file
    "tier2_patterns.py",
    "AUDIT_TOOL_IMPROVEMENTS.md",
})


@dataclass
class FileReadGcountResult:
    """Result of the ``.read()``-without-``.gcount()`` sweep."""
    findings: list[dict[str, Any]] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "findings": self.findings[:30],
            "count": len(self.findings),
        }


def _is_excluded_line(line: str) -> bool:
    """True if this line contains a substring that opts it out of the check."""
    return any(sub in line for sub in _EXCLUDE_SUBSTRINGS)


def _read_is_inside_string_literal(line: str) -> bool:
    """Heuristic: if the ``.read(`` token sits inside a double-quoted string
    on the same line, this is not a real C++ stream read — it's a literal
    (e.g., an embedded Python snippet or an error-message template).

    We count unescaped double quotes preceding the first ``.read(`` match;
    an odd count means the token is inside an open string.
    """
    m = _READ_RE.search(line)
    if not m:
        return False
    prefix = line[: m.start()]
    # Strip escaped quotes (\"); count the rest.
    prefix = prefix.replace(r"\"", "")
    return prefix.count('"') % 2 == 1


def _scan_file(
    path: Path, rel: str, window_lines: int,
) -> list[dict[str, Any]]:
    """Scan one .cpp file for unchecked ``.read()`` calls."""
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return []

    lines = text.splitlines()
    results: list[dict[str, Any]] = []

    for idx, line in enumerate(lines):
        if not _READ_RE.search(line):
            continue
        if _is_excluded_line(line):
            continue
        if _read_is_inside_string_literal(line):
            continue

        # Skip lines that are comments — a C++ comment discussing .read() is
        # not an actual call.
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue

        # Look ahead up to `window_lines` lines (including the current line)
        # for a .gcount() reference.
        end = min(len(lines), idx + 1 + window_lines)
        window_text = "\n".join(lines[idx:end])
        if _GCOUNT_RE.search(window_text):
            continue

        results.append({
            "file": rel,
            "line": idx + 1,
            "text": line.strip()[:120],
        })

    return results


def analyze_file_read_gcount(
    config: Config,
) -> tuple[FileReadGcountResult, list[Finding]]:
    """Detect ``.read(buf, N)`` calls with no ``.gcount()`` check nearby."""
    result = FileReadGcountResult()
    findings: list[Finding] = []

    if not config.get("tier4", "file_read_gcount", "enabled", default=True):
        log.info("Tier 4 file_read_gcount: disabled")
        return result, findings

    window_lines = int(
        config.get("tier4", "file_read_gcount", "window_lines", default=20)
    )
    max_findings = int(
        config.get("tier4", "file_read_gcount", "max_findings", default=30)
    )

    # Only scan .cpp sources — the check is a C++ idiom. Header files
    # occasionally contain inline .read() calls too, so we accept .h / .hpp
    # as well if they're in the configured extensions.
    extensions = [
        e for e in config.source_extensions
        if e.lower() in (".cpp", ".cxx", ".cc", ".c", ".h", ".hpp")
    ]
    if not extensions:
        extensions = [".cpp"]

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=extensions,
        exclude_dirs=config.exclude_dirs,
    )

    # Filter exempt basenames (rule-definition files that would otherwise
    # match their own description strings).
    scanned: list[Path] = [f for f in all_files if f.name not in _EXEMPT_BASENAMES]

    log.info(
        "Tier 4 file_read_gcount: scanning %d files (window=%d)",
        len(scanned), window_lines,
    )

    for fpath in scanned:
        rel = relative_path(fpath, config.root)
        hits = _scan_file(fpath, rel, window_lines)
        result.findings.extend(hits)

    # Emit findings (capped).
    for item in result.findings[:max_findings]:
        findings.append(Finding(
            file=item["file"],
            line=item["line"],
            severity=Severity.MEDIUM,
            category="bug",
            source_tier=4,
            title="stream.read() without .gcount() check",
            detail=(
                f"Binary read may silently truncate; no .gcount() "
                f"found in the next {window_lines} lines. "
                f"Line: {item['text']}"
            ),
            pattern_name="file_read_no_gcount",
        ))

    log.info(
        "Tier 4 file_read_gcount: %d unchecked read sites",
        len(result.findings),
    )
    return result, findings
