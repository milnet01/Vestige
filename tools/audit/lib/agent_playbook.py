# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Agent Playbook — 5-phase audit prompt rendered inline at the top of the report.

The playbook block instructs a downstream LLM audit agent (e.g. a Claude Code
session consuming the report) to follow a Discovery → Verify → Cite → Approval
→ Implement+Test cycle before touching any code. This closes the biggest gap
between the automated report output and the manual-audit rigour the project
applies when running audits by hand.

The renderer is pure — it reads already-computed data (finding counts via the
caller, ``tier1_summary`` for baseline warnings) and produces markdown. Keeping
it pure makes unit testing trivial and avoids adding a second runtime
dependency to ``ReportBuilder``.
"""

from __future__ import annotations

from .findings import Finding, Severity


# Severity → human-readable "at or above" string used in the approval-gate
# sentence. Ordered per IntEnum values (CRITICAL=0 is strictest).
_THRESHOLD_PHRASING: dict[Severity, str] = {
    Severity.CRITICAL: "CRITICAL",
    Severity.HIGH: "HIGH or CRITICAL",
    Severity.MEDIUM: "MEDIUM or higher",
    Severity.LOW: "LOW or higher",
    Severity.INFO: "any severity",
}


def threshold_from_string(value: str) -> Severity:
    """Parse the config string (``"medium"`` etc.) into a Severity.

    Returns :pydata:`Severity.MEDIUM` on unknown values rather than raising —
    the playbook should always render, never break report generation.
    """
    try:
        return Severity.from_string(value)
    except Exception:  # pragma: no cover — Severity.from_string already returns INFO on unknowns
        return Severity.MEDIUM


def build(
    findings: list[Finding],
    tier1_summary: dict,
    approval_threshold: Severity = Severity.MEDIUM,
) -> str:
    """Render the agent playbook section as markdown.

    Parameters
    ----------
    findings
        Deduplicated findings — used only for the "read counts from the
        Executive Summary below" reference. Content is not duplicated.
    tier1_summary
        Same shape ``ReportBuilder._build_summary`` consumes. If
        ``tier1_summary["build"]["build_ok"]`` is False or
        ``tier1_summary["tests"]["failed"]`` > 0 the playbook opens with a
        baseline-broken warning so the agent surfaces the pre-existing
        breakage before running Phase 2.
    approval_threshold
        Minimum severity requiring user approval before a fix is applied.
        Findings below this bar may be auto-fixed (unused imports,
        reproduced typo bugs, etc.) if the agent deems them trivial.
    """
    build_info = tier1_summary.get("build", {}) or {}
    test_info = tier1_summary.get("tests", {}) or {}
    build_ok = build_info.get("build_ok", True)
    tests_failed = int(test_info.get("failed", 0) or 0)

    threshold_phrase = _THRESHOLD_PHRASING.get(
        approval_threshold, _THRESHOLD_PHRASING[Severity.MEDIUM]
    )

    baseline_warning = ""
    if build_ok is False or tests_failed > 0:
        build_status = "OK" if build_ok else "FAILED"
        baseline_warning = (
            "\n> **Baseline is already broken.** "
            f"Build: {build_status}; tests failed: {tests_failed}. "
            "Surface this to the user and get acknowledgement before "
            "proceeding past Phase 1.\n"
        )

    return (
        "## Agent Playbook — Read This First\n"
        "\n"
        "This report is input to a 5-phase audit. Do not jump to fixes. Work "
        "phase-by-phase.\n"
        f"{baseline_warning}"
        "\n"
        "**Phase 1 — Baseline.** Re-run the test suite locally. Compare "
        "pass/fail to the Executive Summary below. Record the numbers; "
        "you'll produce a pre/post delta at the end.\n"
        "\n"
        "**Phase 2 — Verify each HIGH+ finding.** For every CRITICAL or "
        "HIGH finding below, mark it VERIFIED, UNCONFIRMED, or "
        "FALSE-POSITIVE:\n"
        "- Bug: reproduce with a trace through the code or a failing test.\n"
        "- Security: confirm exploitable in context (attacker-reachable data "
        "flow), not just a regex match on a keyword.\n"
        "- Dead code: confirm no callers — including dynamic dispatch, "
        "reflection, event/entity registries, CMake scripts, and tests.\n"
        "\n"
        "**Phase 3 — Cite sources for research findings.** For every Tier-5 "
        "CVE or dependency finding you act on, include an authoritative URL "
        "(NVD, GitHub advisory, official project docs). Cross-check the "
        "affected version range against the project's pinned version "
        "before assuming the CVE applies.\n"
        "\n"
        "**Phase 4 — Approval gate.** Produce a findings list with "
        "`file:line`, severity, verification status, and proposed fix, then "
        f"**wait for user approval** before touching code for any finding at "
        f"{threshold_phrase}. Exceptions (may proceed directly): trivial "
        "unused-import removal, obvious typo-conditionals with a reproducing "
        "test.\n"
        "\n"
        "**Phase 5 — Implement + test.**\n"
        "- Fix root causes, not symptoms. No silencing warnings, no "
        "swallowing exceptions, no commented-out broken code, no capped "
        "iteration counts that hide real divergences. If a workaround is "
        "genuinely unavoidable, comment the constraint.\n"
        "- Every behavioural fix gets a regression test.\n"
        "- Keep edits scoped to each finding; no drive-by refactors.\n"
        "- Re-run the full suite. Emit a pre/post diff: tests, warnings, "
        "finding counts by severity. Task fails if the suite regresses.\n"
        "\n"
        "**Deliverable.** At the end, report:\n"
        "1. Findings list with VERIFIED / UNCONFIRMED / FALSE-POSITIVE tags\n"
        "2. Changes made — files touched, why, test evidence\n"
        "3. Deferred items — why, and what would unblock them\n"
        "4. Baseline comparison — pre vs post\n"
        "5. Allowlist additions — any URLs added during research\n"
        "\n"
        "Be terse and concrete. Skip categories with zero findings rather "
        'than writing "none found" filler.\n'
    )
