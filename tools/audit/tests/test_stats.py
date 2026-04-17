# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.stats — per-rule counters and self-triage report."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from lib.findings import Finding, Severity
from lib.stats import (
    DEFAULT_DEMOTION_POLICY,
    MAX_RUN_HISTORY,
    STATS_SCHEMA_VERSION,
    AuditStats,
    RuleStat,
    RunSummary,
    _rule_id,
    compute_demotions,
    load_stats,
    render_triage_markdown,
    save_stats,
    update_stats,
)


# ---------------------------------------------------------------------------
# RuleStat derived properties
# ---------------------------------------------------------------------------


class TestRuleStatRatios:
    def test_zero_hits_yields_zero_ratios(self):
        r = RuleStat(rule_id="x")
        assert r.noise_ratio == 0.0
        assert r.actionable_ratio == 0.0
        assert r.unresolved == 0

    def test_noise_ratio_is_suppressed_over_hits(self):
        r = RuleStat(rule_id="x", hits=20, suppressed=15)
        assert r.noise_ratio == 0.75

    def test_actionable_ratio_is_verified_over_hits(self):
        r = RuleStat(rule_id="x", hits=20, verified=5)
        assert r.actionable_ratio == 0.25

    def test_unresolved_excludes_verified_and_suppressed(self):
        r = RuleStat(rule_id="x", hits=20, verified=5, suppressed=10)
        assert r.unresolved == 5

    def test_unresolved_never_negative(self):
        # Defensive: miscounts (verified + suppressed > hits) should
        # clamp rather than return a negative integer.
        r = RuleStat(rule_id="x", hits=3, verified=5, suppressed=5)
        assert r.unresolved == 0


# ---------------------------------------------------------------------------
# _rule_id derivation from Finding
# ---------------------------------------------------------------------------


class TestRuleIdDerivation:
    def test_prefers_pattern_name_when_present(self):
        f = Finding(
            file="a.cpp", line=1, severity=Severity.HIGH, category="cppcheck",
            source_tier=1, title="any", pattern_name="cppcheck:invalidPointerCast",
        )
        assert _rule_id(f) == "cppcheck:invalidPointerCast"

    def test_falls_back_to_category_and_title_when_pattern_empty(self):
        f = Finding(
            file="a.cpp", line=1, severity=Severity.HIGH, category="complexity",
            source_tier=4, title="cyclomatic >30: renderScene()",
            pattern_name="",
        )
        # Fallback uses `category:title_up_to_first_colon`
        assert _rule_id(f) == "complexity:cyclomatic >30"

    def test_fallback_handles_blank_title(self):
        f = Finding(
            file="a.cpp", line=1, severity=Severity.INFO, category="tier6",
            source_tier=6, title="", pattern_name="",
        )
        # No crash, deterministic output
        assert _rule_id(f) == "tier6:unknown"


# ---------------------------------------------------------------------------
# load_stats / save_stats round-trip
# ---------------------------------------------------------------------------


class TestLoadSaveRoundTrip:
    def test_missing_file_returns_empty(self, tmp_path: Path):
        stats = load_stats(tmp_path)
        assert stats.schema_version == STATS_SCHEMA_VERSION
        assert stats.rules == {}
        assert stats.runs == []

    def test_round_trip_preserves_rules_and_runs(self, tmp_path: Path):
        stats = AuditStats()
        stats.rules["foo"] = RuleStat(
            rule_id="foo", hits=3, verified=1, suppressed=1,
            last_hit_at="2026-04-17T10:00:00+00:00",
            first_seen_at="2026-04-10T10:00:00+00:00",
        )
        stats.runs.append(RunSummary(
            timestamp="2026-04-17T10:00:00+00:00",
            total_findings=10, duration_s=1.5,
            tiers_run=[1, 2], severity_counts={"high": 3},
        ))
        save_stats(stats, tmp_path)
        reloaded = load_stats(tmp_path)
        assert reloaded.rules["foo"].hits == 3
        assert reloaded.rules["foo"].verified == 1
        assert reloaded.rules["foo"].suppressed == 1
        assert reloaded.runs[0].total_findings == 10
        assert reloaded.runs[0].tiers_run == [1, 2]
        assert reloaded.runs[0].severity_counts == {"high": 3}

    def test_corrupt_file_starts_fresh(self, tmp_path: Path):
        (tmp_path / ".audit_stats.json").write_text("{ not valid json")
        stats = load_stats(tmp_path)
        assert stats.rules == {}
        assert stats.runs == []

    def test_wrong_schema_version_starts_fresh(self, tmp_path: Path):
        # Future-us will eventually bump the schema — make sure the file
        # doesn't silently misparse under the new version.
        (tmp_path / ".audit_stats.json").write_text(
            json.dumps({"schema_version": 999, "runs": [], "rules": {}})
        )
        stats = load_stats(tmp_path)
        assert stats.rules == {}


