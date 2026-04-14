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
    """A point-in-time snapshot of audit findings for trend analysis.

    Baseline metadata fields (build_ok, tests_*, total_loc, file_count,
    duration_seconds) were added in 2.2.0 (D5 — Baseline Comparison).
    They are optional and default to neutral values so that snapshots
    written by older audit runs deserialize cleanly.
    """
    timestamp: str = ""
    finding_count: int = 0
    by_severity: dict[str, int] = field(default_factory=dict)
    by_category: dict[str, int] = field(default_factory=dict)
    # ---- Baseline metadata (D5) -----------------------------------------
    # build_ok=None means "not captured" (legacy snapshot pre-2.2.0);
    # True/False means the build outcome was recorded.
    build_ok: bool | None = None
    build_warnings: int = 0
    build_errors: int = 0
    tests_passed: int = 0
    tests_failed: int = 0
    tests_skipped: int = 0
    total_loc: int = 0
    file_count: int = 0
    duration_seconds: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        """Serialize to a JSON-compatible dict."""
        return {
            "timestamp": self.timestamp,
            "finding_count": self.finding_count,
            "by_severity": self.by_severity,
            "by_category": self.by_category,
            "build_ok": self.build_ok,
            "build_warnings": self.build_warnings,
            "build_errors": self.build_errors,
            "tests_passed": self.tests_passed,
            "tests_failed": self.tests_failed,
            "tests_skipped": self.tests_skipped,
            "total_loc": self.total_loc,
            "file_count": self.file_count,
            "duration_seconds": self.duration_seconds,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "TrendSnapshot":
        """Deserialize from a dict.

        Missing baseline fields default to neutral values so legacy
        snapshot files (pre-2.2.0) load cleanly. ``build_ok`` defaults
        to ``None`` to distinguish "not captured" from "build failed".
        """
        return cls(
            timestamp=data.get("timestamp", ""),
            finding_count=data.get("finding_count", 0),
            by_severity=data.get("by_severity", {}),
            by_category=data.get("by_category", {}),
            build_ok=data.get("build_ok", None),
            build_warnings=data.get("build_warnings", 0),
            build_errors=data.get("build_errors", 0),
            tests_passed=data.get("tests_passed", 0),
            tests_failed=data.get("tests_failed", 0),
            tests_skipped=data.get("tests_skipped", 0),
            total_loc=data.get("total_loc", 0),
            file_count=data.get("file_count", 0),
            duration_seconds=data.get("duration_seconds", 0.0),
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


@dataclass
class BaselineComparison:
    """Run-over-run delta between the most recent two audits.

    D5 — produced by :func:`compute_baseline_comparison` from a list of
    :class:`TrendSnapshot` (newest first) and rendered by the markdown
    report builder so reviewers see at a glance what changed since the
    previous run, without having to diff JSON sidecars by hand.

    Severity and category deltas are *current minus previous*: a
    positive number means more findings now than before (worse), a
    negative number means fewer (better). ``build_status_change``
    encodes the build-OK transition as a short string for prose
    rendering. ``previous_captured_baseline`` is False when the previous
    snapshot was a legacy (pre-2.2.0) record without baseline metadata
    — in that case build/tests/loc deltas are not meaningful and the
    renderer should suppress them.
    """
    previous_timestamp: str = ""
    current_timestamp: str = ""
    finding_delta: int = 0
    severity_deltas: dict[str, int] = field(default_factory=dict)
    category_deltas: dict[str, int] = field(default_factory=dict)
    build_status_change: str = ""  # "OK", "FAILED", "OK→FAILED", "FAILED→OK", or ""
    test_passed_delta: int = 0
    test_failed_delta: int = 0
    loc_delta: int = 0
    file_count_delta: int = 0
    duration_delta: float = 0.0
    previous_captured_baseline: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "previous_timestamp": self.previous_timestamp,
            "current_timestamp": self.current_timestamp,
            "finding_delta": self.finding_delta,
            "severity_deltas": self.severity_deltas,
            "category_deltas": self.category_deltas,
            "build_status_change": self.build_status_change,
            "test_passed_delta": self.test_passed_delta,
            "test_failed_delta": self.test_failed_delta,
            "loc_delta": self.loc_delta,
            "file_count_delta": self.file_count_delta,
            "duration_delta": self.duration_delta,
            "previous_captured_baseline": self.previous_captured_baseline,
        }


def save_snapshot(
    results_dir: Path,
    findings: list[Finding],
    keep: int | None = None,
    tier1_summary: dict | None = None,
    audit_data: Any = None,
    duration: float = 0.0,
) -> TrendSnapshot:
    """Save a JSON snapshot of current findings to results_dir.

    Returns the saved :class:`TrendSnapshot`.

    AUDIT.md §L9 / FIXPLAN I5: *keep* caps the number of trend snapshots
    retained on disk. Older files are unlinked after the new snapshot
    lands. Pass ``None`` (the default) to preserve legacy behaviour and
    keep every snapshot ever taken; pass a positive integer to enable
    rolling retention.

    D5 (2.2.0): *tier1_summary*, *audit_data* and *duration* are optional
    and capture baseline metadata onto the snapshot so the next run can
    render a Baseline Comparison section. Older callers that omit these
    keep working — the snapshot just won't carry baseline info, which
    the comparison renderer detects and suppresses cleanly.
    """
    now = datetime.now()
    timestamp = now.isoformat()

    by_severity: dict[str, int] = defaultdict(int)
    by_category: dict[str, int] = defaultdict(int)
    for f in findings:
        by_severity[f.severity.name.lower()] += 1
        by_category[f.category] += 1

    # Extract baseline metadata. tier1_summary may be missing keys (e.g.
    # when only Tier 2-5 are run); fall back to defaults silently rather
    # than misreporting "build OK" when no build was attempted.
    build_info = (tier1_summary or {}).get("build") or {}
    test_info = (tier1_summary or {}).get("tests") or {}
    total_loc = getattr(audit_data, "total_loc", 0) if audit_data else 0
    file_count = getattr(audit_data, "file_count", 0) if audit_data else 0

    snapshot = TrendSnapshot(
        timestamp=timestamp,
        finding_count=len(findings),
        by_severity=dict(by_severity),
        by_category=dict(by_category),
        build_ok=build_info.get("build_ok"),
        build_warnings=build_info.get("warnings", 0),
        build_errors=build_info.get("errors", 0),
        tests_passed=test_info.get("passed", 0),
        tests_failed=test_info.get("failed", 0),
        tests_skipped=test_info.get("skipped", 0),
        total_loc=total_loc,
        file_count=file_count,
        duration_seconds=round(duration, 1),
    )

    results_dir.mkdir(parents=True, exist_ok=True)
    filename = f"trend_snapshot_{now.strftime('%Y%m%d_%H%M%S')}.json"
    path = results_dir / filename
    path.write_text(json.dumps(snapshot.to_dict(), indent=2))
    log.info("Trend snapshot saved: %s (%d findings)", path, len(findings))

    if keep is not None and keep > 0:
        _prune_old_snapshots(results_dir, keep)

    return snapshot


def _prune_old_snapshots(results_dir: Path, keep: int) -> None:
    """Delete all but the newest *keep* trend snapshots.

    Invoked from :func:`save_snapshot` when the caller opts in via the
    ``keep`` parameter. The most-recent snapshot (the one just written)
    is always retained; older files are unlinked. Errors are logged
    rather than raised so a filesystem glitch cannot hide a fresh
    snapshot that just saved successfully.
    """
    try:
        all_snaps = sorted(
            results_dir.glob("trend_snapshot_*.json"),
            key=lambda p: p.name,
            reverse=True,
        )
    except OSError as exc:
        log.warning("Could not enumerate snapshots for pruning: %s", exc)
        return

    stale = all_snaps[keep:]
    for p in stale:
        try:
            p.unlink()
            log.info("Pruned old snapshot: %s", p)
        except OSError as exc:
            log.warning("Could not delete stale snapshot %s: %s", p, exc)


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


def compute_baseline_comparison(
    snapshots: list[TrendSnapshot],
) -> "BaselineComparison | None":
    """Compute a previous-vs-current baseline delta from snapshots.

    Compares ``snapshots[0]`` (newest, the run that just finished) against
    ``snapshots[1]`` (the immediately preceding run). Returns ``None`` if
    fewer than 2 snapshots exist — the report renderer treats ``None`` as
    "first run, nothing to compare against" and skips the section.

    Unlike :func:`compute_trends` (which contrasts newest vs *oldest* and
    is best read as a long-horizon trajectory), this function is the
    immediate run-over-run delta — the answer to "what changed since
    last audit?".
    """
    if len(snapshots) < 2:
        return None

    current = snapshots[0]
    previous = snapshots[1]

    # Severity deltas — current minus previous, only emit non-zero entries
    # to keep the report compact. Iterate the union so newly-introduced
    # severities are accounted for.
    all_sevs = set(current.by_severity.keys()) | set(previous.by_severity.keys())
    severity_deltas: dict[str, int] = {}
    for sev in all_sevs:
        delta = current.by_severity.get(sev, 0) - previous.by_severity.get(sev, 0)
        if delta != 0:
            severity_deltas[sev] = delta

    # Category deltas — same shape as severity, used for "top movers".
    all_cats = set(current.by_category.keys()) | set(previous.by_category.keys())
    category_deltas: dict[str, int] = {}
    for cat in all_cats:
        delta = current.by_category.get(cat, 0) - previous.by_category.get(cat, 0)
        if delta != 0:
            category_deltas[cat] = delta

    # Build status change. Only meaningful when both snapshots captured
    # build_ok (i.e. neither is a legacy pre-2.2.0 record). Encode as a
    # short string so the renderer can paste it directly.
    build_status_change = ""
    if current.build_ok is not None and previous.build_ok is not None:
        prev_label = "OK" if previous.build_ok else "FAILED"
        curr_label = "OK" if current.build_ok else "FAILED"
        if prev_label == curr_label:
            build_status_change = curr_label  # unchanged
        else:
            build_status_change = f"{prev_label}→{curr_label}"
    elif current.build_ok is not None:
        # Current run captured build but previous didn't — surface the
        # current state without claiming a transition.
        build_status_change = "OK" if current.build_ok else "FAILED"

    previous_captured = previous.build_ok is not None or previous.total_loc > 0

    return BaselineComparison(
        previous_timestamp=previous.timestamp,
        current_timestamp=current.timestamp,
        finding_delta=current.finding_count - previous.finding_count,
        severity_deltas=severity_deltas,
        category_deltas=category_deltas,
        build_status_change=build_status_change,
        test_passed_delta=current.tests_passed - previous.tests_passed,
        test_failed_delta=current.tests_failed - previous.tests_failed,
        loc_delta=current.total_loc - previous.total_loc,
        file_count_delta=current.file_count - previous.file_count,
        duration_delta=round(current.duration_seconds - previous.duration_seconds, 1),
        previous_captured_baseline=previous_captured,
    )
