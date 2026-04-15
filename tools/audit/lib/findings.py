# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Finding data model and severity classification."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field, asdict
from enum import IntEnum
from typing import Any


class Severity(IntEnum):
    """Finding severity levels, ordered for sorting (most severe first)."""
    CRITICAL = 0
    HIGH = 1
    MEDIUM = 2
    LOW = 3
    INFO = 4

    @classmethod
    def from_string(cls, s: str) -> "Severity":
        mapping = {
            "critical": cls.CRITICAL,
            "high": cls.HIGH,
            "medium": cls.MEDIUM,
            "low": cls.LOW,
            "info": cls.INFO,
            # cppcheck mappings
            "error": cls.HIGH,
            "warning": cls.MEDIUM,
            "style": cls.LOW,
            "performance": cls.MEDIUM,
            "portability": cls.MEDIUM,
            "information": cls.INFO,
        }
        return mapping.get(s.lower(), cls.INFO)


@dataclass
class Finding:
    """A single audit finding from any tier."""
    file: str
    line: int | None
    severity: Severity
    category: str
    source_tier: int
    title: str
    detail: str = ""
    pattern_name: str = ""
    # D2 (2.4.0): list of distinct source_keys that independently flagged
    # the same (file, line). Populated by lib.corroboration.corroborate()
    # after dedup. Empty list = solo finding. A single entry means exactly
    # one *other* source also flagged this line.
    corroborated_by: list[str] = field(default_factory=list)
    # D3 (2.5.0): True when a human has confirmed this finding is real
    # (via `--verified-add KEY` or an entry in `.audit_verified`). The tag
    # is a reviewer signal — it does not change severity or filter the
    # finding out. Purpose: distinguish "reviewed, real, still needs fix"
    # from "not yet looked at" across runs.
    verified: bool = False
    _dedup_key: str = field(default="", repr=False)

    def __post_init__(self) -> None:
        if not self._dedup_key:
            raw = f"{self.file}:{self.line}:{self.category}:{self.title}"
            self._dedup_key = hashlib.sha256(raw.encode()).hexdigest()[:16]

    @property
    def dedup_key(self) -> str:
        return self._dedup_key

    @property
    def source_key(self) -> str:
        """Stable identifier for the tool/tier that produced this finding.

        D2 (2.4.0): Used by the corroboration layer to decide whether two
        findings at the same (file, line) came from *independent* sources.
        Two tier-2 pattern hits share `pattern_scan` (same grep-based
        mechanism, not corroboration); cppcheck + clang-tidy at the same
        line are distinct sources and do corroborate each other.
        """
        if self.source_tier == 1:
            if self.category == "cppcheck":
                return "cppcheck"
            if self.category == "clang_tidy":
                return "clang_tidy"
            return "build"
        if self.source_tier == 2:
            return "pattern_scan"
        if self.source_tier == 3:
            return "tier3_diff"
        if self.source_tier == 4:
            # Each tier-4 submodule has its own category, so these remain
            # distinct sources (complexity + cognitive_complexity both
            # flagging a function is genuine corroboration).
            return f"tier4_{self.category}"
        if self.source_tier == 5:
            return "tier5_research"
        return f"tier{self.source_tier}"

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "file": self.file,
            "line": self.line,
            "severity": self.severity.name.lower(),
            "category": self.category,
            "source_tier": self.source_tier,
            "title": self.title,
            "detail": self.detail,
            "pattern_name": self.pattern_name,
            "dedup_key": self.dedup_key,
        }
        # Only surface corroborated_by when it's non-empty — keeps the
        # JSON sidecar and SARIF output compact for the common case of
        # solo findings.
        if self.corroborated_by:
            d["corroborated_by"] = list(self.corroborated_by)
        # D3 (2.5.0): same pattern — only surface `verified` when True so
        # unverified findings keep a compact dict.
        if self.verified:
            d["verified"] = True
        return d


