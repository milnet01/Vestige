"""Tier 1: cppcheck static analysis with XML output parsing."""

from __future__ import annotations

import logging
from pathlib import Path

from .config import Config
from .findings import Finding, Severity
from .utils import run_cmd, relative_path

log = logging.getLogger("audit")


def run(config: Config) -> list[Finding]:
    """Run cppcheck and parse results into findings."""
    findings: list[Finding] = []
    cpp_config = config.get("static_analysis", "cppcheck", default={})

    if not cpp_config.get("enabled", False):
        log.info("cppcheck disabled — skipping")
        return findings

    binary = cpp_config.get("binary", "cppcheck")
    args = cpp_config.get("args", "")
    targets = " ".join(cpp_config.get("targets", []))
    timeout = cpp_config.get("timeout", 600)

    # Use XML output for structured parsing
    cmd = f"{binary} --xml --xml-version=2 {args} {targets}"
    log.info("Running cppcheck...")

    rc, stdout, stderr = run_cmd(cmd, cwd=config.root, timeout=timeout)

    if rc == -1 and "TIMEOUT" in stderr:
        findings.append(Finding(
            file="",
            line=None,
            severity=Severity.HIGH,
            category="cppcheck",
            source_tier=1,
            title=f"cppcheck timed out after {timeout}s",
            pattern_name="cppcheck_timeout",
        ))
        return findings

    # cppcheck writes XML to stderr
    findings.extend(_parse_xml(stderr, config))

    if not findings:
        # Try parsing text output as fallback
        findings.extend(_parse_text(stderr, config))

    log.info("cppcheck: %d findings", len(findings))
    return findings


def _parse_xml(xml_text: str, config: Config) -> list[Finding]:
    """Parse cppcheck XML v2 output."""
    findings: list[Finding] = []
    try:
        import xmltodict
    except ImportError:
        log.warning("xmltodict not installed — falling back to text parsing")
        return findings

    if "<?xml" not in xml_text:
        return findings

    try:
        parsed = xmltodict.parse(xml_text)
    except Exception as e:
        log.warning("Failed to parse cppcheck XML: %s", e)
        return findings

    results = parsed.get("results", {})
    errors = results.get("errors", {}).get("error", [])

    # xmltodict returns a single dict if there's only one error
    if isinstance(errors, dict):
        errors = [errors]

    exclude_dirs = config.exclude_dirs

    for err in errors:
        err_id = err.get("@id", "")
        severity_str = err.get("@severity", "information")
        msg = err.get("@msg", "")
        verbose = err.get("@verbose", msg)

        # Get location
        location = err.get("location", {})
        if isinstance(location, list):
            location = location[0] if location else {}
        file_path = location.get("@file", "")
        line_num = location.get("@line")

        # Skip external files
        if any(ex in file_path for ex in exclude_dirs):
            continue

        # Skip certain noisy IDs
        if err_id in ("missingIncludeSystem", "unusedFunction", "unmatchedSuppression"):
            continue

        line = int(line_num) if line_num else None
        rel_file = file_path

        findings.append(Finding(
            file=rel_file,
            line=line,
            severity=Severity.from_string(severity_str),
            category="cppcheck",
            source_tier=1,
            title=f"[{err_id}] {msg[:120]}",
            detail=verbose[:200] if verbose != msg else "",
            pattern_name=f"cppcheck:{err_id}",
        ))

    return findings


def _parse_text(text: str, config: Config) -> list[Finding]:
    """Fallback: parse cppcheck plain text output."""
    import re
    findings: list[Finding] = []
    exclude_dirs = config.exclude_dirs

    for line in text.splitlines():
        # Format: [file:line]: (severity) message
        m = re.match(r"\[(.+?):(\d+)\]:\s*\((\w+)\)\s*(.*)", line)
        if not m:
            continue

        file_path = m.group(1)
        if any(ex in file_path for ex in exclude_dirs):
            continue

        findings.append(Finding(
            file=file_path,
            line=int(m.group(2)),
            severity=Severity.from_string(m.group(3)),
            category="cppcheck",
            source_tier=1,
            title=m.group(4).strip()[:120],
            pattern_name="cppcheck",
        ))

    return findings
