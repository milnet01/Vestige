# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.ci_output — GitHub Actions annotation formatting, step summary, exit codes."""

from __future__ import annotations

import os
from pathlib import Path
from unittest.mock import patch

import pytest

from lib.ci_output import (
    format_github_annotations,
    format_step_summary,
    get_exit_code,
    write_step_summary,
)
from lib.findings import Finding, Severity


# ---------------------------------------------------------------------------
# format_github_annotations
# ---------------------------------------------------------------------------


class TestFormatGitHubAnnotations:
    """format_github_annotations should emit ::error and ::warning commands."""

    def test_critical_emits_error(self):
        findings = [Finding("a.cpp", 10, Severity.CRITICAL, "cat", 1, "Critical issue")]
        result = format_github_annotations(findings)
        assert "::error" in result
        assert "Critical issue" in result

    def test_high_emits_error(self):
        findings = [Finding("a.cpp", 10, Severity.HIGH, "cat", 1, "High issue")]
        result = format_github_annotations(findings)
        assert "::error" in result

    def test_medium_emits_warning(self):
        findings = [Finding("a.cpp", 10, Severity.MEDIUM, "cat", 1, "Medium issue")]
        result = format_github_annotations(findings)
        assert "::warning" in result

    def test_low_not_emitted(self):
        findings = [Finding("a.cpp", 10, Severity.LOW, "cat", 1, "Low issue")]
        result = format_github_annotations(findings)
        assert result == ""

    def test_info_not_emitted(self):
        findings = [Finding("a.cpp", 10, Severity.INFO, "cat", 1, "Info")]
        result = format_github_annotations(findings)
        assert result == ""

    def test_respects_max_annotations(self):
        findings = [
            Finding(f"f{i}.cpp", i, Severity.CRITICAL, "cat", 1, f"Issue {i}")
            for i in range(100)
        ]
        result = format_github_annotations(findings, max_annotations=5)
        lines = [l for l in result.strip().split("\n") if l]
        assert len(lines) <= 5

    def test_error_limit_10(self):
        findings = [
            Finding(f"f{i}.cpp", i, Severity.HIGH, "cat", 1, f"High {i}")
            for i in range(20)
        ]
        result = format_github_annotations(findings)
        error_count = result.count("::error")
        assert error_count <= 10

    def test_warning_limit_10(self):
        findings = [
            Finding(f"f{i}.cpp", i, Severity.MEDIUM, "cat", 1, f"Med {i}")
            for i in range(20)
        ]
        result = format_github_annotations(findings)
        warning_count = result.count("::warning")
        assert warning_count <= 10

    def test_file_and_line_in_output(self):
        findings = [Finding("src/main.cpp", 42, Severity.HIGH, "cat", 1, "Bug")]
        result = format_github_annotations(findings)
        assert "file=src/main.cpp" in result
        assert "line=42" in result

    def test_empty_findings(self):
        result = format_github_annotations([])
        assert result == ""


class TestSeverityMapping:
    """Verify correct mapping of severity levels to annotation types."""

    @pytest.mark.parametrize(
        "severity,expected_prefix",
        [
            (Severity.CRITICAL, "::error"),
            (Severity.HIGH, "::error"),
            (Severity.MEDIUM, "::warning"),
        ],
    )
    def test_severity_to_annotation_type(self, severity: Severity, expected_prefix: str):
        findings = [Finding("f.cpp", 1, severity, "cat", 1, "Issue")]
        result = format_github_annotations(findings)
        assert result.startswith(expected_prefix)


# ---------------------------------------------------------------------------
# format_step_summary
# ---------------------------------------------------------------------------


class TestFormatStepSummary:
    """format_step_summary should produce a markdown table."""

    def test_produces_markdown_table(self):
        results_dict = {
            "findings": [
                {"severity": "critical"},
                {"severity": "high"},
                {"severity": "medium"},
            ],
            "tier1_summary": {
                "build": {"ok": True},
                "tests": {"passed": 10, "failed": 0},
            },
            "duration": 5.5,
        }
        result = format_step_summary(results_dict)
        assert "## Audit Results" in result
        assert "Critical" in result
        assert "High" in result
        assert "Total" in result

    def test_empty_findings(self):
        result = format_step_summary({"findings": [], "tier1_summary": {}, "duration": 0})
        assert "Total" in result

    def test_duration_included(self):
        result = format_step_summary({
            "findings": [],
            "tier1_summary": {},
            "duration": 12.3,
        })
        assert "12.3" in result


# ---------------------------------------------------------------------------
# write_step_summary
# ---------------------------------------------------------------------------


class TestWriteStepSummary:
    """write_step_summary writes to GITHUB_STEP_SUMMARY path."""

    def test_writes_to_file(self, tmp_path: Path):
        summary_file = tmp_path / "summary.md"
        with patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(summary_file)}):
            result = write_step_summary({"findings": [], "tier1_summary": {}, "duration": 0})
        assert result is True
        assert summary_file.exists()
        assert "Audit Results" in summary_file.read_text()

    def test_returns_false_without_env(self):
        with patch.dict(os.environ, {}, clear=True):
            result = write_step_summary({"findings": []})
        assert result is False


# ---------------------------------------------------------------------------
# get_exit_code
# ---------------------------------------------------------------------------


class TestGetExitCode:
    """get_exit_code should return 0, 1, or 2 based on severity."""

    def test_no_findings_returns_0(self):
        assert get_exit_code([]) == 0

    def test_only_low_returns_0(self):
        findings = [Finding("f.cpp", 1, Severity.LOW, "cat", 1, "Low")]
        assert get_exit_code(findings) == 0

    def test_only_info_returns_0(self):
        findings = [Finding("f.cpp", 1, Severity.INFO, "cat", 1, "Info")]
        assert get_exit_code(findings) == 0

    def test_high_returns_1(self):
        findings = [Finding("f.cpp", 1, Severity.HIGH, "cat", 1, "High")]
        assert get_exit_code(findings) == 1

    def test_critical_returns_2(self):
        findings = [Finding("f.cpp", 1, Severity.CRITICAL, "cat", 1, "Crit")]
        assert get_exit_code(findings) == 2

    def test_critical_overrides_high(self):
        findings = [
            Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "High"),
            Finding("b.cpp", 2, Severity.CRITICAL, "cat", 1, "Crit"),
        ]
        assert get_exit_code(findings) == 2

    def test_medium_returns_0(self):
        findings = [Finding("f.cpp", 1, Severity.MEDIUM, "cat", 1, "Med")]
        assert get_exit_code(findings) == 0
