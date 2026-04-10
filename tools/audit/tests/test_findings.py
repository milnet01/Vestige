"""Tests for lib.findings — Severity, Finding, deduplicate, data models."""

from __future__ import annotations

import hashlib

import pytest

from lib.findings import (
    AuditData,
    ChangeSummary,
    Finding,
    ResearchResult,
    Severity,
    deduplicate,
)


# ---------------------------------------------------------------------------
# Severity ordering
# ---------------------------------------------------------------------------


class TestSeverityOrdering:
    """Severity values should sort from most severe to least."""

    def test_critical_is_lowest_value(self):
        assert Severity.CRITICAL < Severity.HIGH

    def test_high_less_than_medium(self):
        assert Severity.HIGH < Severity.MEDIUM

    def test_medium_less_than_low(self):
        assert Severity.MEDIUM < Severity.LOW

    def test_low_less_than_info(self):
        assert Severity.LOW < Severity.INFO

    def test_full_ordering(self):
        ordered = sorted(
            [Severity.INFO, Severity.CRITICAL, Severity.LOW, Severity.MEDIUM, Severity.HIGH]
        )
        assert ordered == [
            Severity.CRITICAL,
            Severity.HIGH,
            Severity.MEDIUM,
            Severity.LOW,
            Severity.INFO,
        ]


# ---------------------------------------------------------------------------
# Severity.from_string
# ---------------------------------------------------------------------------


class TestSeverityFromString:
    """from_string should handle canonical and cppcheck severity names."""

    @pytest.mark.parametrize(
        "input_str, expected",
        [
            ("critical", Severity.CRITICAL),
            ("high", Severity.HIGH),
            ("medium", Severity.MEDIUM),
            ("low", Severity.LOW),
            ("info", Severity.INFO),
            ("CRITICAL", Severity.CRITICAL),
            ("High", Severity.HIGH),
        ],
    )
    def test_canonical_names(self, input_str: str, expected: Severity):
        assert Severity.from_string(input_str) == expected

    @pytest.mark.parametrize(
        "input_str, expected",
        [
            ("error", Severity.HIGH),
            ("warning", Severity.MEDIUM),
            ("style", Severity.LOW),
            ("performance", Severity.MEDIUM),
            ("portability", Severity.MEDIUM),
            ("information", Severity.INFO),
        ],
    )
    def test_cppcheck_mappings(self, input_str: str, expected: Severity):
        assert Severity.from_string(input_str) == expected

    def test_unknown_defaults_to_info(self):
        assert Severity.from_string("banana") == Severity.INFO

    def test_empty_string_defaults_to_info(self):
        assert Severity.from_string("") == Severity.INFO


# ---------------------------------------------------------------------------
# Finding.dedup_key
# ---------------------------------------------------------------------------


class TestFindingDedupKey:
    """dedup_key should be a deterministic hash of file:line:category:title."""

    def test_same_inputs_produce_same_key(self):
        a = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "title")
        b = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "title")
        assert a.dedup_key == b.dedup_key

    def test_different_file_produces_different_key(self):
        a = Finding("a.cpp", 10, Severity.HIGH, "cat", 1, "title")
        b = Finding("b.cpp", 10, Severity.HIGH, "cat", 1, "title")
        assert a.dedup_key != b.dedup_key

    def test_different_line_produces_different_key(self):
        a = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "title")
        b = Finding("f.cpp", 20, Severity.HIGH, "cat", 1, "title")
        assert a.dedup_key != b.dedup_key

    def test_different_category_produces_different_key(self):
        a = Finding("f.cpp", 10, Severity.HIGH, "cat1", 1, "title")
        b = Finding("f.cpp", 10, Severity.HIGH, "cat2", 1, "title")
        assert a.dedup_key != b.dedup_key

    def test_different_title_produces_different_key(self):
        a = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "title_a")
        b = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "title_b")
        assert a.dedup_key != b.dedup_key

    def test_key_is_hex_string_of_16_chars(self):
        f = Finding("f.cpp", 1, Severity.LOW, "cat", 2, "title")
        assert len(f.dedup_key) == 16
        # Should be valid hex
        int(f.dedup_key, 16)

    def test_key_matches_expected_sha256(self):
        f = Finding("f.cpp", 1, Severity.LOW, "cat", 2, "title")
        raw = "f.cpp:1:cat:title"
        expected = hashlib.sha256(raw.encode()).hexdigest()[:16]
        assert f.dedup_key == expected


# ---------------------------------------------------------------------------
# deduplicate()
# ---------------------------------------------------------------------------


