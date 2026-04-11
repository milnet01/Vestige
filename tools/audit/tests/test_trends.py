"""Tests for lib.trends — TrendSnapshot, save/load, compute_trends."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path

import pytest

from lib.findings import Finding, Severity
from lib.trends import (
    TrendReport,
    TrendSnapshot,
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
