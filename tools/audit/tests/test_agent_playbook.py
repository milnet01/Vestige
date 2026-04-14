"""Tests for lib.agent_playbook — 5-phase prompt renderer."""

from __future__ import annotations

import pytest

from lib import agent_playbook
from lib.findings import Finding, Severity
from lib.utils import estimate_tokens


# ---------------------------------------------------------------------------
# threshold_from_string
# ---------------------------------------------------------------------------


class TestThresholdFromString:
    """Config string → Severity mapping for the approval gate."""

    @pytest.mark.parametrize(
        "value,expected",
        [
            ("critical", Severity.CRITICAL),
            ("high", Severity.HIGH),
            ("medium", Severity.MEDIUM),
            ("low", Severity.LOW),
            ("info", Severity.INFO),
            ("CRITICAL", Severity.CRITICAL),  # case-insensitive
            ("High", Severity.HIGH),
        ],
    )
    def test_known_values(self, value: str, expected: Severity):
        assert agent_playbook.threshold_from_string(value) is expected

    def test_unknown_value_falls_through_to_info(self):
        """Severity.from_string returns INFO on unknowns — renderer must not raise."""
        result = agent_playbook.threshold_from_string("nonsense")
        # INFO is the documented fallback; the playbook just renders "any
        # severity", which is still valid (most permissive gate).
        assert result is Severity.INFO


# ---------------------------------------------------------------------------
# build() — happy path
# ---------------------------------------------------------------------------


class TestBuildHappyPath:
    """Clean baseline, default threshold."""

    def _clean_summary(self) -> dict:
        return {
            "build": {"build_ok": True, "warnings": 0, "errors": 0},
            "tests": {"passed": 100, "failed": 0, "skipped": 0},
        }

    def test_renders_all_five_phases(self):
        text = agent_playbook.build([], self._clean_summary())
        assert "Phase 1 — Baseline" in text
        assert "Phase 2 — Verify" in text
        assert "Phase 3 — Cite sources" in text
        assert "Phase 4 — Approval gate" in text
        assert "Phase 5 — Implement + test" in text

    def test_renders_deliverable_block(self):
        text = agent_playbook.build([], self._clean_summary())
        assert "**Deliverable.**" in text
        assert "VERIFIED" in text
        assert "UNCONFIRMED" in text
        assert "FALSE-POSITIVE" in text

    def test_default_threshold_is_medium(self):
        text = agent_playbook.build([], self._clean_summary())
        assert "MEDIUM or higher" in text

    def test_clean_baseline_omits_warning_block(self):
        text = agent_playbook.build([], self._clean_summary())
        assert "Baseline is already broken" not in text


# ---------------------------------------------------------------------------
# build() — threshold phrasing
# ---------------------------------------------------------------------------


class TestBuildThresholdPhrasing:
    """The approval-gate sentence adapts to the configured threshold."""

    def _clean_summary(self) -> dict:
        return {
            "build": {"build_ok": True, "warnings": 0, "errors": 0},
            "tests": {"passed": 1, "failed": 0, "skipped": 0},
        }

    @pytest.mark.parametrize(
        "threshold,phrase",
        [
            (Severity.CRITICAL, "CRITICAL"),
            (Severity.HIGH, "HIGH or CRITICAL"),
            (Severity.MEDIUM, "MEDIUM or higher"),
            (Severity.LOW, "LOW or higher"),
            (Severity.INFO, "any severity"),
        ],
    )
    def test_phrasing_per_threshold(self, threshold: Severity, phrase: str):
        text = agent_playbook.build([], self._clean_summary(), threshold)
        # Phrase should appear in the approval-gate paragraph.
        assert phrase in text


# ---------------------------------------------------------------------------
# build() — baseline-broken warning
# ---------------------------------------------------------------------------


class TestBuildBaselineBroken:
    """When build is broken or tests fail, the playbook surfaces a warning."""

    def test_build_failed_surfaces_warning(self):
        summary = {
            "build": {"build_ok": False, "warnings": 0, "errors": 1},
            "tests": {"passed": 0, "failed": 0, "skipped": 0},
        }
        text = agent_playbook.build([], summary)
        assert "Baseline is already broken" in text
        assert "Build: FAILED" in text

    def test_tests_failed_surfaces_warning(self):
        summary = {
            "build": {"build_ok": True, "warnings": 0, "errors": 0},
            "tests": {"passed": 99, "failed": 3, "skipped": 0},
        }
        text = agent_playbook.build([], summary)
        assert "Baseline is already broken" in text
        assert "tests failed: 3" in text

    def test_both_failures_still_single_warning_block(self):
        summary = {
            "build": {"build_ok": False, "warnings": 0, "errors": 2},
            "tests": {"passed": 0, "failed": 5, "skipped": 0},
        }
        text = agent_playbook.build([], summary)
        # The warning block appears exactly once.
        assert text.count("Baseline is already broken") == 1
        assert "Build: FAILED" in text
        assert "tests failed: 5" in text


# ---------------------------------------------------------------------------
# build() — robustness
# ---------------------------------------------------------------------------


class TestBuildRobustness:
    """The renderer must never raise on degenerate inputs."""

    def test_empty_tier1_summary(self):
        # No build/tests keys at all — defaults should kick in.
        text = agent_playbook.build([], {})
        assert "Phase 1 — Baseline" in text
        # Missing build.build_ok defaults to True (clean) — no warning.
        assert "Baseline is already broken" not in text

    def test_none_build_and_tests(self):
        summary = {"build": None, "tests": None}
        text = agent_playbook.build([], summary)
        assert "Phase 1 — Baseline" in text

    def test_findings_list_is_ignored_for_content(self):
        """Findings are referenced by the Executive Summary, not inlined.

        This is deliberate — we don't duplicate counts the summary already has.
        The findings parameter is still accepted for API future-compatibility
        (later phases may want to highlight the top N verified-HIGH items).
        """
        summary = {
            "build": {"build_ok": True},
            "tests": {"passed": 1, "failed": 0, "skipped": 0},
        }
        a_finding = Finding(
            file="x.cpp",
            line=1,
            severity=Severity.HIGH,
            category="test",
            source_tier=1,
            title="test",
        )
        empty = agent_playbook.build([], summary)
        one = agent_playbook.build([a_finding], summary)
        assert empty == one


# ---------------------------------------------------------------------------
# Token budget
# ---------------------------------------------------------------------------


class TestBuildTokenBudget:
    """The playbook is shipped in every report; size must stay bounded."""

    def test_token_estimate_under_ceiling(self):
        summary = {
            "build": {"build_ok": True, "warnings": 0},
            "tests": {"passed": 1, "failed": 0, "skipped": 0},
        }
        text = agent_playbook.build([], summary)
        tokens = estimate_tokens(text)
        # Budget: 800 tokens. The playbook is inserted into every report;
        # if this assertion fails, either trim the prose or split the
        # block behind a flag before accepting a larger footprint.
        assert tokens < 800, (
            f"Agent Playbook grew to ~{tokens} tokens — exceeds 800 ceiling. "
            "Trim or gate before merging."
        )

    def test_baseline_warning_stays_within_budget(self):
        """The warning branch adds ~40 tokens; still well under the ceiling."""
        summary = {
            "build": {"build_ok": False, "warnings": 0, "errors": 2},
            "tests": {"passed": 0, "failed": 7, "skipped": 0},
        }
        text = agent_playbook.build([], summary)
        assert estimate_tokens(text) < 800