class TestDeduplicate:
    """deduplicate() should remove duplicates, keeping the lower-tier finding."""

    def test_keeps_lower_tier_on_duplicate(self):
        tier1 = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "dup")
        tier2 = Finding("f.cpp", 10, Severity.HIGH, "cat", 2, "dup")
        result = deduplicate([tier2, tier1])
        assert len(result) == 1
        assert result[0].source_tier == 1

    def test_replaces_higher_tier_with_lower(self):
        tier3 = Finding("f.cpp", 10, Severity.HIGH, "cat", 3, "dup")
        tier1 = Finding("f.cpp", 10, Severity.HIGH, "cat", 1, "dup")
        result = deduplicate([tier3, tier1])
        assert len(result) == 1
        assert result[0].source_tier == 1

    def test_no_duplicates_returns_all(self):
        a = Finding("a.cpp", 1, Severity.HIGH, "cat", 1, "A")
        b = Finding("b.cpp", 2, Severity.LOW, "cat", 2, "B")
        result = deduplicate([a, b])
        assert len(result) == 2

    def test_sorts_by_severity_then_file_then_line(self):
        f1 = Finding("b.cpp", 10, Severity.LOW, "cat1", 2, "low_b")
        f2 = Finding("a.cpp", 5, Severity.HIGH, "cat2", 1, "high_a")
        f3 = Finding("a.cpp", 1, Severity.HIGH, "cat3", 1, "high_a_early")
        result = deduplicate([f1, f2, f3])
        assert result[0].severity == Severity.HIGH
        assert result[0].file == "a.cpp"
        assert result[0].line == 1
        assert result[1].severity == Severity.HIGH
        assert result[1].file == "a.cpp"
        assert result[1].line == 5
        assert result[2].severity == Severity.LOW

    def test_empty_list(self):
        assert deduplicate([]) == []

    def test_single_finding(self):
        f = Finding("f.cpp", 1, Severity.INFO, "cat", 2, "only")
        result = deduplicate([f])
        assert len(result) == 1
        assert result[0] is f


# ---------------------------------------------------------------------------
# Finding.to_dict
# ---------------------------------------------------------------------------


class TestFindingToDict:
    """to_dict() should contain all expected public fields."""

    def test_all_fields_present(self):
        f = Finding(
            file="src/main.cpp",
            line=42,
            severity=Severity.HIGH,
            category="memory",
            source_tier=1,
            title="Leak",
            detail="ptr not freed",
            pattern_name="raw_new",
        )
        d = f.to_dict()
        assert d["file"] == "src/main.cpp"
        assert d["line"] == 42
        assert d["severity"] == "high"
        assert d["category"] == "memory"
        assert d["source_tier"] == 1
        assert d["title"] == "Leak"
        assert d["detail"] == "ptr not freed"
        assert d["pattern_name"] == "raw_new"

    def test_severity_is_lowercase_string(self):
        f = Finding("f.cpp", 1, Severity.CRITICAL, "cat", 1, "t")
        assert f.to_dict()["severity"] == "critical"

    def test_none_line(self):
        f = Finding("f.cpp", None, Severity.INFO, "cat", 1, "t")
        assert f.to_dict()["line"] is None

    def test_dedup_key_in_dict(self):
        """The dedup_key should be included in to_dict for suppress/diff features."""
        f = Finding("f.cpp", 1, Severity.LOW, "cat", 2, "t")
        d = f.to_dict()
        assert "_dedup_key" not in d
        assert "dedup_key" in d
        assert len(d["dedup_key"]) == 16


# ---------------------------------------------------------------------------
# ChangeSummary.to_dict
# ---------------------------------------------------------------------------


class TestChangeSummaryToDict:

    def test_empty_summary(self):
        cs = ChangeSummary()
        d = cs.to_dict()
        assert d["changed_files"] == []
        assert d["subsystems_touched"] == []
        assert d["total_delta"] == {"+": 0, "-": 0}

    def test_populated_summary(self):
        cs = ChangeSummary(
            changed_files=[{"file": "a.cpp", "status": "M"}],
            subsystems_touched=["rendering"],
            total_added=50,
            total_removed=10,
        )
        d = cs.to_dict()
        assert len(d["changed_files"]) == 1
        assert d["subsystems_touched"] == ["rendering"]
        assert d["total_delta"] == {"+": 50, "-": 10}


# ---------------------------------------------------------------------------
# AuditData.to_dict
# ---------------------------------------------------------------------------


class TestAuditDataToDict:

    def test_empty_data(self):
        ad = AuditData()
        d = ad.to_dict()
        assert d["total_loc"] == 0
        assert d["file_count"] == 0
        assert d["loc_by_subsystem"] == {}
        assert d["gpu_resource_classes"] == []
        assert d["event_lifecycle"] == []
        assert d["deferred_markers_total"] == 0
        assert d["deferred_markers_by_subsystem"] == {}
        assert d["large_files"] == []

    def test_populated_data(self):
        ad = AuditData(
            loc_by_subsystem={"rendering": 5000, "core": 2000},
            total_loc=7000,
            file_count=42,
            gpu_resource_classes=[{"class": "Texture", "file": "texture.h"}],
            deferred_markers=[{"file": "a.cpp", "line": 1}],
            deferred_by_subsystem={"rendering": 1},
        )
        d = ad.to_dict()
        assert d["total_loc"] == 7000
        assert d["file_count"] == 42
        assert d["deferred_markers_total"] == 1
        assert d["deferred_markers_by_subsystem"] == {"rendering": 1}


# ---------------------------------------------------------------------------
# ResearchResult.to_dict
# ---------------------------------------------------------------------------


class TestResearchResultToDict:

    def test_basic(self):
        r = ResearchResult(query="opengl pbr", results=[{"title": "PBR guide"}])
        d = r.to_dict()
        assert d["query"] == "opengl pbr"
        assert len(d["results"]) == 1
        assert "cached" not in d
        assert "error" not in d

    def test_cached_flag(self):
        r = ResearchResult(query="q", results=[], cached=True)
        d = r.to_dict()
        assert d["cached"] is True

    def test_error_field(self):
        r = ResearchResult(query="q", results=[], error="timeout")
        d = r.to_dict()
        assert d["error"] == "timeout"
