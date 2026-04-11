"""Finding trend tracking — compare findings across multiple audit runs."""

from __future__ import annotations

import json
import logging
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

from .findings import Finding, Severity

log = logging.getLogger("audit")


@dataclass
class TrendSnapshot:
    """A point-in-time snapshot of audit findings for trend analysis."""
    timestamp: str = ""
    finding_count: int = 0
    by_severity: dict[str, int] = field(default_factory=dict)
    by_category: dict[str, int] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        """Serialize to a JSON-compatible dict."""
        return {
            "timestamp": self.timestamp,
            "finding_count": self.finding_count,
            "by_severity": self.by_severity,
            "by_category": self.by_category,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "TrendSnapshot":
        """Deserialize from a dict."""
        return cls(
            timestamp=data.get("timestamp", ""),
            finding_count=data.get("finding_count", 0),
            by_severity=data.get("by_severity", {}),
            by_category=data.get("by_category", {}),
        )


@dataclass
class TrendReport:
    """Summary of finding trends across multiple audit runs."""
    direction: str = "stable"  # "improving", "worsening", or "stable"
    categories: dict[str, str] = field(default_factory=dict)
    finding_delta: int = 0

    def to_dict(self) -> dict[str, Any]:
        """Serialize to a JSON-compatible dict."""
        return {
            "direction": self.direction,
            "categories": self.categories,
            "finding_delta": self.finding_delta,
        }


def save_snapshot(
    results_dir: Path,
    findings: list[Finding],
) -> TrendSnapshot:
    """Save a JSON snapshot of current findings to results_dir.

    Returns the saved :class:`TrendSnapshot`.
    """
    now = datetime.now()
    timestamp = now.isoformat()

    by_severity: dict[str, int] = defaultdict(int)
    by_category: dict[str, int] = defaultdict(int)
    for f in findings:
        by_severity[f.severity.name.lower()] += 1
        by_category[f.category] += 1

    snapshot = TrendSnapshot(
        timestamp=timestamp,
        finding_count=len(findings),
        by_severity=dict(by_severity),
        by_category=dict(by_category),
    )

    results_dir.mkdir(parents=True, exist_ok=True)
    filename = f"trend_snapshot_{now.strftime('%Y%m%d_%H%M%S')}.json"
    path = results_dir / filename
    path.write_text(json.dumps(snapshot.to_dict(), indent=2))
    log.info("Trend snapshot saved: %s (%d findings)", path, len(findings))

    return snapshot


def load_snapshots(
    results_dir: Path,
    max_count: int = 20,
) -> list[TrendSnapshot]:
    """Load recent trend snapshots from results_dir, newest first.

    Returns up to *max_count* snapshots sorted by timestamp descending.
    """
    if not results_dir.is_dir():
        return []

    files = sorted(
        results_dir.glob("trend_snapshot_*.json"),
        key=lambda p: p.name,
        reverse=True,
    )

    snapshots: list[TrendSnapshot] = []
    for path in files[:max_count]:
        try:
            data = json.loads(path.read_text())
            snapshots.append(TrendSnapshot.from_dict(data))
        except (json.JSONDecodeError, OSError, KeyError) as e:
            log.warning("Skipping corrupt trend snapshot %s: %s", path, e)

    return snapshots


def compute_trends(snapshots: list[TrendSnapshot]) -> TrendReport:
    """Compare snapshots to classify finding trends per category.

    Requires at least 2 snapshots.  Compares the newest against the oldest
    in the list to determine direction.
    """
    if len(snapshots) < 2:
        return TrendReport(direction="stable", finding_delta=0)

    # snapshots are newest-first; compare newest vs oldest
    newest = snapshots[0]
    oldest = snapshots[-1]

    finding_delta = newest.finding_count - oldest.finding_count

    # Overall direction
    if finding_delta < 0:
        direction = "improving"
    elif finding_delta > 0:
        direction = "worsening"
    else:
        direction = "stable"

    # Per-category direction
    all_cats = set(newest.by_category.keys()) | set(oldest.by_category.keys())
    categories: dict[str, str] = {}
    for cat in sorted(all_cats):
        new_count = newest.by_category.get(cat, 0)
        old_count = oldest.by_category.get(cat, 0)
        if new_count < old_count:
            categories[cat] = "improving"
        elif new_count > old_count:
            categories[cat] = "worsening"
        else:
            categories[cat] = "stable"

    return TrendReport(
        direction=direction,
        categories=categories,
        finding_delta=finding_delta,
    )
