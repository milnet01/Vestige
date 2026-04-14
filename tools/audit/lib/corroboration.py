"""Cross-source corroboration layer (D2, 2.4.0).

A finding gains credibility when more than one *independent* source
flags the same line of code. This module:

  * Tags findings with ``corroborated_by`` when ≥2 distinct
    ``source_key`` values flag the same (file, line).
  * Optionally promotes severity by ``promote_level`` steps for every
    corroborated finding (``promote_level=1`` by default).
  * Demotes *solo* (uncorroborated) hits whose ``pattern_name`` appears
    in ``demoted_patterns`` — the known-noisy tier-2 patterns where a
    single match is rarely actionable (std::endl, push_back, TODO
    markers, etc).

Rationale: the manual audit prompt explicitly asks the agent to weight
findings by how many independent signals converge on the same spot.
The automated tool should do the same mechanically so the downstream
LLM consumer isn't drowned in single-source noise.

Severity promotion happens *before* ``apply_severity_overrides`` in the
runner, so explicit user overrides still win.
"""

from __future__ import annotations

import logging
from collections import defaultdict
from typing import Any

from .findings import Finding, Severity

log = logging.getLogger("audit")


# Promoting INFO → LOW, LOW → MEDIUM, etc. is a reduction in the
# integer value (CRITICAL=0 is most severe). We clamp promotions at
# CRITICAL so already-critical findings don't wrap around.
def _promote(sev: Severity, levels: int) -> Severity:
    if levels <= 0:
        return sev
    new_val = max(int(Severity.CRITICAL), int(sev) - levels)
    return Severity(new_val)


def corroborate(
    findings: list[Finding],
    config: dict[str, Any] | None,
) -> list[Finding]:
    """Tag, promote, and demote findings based on cross-source signal.

    Mutates findings in place and returns the same list for
    convenience. Expects findings to already be deduplicated; grouping
    across raw duplicates would inflate the source-count.

    Config shape::

        corroboration:
          enabled: true
          promote_level: 1
          demoted_patterns: ["std_endl", "push_back_loop", "todo_fixme"]

    When ``enabled`` is False, this function is a no-op.
    """
    cfg = config or {}
    if not cfg.get("enabled", True):
        return findings

    promote_level = int(cfg.get("promote_level", 1))
    demoted_patterns = set(cfg.get("demoted_patterns", []) or [])

    # Group by (file, line). Findings without a line number can't be
    # corroborated positionally (we don't know where they are), so they
    # skip both promotion and demotion.
    groups: dict[tuple[str, int], list[Finding]] = defaultdict(list)
    for f in findings:
        if f.line is None or not f.file:
            continue
        groups[(f.file, f.line)].append(f)

    promoted = 0
    tagged = 0
    for (_file, _line), group in groups.items():
        sources = {f.source_key for f in group}
        if len(sources) < 2:
            continue
        # Each finding lists *other* sources that agreed with it.
        for f in group:
            others = sorted(sources - {f.source_key})
            # Merge with any pre-existing tags (e.g. from a future
            # re-run or from deduplicate's merging behaviour).
            merged = sorted(set(f.corroborated_by) | set(others))
            if merged != f.corroborated_by:
                f.corroborated_by = merged
                tagged += 1
            if promote_level > 0:
                new_sev = _promote(f.severity, promote_level)
                if new_sev != f.severity:
                    f.severity = new_sev
                    promoted += 1

    demoted = 0
    if demoted_patterns:
        for f in findings:
            if f.corroborated_by:
                continue
            if f.pattern_name in demoted_patterns and f.severity != Severity.INFO:
                f.severity = Severity.INFO
                demoted += 1

    if tagged or promoted or demoted:
        log.info(
            "Corroboration: %d tagged, %d promoted, %d demoted",
            tagged, promoted, demoted,
        )

    return findings
