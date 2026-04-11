"""Tests for lib.html_report — generate_html_report produces valid HTML."""

from __future__ import annotations

from pathlib import Path

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.findings import AuditData, ChangeSummary, Finding, Severity
from lib.html_report import generate_html_report
from lib.runner import AuditResults
from lib.trends import TrendReport


def _make_config(tmp_path: Path) -> Config:
    """Create a minimal Config for testing."""
    raw = _deep_merge(DEFAULTS, {
        "project": {"name": "HTMLTestProject", "root": str(tmp_path)},
        "report": {"output_path": "docs/REPORT.md"},
    })
    return Config(raw=raw, root=tmp_path)


def _make_results(findings: list[Finding] | None = None) -> AuditResults:
    """Create AuditResults with optional findings."""
    return AuditResults(
        findings=findings or [],
        tier1_summary={"build": {"build_ok": True, "warnings": 0, "errors": 0},
                       "tests": {"passed": 5, "failed": 0, "skipped": 0}},
        tiers_run=[1, 2, 3, 4],
        duration=2.5,
    )


# ---------------------------------------------------------------------------
# Basic HTML output
# ---------------------------------------------------------------------------


class TestGenerateHtmlReport:
    """generate_html_report should produce a complete HTML document."""

    def test_returns_string(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        results = _make_results()
        html = generate_html_report(results, cfg)
        assert isinstance(html, str)

    def test_starts_with_doctype(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert html.strip().startswith("<!DOCTYPE html>")

    def test_contains_html_tags(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert "<html" in html
        assert "</html>" in html
        assert "<head>" in html
        assert "<body>" in html

    def test_contains_project_name(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert "HTMLTestProject" in html

    def test_contains_css(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert "<style>" in html
        assert "--bg:" in html


# ---------------------------------------------------------------------------
# Sections
# ---------------------------------------------------------------------------


class TestHtmlSections:
    """HTML report should contain expected sections."""

    def test_executive_summary(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert "Executive Summary" in html

    def test_findings_section(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [Finding("a.cpp", 10, Severity.HIGH, "mem", 1, "Issue")]
        html = generate_html_report(_make_results(findings), cfg)
        assert "Findings" in html
        assert "a.cpp" in html

    def test_tier_breakdown(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert "Tier Breakdown" in html

    def test_no_findings_shows_message(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results([]), cfg)
        assert "No findings" in html


# ---------------------------------------------------------------------------
# Severity badges
# ---------------------------------------------------------------------------


class TestHtmlSeverityBadges:
    """HTML should contain severity badges matching finding counts."""

    def test_critical_badge(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [Finding("a.cpp", 1, Severity.CRITICAL, "cat", 1, "Crit")]
        html = generate_html_report(_make_results(findings), cfg)
        assert "badge-critical" in html
        assert "CRITICAL" in html

    def test_multiple_severities(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [
            Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "High"),
            Finding("b.cpp", 2, Severity.LOW, "cat", 2, "Low"),
        ]
        html = generate_html_report(_make_results(findings), cfg)
        assert "badge-high" in html
        assert "badge-low" in html


# ---------------------------------------------------------------------------
# Findings table
# ---------------------------------------------------------------------------


class TestHtmlFindingsTable:
    """The findings table should be sortable and contain all findings."""

    def test_table_has_headers(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "Issue")]
        html = generate_html_report(_make_results(findings), cfg)
        assert "<table>" in html
        assert "Severity" in html
        assert "File" in html
        assert "Category" in html

    def test_table_has_sortable_headers(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "Issue")]
        html = generate_html_report(_make_results(findings), cfg)
        assert 'data-sort="0"' in html

    def test_all_findings_in_table(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [
            Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "Issue A"),
            Finding("b.cpp", 2, Severity.LOW, "cat", 2, "Issue B"),
            Finding("c.cpp", 3, Severity.INFO, "cat", 2, "Issue C"),
        ]
        html = generate_html_report(_make_results(findings), cfg)
        assert "Issue A" in html
        assert "Issue B" in html
        assert "Issue C" in html


# ---------------------------------------------------------------------------
# Trend section
# ---------------------------------------------------------------------------


class TestHtmlTrendSection:
    """Trend section should appear when trend_report is provided."""

    def test_no_trend_no_section(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg, trend_report=None)
        assert "Finding Trends" not in html

    def test_trend_section_present(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        trend = TrendReport(
            direction="improving",
            categories={"memory": "improving", "style": "stable"},
            finding_delta=-3,
        )
        html = generate_html_report(_make_results(), cfg, trend_report=trend)
        assert "Finding Trends" in html
        assert "improving" in html

    def test_trend_svg_chart(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        trend = TrendReport(
            direction="worsening",
            categories={"memory": "worsening", "perf": "improving"},
            finding_delta=5,
        )
        html = generate_html_report(_make_results(), cfg, trend_report=trend)
        assert "<svg" in html

    def test_html_escaping(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        findings = [Finding("a.cpp", 1, Severity.HIGH, "cat", 1, 'Title with <script>alert("xss")</script>')]
        html = generate_html_report(_make_results(findings), cfg)
        assert "<script>alert" not in html
        assert "&lt;script&gt;" in html


# ---------------------------------------------------------------------------
# JavaScript inclusion
# ---------------------------------------------------------------------------


class TestHtmlJavaScript:
    """HTML should include inline JavaScript for table sorting."""

    def test_contains_script(self, tmp_path: Path):
        cfg = _make_config(tmp_path)
        html = generate_html_report(_make_results(), cfg)
        assert "<script>" in html
        assert "querySelectorAll" in html
