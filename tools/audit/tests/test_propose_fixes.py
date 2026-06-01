# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.stats Phase 3 — the propose-fix layer.

compute_proposals mines the false-positive set of noisy tier-2 pattern rules
for a common textual signature and suggests an exclude_pattern addition. These
tests pin the policy gates, the signature-mining heuristic, and the markdown
rendering.
"""

from __future__ import annotations

import pytest

from lib.findings import Finding, Severity
from lib.stats import (
    DEFAULT_PROPOSE_POLICY,
    AuditStats,
    PatternProposal,
    RuleStat,
    RunSummary,
    compute_proposals,
    render_proposals_markdown,
    _mine_signatures,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _fp(pattern_name: str, detail: str, line: int = 1) -> Finding:
    """A tier-2 pattern finding whose matched text lives in `detail`."""
    return Finding(
        file="x.cpp", line=line, severity=Severity.HIGH,
        category="memory_safety", source_tier=2, title="Raw new",
        detail=detail, pattern_name=pattern_name,
    )


def _stats(rid: str, *, hits: int, suppressed: int, runs: int = 3) -> AuditStats:
    """AuditStats with `runs` run summaries and one rule's cumulative counts."""
    s = AuditStats()
    s.rules[rid] = RuleStat(rule_id=rid, hits=hits, suppressed=suppressed)
    for _ in range(runs):
        s.runs.append(RunSummary(timestamp="t", total_findings=hits,
                                 duration_s=1.0))
    return s


_PATTERNS = {"memory_safety": [{"name": "raw_new", "exclude_pattern": ""}]}


def _suppress_all(findings: list[Finding]) -> set[str]:
    return {f.dedup_key for f in findings}


# ---------------------------------------------------------------------------
# _mine_signatures
# ---------------------------------------------------------------------------

class TestMineSignatures:
    def test_finds_token_common_to_all_fps(self):
        details = [
            "auto x = new tinygltf::Image()",
            "auto y = new tinygltf::Mesh()",
            "auto z = new tinygltf::Node()",
        ]
        sigs, support = _mine_signatures(details, "", DEFAULT_PROPOSE_POLICY)
        # The common namespace prefix (with trailing ::) is the signature —
        # the full tokens (tinygltf::Image, ::Mesh, ::Node) all differ.
        assert "tinygltf::" in sigs
        assert support["tinygltf::"] == 3

    def test_respects_min_support_fraction(self):
        # "make_unique" in 1/4 only — below the 60 % support floor.
        details = [
            "x = new Foo_widget()",
            "y = new Foo_widget()",
            "z = new Foo_widget()",
            "w = make_unique<Bar>()",
        ]
        sigs, _ = _mine_signatures(details, "", DEFAULT_PROPOSE_POLICY)
        assert "Foo_widget" in sigs
        assert "make_unique" not in sigs

    def test_excludes_stopwords(self):
        details = ["return new Thing()", "return new Thing()", "return new Thing()"]
        sigs, _ = _mine_signatures(details, "", DEFAULT_PROPOSE_POLICY)
        # "return" and "new" are stopwords; only "Thing" qualifies.
        assert "return" not in sigs
        assert "new" not in sigs
        assert "Thing" in sigs

    def test_skips_tokens_already_in_existing_exclude(self):
        details = ["new tinygltf::Image()"] * 3
        sigs, _ = _mine_signatures(details, "tinygltf", DEFAULT_PROPOSE_POLICY)
        assert "tinygltf" not in sigs

    def test_short_tokens_below_min_len_are_dropped(self):
        # "ab" is 2 chars (< default min_token_len 4) and has no "::".
        details = ["new ab()", "new ab()", "new ab()"]
        sigs, _ = _mine_signatures(details, "", DEFAULT_PROPOSE_POLICY)
        assert "ab" not in sigs

    def test_namespace_qualified_token_kept_even_when_short(self):
        details = ["new a::b()", "new a::b()", "new a::b()"]
        sigs, _ = _mine_signatures(details, "", DEFAULT_PROPOSE_POLICY)
        # "a::" is the general prefix; "a::b" is dropped as redundant.
        assert "a::" in sigs
        assert "a::b" not in sigs


# ---------------------------------------------------------------------------
# compute_proposals — policy gates
# ---------------------------------------------------------------------------

class TestComputeProposalsGates:
    def test_disabled_policy_returns_empty(self):
        findings = [_fp("raw_new", "new tinygltf::Image()", i) for i in range(5)]
        stats = _stats("raw_new", hits=20, suppressed=20)
        out = compute_proposals(stats, findings, _suppress_all(findings),
                                _PATTERNS, policy={"enabled": False})
        assert out == []

    def test_history_gate_blocks_until_min_runs(self):
        findings = [_fp("raw_new", "new tinygltf::Image()", i) for i in range(5)]
        stats = _stats("raw_new", hits=20, suppressed=20, runs=2)  # < 3
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert out == []

    def test_min_fp_gate(self):
        # Only 2 FPs in the run — below min_fp=3.
        findings = [_fp("raw_new", "new tinygltf::Image()", i) for i in range(2)]
        stats = _stats("raw_new", hits=20, suppressed=20)
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert out == []

    def test_min_hits_gate(self):
        findings = [_fp("raw_new", "new tinygltf::Image()", i) for i in range(5)]
        stats = _stats("raw_new", hits=5, suppressed=5)  # < min_hits 10
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert out == []

    def test_noise_threshold_gate(self):
        findings = [_fp("raw_new", "new tinygltf::Image()", i) for i in range(5)]
        stats = _stats("raw_new", hits=20, suppressed=4)  # 20 % noise < 0.5
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert out == []

    def test_rule_not_in_patterns_is_out_of_scope(self):
        # cppcheck rule has no exclude_pattern knob → never proposed.
        findings = [_fp("cppcheck:foo", "new tinygltf::Image()", i) for i in range(5)]
        stats = _stats("cppcheck:foo", hits=20, suppressed=20)
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert out == []

    def test_unsuppressed_findings_are_ignored(self):
        findings = [_fp("raw_new", "new tinygltf::Image()", i) for i in range(5)]
        stats = _stats("raw_new", hits=20, suppressed=20)
        # Empty suppressed set → no false positives to mine.
        out = compute_proposals(stats, findings, set(), _PATTERNS)
        assert out == []


