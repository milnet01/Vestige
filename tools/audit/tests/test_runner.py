"""Tests for lib.runner — AuditRunner orchestration, progress events, cancellation."""

from __future__ import annotations

import threading
from pathlib import Path
from typing import Any
from unittest.mock import MagicMock, patch

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.findings import Finding, Severity, AuditData, ChangeSummary
from lib.runner import AuditRunner


def _make_config(tmp_path: Path, tiers: list[int] | None = None) -> Config:
    """Create a Config with specific tiers enabled."""
    src = tmp_path / "src"
    src.mkdir(exist_ok=True)
    (src / "main.cpp").write_text("int main() { return 0; }\n")

    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "RunnerTestProject",
            "root": str(tmp_path),
            "source_dirs": ["src/"],
            "exclude_dirs": [],
        },
        "tiers": tiers if tiers is not None else [1, 2, 3, 4, 5],
        "patterns": {},
        "report": {
            "output_path": "docs/REPORT.md",
            "include_json_blocks": True,
            "include_token_estimate": True,
        },
        "static_analysis": {
            "cppcheck": {"enabled": False},
            "clang_tidy": {"enabled": False},
        },
        "research": {"enabled": False},
    })
    return Config(raw=raw, root=tmp_path)


# ---------------------------------------------------------------------------
# Tier selection
# ---------------------------------------------------------------------------


class TestTierSelection:
    """AuditRunner should only run the tiers listed in config."""

    def test_only_tier2_runs(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[2])
        runner = AuditRunner(cfg)

        with patch("lib.tier2_patterns.run", return_value=[]) as mock_t2:
            results = runner.run()

        mock_t2.assert_called_once()
        assert 2 in results.tiers_run
        assert 1 not in results.tiers_run

    def test_empty_tiers_runs_nothing(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[])
        runner = AuditRunner(cfg)
        results = runner.run()
        assert results.tiers_run == []

    def test_tier1_only(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[1])
        runner = AuditRunner(cfg)

        with patch("lib.tier1_build.run", return_value=([], {})), \
             patch("lib.tier1_cppcheck.run", return_value=[]), \
             patch("lib.tier1_clangtidy.run", return_value=[]):
            results = runner.run()

        assert results.tiers_run == [1]


# ---------------------------------------------------------------------------
# Progress callback
# ---------------------------------------------------------------------------


class TestProgressCallback:
    """progress_callback should receive tier_start and tier_end events."""

    def test_receives_tier_events(self, tmp_path: Path):
        events: list[tuple[str, dict]] = []
        def callback(event: str, data: dict) -> None:
            events.append((event, data))

        cfg = _make_config(tmp_path, tiers=[2])
        runner = AuditRunner(cfg, progress_callback=callback)

        with patch("lib.tier2_patterns.run", return_value=[]):
            runner.run()

        event_types = [e[0] for e in events]
        assert "audit_start" in event_types
        assert "tier_start" in event_types
        assert "tier_end" in event_types
        assert "audit_end" in event_types

    def test_tier_start_contains_tier_number(self, tmp_path: Path):
        events: list[tuple[str, dict]] = []
        def callback(event: str, data: dict) -> None:
            events.append((event, data))

        cfg = _make_config(tmp_path, tiers=[2])
        runner = AuditRunner(cfg, progress_callback=callback)

        with patch("lib.tier2_patterns.run", return_value=[]):
            runner.run()

        tier_starts = [(e, d) for e, d in events if e == "tier_start"]
        assert len(tier_starts) >= 1
        assert tier_starts[0][1]["tier"] == 2

    def test_no_callback_does_not_raise(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[2])
        runner = AuditRunner(cfg, progress_callback=None)

        with patch("lib.tier2_patterns.run", return_value=[]):
            results = runner.run()

        assert 2 in results.tiers_run


# ---------------------------------------------------------------------------
# Cancel event
# ---------------------------------------------------------------------------


class TestCancelEvent:
    """cancel_event should stop execution between tiers."""

    def test_cancel_before_first_tier(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[1, 2, 3])
        runner = AuditRunner(cfg)

        cancel = threading.Event()
        cancel.set()  # Already cancelled

        results = runner.run(cancel_event=cancel)
        assert len(results.tiers_run) == 0

    def test_cancel_emits_cancelled_event(self, tmp_path: Path):
        events: list[tuple[str, dict]] = []
        def callback(event: str, data: dict) -> None:
            events.append((event, data))

        cfg = _make_config(tmp_path, tiers=[1, 2])
        runner = AuditRunner(cfg, progress_callback=callback)

        cancel = threading.Event()
        cancel.set()

        runner.run(cancel_event=cancel)
        event_types = [e[0] for e in events]
        assert "cancelled" in event_types


# ---------------------------------------------------------------------------
# AuditResults
# ---------------------------------------------------------------------------


class TestAuditResults:
    """AuditResults accumulates findings from all tiers."""

    def test_findings_accumulated(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[2])
        runner = AuditRunner(cfg)

        fake_findings = [
            Finding("a.cpp", 1, Severity.HIGH, "test", 2, "issue1"),
            Finding("b.cpp", 2, Severity.LOW, "test", 2, "issue2"),
        ]

        with patch("lib.tier2_patterns.run", return_value=fake_findings):
            results = runner.run()

        assert len(results.findings) == 2

    def test_to_dict_has_required_keys(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[])
        runner = AuditRunner(cfg)
        results = runner.run()

        d = results.to_dict()
        assert "findings" in d
        assert "tier1_summary" in d
        assert "tiers_run" in d
        assert "duration" in d

    def test_duration_is_positive(self, tmp_path: Path):
        cfg = _make_config(tmp_path, tiers=[2])
        runner = AuditRunner(cfg)

        with patch("lib.tier2_patterns.run", return_value=[]):
            results = runner.run()

        assert results.duration > 0
