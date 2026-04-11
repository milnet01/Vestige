"""Tier 3: Git diff analysis — changed files, function signatures, subsystem mapping, diff-aware security scanning."""

from __future__ import annotations

import logging
import re
from pathlib import Path

from .config import Config
from .findings import ChangeSummary, Finding, Severity
from .utils import run_cmd

log = logging.getLogger("audit")

# Security-sensitive patterns to scan in added lines (new code only)
_DIFF_SECURITY_PATTERNS: list[dict] = [
    {"name": "new_system_call", "pattern": r"\b(system|popen|exec[lv]p?)\s*\(",
     "severity": "high", "description": "New system/exec call in changed code"},
    {"name": "new_shell_concat", "pattern": r'"(?:cd|rm|sh|bash)\s.*\+',
     "severity": "high", "description": "Shell command string concatenation in new code"},
    {"name": "new_path_concat", "pattern": r"(?:QDir|QFile|fopen|ofstream|ifstream).*\+.*(?:name|path|id|file)",
     "severity": "high", "description": "File path from concatenation in new code — verify sanitization"},
    {"name": "new_raw_sql", "pattern": r'(?:exec|query|execute)\s*\(.*["\'].*(?:SELECT|INSERT|UPDATE|DELETE).*\+',
     "severity": "high", "description": "SQL concatenation in new code — use parameterized queries"},
    {"name": "new_eval", "pattern": r"\b(eval|exec)\s*\(",
     "severity": "high", "description": "eval/exec in new code — code injection risk"},
    {"name": "new_unsafe_c_str", "pattern": r"\b(strcpy|strcat|sprintf|gets)\s*\(",
     "severity": "high", "description": "Unsafe C string function in new code"},
    {"name": "new_socket_listen", "pattern": r"\blisten\s*\(",
     "severity": "medium", "description": "New socket listener — verify permissions and binding"},
    {"name": "new_deserialization", "pattern": r"\b(luaL_dofile|pickle\.load|yaml\.load|readObjectFromFile)\b",
     "severity": "high", "description": "Deserialization in new code — verify input validation"},
    {"name": "new_fork", "pattern": r"\b(forkpty|fork)\s*\(",
     "severity": "medium", "description": "New fork — verify FD cleanup and O_CLOEXEC"},
]


def run(config: Config) -> tuple[ChangeSummary, list[Finding]]:
    """Analyze git changes since base_ref. Returns (summary, security_findings)."""
    summary = ChangeSummary()
    findings: list[Finding] = []
    base_ref = config.get("changes", "base_ref", default="HEAD~1")

    # Check we're in a git repo
    git_dir = config.root / ".git"
    if not git_dir.exists():
        log.warning("Not a git repository — skipping Tier 3")
        return summary, findings

    log.info("Tier 3: analyzing changes since %s", base_ref)

    # Get changed files with status
    rc, stdout, _ = run_cmd(
        f"git diff --name-status {base_ref}",
        cwd=config.root,
    )
    if rc != 0:
        log.warning("git diff failed (bad base_ref '%s'?) — skipping Tier 3", base_ref)
        return summary, findings

    changed_files = _parse_name_status(stdout)

    # Get line counts
    rc, numstat_out, _ = run_cmd(
        f"git diff --numstat {base_ref}",
        cwd=config.root,
    )
    line_counts = _parse_numstat(numstat_out)

    # Get changed functions per file
    exclude_dirs = config.exclude_dirs
    subsystems: set[str] = set()

    for entry in changed_files:
        fname = entry["file"]

        # Skip excluded directories
        if any(fname.startswith(ex) for ex in exclude_dirs):
            continue

        # Add line counts
        if fname in line_counts:
            entry["added"] = line_counts[fname][0]
            entry["removed"] = line_counts[fname][1]
            summary.total_added += line_counts[fname][0]
            summary.total_removed += line_counts[fname][1]

        # Determine subsystem from path
        subsystem = _get_subsystem(fname)
        if subsystem:
            subsystems.add(subsystem)
            entry["subsystem"] = subsystem

        # Extract changed function names (for source files)
        source_exts = tuple(config.source_extensions)
        if fname.endswith(source_exts):
            funcs = _get_changed_functions(config.root, base_ref, fname)
            if funcs:
                entry["changed_functions"] = funcs

        # Diff-aware security scanning on added lines
        diff_findings = _scan_diff_for_security(config.root, base_ref, fname)
        findings.extend(diff_findings)

        summary.changed_files.append(entry)

    summary.subsystems_touched = sorted(subsystems)

    log.info("Tier 3: %d changed files across %d subsystems (+%d/-%d lines), %d security findings",
             len(summary.changed_files), len(subsystems),
             summary.total_added, summary.total_removed, len(findings))
    return summary, findings