# ---------------------------------------------------------------------------
# compute_proposals — happy path
# ---------------------------------------------------------------------------

class TestComputeProposalsHappyPath:
    def test_emits_proposal_with_signature_and_support(self):
        findings = [_fp("raw_new", f"new tinygltf::Image{i}()", i) for i in range(5)]
        stats = _stats("raw_new", hits=20, suppressed=18)  # 90 % noise
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert len(out) == 1
        p = out[0]
        assert p.rule_id == "raw_new"
        assert p.category == "memory_safety"
        assert "tinygltf::" in p.signatures
        assert p.fp_count == 5
        assert p.support["tinygltf::"] == 5
        assert p.noise_ratio == pytest.approx(0.9)

    def test_suggested_exclude_appends_escaped_signature(self):
        findings = [_fp("raw_new", "new JPH::Body()", i) for i in range(4)]
        stats = _stats("raw_new", hits=20, suppressed=20)
        out = compute_proposals(stats, findings, _suppress_all(findings),
                                {"memory_safety": [{"name": "raw_new",
                                                    "exclude_pattern": "placement"}]})
        assert len(out) == 1
        # The general namespace prefix wins (JPH::Body dropped as redundant).
        assert out[0].signatures == ["JPH::"]
        # Existing pattern preserved first, new signature appended. ':' is not
        # a regex metacharacter so re.escape leaves "JPH::" verbatim.
        assert out[0].suggested_exclude == "placement|JPH::"

    def test_loudest_rule_sorts_first(self):
        loud = [_fp("loud", f"new Loud::T{i}()", i) for i in range(5)]
        mild = [_fp("mild", f"new Mild::T{i}()", i) for i in range(5)]
        findings = loud + mild
        s = AuditStats()
        s.rules["loud"] = RuleStat(rule_id="loud", hits=40, suppressed=40)  # 1.0 × 40
        s.rules["mild"] = RuleStat(rule_id="mild", hits=20, suppressed=12)  # 0.6 × 20
        for _ in range(3):
            s.runs.append(RunSummary(timestamp="t", total_findings=0, duration_s=1.0))
        patterns = {"memory_safety": [
            {"name": "loud", "exclude_pattern": ""},
            {"name": "mild", "exclude_pattern": ""},
        ]}
        out = compute_proposals(s, findings, _suppress_all(findings), patterns)
        assert [p.rule_id for p in out] == ["loud", "mild"]

    def test_max_rules_cap(self):
        findings = []
        patterns_list = []
        s = AuditStats()
        for r in range(5):
            rid = f"rule{r}"
            findings += [_fp(rid, f"new Ns{r}::T{i}()", i) for i in range(4)]
            s.rules[rid] = RuleStat(rule_id=rid, hits=20, suppressed=20)
            patterns_list.append({"name": rid, "exclude_pattern": ""})
        for _ in range(3):
            s.runs.append(RunSummary(timestamp="t", total_findings=0, duration_s=1.0))
        out = compute_proposals(s, findings, _suppress_all(findings),
                                {"memory_safety": patterns_list},
                                policy={"max_rules": 2})
        assert len(out) == 2

    def test_no_common_signature_yields_no_proposal(self):
        # Each FP has a distinct identifier — nothing shared above support floor.
        findings = [_fp("raw_new", f"new Distinct{i}thing()", i) for i in range(5)]
        stats = _stats("raw_new", hits=20, suppressed=20)
        out = compute_proposals(stats, findings, _suppress_all(findings), _PATTERNS)
        assert out == []


# ---------------------------------------------------------------------------
# render_proposals_markdown
# ---------------------------------------------------------------------------

class TestRenderProposals:
    def test_empty_has_no_proposals_note(self):
        out = render_proposals_markdown([])
        assert "No proposals" in out

    def test_renders_rule_signature_and_suggested_pattern(self):
        p = PatternProposal(
            rule_id="raw_new", category="memory_safety",
            signatures=["tinygltf"], fp_count=5, support={"tinygltf": 5},
            hits=20, noise_ratio=0.9, existing_exclude="placement",
            samples=["new tinygltf::Image()"],
        )
        out = render_proposals_markdown([p])
        assert "`raw_new`" in out
        assert "tinygltf" in out
        assert "placement|tinygltf" in out
        assert "90 % noise" in out
        assert "new tinygltf::Image()" in out
