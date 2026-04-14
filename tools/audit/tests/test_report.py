"""Tests for lib.report — ReportBuilder.build() output format and constraints."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.findings import (
    AuditData,
    ChangeSummary,
    Finding,
    ResearchResult,
    Severity,
)
from lib.report import ReportBuilder
from lib.trends import BaselineComparison
from lib.utils import estimate_tokens


def _make_config(tmp_path: Path, **overrides) -> Config:
    """Helper to create a Config pointing at tmp_path."""
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "ReportTestProject",
            "root": str(tmp_path),
        },
        "report": {
            "output_path": "docs/AUTOMATED_AUDIT_REPORT.md",
            "include_json_blocks": True,
            "include_token_estimate": True,
        },
    })
    raw = _deep_merge(raw, overrides)
    return Config(raw=raw, root=tmp_path)


def _baseline_block(report: str) -> str:
    """Extract just the '## Baseline Comparison' section from a report.

    Used to scope assertions so they don't accidentally match strings
    elsewhere in the report (e.g. the Tier 1 section legitimately
    contains a `**Tests:**` line for the current run).
    """
    start = report.index("## Baseline Comparison")
    # Next H2 (or end of report) marks the section boundary.
    end = report.find("\n## ", start + 1)
    return report[start:end] if end != -1 else report[start:]


def _build_report(
    tmp_path: Path,
    findings: list[Finding] | None = None,
    config_overrides: dict | None = None,
    baseline_comparison: BaselineComparison | None = None,
) -> str:
    """Convenience wrapper to build a report and return the text."""
    cfg = _make_config(tmp_path, **(config_overrides or {}))
    builder = ReportBuilder(cfg)
    return builder.build(
        findings=findings or [],
        tier1_summary={"build": {"build_ok": True, "warnings": 0, "errors": 0},
                       "tests": {"passed": 10, "failed": 0, "skipped": 1}},
        change_summary=ChangeSummary(
            changed_files=[{"file": "a.cpp", "status": "M", "added": 5, "removed": 2}],
            subsystems_touched=["core"],
            total_added=5,
            total_removed=2,
        ),
        audit_data=AuditData(total_loc=1000, file_count=20),
        research_results=[],
        tiers_run=[1, 2, 3, 4],
        duration=1.5,
        baseline_comparison=baseline_comparison,
    )


# ---------------------------------------------------------------------------
# Report structure
# ---------------------------------------------------------------------------


class TestReportStructure:
    """ReportBuilder.build() should produce valid markdown."""

    def test_starts_with_h1(self, tmp_path: Path):
        report = _build_report(tmp_path)
        assert report.startswith("# ")

    def test_contains_project_name(self, tmp_path: Path):
        report = _build_report(tmp_path)
        assert "ReportTestProject" in report

    def test_contains_tier_headings(self, tmp_path: Path):
        report = _build_report(tmp_path)
        assert "## Tier 1" in report
        assert "## Tier 2" in report
        assert "## Tier 3" in report
        assert "## Tier 4" in report

    def test_tier5_not_present_when_not_run(self, tmp_path: Path):
        """Tier 5 section should not appear if tier 5 was not in tiers_run."""
        report = _build_report(tmp_path)
        assert "## Tier 5" not in report


# ---------------------------------------------------------------------------
# Executive summary JSON block
# ---------------------------------------------------------------------------


class TestExecutiveSummary:
    """The executive summary should contain a JSON block."""

    def test_contains_json_block(self, tmp_path: Path):
        report = _build_report(tmp_path)
        assert "```json" in report

    def test_json_block_is_valid(self, tmp_path: Path):
        report = _build_report(tmp_path)
        # Extract JSON block
        start = report.index("```json") + len("```json")
        end = report.index("```", start)
        json_text = report[start:end].strip()
        data = json.loads(json_text)
        assert "findings" in data
        assert "build" in data
        assert "tests" in data
        assert "duration_seconds" in data

    def test_json_findings_counts(self, tmp_path: Path):
        findings = [
            Finding("a.cpp", 1, Severity.CRITICAL, "cat", 1, "crit"),
            Finding("b.cpp", 2, Severity.HIGH, "cat", 1, "high"),
            Finding("c.cpp", 3, Severity.INFO, "cat", 2, "info"),
        ]
        report = _build_report(tmp_path, findings=findings)
        start = report.index("```json") + len("```json")
        end = report.index("```", start)
        data = json.loads(report[start:end].strip())
        assert data["findings"]["critical"] == 1
        assert data["findings"]["high"] == 1
        assert data["findings"]["info"] == 1
        assert data["findings"]["total"] == 3


# ---------------------------------------------------------------------------
# Token estimate footer
# ---------------------------------------------------------------------------


class TestTokenEstimate:
    """The report footer should include an estimated token count."""

    def test_token_estimate_present(self, tmp_path: Path):
        report = _build_report(tmp_path)
        assert "Estimated tokens:" in report

    def test_token_estimate_disabled(self, tmp_path: Path):
        report = _build_report(
            tmp_path,
            config_overrides={
                "report": {
                    "include_token_estimate": False,
                    "output_path": "docs/AUTOMATED_AUDIT_REPORT.md",
                },
            },
        )
        assert "Estimated tokens:" not in report


# ---------------------------------------------------------------------------
# Report size constraint
# ---------------------------------------------------------------------------


class TestReportSize:
    """A report with 100 synthetic findings should stay under 5000 tokens."""

    def test_100_findings_under_5000_tokens(self, tmp_path: Path):
        findings = []
        for i in range(100):
            sev = [Severity.HIGH, Severity.MEDIUM, Severity.LOW, Severity.INFO][i % 4]
            findings.append(Finding(
                file=f"src/module_{i % 10}.cpp",
                line=i + 1,
                severity=sev,
                category="pattern_scan",
                source_tier=2,
                title=f"Finding {i}: some issue description",
                detail=f"Detail line content for finding number {i}",
                pattern_name=f"pat_{i % 5}",
            ))

        report = _build_report(tmp_path, findings=findings)
        tokens = estimate_tokens(report)
        assert tokens < 5000, (
            f"Report with 100 findings is ~{tokens} tokens, exceeds 5000 limit"
        )


# ---------------------------------------------------------------------------
# Report file writing
# ---------------------------------------------------------------------------


class TestReportFileWriting:
    """build() should write the report to disk."""

    def test_writes_to_configured_path(self, tmp_path: Path):
        _build_report(tmp_path)
        expected = tmp_path / "docs" / "AUTOMATED_AUDIT_REPORT.md"
        assert expected.exists()
        content = expected.read_text()
        assert content.startswith("# ")

    def test_writes_timestamped_copy(self, tmp_path: Path):
        _build_report(tmp_path)
        docs = tmp_path / "docs"
        md_files = list(docs.glob("AUTOMATED_AUDIT_REPORT_*.md"))
        assert len(md_files) >= 1


# ---------------------------------------------------------------------------
# D5 — Baseline Comparison section
# ---------------------------------------------------------------------------


class TestBaselineSection:
    """The Baseline Comparison section is rendered when comparison provided."""

    def test_section_absent_when_no_comparison(self, tmp_path: Path):
        report = _build_report(tmp_path, baseline_comparison=None)
        assert "## Baseline Comparison" not in report

    def test_section_present_when_comparison_provided(self, tmp_path: Path):
        comp = BaselineComparison(
            previous_timestamp="2026-04-13T10:00:00",
            current_timestamp="2026-04-14T10:00:00",
            finding_delta=2,
            severity_deltas={"high": 2},
            previous_captured_baseline=True,
            build_status_change="OK",
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        assert "## Baseline Comparison" in report
        assert "Compared against:** previous run at `2026-04-13T10:00:00`" in report
        assert "**Findings:** +2 total" in report
        assert "high +2" in report

    def test_build_status_transition_rendered(self, tmp_path: Path):
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=0,
            build_status_change="OK→FAILED",
            previous_captured_baseline=True,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        assert "**Build:** OK→FAILED" in report

    def test_test_delta_rendered(self, tmp_path: Path):
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=0,
            test_passed_delta=-3,
            test_failed_delta=2,
            previous_captured_baseline=True,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        assert "**Tests:** passed -3, failed +2" in report

    def test_loc_and_file_deltas_rendered(self, tmp_path: Path):
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=0,
            loc_delta=250,
            file_count_delta=-2,
            previous_captured_baseline=True,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        assert "**LOC:** +250" in report
        assert "**Files:** -2" in report

    def test_unchanged_metrics_omit_lines(self, tmp_path: Path):
        """No-op deltas must not emit visual noise.

        Note: assertions are scoped to the Baseline Comparison section
        only — the Tier 1 section legitimately includes a `**Tests:**`
        line for the current run.
        """
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=0,
            previous_captured_baseline=True,
            build_status_change="OK",
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        section = _baseline_block(report)
        assert "Tests:**" not in section  # 0/0 deltas suppressed
        assert "LOC:**" not in section
        assert "Files:**" not in section

    def test_legacy_previous_suppresses_test_loc_lines(self, tmp_path: Path):
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=4,
            severity_deltas={"high": 4},
            build_status_change="OK",
            test_passed_delta=100,  # noise from legacy zero-baseline
            loc_delta=10000,
            previous_captured_baseline=False,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        section = _baseline_block(report)
        # Test/LOC lines should be suppressed when baseline wasn't captured
        # (otherwise we'd report misleading 100-test "improvements").
        assert "Tests:**" not in section
        assert "LOC:**" not in section
        # But the build label still surfaces with the caveat.
        assert "no prior baseline" in section

    def test_severity_delta_ordering(self, tmp_path: Path):
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=0,
            severity_deltas={"low": -2, "critical": 1, "high": 3},
            previous_captured_baseline=True,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        # Severity order must follow the Severity enum: critical, high, low.
        idx_crit = report.index("critical +1")
        idx_high = report.index("high +3")
        idx_low = report.index("low -2")
        assert idx_crit < idx_high < idx_low

    def test_top_movers_capped_at_5(self, tmp_path: Path):
        """Long category lists should not blow the report up."""
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=0,
            category_deltas={f"cat_{i}": (i + 1) for i in range(20)},
            previous_captured_baseline=True,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        # The biggest mover (cat_19, +20) should be present; cat_0 (+1) must not.
        assert "cat_19 +20" in report
        assert "cat_0 +1" not in report

    def test_section_under_token_budget(self, tmp_path: Path):
        """The baseline section must stay compact (~100 tokens or so)."""
        comp = BaselineComparison(
            previous_timestamp="2026-04-13T10:00:00",
            current_timestamp="2026-04-14T10:00:00",
            finding_delta=10,
            severity_deltas={"critical": 1, "high": 3, "medium": 2, "low": 4, "info": 0},
            category_deltas={f"cat_{i}": i for i in range(20)},
            build_status_change="OK→FAILED",
            test_passed_delta=-5,
            test_failed_delta=3,
            loc_delta=500,
            file_count_delta=2,
            duration_delta=10.5,
            previous_captured_baseline=True,
        )
        report = _build_report(tmp_path, baseline_comparison=comp)
        # Whole report still under the 5K ceiling that test_report enforces.
        assert estimate_tokens(report) < 5000
