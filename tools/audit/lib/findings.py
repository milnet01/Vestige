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
    _dedup_key: str = field(default="", repr=False)

    def __post_init__(self) -> None:
        if not self._dedup_key:
            raw = f"{self.file}:{self.line}:{self.category}:{self.title}"
            self._dedup_key = hashlib.sha256(raw.encode()).hexdigest()[:16]

    @property
    def dedup_key(self) -> str:
        return self._dedup_key

    def to_dict(self) -> dict[str, Any]:
        return {
            "file": self.file,
            "line": self.line,
            "severity": self.severity.name.lower(),
            "category": self.category,
            "source_tier": self.source_tier,
            "title": self.title,
            "detail": self.detail,
            "pattern_name": self.pattern_name,
        }


def deduplicate(findings: list[Finding]) -> list[Finding]:
    """Remove duplicate findings, keeping the one from the earliest tier."""
    seen: dict[str, Finding] = {}
    for f in findings:
        if f.dedup_key not in seen:
            seen[f.dedup_key] = f
        else:
            existing = seen[f.dedup_key]
            # Keep the one with richer detail (lower tier = automated tools = richer)
            if f.source_tier < existing.source_tier:
                seen[f.dedup_key] = f
    return sorted(seen.values(), key=lambda f: (f.severity, f.file, f.line or 0))


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

    def to_dict(self) -> dict[str, Any]:
        return {
            "loc_by_subsystem": self.loc_by_subsystem,
            "total_loc": self.total_loc,
            "file_count": self.file_count,
            "gpu_resource_classes": self.gpu_resource_classes,
            "event_lifecycle": self.event_lifecycle,
            "deferred_markers_by_subsystem": self.deferred_by_subsystem,
            "deferred_markers_total": len(self.deferred_markers),
            "large_files": self.large_files,
        }


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