# ---------------------------------------------------------------------------
# update_stats — the core aggregation step
# ---------------------------------------------------------------------------


def _mk_finding(pattern_name: str, **kwargs) -> Finding:
    """Small helper so tests stay readable."""
    defaults = dict(
        file="x.cpp", line=10, severity=Severity.MEDIUM,
        category="cat", source_tier=2, title="t",
        pattern_name=pattern_name,
    )
    defaults.update(kwargs)
    return Finding(**defaults)


class TestUpdateStats:
    def test_first_update_creates_rule_entries(self):
        stats = AuditStats()
        findings = [_mk_finding("rule_a"), _mk_finding("rule_b")]
        update_stats(stats, findings, suppressed_keys=set(),
                     tiers_run=[1, 2], duration_s=1.0)
        assert "rule_a" in stats.rules
        assert "rule_b" in stats.rules
        assert stats.rules["rule_a"].hits == 1
        assert stats.rules["rule_b"].hits == 1

    def test_repeated_hits_accumulate(self):
        stats = AuditStats()
        update_stats(stats, [_mk_finding("rule_a")],
                     suppressed_keys=set(), tiers_run=[1], duration_s=1.0)
        update_stats(stats, [_mk_finding("rule_a"), _mk_finding("rule_a")],
                     suppressed_keys=set(), tiers_run=[1], duration_s=1.0)
        assert stats.rules["rule_a"].hits == 3

    def test_verified_flag_increments_verified_counter(self):
        stats = AuditStats()
        f1 = _mk_finding("rule_a")
        f1.verified = True
        f2 = _mk_finding("rule_a")
        # f2 stays unverified
        update_stats(stats, [f1, f2], suppressed_keys=set(),
                     tiers_run=[1], duration_s=1.0)
        assert stats.rules["rule_a"].hits == 2
        assert stats.rules["rule_a"].verified == 1

    def test_suppressed_key_increments_suppressed_counter(self):
        stats = AuditStats()
        f1 = _mk_finding("rule_a", file="a.cpp", line=1, title="t1")
        f2 = _mk_finding("rule_a", file="a.cpp", line=2, title="t2")
        # Only f1 appears in the suppressed set.
        update_stats(
            stats, [f1, f2],
            suppressed_keys={f1.dedup_key},
            tiers_run=[1], duration_s=1.0,
        )
        assert stats.rules["rule_a"].hits == 2
        assert stats.rules["rule_a"].suppressed == 1

    def test_run_summary_appended(self):
        stats = AuditStats()
        update_stats(stats, [_mk_finding("rule_a", severity=Severity.HIGH),
                             _mk_finding("rule_b", severity=Severity.LOW)],
                     suppressed_keys=set(), tiers_run=[1, 2], duration_s=2.5)
        assert len(stats.runs) == 1
        run = stats.runs[0]
        assert run.total_findings == 2
        assert run.tiers_run == [1, 2]
        assert run.severity_counts == {"high": 1, "low": 1}

    def test_run_history_caps_at_max(self):
        stats = AuditStats()
        for _ in range(MAX_RUN_HISTORY + 10):
            update_stats(stats, [], suppressed_keys=set(),
                         tiers_run=[1], duration_s=0.1)
        assert len(stats.runs) == MAX_RUN_HISTORY

    def test_first_seen_at_populated_on_new_rule_only(self):
        stats = AuditStats()
        update_stats(stats, [_mk_finding("rule_a")],
                     suppressed_keys=set(), tiers_run=[1], duration_s=1.0,
                     now="2026-04-10T10:00:00+00:00")
        update_stats(stats, [_mk_finding("rule_a")],
                     suppressed_keys=set(), tiers_run=[1], duration_s=1.0,
                     now="2026-04-17T10:00:00+00:00")
        r = stats.rules["rule_a"]
        assert r.first_seen_at == "2026-04-10T10:00:00+00:00"
        assert r.last_hit_at == "2026-04-17T10:00:00+00:00"


