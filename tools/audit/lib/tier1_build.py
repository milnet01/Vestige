"""Tier 1: Build compilation, test execution, and sanitizer runs."""

from __future__ import annotations

import logging
import re
from pathlib import Path

from .config import Config
from .findings import Finding, Severity
from .utils import run_cmd

log = logging.getLogger("audit")


def run_build(config: Config) -> tuple[list[Finding], dict]:
    """Run the build command and parse warnings/errors.

    Returns (findings, summary_dict).
    """
    findings: list[Finding] = []
    summary = {"warnings": 0, "errors": 0, "build_ok": False}

    build_cmd = config.get("build", "build_cmd")
    if not build_cmd:
        log.info("No build command configured — skipping build check")
        return findings, summary

    log.info("Running build...")
    rc, stdout, stderr = run_cmd(build_cmd, cwd=config.root, timeout=300)
    output = stdout + "\n" + stderr
    warning_regex = config.get("build", "warning_regex", default=r"warning:|error:")

    for line in output.splitlines():
        if re.search(warning_regex, line, re.IGNORECASE):
            # Skip external/ lines
            if "/external/" in line or "external/" in line:
                continue
            is_error = "error:" in line.lower()
            if is_error:
                summary["errors"] += 1
            else:
                summary["warnings"] += 1

            # Try to extract file:line from the warning
            m = re.match(r"(.+?):(\d+):\d*:?\s*(warning|error):\s*(.*)", line)
            if m:
                findings.append(Finding(
                    file=m.group(1),
                    line=int(m.group(2)),
                    severity=Severity.HIGH if is_error else Severity.MEDIUM,
                    category="build",
                    source_tier=1,
                    title=m.group(4).strip()[:120],
                    detail=line.strip(),
                    pattern_name="compiler",
                ))

    summary["build_ok"] = (rc == 0)
    if rc != 0 and summary["errors"] == 0:
        findings.append(Finding(
            file="",
            line=None,
            severity=Severity.CRITICAL,
            category="build",
            source_tier=1,
            title="Build failed with non-zero exit code",
            detail=f"Exit code: {rc}",
            pattern_name="build_failure",
        ))

    log.info("Build: %s, %d warnings, %d errors",
             "OK" if summary["build_ok"] else "FAILED",
             summary["warnings"], summary["errors"])
    return findings, summary


def run_tests(config: Config) -> tuple[list[Finding], dict]:
    """Run the test suite and parse results.

    Returns (findings, summary_dict).
    """
    findings: list[Finding] = []
    summary = {"passed": 0, "failed": 0, "skipped": 0, "total": 0, "tests_ok": False}

    test_cmd = config.get("build", "test_cmd")
    if not test_cmd:
        log.info("No test command configured — skipping tests")
        return findings, summary

    log.info("Running tests...")
    rc, stdout, stderr = run_cmd(test_cmd, cwd=config.root, timeout=600)
    output = stdout + "\n" + stderr

    # Parse ctest summary line: "X% tests passed, N tests failed out of M"
    m = re.search(r"(\d+)% tests passed,\s*(\d+) tests failed out of (\d+)", output)
    if m:
        summary["failed"] = int(m.group(2))
        summary["total"] = int(m.group(3))
        summary["passed"] = summary["total"] - summary["failed"]

    # Count skipped (ctest marks as "Not Run")
    skipped = re.findall(r"Not Run", output)
    summary["skipped"] = len(skipped)

    summary["tests_ok"] = (rc == 0)

    # Find individual test failures
    for line in output.splitlines():
        fail_match = re.match(r"\s*\d+/\d+ Test\s+#\d+:\s+(\S+)\s+\.+\s*\*\*\*Failed", line)
        if fail_match:
            findings.append(Finding(
                file="tests/",
                line=None,
                severity=Severity.HIGH,
                category="tests",
                source_tier=1,
                title=f"Test failed: {fail_match.group(1)}",
                detail=line.strip(),
                pattern_name="test_failure",
            ))

    log.info("Tests: %d passed, %d failed, %d skipped",
             summary["passed"], summary["failed"], summary["skipped"])
    return findings, summary


def run(config: Config) -> tuple[list[Finding], dict]:
    """Run all Tier 1 build checks. Returns (findings, summary)."""
    all_findings: list[Finding] = []
    summary: dict = {}

    build_findings, build_summary = run_build(config)
    all_findings.extend(build_findings)
    summary["build"] = build_summary

    test_findings, test_summary = run_tests(config)
    all_findings.extend(test_findings)
    summary["tests"] = test_summary

    return all_findings, summary
