"""Differential reporting -- compare current audit against previous."""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .findings import Finding

log = logging.getLogger("audit")


@dataclass
class ReportDiff:
    """Result of comparing the current audit run against a previous one."""

    new_findings: list[dict] = field(default_factory=list)
    resolved_findings: list[dict] = field(default_factory=list)
    persistent_count: int = 0
    previous_path: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "new_findings": self.new_findings,
            "resolved_findings": self.resolved_findings,
            "persistent_count": self.persistent_count,
            "previous_path": self.previous_path,
        }


def load_previous_results(
    report_dir: Path,
    current_stem: str,
) -> list[dict] | None:
    """Find the most recent ``*_results.json`` sidecar, excluding the current one.

    Returns the parsed findings list or ``None`` if no previous results exist.
    """
    if not report_dir.is_dir():
        return None

    candidates = sorted(
        (
            p
            for p in report_dir.glob("*_results.json")
            if p.stem != current_stem
        ),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )

    if not candidates:
        log.info("No previous results sidecar found in %s", report_dir)
        return None

    latest = candidates[0]
    log.info("Loading previous results from %s", latest)
    try:
        data = json.loads(latest.read_text())
        if isinstance(data, list):
            return data
        # Handle wrapper dict with a "findings" key
        if isinstance(data, dict) and "findings" in data:
            return data["findings"]
        log.warning("Unexpected JSON structure in %s", latest)
        return None
    except (json.JSONDecodeError, OSError) as exc:
        log.warning("Failed to load previous results from %s: %s", latest, exc)
        return None


def compute_diff(
    previous: list[dict],
    current: list[Finding],
) -> ReportDiff:
    """Compare *previous* (raw dicts with ``dedup_key``) against *current* findings.

    Returns a :class:`ReportDiff` describing new, resolved, and persistent
    findings.
    """
    prev_by_key: dict[str, dict] = {}
    for entry in previous:
        key = entry.get("dedup_key", "")
        if key:
            prev_by_key[key] = entry

    curr_by_key: dict[str, dict] = {}
    for f in current:
        curr_by_key[f.dedup_key] = f.to_dict()

    prev_keys = set(prev_by_key.keys())
    curr_keys = set(curr_by_key.keys())

    new_keys = curr_keys - prev_keys
    resolved_keys = prev_keys - curr_keys
    persistent_keys = prev_keys & curr_keys

    return ReportDiff(
        new_findings=[curr_by_key[k] for k in sorted(new_keys)],
        resolved_findings=[prev_by_key[k] for k in sorted(resolved_keys)],
        persistent_count=len(persistent_keys),
    )