def deduplicate(findings: list[Finding]) -> list[Finding]:
    """Remove duplicate findings, keeping the one from the earliest tier.

    D2 (2.4.0): when two findings share a dedup_key, the kept finding's
    corroborated_by list is merged with the discarded one's. This is
    independent of the cross-source corroboration pass in
    lib.corroboration; it just ensures that any already-set tags
    survive deduplication.

    D3 (2.5.0): the `verified` flag also survives dedup — if either
    finding in the pair was verified, the kept finding retains the tag.
    Verification is a human-review signal; losing it on a tier-ordering
    swap would silently discard reviewer effort.
    """
    seen: dict[str, Finding] = {}
    for f in findings:
        if f.dedup_key not in seen:
            seen[f.dedup_key] = f
        else:
            existing = seen[f.dedup_key]
            # Keep the one with richer detail (lower tier = automated tools = richer)
            if f.source_tier < existing.source_tier:
                # Merge corroboration tags from the discarded finding so
                # information isn't lost on tier-ordering swaps.
                merged = sorted(set(f.corroborated_by) | set(existing.corroborated_by))
                f.corroborated_by = merged
                f.verified = f.verified or existing.verified
                seen[f.dedup_key] = f
            else:
                merged = sorted(set(existing.corroborated_by) | set(f.corroborated_by))
                existing.corroborated_by = merged
                existing.verified = existing.verified or f.verified
    return sorted(seen.values(), key=lambda f: (f.severity, f.file, f.line or 0))


def apply_severity_overrides(
    findings: list[Finding],
    overrides: list[dict],
) -> list[Finding]:
    """Modify finding severities based on pattern_name overrides.

    Each entry in *overrides* should have ``pattern_name`` and ``severity``
    keys.  Findings whose :pyattr:`pattern_name` matches are updated in
    place.  Returns the (mutated) findings list for convenience.
    """
    if not overrides:
        return findings

    # Build a lookup: pattern_name -> target Severity
    override_map: dict[str, Severity] = {}
    for entry in overrides:
        name = entry.get("pattern_name", "")
        sev_str = entry.get("severity", "")
        if name and sev_str:
            override_map[name] = Severity.from_string(sev_str)

    if not override_map:
        return findings

    for finding in findings:
        if finding.pattern_name in override_map:
            finding.severity = override_map[finding.pattern_name]

    return findings


@dataclass
class ChangeSummary:
    """Summary of git changes for Tier 3."""
    changed_files: list[dict[str, Any]] = field(default_factory=list)
    subsystems_touched: list[str] = field(default_factory=list)
    total_added: int = 0
    total_removed: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "changed_files": self.changed_files,
            "subsystems_touched": self.subsystems_touched,
            "total_delta": {"+": self.total_added, "-": self.total_removed},
        }


@dataclass
class AuditData:
    """Collected statistics from Tier 4."""
    loc_by_subsystem: dict[str, int] = field(default_factory=dict)
    total_loc: int = 0
    file_count: int = 0
    gpu_resource_classes: list[dict[str, Any]] = field(default_factory=list)
    event_lifecycle: list[dict[str, Any]] = field(default_factory=list)
    deferred_markers: list[dict[str, Any]] = field(default_factory=list)
    deferred_by_subsystem: dict[str, int] = field(default_factory=dict)
    large_files: list[dict[str, Any]] = field(default_factory=list)
    uniform_sync: dict[str, Any] | None = None
    include_analysis: dict[str, Any] | None = None
    complexity: dict[str, Any] | None = None
    dead_code: dict[str, Any] | None = None
    build_audit: dict[str, Any] | None = None
    duplication: dict[str, Any] | None = None
    refactoring: dict[str, Any] | None = None
    cognitive_complexity: dict[str, Any] | None = None

    def to_dict(self) -> dict[str, Any]:
        d = {
            "loc_by_subsystem": self.loc_by_subsystem,
            "total_loc": self.total_loc,
            "file_count": self.file_count,
            "gpu_resource_classes": self.gpu_resource_classes,
            "event_lifecycle": self.event_lifecycle,
            "deferred_markers_by_subsystem": self.deferred_by_subsystem,
            "deferred_markers_total": len(self.deferred_markers),
            "large_files": self.large_files,
        }
        if self.uniform_sync:
            d["uniform_sync"] = self.uniform_sync
        if self.include_analysis:
            d["include_analysis"] = self.include_analysis
        if self.complexity:
            d["complexity"] = self.complexity
        if self.dead_code:
            d["dead_code"] = self.dead_code
        if self.build_audit:
            d["build_audit"] = self.build_audit
        if self.duplication:
            d["duplication"] = self.duplication
        if self.refactoring:
            d["refactoring"] = self.refactoring
        if self.cognitive_complexity:
            d["cognitive_complexity"] = self.cognitive_complexity
        return d


@dataclass
class ResearchResult:
    """A web research result from Tier 5."""
    query: str
    results: list[dict[str, str]] = field(default_factory=list)
    cached: bool = False
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"query": self.query, "results": self.results}
        if self.cached:
            d["cached"] = True
        if self.error:
            d["error"] = self.error
        return d
