"""CI integration — format findings as GitHub Actions annotations."""

from __future__ import annotations

import os
from typing import Any

from .findings import Finding, Severity


def format_github_annotations(findings: list[Finding], max_annotations: int = 50) -> str:
    """Format findings as GitHub Actions annotation commands.

    GitHub limits: 10 warnings + 10 errors per step, 50 per run.
    Strategy: emit errors for CRITICAL+HIGH, warnings for MEDIUM.
    """
    lines: list[str] = []
    error_count = 0
    warning_count = 0

    sorted_findings = sorted(findings, key=lambda f: (f.severity, f.file, f.line or 0))

    for f in sorted_findings:
        if error_count + warning_count >= max_annotations:
            break

        file_part = f"file={f.file}" if f.file else ""
        line_part = f",line={f.line}" if f.line else ""
        location = f"{file_part}{line_part}" if file_part else ""
        title = f.title.replace("\n", " ")[:200]

        if f.severity <= Severity.HIGH and error_count < 10:
            lines.append(f"::error {location}::{title}")
            error_count += 1
        elif f.severity == Severity.MEDIUM and warning_count < 10:
            lines.append(f"::warning {location}::{title}")
            warning_count += 1

    return "\n".join(lines)


def format_step_summary(results_dict: dict[str, Any]) -> str:
    """Format a compact markdown summary for $GITHUB_STEP_SUMMARY."""
    findings = results_dict.get("findings", [])
    counts: dict[str, int] = {}
    for f in findings:
        sev = f.get("severity", "info")
        counts[sev] = counts.get(sev, 0) + 1

    t1 = results_dict.get("tier1_summary", {})
    build = t1.get("build", {})
    tests = t1.get("tests", {})
    duration = results_dict.get("duration", 0)

    lines = [
        "## Audit Results",
        "",
        f"| Metric | Value |",
        f"|--------|-------|",
        f"| Critical | {counts.get('critical', 0)} |",
        f"| High | {counts.get('high', 0)} |",
        f"| Medium | {counts.get('medium', 0)} |",
        f"| Low | {counts.get('low', 0)} |",
        f"| Total | {len(findings)} |",
        f"| Build | {'Pass' if build.get('ok') else 'Fail'} |",
        f"| Tests | {tests.get('passed', 0)} passed, {tests.get('failed', 0)} failed |",
        f"| Duration | {duration}s |",
    ]

    return "\n".join(lines)


def write_step_summary(results_dict: dict[str, Any]) -> bool:
    """Write summary to $GITHUB_STEP_SUMMARY if available. Returns True if written."""
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return False

    try:
        with open(summary_path, "a") as f:
            f.write(format_step_summary(results_dict))
            f.write("\n")
        return True
    except OSError:
        return False


def get_exit_code(findings: list[Finding]) -> int:
    """Determine CI exit code based on finding severity.

    Returns: 0 = no HIGH+, 1 = has HIGH, 2 = has CRITICAL.
    """
    has_critical = any(f.severity == Severity.CRITICAL for f in findings)
    has_high = any(f.severity <= Severity.HIGH for f in findings)

    if has_critical:
        return 2
    elif has_high:
        return 1
    return 0
