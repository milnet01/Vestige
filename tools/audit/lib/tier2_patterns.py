# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 2: Configurable regex pattern scanning across source files."""

from __future__ import annotations

import fnmatch
import logging
import re
from pathlib import Path

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")


def run(config: Config) -> list[Finding]:
    """Scan all source files against configured patterns."""
    findings: list[Finding] = []
    patterns_config = config.patterns

    if not patterns_config:
        log.info("No patterns configured — skipping Tier 2")
        return findings

    # Collect all source and shader files
    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs + config.shader_dirs,
        extensions=config.source_extensions + [".glsl"],
        exclude_dirs=config.exclude_dirs,
    )

    # Task A1 — rule-source file exclusion. Files that register
    # audit rules (and therefore contain the tokens the rules are
    # looking for as *descriptive text*, not as code) produce
    # 100% false positives when a scan happens to include them.
    # The canonical example: `tools/audit/lib/auto_config.py` has
    # rule bodies like `{"description": "gets() is inherently unsafe"}`
    # — the `unsafe_gets` rule matches on that line.
    #
    # Honour two sources: per-project `exclude_file_patterns` in the
    # config (already declared in DEFAULTS but previously unread), and
    # a hard-coded set of built-in audit rule-source files so the
    # exclusion applies even when the user forgot to configure it.
    # Matching is a glob on the filename only (not the full path), so
    # a project that ships its own `auto_config.py` gets the same
    # protection for free.
    user_excludes = config.get("project", "exclude_file_patterns", default=[]) or []
    builtin_excludes = [
        # Vestige audit tool's rule-source; see header docstring.
        "auto_config.py",
    ]
    exclude_file_globs = list(user_excludes) + builtin_excludes

    def _is_file_excluded(path: Path) -> bool:
        name = path.name
        return any(fnmatch.fnmatch(name, g) for g in exclude_file_globs)

    if exclude_file_globs:
        pre = len(all_files)
        all_files = [f for f in all_files if not _is_file_excluded(f)]
        skipped = pre - len(all_files)
        if skipped > 0:
            log.info("Tier 2: excluded %d file(s) matching "
                     "exclude_file_patterns/builtin rule-source list",
                     skipped)

    log.info("Tier 2: scanning %d files against %d pattern categories",
             len(all_files), len(patterns_config))

    # Build a mapping of file extension globs -> files for efficient matching
    max_per_cat = config.get("report", "max_findings_per_category", default=100)

    # Pre-compile all regex patterns once (not per-file)
    compiled_patterns: list[tuple[str, dict, re.Pattern, re.Pattern | None, list[str]]] = []
    for category_name, pattern_list in patterns_config.items():
        if not isinstance(pattern_list, list):
            continue
        for pat_def in pattern_list:
            regex_str = pat_def.get("pattern", "")
            try:
                regex = re.compile(regex_str)
            except re.error as e:
                log.warning("Invalid regex in pattern '%s': %s",
                            pat_def.get("name", "unnamed"), e)
                continue
            exclude_re = None
            exclude_pattern = pat_def.get("exclude_pattern", "")
            if exclude_pattern:
                try:
                    exclude_re = re.compile(exclude_pattern)
                except re.error:
                    pass
            globs = [g.strip() for g in pat_def.get("file_glob", "*").split(",")]
            compiled_patterns.append((category_name, pat_def, regex, exclude_re, globs))

    cat_counts: dict[str, int] = {}
    for category_name, pat_def, regex, exclude_re, globs in compiled_patterns:
        cat_count = cat_counts.get(category_name, 0)
        if cat_count >= max_per_cat:
            continue

        name = pat_def.get("name", "unnamed")
        severity_str = pat_def.get("severity", "low")
        description = pat_def.get("description", "")
        skip_comments = pat_def.get("skip_comments", False)

        matching_files = _filter_files(all_files, globs)

        for fpath in matching_files:
            if cat_count >= max_per_cat:
                break
            hits = _scan_file(fpath, regex, exclude_re, skip_comments)
            for line_num, line_text in hits:
                if cat_count >= max_per_cat:
                    break
                findings.append(Finding(
                    file=relative_path(fpath, config.root),
                    line=line_num,
                    severity=Severity.from_string(severity_str),
                    category=category_name,
                    source_tier=2,
                    title=description,
                    detail=line_text.strip()[:200],
                    pattern_name=name,
                ))
                cat_count += 1
        cat_counts[category_name] = cat_count

    log.info("Tier 2: %d total findings", len(findings))
    return findings