def _parse_name_status(output: str) -> list[dict]:
    """Parse `git diff --name-status` output."""
    entries: list[dict] = []
    for line in output.strip().splitlines():
        parts = line.split("\t", 1)
        if len(parts) == 2:
            status, fname = parts
            entries.append({"file": fname, "status": status[0], "added": 0, "removed": 0})
    return entries


def _parse_numstat(output: str) -> dict[str, tuple[int, int]]:
    """Parse `git diff --numstat` output into {file: (added, removed)}."""
    counts: dict[str, tuple[int, int]] = {}
    for line in output.strip().splitlines():
        parts = line.split("\t")
        if len(parts) == 3:
            try:
                added = int(parts[0]) if parts[0] != "-" else 0
                removed = int(parts[1]) if parts[1] != "-" else 0
                counts[parts[2]] = (added, removed)
            except ValueError:
                continue
    return counts


def _get_subsystem(path: str) -> str:
    """Extract subsystem from file path (e.g., 'engine/physics/foo.cpp' -> 'physics')."""
    parts = Path(path).parts
    if len(parts) >= 2 and parts[0] == "engine":
        return parts[1]
    if len(parts) >= 1:
        return parts[0]
    return ""


def _get_changed_functions(root: Path, base_ref: str, filename: str) -> list[str]:
    """Extract function names from changed hunks."""
    rc, stdout, _ = run_cmd(
        f"git diff -U0 {base_ref} -- {filename}",
        cwd=root,
    )
    if rc != 0:
        return []

    funcs: set[str] = set()
    # Look for function signatures in diff hunk headers (@@...@@ function_name)
    for line in stdout.splitlines():
        # Git diff hunk headers contain the enclosing function
        m = re.match(r"@@.*@@\s+(.*)", line)
        if m:
            func_context = m.group(1).strip()
            # Try to extract function name from context
            func_m = re.search(r"(\w+)\s*\(", func_context)
            if func_m:
                funcs.add(func_m.group(1))

        # Also look for function definitions in added/removed lines
        if line.startswith(("+", "-")) and not line.startswith(("+++", "---")):
            func_m = re.match(r"[+-]\s*(?:void|bool|int|float|double|auto|glm::\w+|std::\w+|[\w:]+)\s+(\w+)\s*\(", line)
            if func_m:
                funcs.add(func_m.group(1))

    return sorted(funcs)


def _scan_diff_for_security(root: Path, base_ref: str, filename: str) -> list[Finding]:
    """Scan added lines in a diff for security-sensitive patterns."""
    # Only scan source files
    if not filename.endswith((".cpp", ".h", ".hpp", ".py", ".js", ".ts", ".go", ".rs",
                               ".java", ".c", ".cc", ".cxx")):
        return []

    rc, stdout, _ = run_cmd(
        f"git diff -U0 {base_ref} -- {filename}",
        cwd=root,
    )
    if rc != 0:
        return []

    findings: list[Finding] = []
    current_line = 0

    for diff_line in stdout.splitlines():
        # Track line numbers from hunk headers
        hunk_match = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@", diff_line)
        if hunk_match:
            current_line = int(hunk_match.group(1))
            continue

        # Only scan added lines (not removed or context)
        if not diff_line.startswith("+") or diff_line.startswith("+++"):
            if not diff_line.startswith("-"):
                current_line += 1
            continue

        added_content = diff_line[1:]  # Strip the leading +

        for pat_def in _DIFF_SECURITY_PATTERNS:
            if re.search(pat_def["pattern"], added_content):
                findings.append(Finding(
                    file=filename,
                    line=current_line,
                    severity=Severity.from_string(pat_def["severity"]),
                    category="diff_security",
                    source_tier=3,
                    title=pat_def["description"],
                    detail=added_content.strip()[:200],
                    pattern_name=f"diff_{pat_def['name']}",
                ))
                break  # One finding per line

        current_line += 1

    return findings
