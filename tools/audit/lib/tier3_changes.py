"""Tier 3: Git diff analysis — changed files, function signatures, subsystem mapping."""

from __future__ import annotations

import logging
import re
from pathlib import Path

from .config import Config
from .findings import ChangeSummary
from .utils import run_cmd

log = logging.getLogger("audit")


def run(config: Config) -> ChangeSummary:
    """Analyze git changes since base_ref."""
    summary = ChangeSummary()
    base_ref = config.get("changes", "base_ref", default="HEAD~1")

    # Check we're in a git repo
    git_dir = config.root / ".git"
    if not git_dir.exists():
        log.warning("Not a git repository — skipping Tier 3")
        return summary

    log.info("Tier 3: analyzing changes since %s", base_ref)

    # Get changed files with status
    rc, stdout, _ = run_cmd(
        f"git diff --name-status {base_ref}",
        cwd=config.root,
    )
    if rc != 0:
        log.warning("git diff failed (bad base_ref '%s'?) — skipping Tier 3", base_ref)
        return summary

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

        # Extract changed function names (for C++ files)
        if fname.endswith((".cpp", ".h")):
            funcs = _get_changed_functions(config.root, base_ref, fname)
            if funcs:
                entry["changed_functions"] = funcs

        summary.changed_files.append(entry)

    summary.subsystems_touched = sorted(subsystems)

    log.info("Tier 3: %d changed files across %d subsystems (+%d/-%d lines)",
             len(summary.changed_files), len(subsystems),
             summary.total_added, summary.total_removed)
    return summary


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