def _filter_files(files: list[Path], globs: list[str]) -> list[Path]:
    """Filter file list by glob patterns (e.g., '*.cpp', '*.h')."""
    matched: list[Path] = []
    for f in files:
        for g in globs:
            if fnmatch.fnmatch(f.name, g):
                matched.append(f)
                break
    return matched


def _classify_line(line: str, in_block_comment: bool) -> tuple[str, bool]:
    """Strip C/C++ comments and string literals from a line.

    Returns (effective_line, still_in_block_comment). Comments and strings
    are replaced with spaces so regex column positions stay approximately correct.

    Python / shell-style ``#`` comments and triple-quoted strings are
    handled by ``_classify_line_python`` — dispatch by file extension
    lives in ``_scan_file``. Separating the two keeps each classifier
    simple and testable.
    """
    result: list[str] = []
    i = 0
    in_string: str | None = None

    if in_block_comment:
        idx = line.find("*/")
        if idx == -1:
            return ("", True)
        result.append(" " * (idx + 2))
        i = idx + 2
        in_block_comment = False

    while i < len(line):
        c = line[i]
        if in_string:
            if c == "\\" and i + 1 < len(line):
                result.append("  ")
                i += 2
                continue
            if c == in_string:
                in_string = None
            result.append(" ")
            i += 1
        elif c == "/" and i + 1 < len(line):
            if line[i + 1] == "/":
                break  # rest is comment
            elif line[i + 1] == "*":
                end = line.find("*/", i + 2)
                if end == -1:
                    in_block_comment = True
                    break
                else:
                    result.append(" " * (end + 2 - i))
                    i = end + 2
                    continue
            else:
                result.append(c)
                i += 1
        elif c in ('"', "'"):
            in_string = c
            result.append(" ")
            i += 1
        else:
            result.append(c)
            i += 1

    return ("".join(result), in_block_comment)


def _is_python_like(path: Path) -> bool:
    """True for files whose comment syntax is Python-style (# + triple-quoted).

    Covers ``.py`` and also shell / yaml / toml families where ``#``
    starts a line comment. Keeps dispatch simple — the goal is
    "treat `#` as a comment" rather than precise language parsing.
    """
    return path.suffix.lower() in {
        ".py", ".pyi", ".pyx",
        ".sh", ".bash", ".zsh",
        ".yaml", ".yml", ".toml", ".ini", ".cfg",
        ".rb",
    }


def _classify_line_python(
    line: str, in_triple: tuple[bool, str]
) -> tuple[str, tuple[bool, str]]:
    """Strip Python-style ``#`` comments and triple-quoted strings.

    State tuple is ``(inside_triple_quoted, quote_char)``. Single-line
    strings (``"..."`` / ``'...'``) are also masked so rules matching
    bare identifiers don't fire on string literals. Triple-quoted
    strings can span multiple lines; when a triple-quote opens
    mid-line the rest of the line is replaced with spaces and the
    caller stays in the triple state until the closing triple
    appears on a subsequent line.
    """
    result: list[str] = []
    i = 0
    inside_triple, triple_char = in_triple
    n = len(line)

    # Resume an open triple-quoted string from a previous line.
    if inside_triple:
        end_marker = triple_char * 3
        idx = line.find(end_marker)
        if idx == -1:
            # Entire line is inside the string; mask and keep the state.
            return (" " * n, (True, triple_char))
        result.append(" " * (idx + 3))
        i = idx + 3
        inside_triple = False

    in_string: str | None = None
    while i < n:
        c = line[i]
        if inside_triple:
            end_marker = triple_char * 3
            idx = line.find(end_marker, i)
            if idx == -1:
                result.append(" " * (n - i))
                return ("".join(result), (True, triple_char))
            result.append(" " * (idx + 3 - i))
            i = idx + 3
            inside_triple = False
            continue
        if in_string is not None:
            if c == "\\" and i + 1 < n:
                result.append("  ")
                i += 2
                continue
            if c == in_string:
                in_string = None
            result.append(" ")
            i += 1
            continue
        if c == "#":
            # Rest of line is a comment.
            break
        if c in ('"', "'"):
            # Triple-quoted string start?
            if i + 2 < n and line[i + 1] == c and line[i + 2] == c:
                end_marker = c * 3
                close = line.find(end_marker, i + 3)
                if close == -1:
                    # Opens on this line, doesn't close — rest of
                    # line is string, carry state into next line.
                    result.append(" " * (n - i))
                    return ("".join(result), (True, c))
                result.append(" " * (close + 3 - i))
                i = close + 3
                continue
            in_string = c
            result.append(" ")
            i += 1
            continue
        result.append(c)
        i += 1
    return ("".join(result), (False, ""))


