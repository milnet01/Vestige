# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Per-rule audit statistics and self-triage report.

Persists cumulative hit / verified / suppressed counts per rule into
``.audit_stats.json`` at the project root. The counters are updated at
the end of every run (after corroboration + verified tagging, BEFORE
the suppress filter runs — so suppressed pattern_names still count
toward the rule's ``suppressed`` total).

Paired with ``--self-triage``, which emits the audit-tool equivalent
of ``AUDIT_TOOL_IMPROVEMENTS.md``: per-rule hit-to-actioned ratio
sorted descending, so noisy rules surface automatically instead of
needing a human to hand-triage a run.

This is Phase 1 of the self-learning loop sketched in
``docs/research/formula_workbench_self_learning_design.md`` §6 (audit analog).
Phase 2 (auto-demotion based on noise ratio) reads from the same
stats file; keeping the collector independent means it works on its
own even without the demotion layer wired up. Phase 3 (``compute_proposals`` /
``render_proposals_markdown``) is the propose-fix layer: it mines the
false-positive set of partially-noisy tier-2 rules for a common signature and
suggests an ``exclude_pattern`` addition — a surgical alternative to Phase 2's
blanket demotion.
"""

from __future__ import annotations

import json
import logging
import re
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .findings import Finding, Severity

log = logging.getLogger("audit")

# Schema version. Bump on any non-additive change so old files are
# rejected with a clear message rather than silently misparsed.
STATS_SCHEMA_VERSION = 1

# How many recent runs to retain in the rolling history. Beyond this,
# the oldest run summary is dropped. Per-rule counters are cumulative
# and are never truncated — only the ``runs`` list rolls.
MAX_RUN_HISTORY = 50


@dataclass
class RuleStat:
    """Per-rule cumulative statistics across the run history."""
    rule_id: str
    hits: int = 0
    verified: int = 0
    suppressed: int = 0
    last_hit_at: str = ""
    # First time this rule was observed. Lets the triage report
    # distinguish "always noisy" from "recently noisy".
    first_seen_at: str = ""

    @property
    def noise_ratio(self) -> float:
        """Fraction of hits the user marked as suppressed (false positive).

        0.0 = no suppressions observed. A rule with high hits and high
        noise_ratio is a strong demotion candidate.
        """
        if self.hits == 0:
            return 0.0
        return self.suppressed / self.hits

    @property
    def actionable_ratio(self) -> float:
        """Fraction of hits the user marked as verified (real)."""
        if self.hits == 0:
            return 0.0
        return self.verified / self.hits

    @property
    def unresolved(self) -> int:
        """Hits that were neither verified nor suppressed.

        These are findings a reviewer saw but didn't classify. A high
        unresolved count usually means the rule fires a lot in places
        nobody bothers to review — sometimes that's legitimately
        low-priority noise, sometimes it's work queue backlog.
        """
        return max(0, self.hits - self.verified - self.suppressed)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "RuleStat":
        return cls(
            rule_id=d["rule_id"],
            hits=d.get("hits", 0),
            verified=d.get("verified", 0),
            suppressed=d.get("suppressed", 0),
            last_hit_at=d.get("last_hit_at", ""),
            first_seen_at=d.get("first_seen_at", ""),
        )


@dataclass
class RunSummary:
    """One entry in the rolling ``runs`` list."""
    timestamp: str
    total_findings: int
    duration_s: float
    tiers_run: list[int] = field(default_factory=list)
    severity_counts: dict[str, int] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "RunSummary":
        return cls(
            timestamp=d["timestamp"],
            total_findings=d.get("total_findings", 0),
            duration_s=d.get("duration_s", 0.0),
            tiers_run=d.get("tiers_run", []),
            severity_counts=d.get("severity_counts", {}),
        )


@dataclass
class AuditStats:
    """Root container persisted to ``.audit_stats.json``."""
    schema_version: int = STATS_SCHEMA_VERSION
    runs: list[RunSummary] = field(default_factory=list)
    rules: dict[str, RuleStat] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": self.schema_version,
            "runs": [r.to_dict() for r in self.runs],
            "rules": {k: v.to_dict() for k, v in self.rules.items()},
        }

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "AuditStats":
        ver = d.get("schema_version", 0)
        if ver != STATS_SCHEMA_VERSION:
            raise ValueError(
                f".audit_stats.json schema_version {ver} does not match "
                f"the tool's supported version {STATS_SCHEMA_VERSION}. "
                f"Delete the file to restart counters from scratch."
            )
        return cls(
            schema_version=ver,
            runs=[RunSummary.from_dict(r) for r in d.get("runs", [])],
            rules={k: RuleStat.from_dict(v) for k, v in d.get("rules", {}).items()},
        )


def load_stats(root: Path, filename: str = ".audit_stats.json") -> AuditStats:
    """Load audit stats from disk, or return an empty container."""
    path = root / filename
    if not path.exists():
        return AuditStats()
    try:
        return AuditStats.from_dict(json.loads(path.read_text()))
    except (json.JSONDecodeError, ValueError) as e:
        log.warning("Failed to load %s: %s — starting fresh", path, e)
        return AuditStats()


def save_stats(
    stats: AuditStats,
    root: Path,
    filename: str = ".audit_stats.json",
) -> None:
    """Persist audit stats to disk."""
    path = root / filename
    path.write_text(json.dumps(stats.to_dict(), indent=2, sort_keys=True) + "\n")
    log.debug("Saved stats for %d rules to %s", len(stats.rules), path)


def _rule_id(finding: Finding) -> str:
    """Stable identifier grouping findings that share a rule.

    Primary key is ``pattern_name`` (the tool-supplied identifier,
    e.g. ``cppcheck:invalidPointerCast`` or ``raw_new``). Falls back
    to ``category:title`` when ``pattern_name`` is empty so tiers
    that don't populate it (some tier-4 modules) still aggregate
    cleanly. Anchored to the full title rather than a prefix so
    distinct tier-4 findings under the same category stay separate.
    """
    if finding.pattern_name:
        return finding.pattern_name
    # Fallback — tier-4 findings often leave pattern_name blank
    # because they're structural rather than pattern-matched.
    title_slug = finding.title.split(":", 1)[0][:64] if finding.title else "unknown"
    return f"{finding.category}:{title_slug}"


def update_stats(
    stats: AuditStats,
    findings: list[Finding],
    suppressed_keys: set[str],
    tiers_run: list[int],
    duration_s: float,
    now: str | None = None,
) -> None:
    """Update per-rule counters and append a new run summary.

    Must be called BEFORE the suppress filter runs — ``findings`` here
    is the full post-verify list including keys that will be dropped.
    This way suppressed pattern_names still increment their parent
    rule's ``suppressed`` counter, which is the whole point: a rule
    that fires 30 times and gets suppressed 30 times has a noise
    ratio of 1.0 and should be demoted.

    ``suppressed_keys`` is the set of ``dedup_key`` values loaded from
    ``.audit_suppress``. ``tiers_run`` and ``duration_s`` come from
    the runner for the RunSummary entry.
    """
    if now is None:
        now = datetime.now(timezone.utc).isoformat(timespec="seconds")

    severity_counts: dict[str, int] = {}
    for f in findings:
        severity_counts[f.severity.name.lower()] = (
            severity_counts.get(f.severity.name.lower(), 0) + 1
        )

        rid = _rule_id(f)
        rs = stats.rules.get(rid)
        if rs is None:
            rs = RuleStat(rule_id=rid, first_seen_at=now)
            stats.rules[rid] = rs
        rs.hits += 1
        rs.last_hit_at = now
        if f.verified:
            rs.verified += 1
        if f.dedup_key in suppressed_keys:
            rs.suppressed += 1

    # Append run summary. Cap the rolling history so the file doesn't
    # grow unboundedly across years of CI runs.
    stats.runs.append(RunSummary(
        timestamp=now,
        total_findings=len(findings),
        duration_s=round(duration_s, 2),
        tiers_run=list(tiers_run),
        severity_counts=severity_counts,
    ))
    if len(stats.runs) > MAX_RUN_HISTORY:
        stats.runs = stats.runs[-MAX_RUN_HISTORY:]


# ---------------------------------------------------------------------------
# Self-triage report — the automated equivalent of AUDIT_TOOL_IMPROVEMENTS.md.
# Sorts rules by noise_ratio × hits so high-signal-of-noise rules float to
# the top and a human can decide whether to tighten the rule or outright
# demote it.
# ---------------------------------------------------------------------------

def render_triage_markdown(stats: AuditStats, min_hits: int = 5) -> str:
    """Produce a markdown triage report ranking rules by noise score.

    ``min_hits`` filters out rules with too few observations for the
    ratio to mean anything — a rule that fired once and got suppressed
    has noise_ratio 1.0, which is statistically meaningless.
    """
    lines: list[str] = []
    lines.append("# Audit self-triage report\n")
    lines.append(
        "Per-rule cumulative statistics across the most recent "
        f"{len(stats.runs)} run(s). Rules sorted by (noise_ratio × hits) "
        "descending so the loudest false-positive sources surface first. "
        f"Rules with fewer than {min_hits} hits are listed separately — "
        "their ratios aren't yet meaningful.\n"
    )

    if stats.runs:
        last = stats.runs[-1]
        first = stats.runs[0]
        lines.append(f"- **First run:** {first.timestamp}")
        lines.append(f"- **Most recent run:** {last.timestamp}")
        lines.append(f"- **Total rules tracked:** {len(stats.rules)}")
        cumulative_hits = sum(r.hits for r in stats.rules.values())
        cumulative_verified = sum(r.verified for r in stats.rules.values())
        cumulative_suppressed = sum(r.suppressed for r in stats.rules.values())
        lines.append(
            f"- **Cumulative:** {cumulative_hits} hits, "
            f"{cumulative_verified} verified, "
            f"{cumulative_suppressed} suppressed, "
            f"{max(0, cumulative_hits - cumulative_verified - cumulative_suppressed)} unresolved\n"
        )

    qualified = [r for r in stats.rules.values() if r.hits >= min_hits]
    qualified.sort(key=lambda r: (-r.noise_ratio * r.hits, -r.hits))
    pending = [r for r in stats.rules.values() if r.hits < min_hits]
    pending.sort(key=lambda r: (-r.hits, r.rule_id))

    if qualified:
        lines.append(f"## Ranked rules (≥{min_hits} hits)\n")
        lines.append("| Rule | Hits | Verified | Suppressed | Unresolved | Noise % | Actionable % |")
        lines.append("|------|-----:|---------:|-----------:|-----------:|--------:|-------------:|")
        for r in qualified:
            noise = f"{r.noise_ratio * 100:.0f}"
            actionable = f"{r.actionable_ratio * 100:.0f}"
            lines.append(
                f"| `{r.rule_id}` | {r.hits} | {r.verified} | "
                f"{r.suppressed} | {r.unresolved} | {noise} | {actionable} |"
            )
        lines.append("")

    if pending:
        lines.append(f"## Not yet ranked (<{min_hits} hits)\n")
        for r in pending:
            lines.append(
                f"- `{r.rule_id}` — {r.hits} hit(s), "
                f"{r.verified} verified, {r.suppressed} suppressed"
            )
        lines.append("")

    # Actionable recommendations surface automatically.
    loudest = [r for r in qualified if r.noise_ratio >= 0.9 and r.hits >= 10]
    if loudest:
        lines.append("## Recommended actions\n")
        lines.append(
            "Rules below have noise ratio ≥ 90 % over ≥ 10 observations. "
            "Strong candidates for tightening (exclude_pattern) or severity "
            "demotion. Treat this list as the automated equivalent of the "
            "hand-written `AUDIT_TOOL_IMPROVEMENTS.md` triage doc — the "
            "tool is telling you which of its own rules have earned their "
            "keep and which haven't.\n"
        )
        for r in loudest:
            lines.append(
                f"- `{r.rule_id}` — **{r.noise_ratio * 100:.0f} % noise** "
                f"over {r.hits} hits. Consider adding an exclude_pattern, "
                f"tightening the regex, or demoting the default severity."
            )
        lines.append("")

    if not stats.rules:
        lines.append("*No stats yet — run the audit at least once.*\n")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Phase 2 — feedback-driven severity demotion.
# Reads accumulated stats and produces a demotion map that the runner
# applies to the next run's findings. Policy is explicit, configurable,
# and logged so the user can always see WHY a rule got demoted.
# ---------------------------------------------------------------------------

# Default policy. Matches what AUDIT_TOOL_IMPROVEMENTS.md called
# "strong candidates for demotion": ≥90% noise over ≥10 hits with zero
# verified findings. Tightened further: any `verified` hit blocks
# demotion because it means the rule HAS produced at least one real
# signal, and silencing it would hide a proven-real category.
DEFAULT_DEMOTION_POLICY: dict[str, Any] = {
    "enabled": True,
    "min_hits": 10,
    "noise_threshold": 0.9,
    "demote_steps": 1,
    "require_zero_verified": True,
    "exempt": [],           # list of rule_ids never demoted
}


def compute_demotions(
    stats: AuditStats,
    policy: dict[str, Any] | None = None,
) -> dict[str, int]:
    """Return ``{rule_id: severity_steps_to_demote}`` for rules meeting policy.

    The steps value is always positive (Severity is IntEnum where higher
    values = lower severity, so "demote by N steps" = ``severity += N``,
    clamped at ``Severity.INFO``). Rules that don't meet policy do not
    appear in the result.

    Policy shape (all optional, merged over DEFAULT_DEMOTION_POLICY):
      - ``enabled`` (bool): master switch. Default True.
      - ``min_hits`` (int): minimum hits before a rule is considered.
      - ``noise_threshold`` (float, 0-1): noise_ratio must be ≥ this.
      - ``demote_steps`` (int): how many severity steps to drop.
      - ``require_zero_verified`` (bool): if True, any verified hit
        blocks demotion. Safety default — a rule that fired 50 times
        with 49 suppressed and 1 verified is still producing real
        signal; silencing it loses that signal.
      - ``exempt`` (list[str]): rule_ids that are never demoted.

    Returns an empty dict when the policy is disabled or no rules qualify.
    """
    merged = dict(DEFAULT_DEMOTION_POLICY)
    if policy:
        merged.update({k: v for k, v in policy.items() if v is not None})

    if not merged.get("enabled", True):
        return {}

    min_hits = int(merged.get("min_hits", 10))
    threshold = float(merged.get("noise_threshold", 0.9))
    steps = int(merged.get("demote_steps", 1))
    require_zero_verified = bool(merged.get("require_zero_verified", True))
    exempt = set(merged.get("exempt", []) or [])

    if steps <= 0:
        return {}

    out: dict[str, int] = {}
    for rid, rule in stats.rules.items():
        if rid in exempt:
            continue
        if rule.hits < min_hits:
            continue
        if require_zero_verified and rule.verified > 0:
            continue
        if rule.noise_ratio < threshold:
            continue
        out[rid] = steps
    return out


# ---------------------------------------------------------------------------
# Phase 3 — propose-fix layer.
#
# Where Phase 2 *silences* a pure-noise rule (≥90 % noise, no real signal) by
# demoting its severity, Phase 3 proposes a *surgical* fix for rules that are
# noisy but still produce real hits: it mines the rule's false-positive set for
# a common textual signature and suggests an ``exclude_pattern`` addition that
# would drop the FP class while keeping the genuine findings. The output is
# advisory — a markdown file the maintainer reviews, never an automatic config
# edit (the analog of the LLM-as-prior framing in
# ``docs/research/formula_workbench_self_learning_design.md`` §3.6: the tool is
# excellent at *narrowing the search* for a fix, not at applying it blind).
#
# Scope: only tier-2 *pattern* rules carry an ``exclude_pattern`` knob, and only
# their findings put the matched source text in ``Finding.detail`` (see
# ``tier2_patterns.py`` — ``detail = line_text.strip()[:200]``). Tier-1
# (cppcheck / clang-tidy) and tier-4 (structural) rules have no exclude-pattern
# to tune, so they are out of scope here.
# ---------------------------------------------------------------------------

# Default policy. min_runs gates on accumulated history ("after N runs");
# noise_threshold sits BELOW Phase 2's 0.9 because the interesting band for a
# *tightening* suggestion is the partially-noisy rule (still has real signal,
# so demotion would lose it). A signature must appear in a strong majority of
# the rule's current-run FPs to be proposed — a token that shows up in one FP
# is coincidence, not a class.
DEFAULT_PROPOSE_POLICY: dict[str, Any] = {
    "enabled": True,
    "min_runs": 3,              # need some history before proposing
    "min_hits": 10,             # historical observations across runs
    "noise_threshold": 0.5,     # propose for ≥50 %-noise rules
    "min_fp": 3,                # need ≥3 current-run FPs to mine a signature
    "min_support": 0.6,         # a signature must appear in ≥60 % of those FPs
    "min_support_count": 2,     # …and in at least this many FPs (small-set guard)
    "max_signatures_per_rule": 3,
    "max_rules": 20,
    "min_token_len": 4,         # shorter tokens are too generic to discriminate
    "output_file": ".audit_propose_fixes.md",
}

# Identifier / namespace-qualified token, e.g. ``JPH::Body``, ``make_unique``,
# ``tinygltf``. These are the substrings a maintainer would add to an
# ``exclude_pattern`` alternation to drop a false-positive class.
_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*")

# Ultra-common C/C++ tokens that would dominate a frequency count without
# discriminating the FP class — never proposed as a signature on their own.
_TOKEN_STOPWORDS = frozenset({
    "const", "void", "int", "float", "double", "char", "bool", "auto", "return",
    "if", "else", "for", "while", "switch", "case", "break", "continue", "this",
    "true", "false", "null", "nullptr", "static", "inline", "struct", "class",
    "public", "private", "protected", "namespace", "using", "template", "typename",
    "size_t", "uint32_t", "int32_t", "std", "new", "delete", "sizeof",
})


@dataclass
class PatternProposal:
    """One proposed ``exclude_pattern`` addition for a noisy tier-2 rule."""
    rule_id: str
    category: str
    signatures: list[str]          # tokens to add, highest-support first
    fp_count: int                  # rule's false positives in the current run
    support: dict[str, int]        # signature → how many FPs contain it
    hits: int                      # historical hits (cumulative)
    noise_ratio: float             # historical noise ratio (cumulative)
    existing_exclude: str          # current exclude_pattern ("" if none)
    samples: list[str]             # up to a few example matched lines

    @property
    def suggested_exclude(self) -> str:
        """The existing exclude_pattern extended with the new signatures.

        Each signature is regex-escaped so the suggestion is a valid pattern
        the maintainer can paste verbatim. Alternation order is existing-first
        so the diff reads as an append.
        """
        parts = [self.existing_exclude] if self.existing_exclude else []
        parts.extend(re.escape(s) for s in self.signatures)
        return "|".join(parts)


def _candidate_signatures(token: str, min_len: int) -> list[str]:
    """Expand one matched token into the exclude-pattern candidates it implies.

    A bare identifier (``make_unique``, ``tinygltf``) is its own candidate when
    long enough. A namespace-qualified name (``JPH::Body``) also yields each
    namespace prefix *with the trailing* ``::`` (``JPH::``) — the idiomatic
    exclude form a maintainer writes to drop a whole namespace's worth of false
    positives — plus the full token. Prefixes are kept regardless of length
    because a namespace identifier is meaningful even when short (e.g. ``JPH``).
    """
    if "::" in token:
        segs = token.split("::")
        out = ["::".join(segs[:i]) + "::" for i in range(1, len(segs))]
        out.append(token)
        return out
    return [token] if len(token) >= min_len else []


def _mine_signatures(
    details: list[str],
    existing_exclude: str,
    policy: dict[str, Any],
) -> tuple[list[str], dict[str, int]]:
    """Return (signatures, support) mined from a rule's FP matched-text set.

    A signature is a token (or namespace prefix) appearing in a strong majority
    of the FP lines that is not already matched by the existing exclude regex
    and not a generic stopword. ``support[sig]`` is the count of FP lines
    containing it. Redundant longer candidates are dropped when a more general
    candidate (a substring of them) is already selected — ``JPH::`` wins over
    ``JPH::Body`` so the suggestion drops the whole class, not one case.
    """
    min_len = int(policy.get("min_token_len", 4))
    min_support_frac = float(policy.get("min_support", 0.6))
    min_support_count = int(policy.get("min_support_count", 2))
    max_sigs = int(policy.get("max_signatures_per_rule", 3))

    existing_re = None
    if existing_exclude:
        try:
            existing_re = re.compile(existing_exclude)
        except re.error:
            existing_re = None

    # Count how many distinct FP lines each candidate signature appears in.
    support: dict[str, int] = {}
    for text in details:
        seen: set[str] = set()
        for m in _TOKEN_RE.finditer(text):
            for cand in _candidate_signatures(m.group(0), min_len):
                if cand in seen:
                    continue
                # A candidate already caught by the existing exclude regex is,
                # by definition, not the reason these FPs slipped through.
                if existing_re is not None and existing_re.search(cand):
                    continue
                if cand.lower() in _TOKEN_STOPWORDS:
                    continue
                seen.add(cand)
                support[cand] = support.get(cand, 0) + 1

    fp_count = len(details)
    threshold = max(min_support_count, int(min_support_frac * fp_count + 0.999))
    qualified = [(c, n) for c, n in support.items() if n >= threshold]
    # Deterministic: highest support first, ties broken alphabetically (which
    # also sorts a general prefix ahead of the full token it prefixes).
    qualified.sort(key=lambda kv: (-kv[1], kv[0]))

    kept: list[str] = []
    for cand, _ in qualified:
        if any(k in cand for k in kept):
            continue  # a more general signature already covers this one
        kept.append(cand)
        if len(kept) >= max_sigs:
            break
    return kept, {c: support[c] for c in kept}


def compute_proposals(
    stats: AuditStats,
    findings: list[Finding],
    suppressed_keys: set[str],
    patterns: dict[str, list[dict]],
    policy: dict[str, Any] | None = None,
) -> list[PatternProposal]:
    """Propose ``exclude_pattern`` additions for noisy tier-2 pattern rules.

    Phase 3 of the self-learning loop. Inputs:
      - ``stats``: cumulative counters (already including the current run) —
        used to gate on history (``min_runs``) and per-rule noise.
      - ``findings``: the full pre-suppress-filter finding list of the current
        run (so the suppressed findings' matched text is still present).
      - ``suppressed_keys``: dedup_keys from ``.audit_suppress`` — a finding in
        this set is a confirmed false positive.
      - ``patterns``: ``config.patterns`` — ``{category: [{name, exclude_pattern,
        ...}]}``. Only rules present here are in scope (they have the knob).

    Returns a deterministically-ordered list of proposals (empty when the
    policy is disabled, history is too short, or no rule clears the gates).
    """
    merged = dict(DEFAULT_PROPOSE_POLICY)
    if policy:
        merged.update({k: v for k, v in policy.items() if v is not None})

    if not merged.get("enabled", True):
        return []
    if len(stats.runs) < int(merged.get("min_runs", 3)):
        return []

    min_hits = int(merged.get("min_hits", 10))
    noise_threshold = float(merged.get("noise_threshold", 0.5))
    min_fp = int(merged.get("min_fp", 3))
    max_rules = int(merged.get("max_rules", 20))

    # Map rule_id (pattern name) → (category, exclude_pattern). Only tier-2
    # pattern rules carry an exclude_pattern knob and are in scope.
    rule_category: dict[str, str] = {}
    rule_exclude: dict[str, str] = {}
    for category, pats in (patterns or {}).items():
        for pat in pats or []:
            name = pat.get("name", "")
            if not name:
                continue
            rule_category[name] = category
            rule_exclude[name] = pat.get("exclude_pattern", "") or ""

    # Collect this run's false positives per in-scope rule.
    fp_details: dict[str, list[str]] = {}
    for f in findings:
        if f.dedup_key not in suppressed_keys:
            continue
        rid = _rule_id(f)
        if rid not in rule_category:
            continue
        fp_details.setdefault(rid, []).append(f.detail or f.title)

    proposals: list[PatternProposal] = []
    for rid, details in fp_details.items():
        if len(details) < min_fp:
            continue
        rule = stats.rules.get(rid)
        if rule is None or rule.hits < min_hits:
            continue
        if rule.noise_ratio < noise_threshold:
            continue
        sigs, support = _mine_signatures(details, rule_exclude.get(rid, ""), merged)
        if not sigs:
            continue
        samples = [d for d in details if any(s in d for s in sigs)][:3]
        proposals.append(PatternProposal(
            rule_id=rid,
            category=rule_category.get(rid, ""),
            signatures=sigs,
            fp_count=len(details),
            support=support,
            hits=rule.hits,
            noise_ratio=rule.noise_ratio,
            existing_exclude=rule_exclude.get(rid, ""),
            samples=samples,
        ))

    # Loudest rules first (noise_ratio × hits), ties by rule_id.
    proposals.sort(key=lambda p: (-(p.noise_ratio * p.hits), p.rule_id))
    return proposals[:max_rules]


def render_proposals_markdown(proposals: list[PatternProposal]) -> str:
    """Render Phase 3 proposals as a maintainer-facing markdown report."""
    lines: list[str] = []
    lines.append("# Audit propose-fix report\n")
    lines.append(
        "Phase 3 of the audit self-learning loop. Each section is a tier-2 "
        "pattern rule whose **false-positive set shares a textual signature** "
        "not yet in its `exclude_pattern`. Adding the suggested signature to "
        "`audit_config.yaml` would drop that false-positive class while keeping "
        "the rule's real findings — a surgical alternative to demoting the rule "
        "outright (Phase 2). These are **suggestions**: review the samples and "
        "the proposed pattern before applying.\n"
    )

    if not proposals:
        lines.append(
            "*No proposals — every noisy rule's false positives are too "
            "varied to share a signature, or no rule cleared the history / "
            "noise gates.*\n"
        )
        return "\n".join(lines)

    for p in proposals:
        lines.append(f"## `{p.rule_id}` ({p.category})\n")
        lines.append(
            f"- **{p.noise_ratio * 100:.0f} % noise** over {p.hits} cumulative "
            f"hits; {p.fp_count} false positive(s) in the latest run."
        )
        sig_list = ", ".join(
            f"`{s}` (in {p.support.get(s, 0)}/{p.fp_count})" for s in p.signatures
        )
        lines.append(f"- **Proposed signature(s):** {sig_list}")
        if p.existing_exclude:
            lines.append(f"- **Current `exclude_pattern`:** `{p.existing_exclude}`")
        else:
            lines.append("- **Current `exclude_pattern`:** *(none)*")
        lines.append(f"- **Suggested `exclude_pattern`:** `{p.suggested_exclude}`")
        if p.samples:
            lines.append("- **Sample false positives:**")
            for s in p.samples:
                lines.append(f"  - `{s}`")
        lines.append("")

    return "\n".join(lines)

