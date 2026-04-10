"""Tier 1: clang-tidy static analysis with text output parsing."""

from __future__ import annotations

import logging
import re
from pathlib import Path

from .config import Config
from .findings import Finding, Severity
from .utils import run_cmd, enumerate_files, relative_path

log = logging.getLogger("audit")


def run(config: Config) -> list[Finding]:
    """Run clang-tidy and parse results into findings."""
    findings: list[Finding] = []
    ct_config = config.get("static_analysis", "clang_tidy", default={})

    if not ct_config.get("enabled", False):
        log.info("clang-tidy disabled — skipping")
        return findings

    if config.language not in ("cpp", "c"):
        log.info("clang-tidy only applies to C/C++ — skipping")
        return findings

    binary = ct_config.get("binary", "clang-tidy")
    checks = ct_config.get("checks", "bugprone-*,performance-*")
    compile_commands = ct_config.get("compile_commands")
    fallback_flags = ct_config.get("fallback_flags", "-std=c++17")
    max_files = ct_config.get("max_files", 50)
    timeout = ct_config.get("timeout", 600)

    # Determine source files to analyze
    source_files = _get_target_files(config, max_files)
    if not source_files:
        log.warning("No source files found for clang-tidy")
        return findings

    # Check for compile_commands.json
    has_compile_db = False
    if compile_commands:
        cc_path = config.root / compile_commands
        if cc_path.exists():
            has_compile_db = True

    if not has_compile_db:
        log.warning("compile_commands.json not found — clang-tidy results will have "
                     "many false positives from missing includes. Consider running: "
                     "cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")

    # Build command
    file_list = " ".join(f'"{f}"' for f in source_files)
    if has_compile_db:
        cc_dir = (config.root / compile_commands).parent
        cmd = f"{binary} -p {cc_dir} --checks='{checks}' {file_list}"
    else:
        cmd = f"{binary} --checks='{checks}' {file_list} -- {fallback_flags}"

    log.info("Running clang-tidy on %d files...", len(source_files))
    rc, stdout, stderr = run_cmd(cmd, cwd=config.root, timeout=timeout)

    if rc == -1 and "TIMEOUT" in stderr:
        findings.append(Finding(
            file="",
            line=None,
            severity=Severity.HIGH,
            category="clang_tidy",
            source_tier=1,
            title=f"clang-tidy timed out after {timeout}s",
            pattern_name="clangtidy_timeout",
        ))
        return findings

    # Parse output — clang-tidy writes diagnostics to stdout
    output = stdout + "\n" + stderr
    findings.extend(_parse_output(output, config))

    log.info("clang-tidy: %d findings", len(findings))
    return findings


def _get_target_files(config: Config, max_files: int) -> list[str]:
    """Get list of .cpp files to analyze (limited by max_files)."""
    files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".cpp"],
        exclude_dirs=config.exclude_dirs,
    )
    # Prioritize engine/ over tests/
    engine_files = [f for f in files if "engine/" in str(f)]
    app_files = [f for f in files if "app/" in str(f)]
    test_files = [f for f in files if "tests/" in str(f)]

    ordered = engine_files + app_files + test_files
    selected = ordered[:max_files]
    return [relative_path(f, config.root) for f in selected]


def _parse_output(output: str, config: Config) -> list[Finding]:
    """Parse clang-tidy text output."""
    findings: list[Finding] = []
    exclude_dirs = config.exclude_dirs

    # Pattern: file:line:col: warning: message [check-name]
    pattern = re.compile(
        r"(.+?):(\d+):\d+:\s*(warning|error|note):\s*(.*?)(?:\s*\[([^\]]+)\])?\s*$"
    )

    seen_keys: set[str] = set()

    for line in output.splitlines():
        m = pattern.match(line)
        if not m:
            continue

        file_path = m.group(1)
        line_num = int(m.group(2))
        level = m.group(3)
        message = m.group(4).strip()
        check_name = m.group(5) or ""

        # Skip external files and notes
        if any(ex in file_path for ex in exclude_dirs):
            continue
        if level == "note":
            continue

        # Dedup by file:line:check
        key = f"{file_path}:{line_num}:{check_name}"
        if key in seen_keys:
            continue
        seen_keys.add(key)

        severity = Severity.HIGH if level == "error" else Severity.MEDIUM

        findings.append(Finding(
            file=file_path,
            line=line_num,
            severity=severity,
            category="clang_tidy",
            source_tier=1,
            title=f"[{check_name}] {message[:120]}" if check_name else message[:120],
            pattern_name=f"clang-tidy:{check_name}" if check_name else "clang-tidy",
        ))

    return findings
