"""Tests for lib.corroboration — D2 cross-source corroboration layer."""

from __future__ import annotations

from lib.corroboration import corroborate
from lib.findings import Finding, Severity


def _f(
    *,
    file: str = "src/a.cpp",
    line: int | None = 10,
    severity: Severity = Severity.MEDIUM,
    category: str = "memory_safety",
    tier: int = 2,
    title: str = "t",
    pattern_name: str = "raw_new",
) -> Finding:
    return Finding(
        file=file,
        line=line,
        severity=severity,
        category=category,
        source_tier=tier,
        title=title,
        pattern_name=pattern_name,
    )


# ---------------------------------------------------------------------------
# Solo findings — no cross-source signal to react to
# ---------------------------------------------------------------------------


class TestSoloFindings:

    def test_solo_finding_unchanged(self):
        f = _f(pattern_name="path_traversal")
        original_sev = f.severity
        result = corroborate([f], {"enabled": True, "promote_level": 1})
        assert result[0].corroborated_by == []
        assert result[0].severity == original_sev

    def test_two_findings_same_source_do_not_corroborate(self):
        """Two tier-2 patterns hitting the same line are NOT independent
        — both share source_key='pattern_scan'."""
        a = _f(pattern_name="raw_new", line=10)
        b = _f(pattern_name="raw_delete", line=10)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.corroborated_by == []
        assert b.corroborated_by == []

    def test_different_lines_do_not_corroborate(self):
        a = _f(category="cppcheck", tier=1, line=10)
        b = _f(category="memory_safety", tier=2, line=11)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.corroborated_by == []
        assert b.corroborated_by == []

    def test_different_files_do_not_corroborate(self):
        a = _f(file="src/a.cpp", category="cppcheck", tier=1)
        b = _f(file="src/b.cpp", category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.corroborated_by == []
        assert b.corroborated_by == []

    def test_none_line_skipped(self):
        """Findings without a line number can't be positionally corroborated."""
        a = _f(line=None, category="cppcheck", tier=1)
        b = _f(line=None, category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.corroborated_by == []
        assert b.corroborated_by == []


# ---------------------------------------------------------------------------
# Corroboration — two independent sources agreeing on a line
# ---------------------------------------------------------------------------


class TestCorroboration:

    def test_cppcheck_plus_pattern_corroborate(self):
        """Different tools at the same (file, line) DO corroborate."""
        a = _f(category="cppcheck", tier=1, pattern_name="cppcheck:memleak")
        b = _f(category="memory_safety", tier=2, pattern_name="raw_new")
        corroborate([a, b], {"enabled": True, "promote_level": 0})
        assert a.corroborated_by == ["pattern_scan"]
        assert b.corroborated_by == ["cppcheck"]

    def test_three_sources_all_tagged(self):
        a = _f(category="cppcheck", tier=1, pattern_name="cppcheck:x")
        b = _f(category="clang_tidy", tier=1, pattern_name="clang-tidy:y")
        c = _f(category="memory_safety", tier=2, pattern_name="raw_new")
        corroborate([a, b, c], {"enabled": True, "promote_level": 0})
        assert a.corroborated_by == ["clang_tidy", "pattern_scan"]
        assert b.corroborated_by == ["cppcheck", "pattern_scan"]
        assert c.corroborated_by == ["clang_tidy", "cppcheck"]

    def test_tier4_submodules_corroborate(self):
        """Two tier-4 modules flagging the same function is real corroboration."""
        a = _f(category="complexity", tier=4, pattern_name="complexity")
        b = _f(category="cognitive_complexity", tier=4, pattern_name="cognitive")
        corroborate([a, b], {"enabled": True, "promote_level": 0})
        assert a.corroborated_by == ["tier4_cognitive_complexity"]
        assert b.corroborated_by == ["tier4_complexity"]


# ---------------------------------------------------------------------------
# Severity promotion
# ---------------------------------------------------------------------------


class TestSeverityPromotion:

    def test_medium_promoted_to_high(self):
        a = _f(severity=Severity.MEDIUM, category="cppcheck", tier=1)
        b = _f(severity=Severity.MEDIUM, category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.severity == Severity.HIGH
        assert b.severity == Severity.HIGH

    def test_low_promoted_to_medium(self):
        a = _f(severity=Severity.LOW, category="cppcheck", tier=1)
        b = _f(severity=Severity.LOW, category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.severity == Severity.MEDIUM
        assert b.severity == Severity.MEDIUM

    def test_promote_level_two_jumps_two_steps(self):
        a = _f(severity=Severity.INFO, category="cppcheck", tier=1)
        b = _f(severity=Severity.INFO, category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 2})
        assert a.severity == Severity.MEDIUM
        assert b.severity == Severity.MEDIUM

    def test_critical_stays_critical(self):
        """Promoting an already-CRITICAL finding should not wrap around."""
        a = _f(severity=Severity.CRITICAL, category="cppcheck", tier=1)
        b = _f(severity=Severity.CRITICAL, category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 1})
        assert a.severity == Severity.CRITICAL
        assert b.severity == Severity.CRITICAL

    def test_promote_level_zero_tags_only(self):
        """promote_level=0 means tag but do not bump severity."""
        a = _f(severity=Severity.LOW, category="cppcheck", tier=1)
        b = _f(severity=Severity.LOW, category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 0})
        assert a.corroborated_by == ["pattern_scan"]
        assert a.severity == Severity.LOW
        assert b.severity == Severity.LOW

    def test_solo_finding_not_promoted(self):
        f = _f(severity=Severity.LOW, category="cppcheck", tier=1)
        corroborate([f], {"enabled": True, "promote_level": 1})
        assert f.severity == Severity.LOW


# ---------------------------------------------------------------------------
# Demotion of noisy solo patterns
# ---------------------------------------------------------------------------


class TestDemotion:

    def test_noisy_solo_pattern_demoted_to_info(self):
        f = _f(
            severity=Severity.LOW,
            pattern_name="std_endl",
            category="performance",
            tier=2,
        )
        corroborate(
            [f],
            {"enabled": True, "promote_level": 1, "demoted_patterns": ["std_endl"]},
        )
        assert f.severity == Severity.INFO

    def test_noisy_pattern_not_demoted_when_corroborated(self):
        """A pattern in demoted_patterns keeps its original severity (or a
        promotion) when another source also flagged the line."""
        a = _f(
            severity=Severity.LOW,
            pattern_name="std_endl",
            category="performance",
            tier=2,
        )
        b = _f(
            severity=Severity.LOW,
            pattern_name="clang-tidy:readability",
            category="clang_tidy",
            tier=1,
        )
        corroborate(
            [a, b],
            {"enabled": True, "promote_level": 1, "demoted_patterns": ["std_endl"]},
        )
        assert a.corroborated_by == ["clang_tidy"]
        # Promoted, not demoted, because it's corroborated.
        assert a.severity == Severity.MEDIUM

    def test_non_noisy_solo_pattern_not_demoted(self):
        f = _f(
            severity=Severity.HIGH,
            pattern_name="path_traversal",
            category="security",
            tier=2,
        )
        corroborate(
            [f],
            {"enabled": True, "promote_level": 1, "demoted_patterns": ["std_endl"]},
        )
        assert f.severity == Severity.HIGH

    def test_info_level_demoted_pattern_is_noop(self):
        """Already-INFO findings shouldn't be touched (nothing to demote)."""
        f = _f(
            severity=Severity.INFO,
            pattern_name="todo_fixme",
            category="code_quality",
            tier=2,
        )
        corroborate(
            [f],
            {"enabled": True, "promote_level": 1, "demoted_patterns": ["todo_fixme"]},
        )
        assert f.severity == Severity.INFO


# ---------------------------------------------------------------------------
# Disabled / edge cases
# ---------------------------------------------------------------------------


class TestDisabledAndEdge:

    def test_disabled_is_noop(self):
        a = _f(severity=Severity.LOW, category="cppcheck", tier=1)
        b = _f(severity=Severity.LOW, category="memory_safety", tier=2)
        corroborate(
            [a, b],
            {"enabled": False, "promote_level": 1, "demoted_patterns": ["raw_new"]},
        )
        assert a.corroborated_by == []
        assert b.corroborated_by == []
        assert a.severity == Severity.LOW
        assert b.severity == Severity.LOW

    def test_none_config_defaults_to_enabled(self):
        """Callers passing None should get the enabled default behaviour."""
        a = _f(category="cppcheck", tier=1)
        b = _f(category="memory_safety", tier=2)
        corroborate([a, b], None)
        assert a.corroborated_by == ["pattern_scan"]

    def test_empty_list(self):
        assert corroborate([], {"enabled": True}) == []

    def test_preserves_input_list_identity(self):
        """Return value should be the same list instance (in-place mutation)."""
        findings = [_f()]
        result = corroborate(findings, {"enabled": True})
        assert result is findings

    def test_merges_with_preexisting_tags(self):
        """If a finding already carries corroborated_by tags (e.g. from a
        future re-run or external source), corroborate() should merge
        rather than overwrite."""
        a = _f(category="cppcheck", tier=1)
        a.corroborated_by = ["some_external_source"]
        b = _f(category="memory_safety", tier=2)
        corroborate([a, b], {"enabled": True, "promote_level": 0})
        assert "pattern_scan" in a.corroborated_by
        assert "some_external_source" in a.corroborated_by
