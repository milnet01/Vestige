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

    log.info("Tier 2: scanning %d files against %d pattern categories",
             len(all_files), len(patterns_config))

    # Build a mapping of file extension globs -> files for efficient matching
    max_per_cat = config.get("report", "max_findings_per_category", default=100)

    for category_name, pattern_list in patterns_config.items():
        if not isinstance(pattern_list, list):
            continue

        cat_count = 0
        for pat_def in pattern_list:
            if cat_count >= max_per_cat:
                break

            name = pat_def.get("name", "unnamed")
            regex_str = pat_def.get("pattern", "")
            file_glob = pat_def.get("file_glob", "*")
            severity_str = pat_def.get("severity", "low")
            description = pat_def.get("description", "")
            exclude_pattern = pat_def.get("exclude_pattern", "")
            skip_comments = pat_def.get("skip_comments", False)

            try:
                regex = re.compile(regex_str)
            except re.error as e:
                log.warning("Invalid regex in pattern '%s': %s", name, e)
                continue

            exclude_re = None
            if exclude_pattern:
                try:
                    exclude_re = re.compile(exclude_pattern)
                except re.error:
                    pass

            # Parse file_glob (comma-separated)
            globs = [g.strip() for g in file_glob.split(",")]

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


def _scan_file(
    path: Path,
    regex: re.Pattern,
    exclude_re: re.Pattern | None,
    skip_comments: bool = False,
) -> list[tuple[int, str]]:
    """Scan a single file, return list of (line_number, line_text) for matches."""
    hits: list[tuple[int, str]] = []
    in_block_comment = False
    try:
        with open(path, "r", errors="replace") as f:
            for i, line in enumerate(f, start=1):
                stripped = line.strip()

                if skip_comments:
                    # Track block comments
                    if in_block_comment:
                        if "*/" in stripped:
                            in_block_comment = False
                        continue
                    if stripped.startswith("/*"):
                        if "*/" not in stripped:
                            in_block_comment = True
                        continue
                    # Skip single-line comments and Doxygen
                    if stripped.startswith("//"):
                        continue

                if regex.search(line):
                    if exclude_re and exclude_re.search(line):
                        continue
                    hits.append((i, line))
    except OSError:
        pass
    return hits