# ---------------------------------------------------------------------------
# render_triage_markdown
# ---------------------------------------------------------------------------


class TestRenderTriage:
    def test_empty_stats_says_so(self):
        out = render_triage_markdown(AuditStats())
        assert "No stats yet" in out

    def test_noisy_rule_surfaces_in_recommended_actions(self):
        stats = AuditStats()
        # 15 hits, 14 suppressed → 93% noise — should land in
        # recommended actions.
        for i in range(15):
            f = _mk_finding("noisy_rule", line=i)
        stats.rules["noisy_rule"] = RuleStat(
            rule_id="noisy_rule", hits=15, suppressed=14, verified=0,
        )
        stats.runs.append(RunSummary(timestamp="t", total_findings=0,
                                     duration_s=0.1))
        out = render_triage_markdown(stats)
        assert "noisy_rule" in out
        assert "Recommended actions" in out
        assert "93 % noise" in out

    def test_low_hit_rules_are_listed_separately(self):
        stats = AuditStats()
        stats.rules["rare_rule"] = RuleStat(
            rule_id="rare_rule", hits=2, suppressed=2,
        )
        stats.runs.append(RunSummary(timestamp="t", total_findings=0,
                                     duration_s=0.1))
        out = render_triage_markdown(stats, min_hits=5)
        # Low-hit rule stays out of the ranked section but is listed
        # under "Not yet ranked" so we don't lose sight of it.
        assert "Not yet ranked" in out
        assert "rare_rule" in out
        # And NOT in recommended actions (insufficient observations).
        assert "Recommended actions" not in out

    def test_ranking_sorts_by_noise_score(self):
        stats = AuditStats()
        stats.rules["mild"] = RuleStat(rule_id="mild", hits=10, suppressed=5)
        stats.rules["loud"] = RuleStat(rule_id="loud", hits=20, suppressed=18)
        stats.runs.append(RunSummary(timestamp="t", total_findings=0,
                                     duration_s=0.1))
        out = render_triage_markdown(stats)
        # loud (0.9 * 20 = 18) should appear before mild (0.5 * 10 = 5)
        loud_pos = out.find("`loud`")
        mild_pos = out.find("`mild`")
        assert loud_pos > 0 and mild_pos > 0
        assert loud_pos < mild_pos


# ---------------------------------------------------------------------------
# compute_demotions — Phase 2 severity-demotion policy
# ---------------------------------------------------------------------------


def _stats_with(**rule_kwargs) -> AuditStats:
    """Build AuditStats with a single rule whose kwargs are supplied."""
    s = AuditStats()
    rid = rule_kwargs.get("rule_id", "rule_x")
    s.rules[rid] = RuleStat(**rule_kwargs)
    return s


