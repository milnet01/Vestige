"""Tests for lib.trends — TrendSnapshot, save/load, compute_trends."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path

import pytest

from lib.findings import AuditData, Finding, Severity
from lib.trends import (
    BaselineComparison,
    TrendReport,
    TrendSnapshot,
    compute_baseline_comparison,
    compute_trends,
    load_snapshots,
    save_snapshot,
)


# ---------------------------------------------------------------------------
# TrendSnapshot serialization
# ---------------------------------------------------------------------------


class TestTrendSnapshotSerialization:
    """TrendSnapshot should serialize and deserialize correctly."""

    def test_to_dict(self):
        snap = TrendSnapshot(
            timestamp="2026-04-11T10:00:00",
            finding_count=42,
            by_severity={"high": 5, "medium": 10, "low": 27},
            by_category={"memory": 3, "style": 39},
        )
        d = snap.to_dict()
        assert d["timestamp"] == "2026-04-11T10:00:00"
        assert d["finding_count"] == 42
        assert d["by_severity"]["high"] == 5
        assert d["by_category"]["memory"] == 3

    def test_from_dict(self):
        data = {
            "timestamp": "2026-04-11T10:00:00",
            "finding_count": 10,
            "by_severity": {"high": 2},
            "by_category": {"cat1": 10},
        }
        snap = TrendSnapshot.from_dict(data)
        assert snap.timestamp == "2026-04-11T10:00:00"
        assert snap.finding_count == 10
        assert snap.by_severity == {"high": 2}

    def test_roundtrip(self):
        original = TrendSnapshot(
            timestamp="2026-04-11T12:00:00",
            finding_count=5,
            by_severity={"critical": 1, "info": 4},
            by_category={"security": 1, "style": 4},
        )
        d = original.to_dict()
        restored = TrendSnapshot.from_dict(d)
        assert restored.timestamp == original.timestamp
        assert restored.finding_count == original.finding_count
        assert restored.by_severity == original.by_severity
        assert restored.by_category == original.by_category

    def test_from_dict_missing_fields(self):
        snap = TrendSnapshot.from_dict({})
        assert snap.timestamp == ""
        assert snap.finding_count == 0
        assert snap.by_severity == {}
        assert snap.by_category == {}

    def test_empty_snapshot(self):
        snap = TrendSnapshot()
        d = snap.to_dict()
        assert d["finding_count"] == 0
        assert d["by_severity"] == {}


# ---------------------------------------------------------------------------
# save_snapshot
# ---------------------------------------------------------------------------


class TestSaveSnapshot:
    """save_snapshot should write a JSON file to disk."""

    def test_saves_file(self, tmp_path: Path):
        findings = [
            Finding("a.cpp", 1, Severity.HIGH, "memory", 1, "issue1"),
            Finding("b.cpp", 2, Severity.LOW, "style", 2, "issue2"),
        ]
        snap = save_snapshot(tmp_path, findings)
        assert snap.finding_count == 2
        assert snap.by_severity["high"] == 1
        assert snap.by_severity["low"] == 1

        # Check file on disk
        files = list(tmp_path.glob("trend_snapshot_*.json"))
        assert len(files) == 1
        data = json.loads(files[0].read_text())
        assert data["finding_count"] == 2

    def test_creates_directory(self, tmp_path: Path):
        subdir = tmp_path / "deep" / "nested"
        save_snapshot(subdir, [])
        assert subdir.exists()

    def test_empty_findings(self, tmp_path: Path):
        snap = save_snapshot(tmp_path, [])
        assert snap.finding_count == 0

    def test_timestamp_is_iso(self, tmp_path: Path):
        snap = save_snapshot(tmp_path, [])
        # Should be parseable as ISO format
        datetime.fromisoformat(snap.timestamp)

    def test_categories_counted(self, tmp_path: Path):
        findings = [
            Finding("a.cpp", 1, Severity.HIGH, "memory", 1, "issue1"),
            Finding("b.cpp", 2, Severity.HIGH, "memory", 1, "issue2"),
            Finding("c.cpp", 3, Severity.LOW, "perf", 2, "issue3"),
        ]
        snap = save_snapshot(tmp_path, findings)
        assert snap.by_category["memory"] == 2
        assert snap.by_category["perf"] == 1


# ---------------------------------------------------------------------------
# load_snapshots
# ---------------------------------------------------------------------------


class TestLoadSnapshots:
    """load_snapshots should read and parse snapshot files."""

    def test_loads_single_snapshot(self, tmp_path: Path):
        snap = TrendSnapshot(timestamp="2026-04-11T10:00:00", finding_count=5)
        (tmp_path / "trend_snapshot_20260411_100000.json").write_text(
            json.dumps(snap.to_dict())
        )
        result = load_snapshots(tmp_path)
        assert len(result) == 1
        assert result[0].finding_count == 5

    def test_loads_multiple_sorted_by_name(self, tmp_path: Path):
        for i in range(5):
            snap = TrendSnapshot(timestamp=f"2026-04-1{i}T10:00:00", finding_count=i * 10)
            (tmp_path / f"trend_snapshot_2026041{i}_100000.json").write_text(
                json.dumps(snap.to_dict())
            )
        result = load_snapshots(tmp_path)
        assert len(result) == 5
        # Should be newest first (descending by name)
        assert result[0].finding_count == 40

    def test_respects_max_count(self, tmp_path: Path):
        for i in range(10):
            snap = TrendSnapshot(timestamp=f"2026-04-{i+1:02d}T10:00:00", finding_count=i)
            (tmp_path / f"trend_snapshot_202604{i+1:02d}_100000.json").write_text(
                json.dumps(snap.to_dict())
            )
        result = load_snapshots(tmp_path, max_count=3)
        assert len(result) == 3

    def test_nonexistent_dir_returns_empty(self, tmp_path: Path):
        result = load_snapshots(tmp_path / "nonexistent")
        assert result == []

    def test_skips_corrupt_files(self, tmp_path: Path):
        (tmp_path / "trend_snapshot_20260411_100000.json").write_text("{invalid json")
        snap = TrendSnapshot(timestamp="2026-04-11T12:00:00", finding_count=3)
        (tmp_path / "trend_snapshot_20260411_120000.json").write_text(
            json.dumps(snap.to_dict())
        )
        result = load_snapshots(tmp_path)
        assert len(result) == 1
        assert result[0].finding_count == 3


# ---------------------------------------------------------------------------
# compute_trends
# ---------------------------------------------------------------------------


class TestComputeTrends:
    """compute_trends should classify findings as improving/worsening/stable."""

    def test_fewer_snapshots_returns_stable(self):
        report = compute_trends([TrendSnapshot(finding_count=10)])
        assert report.direction == "stable"
        assert report.finding_delta == 0

    def test_empty_snapshots_returns_stable(self):
        report = compute_trends([])
        assert report.direction == "stable"

    def test_improving_trend(self):
        snapshots = [
            TrendSnapshot(finding_count=5, by_category={"memory": 2, "style": 3}),
            TrendSnapshot(finding_count=10, by_category={"memory": 5, "style": 5}),
        ]
        report = compute_trends(snapshots)
        assert report.direction == "improving"
        assert report.finding_delta == -5

    def test_worsening_trend(self):
        snapshots = [
            TrendSnapshot(finding_count=20, by_category={"memory": 10}),
            TrendSnapshot(finding_count=5, by_category={"memory": 3}),
        ]
        report = compute_trends(snapshots)
        assert report.direction == "worsening"
        assert report.finding_delta == 15

    def test_stable_trend(self):
        snapshots = [
            TrendSnapshot(finding_count=10, by_category={"memory": 5}),
            TrendSnapshot(finding_count=10, by_category={"memory": 5}),
        ]
        report = compute_trends(snapshots)
        assert report.direction == "stable"
        assert report.finding_delta == 0

    def test_per_category_trends(self):
        snapshots = [
            TrendSnapshot(finding_count=10, by_category={"memory": 2, "style": 8}),
            TrendSnapshot(finding_count=10, by_category={"memory": 5, "style": 5}),
        ]
        report = compute_trends(snapshots)
        assert report.categories["memory"] == "improving"
        assert report.categories["style"] == "worsening"

    def test_new_category_detected_as_worsening(self):
        snapshots = [
            TrendSnapshot(finding_count=5, by_category={"new_cat": 5}),
            TrendSnapshot(finding_count=0, by_category={}),
        ]
        report = compute_trends(snapshots)
        assert report.categories["new_cat"] == "worsening"

    def test_removed_category_detected_as_improving(self):
        snapshots = [
            TrendSnapshot(finding_count=0, by_category={}),
            TrendSnapshot(finding_count=5, by_category={"old_cat": 5}),
        ]
        report = compute_trends(snapshots)
        assert report.categories["old_cat"] == "improving"


class TestTrendReportToDict:
    """TrendReport.to_dict should contain all expected fields."""

    def test_to_dict(self):
        report = TrendReport(
            direction="improving",
            categories={"memory": "improving", "style": "stable"},
            finding_delta=-5,
        )
        d = report.to_dict()
        assert d["direction"] == "improving"
        assert d["finding_delta"] == -5
        assert d["categories"]["memory"] == "improving"


# ---------------------------------------------------------------------------
# D5 — Baseline metadata round-trip + compute_baseline_comparison
# ---------------------------------------------------------------------------


class TestTrendSnapshotBaselineFields:
    """Baseline metadata added in 2.2.0 must round-trip through JSON."""

    def test_baseline_fields_default_to_neutral(self):
        snap = TrendSnapshot()
        # build_ok=None signals "not captured" (legacy snapshot)
        assert snap.build_ok is None
        assert snap.tests_passed == 0
        assert snap.total_loc == 0
        assert snap.duration_seconds == 0.0

    def test_to_dict_includes_baseline(self):
        snap = TrendSnapshot(
            build_ok=True,
            build_warnings=3,
            tests_passed=100,
            tests_failed=2,
            total_loc=12345,
            duration_seconds=42.7,
        )
        d = snap.to_dict()
        assert d["build_ok"] is True
        assert d["build_warnings"] == 3
        assert d["tests_passed"] == 100
        assert d["tests_failed"] == 2
        assert d["total_loc"] == 12345
        assert d["duration_seconds"] == 42.7

    def test_from_dict_legacy_snapshot_defaults(self):
        """Pre-2.2.0 snapshots only had finding_count + by_severity/category."""
        data = {
            "timestamp": "2026-04-10T08:00:00",
            "finding_count": 5,
            "by_severity": {"high": 5},
            "by_category": {"memory": 5},
        }
        snap = TrendSnapshot.from_dict(data)
        # No baseline → build_ok must be None so the comparator can
        # tell legacy from "build was OK".
        assert snap.build_ok is None
        assert snap.tests_passed == 0
        assert snap.total_loc == 0

    def test_round_trip_preserves_baseline(self):
        snap = TrendSnapshot(
            timestamp="2026-04-14T10:00:00",
            finding_count=10,
            by_severity={"high": 4, "low": 6},
            by_category={"memory": 4, "style": 6},
            build_ok=False,
            build_errors=2,
            tests_passed=99,
            tests_failed=1,
            total_loc=10000,
            file_count=200,
            duration_seconds=58.3,
        )
        restored = TrendSnapshot.from_dict(snap.to_dict())
        assert restored.build_ok is False
        assert restored.build_errors == 2
        assert restored.tests_passed == 99
        assert restored.tests_failed == 1
        assert restored.total_loc == 10000
        assert restored.file_count == 200
        assert restored.duration_seconds == 58.3


class TestSaveSnapshotBaseline:
    """save_snapshot should capture tier1/audit_data when supplied."""

    def test_captures_baseline_metadata(self, tmp_path: Path):
        tier1 = {
            "build": {"build_ok": True, "warnings": 2, "errors": 0},
            "tests": {"passed": 50, "failed": 1, "skipped": 3},
        }
        ad = AuditData(total_loc=8000, file_count=120)
        snap = save_snapshot(
            tmp_path,
            findings=[],
            tier1_summary=tier1,
            audit_data=ad,
            duration=12.3,
        )
        assert snap.build_ok is True
        assert snap.build_warnings == 2
        assert snap.tests_passed == 50
        assert snap.tests_failed == 1
        assert snap.total_loc == 8000
        assert snap.file_count == 120
        assert snap.duration_seconds == 12.3

    def test_omitted_baseline_args_keep_defaults(self, tmp_path: Path):
        """Legacy callers (no baseline kwargs) must still work."""
        snap = save_snapshot(tmp_path, findings=[])
        assert snap.build_ok is None
        assert snap.total_loc == 0
        assert snap.duration_seconds == 0.0

    def test_missing_build_key_does_not_invent_state(self, tmp_path: Path):
        """tier1_summary may lack 'build' if Tier 1 was skipped."""
        snap = save_snapshot(
            tmp_path,
            findings=[],
            tier1_summary={"tests": {"passed": 5}},
            duration=1.0,
        )
        # No build → build_ok stays None, not False.
        assert snap.build_ok is None
        assert snap.tests_passed == 5


class TestComputeBaselineComparison:
    """compute_baseline_comparison contrasts newest vs second-newest."""

    def test_returns_none_for_single_snapshot(self):
        assert compute_baseline_comparison([TrendSnapshot()]) is None

    def test_returns_none_for_empty(self):
        assert compute_baseline_comparison([]) is None

    def test_finding_delta_basic(self):
        snapshots = [
            TrendSnapshot(timestamp="t2", finding_count=15),
            TrendSnapshot(timestamp="t1", finding_count=10),
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        assert comp.finding_delta == 5
        assert comp.previous_timestamp == "t1"
        assert comp.current_timestamp == "t2"

    def test_severity_deltas_only_emit_nonzero(self):
        snapshots = [
            TrendSnapshot(by_severity={"high": 3, "low": 5, "info": 10}),
            TrendSnapshot(by_severity={"high": 5, "low": 5, "info": 8}),
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        assert comp.severity_deltas == {"high": -2, "info": 2}
        # "low" unchanged → omitted from the dict
        assert "low" not in comp.severity_deltas

    def test_category_deltas_track_movement(self):
        snapshots = [
            TrendSnapshot(by_category={"memory": 2, "perf": 8, "style": 4}),
            TrendSnapshot(by_category={"memory": 5, "perf": 4, "security": 1}),
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        # memory dropped, perf rose, style appeared, security disappeared
        assert comp.category_deltas["memory"] == -3
        assert comp.category_deltas["perf"] == 4
        assert comp.category_deltas["style"] == 4
        assert comp.category_deltas["security"] == -1

    def test_build_status_transition(self):
        snapshots = [
            TrendSnapshot(build_ok=False),
            TrendSnapshot(build_ok=True),
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        assert comp.build_status_change == "OK→FAILED"
        assert comp.previous_captured_baseline is True

    def test_build_status_unchanged_collapses_to_label(self):
        snapshots = [
            TrendSnapshot(build_ok=True),
            TrendSnapshot(build_ok=True),
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        # Single label (no arrow) for stable build state.
        assert comp.build_status_change == "OK"

    def test_legacy_previous_suppresses_baseline_fields(self):
        """Previous snapshot lacks build_ok → flag previous_captured_baseline=False."""
        snapshots = [
            TrendSnapshot(build_ok=True, total_loc=5000, tests_passed=100),
            TrendSnapshot(build_ok=None, total_loc=0, tests_passed=0),  # legacy
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        assert comp.previous_captured_baseline is False
        # Build_status_change still surfaces current state ("OK") so the
        # renderer can present it with a "no prior baseline" caveat.
        assert comp.build_status_change == "OK"

    def test_test_and_loc_deltas(self):
        snapshots = [
            TrendSnapshot(
                tests_passed=110, tests_failed=2,
                total_loc=12000, file_count=180, duration_seconds=45.0,
            ),
            TrendSnapshot(
                tests_passed=100, tests_failed=0,
                total_loc=10000, file_count=170, duration_seconds=40.0,
            ),
        ]
        comp = compute_baseline_comparison(snapshots)
        assert comp is not None
        assert comp.test_passed_delta == 10
        assert comp.test_failed_delta == 2
        assert comp.loc_delta == 2000
        assert comp.file_count_delta == 10
        assert comp.duration_delta == 5.0


class TestBaselineComparisonToDict:
    """BaselineComparison.to_dict is part of the JSON sidecar surface."""

    def test_to_dict_contains_all_fields(self):
        comp = BaselineComparison(
            previous_timestamp="t1",
            current_timestamp="t2",
            finding_delta=3,
            severity_deltas={"high": 1},
            category_deltas={"memory": 1},
            build_status_change="OK",
            test_passed_delta=2,
            test_failed_delta=0,
            loc_delta=100,
            file_count_delta=2,
            duration_delta=1.5,
            previous_captured_baseline=True,
        )
        d = comp.to_dict()
        assert d["finding_delta"] == 3
        assert d["severity_deltas"]["high"] == 1
        assert d["build_status_change"] == "OK"
        assert d["loc_delta"] == 100
        assert d["previous_captured_baseline"] is True