def _scan_file(
    path: Path,
    regex: re.Pattern,
    exclude_re: re.Pattern | None,
    skip_comments: bool = False,
    multiline: bool = False,
) -> list[tuple[int, str]]:
    """Scan a single file, return list of (line_number, line_text) for matches."""
    if multiline:
        return _scan_file_multiline(path, regex, exclude_re, skip_comments)

    hits: list[tuple[int, str]] = []
    in_block = False
    in_triple: tuple[bool, str] = (False, "")
    python_like = _is_python_like(path)
    try:
        with open(path, "r", errors="replace") as f:
            for i, line in enumerate(f, start=1):
                if skip_comments:
                    if python_like:
                        effective, in_triple = _classify_line_python(line, in_triple)
                    else:
                        effective, in_block = _classify_line(line, in_block)
                    if regex.search(effective):
                        if exclude_re and exclude_re.search(effective):
                            continue
                        hits.append((i, line))
                else:
                    if regex.search(line):
                        if exclude_re and exclude_re.search(line):
                            continue
                        hits.append((i, line))
    except OSError:
        pass
    return hits


def _scan_file_multiline(
    path: Path,
    regex: re.Pattern,
    exclude_re: re.Pattern | None,
    skip_comments: bool,
) -> list[tuple[int, str]]:
    """Scan a file with multiline regex (re.DOTALL)."""
    MAX_FILE_SIZE = 1_048_576  # 1MB cap for multiline
    hits: list[tuple[int, str]] = []
    try:
        if path.stat().st_size > MAX_FILE_SIZE:
            return hits
        content = path.read_text(errors="replace")
        if skip_comments:
            content = _strip_comments_multiline(content)
        for match in regex.finditer(content):
            line_num = content[:match.start()].count("\n") + 1
            matched_text = match.group(0).replace("\n", " ")[:200]
            if exclude_re and exclude_re.search(matched_text):
                continue
            hits.append((line_num, matched_text))
    except OSError:
        pass
    return hits


def _strip_comments_multiline(content: str) -> str:
    """Remove C/C++ comments from content while preserving line structure."""
    result: list[str] = []
    i = 0
    in_string: str | None = None
    while i < len(content):
        c = content[i]
        if in_string:
            if c == "\\" and i + 1 < len(content):
                result.append("  ")
                i += 2
                continue
            if c == in_string:
                in_string = None
            result.append(" " if c != "\n" else "\n")
            i += 1
        elif c == "/" and i + 1 < len(content):
            if content[i + 1] == "/":
                # Single-line comment: skip to end of line
                end = content.find("\n", i + 2)
                if end == -1:
                    break
                result.append(" " * (end - i))
                i = end
            elif content[i + 1] == "*":
                # Block comment: replace with spaces (preserve newlines)
                end = content.find("*/", i + 2)
                if end == -1:
                    break
                for ch in content[i:end + 2]:
                    result.append("\n" if ch == "\n" else " ")
                i = end + 2
            else:
                result.append(c)
                i += 1
        elif c in ('"', "'"):
            in_string = c
            result.append(" ")
            i += 1
        else:
            result.append(c)
            i += 1
    return "".join(result)