class TestComputeDemotionsPolicyGates:
    def test_empty_stats_yields_no_demotions(self):
        assert compute_demotions(AuditStats()) == {}

    def test_disabled_policy_short_circuits(self):
        s = _stats_with(rule_id="x", hits=100, suppressed=100)
        assert compute_demotions(s, {"enabled": False}) == {}

    def test_below_min_hits_is_not_demoted(self):
        s = _stats_with(rule_id="x", hits=5, suppressed=5)
        # Default min_hits = 10
        assert compute_demotions(s) == {}

    def test_below_noise_threshold_is_not_demoted(self):
        s = _stats_with(rule_id="x", hits=20, suppressed=10)
        # 50% noise, default threshold 90%
        assert compute_demotions(s) == {}

    def test_meets_all_gates_is_demoted(self):
        s = _stats_with(rule_id="noisy", hits=20, suppressed=20)
        result = compute_demotions(s)
        assert result == {"noisy": 1}

    def test_verified_hit_blocks_demotion_by_default(self):
        # 1 verified out of 50 should protect the rule — it has
        # produced at least one real finding.
        s = _stats_with(rule_id="x", hits=50, suppressed=49, verified=1)
        assert compute_demotions(s) == {}

    def test_opt_out_of_zero_verified_gate(self):
        s = _stats_with(rule_id="x", hits=50, suppressed=49, verified=1)
        assert compute_demotions(
            s, {"require_zero_verified": False}
        ) == {"x": 1}

    def test_exempt_list_skips_matching_rules(self):
        s = AuditStats()
        s.rules["a"] = RuleStat(rule_id="a", hits=20, suppressed=20)
        s.rules["b"] = RuleStat(rule_id="b", hits=20, suppressed=20)
        result = compute_demotions(s, {"exempt": ["a"]})
        assert result == {"b": 1}

    def test_custom_steps_reflected_in_output(self):
        s = _stats_with(rule_id="x", hits=20, suppressed=20)
        assert compute_demotions(s, {"demote_steps": 2}) == {"x": 2}

    def test_zero_or_negative_steps_disables(self):
        s = _stats_with(rule_id="x", hits=20, suppressed=20)
        assert compute_demotions(s, {"demote_steps": 0}) == {}
        assert compute_demotions(s, {"demote_steps": -1}) == {}

    def test_none_valued_policy_key_falls_back_to_default(self):
        # A config file may leave a key as None (YAML null). None
        # values should merge as "use default" rather than overwrite
        # a functional default with nothing.
        s = _stats_with(rule_id="x", hits=20, suppressed=20)
        # Default min_hits=10 should still apply.
        assert compute_demotions(s, {"min_hits": None}) == {"x": 1}


class TestApplyAutoDemotions:
    def test_no_demotions_noops(self):
        from lib.findings import apply_auto_demotions
        f = _mk_finding("rule_a", severity=Severity.HIGH)
        result = apply_auto_demotions([f], {})
        assert result == {}
        assert f.severity == Severity.HIGH

    def test_single_step_demotion(self):
        from lib.findings import apply_auto_demotions
        f = _mk_finding("rule_a", severity=Severity.HIGH)
        applied = apply_auto_demotions([f], {"rule_a": 1})
        assert applied == {"rule_a": 1}
        assert f.severity == Severity.MEDIUM

    def test_multi_step_demotion(self):
        from lib.findings import apply_auto_demotions
        f = _mk_finding("rule_a", severity=Severity.HIGH)
        apply_auto_demotions([f], {"rule_a": 2})
        assert f.severity == Severity.LOW

    def test_clamps_at_info(self):
        from lib.findings import apply_auto_demotions
        f = _mk_finding("rule_a", severity=Severity.LOW)
        apply_auto_demotions([f], {"rule_a": 10})
        # Severity.INFO is the floor — further demotion is a no-op.
        assert f.severity == Severity.INFO

    def test_non_matching_finding_untouched(self):
        from lib.findings import apply_auto_demotions
        f = _mk_finding("other_rule", severity=Severity.CRITICAL)
        apply_auto_demotions([f], {"rule_a": 1})
        assert f.severity == Severity.CRITICAL
