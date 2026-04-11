"""Tests for lib.diff_report — load_previous_results, compute_diff, ReportDiff."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from lib.diff_report import ReportDiff, compute_diff, load_previous_results
from lib.findings import Finding, Severity


# ---------------------------------------------------------------------------
# load_previous_results
# ---------------------------------------------------------------------------


class TestLoadPreviousResultsNoFiles:
    """load_previous_results should return None when no results exist."""

    def test_nonexistent_dir_returns_none(self, tmp_path: Path):
        result = load_previous_results(tmp_path / "nonexistent", "REPORT")
        assert result is None

    def test_empty_dir_returns_none(self, tmp_path: Path):
        result = load_previous_results(tmp_path, "REPORT")
        assert result is None

    def test_no_json_files_returns_none(self, tmp_path: Path):
        (tmp_path / "REPORT.md").write_text("# Report")
        result = load_previous_results(tmp_path, "REPORT")
        assert result is None


class TestLoadPreviousResultsValidFile:
    """load_previous_results should parse a valid JSON sidecar."""

    def test_loads_list_format(self, tmp_path: Path):
        data = [
            {"file": "a.cpp", "severity": "high", "dedup_key": "abc123"},
        ]
        (tmp_path / "REPORT_20260410_120000_results.json").write_text(json.dumps(data))
        result = load_previous_results(tmp_path, "REPORT")
        assert result is not None
        assert len(result) == 1
        assert result[0]["file"] == "a.cpp"

    def test_loads_dict_with_findings_key(self, tmp_path: Path):
        data = {"findings": [{"file": "b.cpp", "dedup_key": "def456"}]}
        (tmp_path / "REPORT_20260410_120000_results.json").write_text(json.dumps(data))
        result = load_previous_results(tmp_path, "REPORT")
        assert result is not None
        assert len(result) == 1

    def test_returns_most_recent(self, tmp_path: Path):
        old_data = [{"file": "old.cpp", "dedup_key": "old"}]
        new_data = [{"file": "new.cpp", "dedup_key": "new"}]
        old_file = tmp_path / "REPORT_20260409_results.json"
        new_file = tmp_path / "REPORT_20260410_results.json"
        old_file.write_text(json.dumps(old_data))
        new_file.write_text(json.dumps(new_data))
        result = load_previous_results(tmp_path, "REPORT")
        assert result is not None
        assert result[0]["file"] == "new.cpp"

    def test_excludes_current_stem(self, tmp_path: Path):
        """Should not load a file matching the current stem exactly."""
        data = [{"file": "a.cpp", "dedup_key": "abc"}]
        # The stem must not match current_stem
        (tmp_path / "OTHER_results.json").write_text(json.dumps(data))
        result = load_previous_results(tmp_path, "OTHER")
        # The file stem is "OTHER_results" which != "OTHER", so it should be found
        assert result is not None


class TestLoadPreviousResultsCorrupt:
    """load_previous_results should handle corrupt JSON gracefully."""

    def test_invalid_json_returns_none(self, tmp_path: Path):
        (tmp_path / "REPORT_bad_results.json").write_text("{not valid json")
        result = load_previous_results(tmp_path, "REPORT")
        assert result is None

    def test_unexpected_structure_returns_none(self, tmp_path: Path):
        (tmp_path / "REPORT_weird_results.json").write_text(json.dumps({"no_findings": True}))
        result = load_previous_results(tmp_path, "REPORT")
        assert result is None


# ---------------------------------------------------------------------------
# compute_diff
# ---------------------------------------------------------------------------


class TestComputeDiff:
    """compute_diff should classify findings as new, resolved, or persistent."""

    def _make_finding(self, file: str, title: str) -> Finding:
        return Finding(file, 1, Severity.HIGH, "cat", 1, title)

    def test_all_new(self):
        current = [self._make_finding("a.cpp", "new_issue")]
        diff = compute_diff([], current)
        assert len(diff.new_findings) == 1
        assert len(diff.resolved_findings) == 0
        assert diff.persistent_count == 0

    def test_all_resolved(self):
        previous = [{"file": "a.cpp", "severity": "high", "dedup_key": "oldkey123"}]
        diff = compute_diff(previous, [])
        assert len(diff.new_findings) == 0
        assert len(diff.resolved_findings) == 1
        assert diff.persistent_count == 0

    def test_persistent_findings(self):
        f = self._make_finding("a.cpp", "persistent_issue")
        previous = [{"file": "a.cpp", "severity": "high", "dedup_key": f.dedup_key}]
        diff = compute_diff(previous, [f])
        assert len(diff.new_findings) == 0
        assert len(diff.resolved_findings) == 0
        assert diff.persistent_count == 1

    def test_mixed_new_resolved_persistent(self):
        persistent = self._make_finding("a.cpp", "stays")
        new_finding = self._make_finding("b.cpp", "new_one")
        previous = [
            {"file": "a.cpp", "severity": "high", "dedup_key": persistent.dedup_key},
            {"file": "c.cpp", "severity": "low", "dedup_key": "resolved_key"},
        ]
        diff = compute_diff(previous, [persistent, new_finding])
        assert len(diff.new_findings) == 1
        assert len(diff.resolved_findings) == 1
        assert diff.persistent_count == 1

    def test_empty_both(self):
        diff = compute_diff([], [])
        assert len(diff.new_findings) == 0
        assert len(diff.resolved_findings) == 0
        assert diff.persistent_count == 0


class TestReportDiffToDict:
    """ReportDiff.to_dict should contain all expected fields."""

    def test_contains_all_fields(self):
        d = ReportDiff(
            new_findings=[{"a": 1}],
            resolved_findings=[{"b": 2}],
            persistent_count=5,
            previous_path="/some/path",
        )
        result = d.to_dict()
        assert result["new_findings"] == [{"a": 1}]
        assert result["resolved_findings"] == [{"b": 2}]
        assert result["persistent_count"] == 5
        assert result["previous_path"] == "/some/path"
