"""AuditRunner — orchestrates all audit tiers and produces the final report."""

from __future__ import annotations

import logging
import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable

from .config import Config
from .findings import AuditData, ChangeSummary, Finding, ResearchResult
from .report import ReportBuilder

log = logging.getLogger("audit")

# Type alias for progress callbacks
ProgressCallback = Callable[[str, dict[str, Any]], None]


@dataclass
class AuditResults:
    """Container for all audit results."""
    findings: list[Finding] = field(default_factory=list)
    tier1_summary: dict = field(default_factory=dict)
    change_summary: ChangeSummary | None = None
    audit_data: AuditData | None = None
    research_results: list[ResearchResult] = field(default_factory=list)
    tiers_run: list[int] = field(default_factory=list)
    duration: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        return {
            "findings": [f.to_dict() for f in self.findings],
            "tier1_summary": self.tier1_summary,
            "change_summary": self.change_summary.to_dict() if self.change_summary else None,
            "audit_data": self.audit_data.to_dict() if self.audit_data else None,
            "research": [r.to_dict() for r in self.research_results],
            "tiers_run": self.tiers_run,
            "duration": round(self.duration, 1),
        }

    @property
    def has_critical(self) -> bool:
        from .findings import Severity
        return any(f.severity == Severity.CRITICAL for f in self.findings)


class AuditRunner:
    """Orchestrates audit tiers and assembles results."""

    def __init__(
        self,
        config: Config,
        verbose: bool = False,
        progress_callback: ProgressCallback | None = None,
    ):
        self.config = config
        self.verbose = verbose
        self._progress_cb = progress_callback

    def _emit(self, event: str, **data: Any) -> None:
        """Send a progress event if a callback is registered."""
        if self._progress_cb:
            self._progress_cb(event, data)

    def run(self, cancel_event: threading.Event | None = None) -> AuditResults:
        """Run all enabled audit tiers."""
        results = AuditResults()
        tiers = self.config.enabled_tiers
        start = time.monotonic()

        log.info("Starting audit: %s (tiers: %s)", self.config.project_name, tiers)
        self._emit("audit_start", project=self.config.project_name, tiers=tiers)

        tier_dispatch = {
            1: ("Build & Static Analysis", self._run_tier1),
            2: ("Pattern Scanning", self._run_tier2),
            3: ("Changed Files", self._run_tier3),
            4: ("Statistics", self._run_tier4),
            5: ("Online Research", self._run_tier5),
        }

        for tier_num in [1, 2, 3, 4, 5]:
            if tier_num not in tiers:
                continue
            if cancel_event and cancel_event.is_set():
                self._emit("cancelled")
                log.info("Audit cancelled by user")
                break

            name, handler = tier_dispatch[tier_num]
            results.tiers_run.append(tier_num)
            self._emit("tier_start", tier=tier_num, name=name)
            handler(results)
            self._emit("tier_end", tier=tier_num, findings_count=len(results.findings))

        results.duration = time.monotonic() - start
        log.info("Audit complete in %.1fs — %d findings", results.duration, len(results.findings))
        self._emit("audit_end", duration=round(results.duration, 1),
                   total_findings=len(results.findings))

        return results

    def _run_tier1(self, results: AuditResults) -> None:
        """Run Tier 1: Build, tests, cppcheck, clang-tidy."""
        log.info("=== Tier 1: Build & Static Analysis ===")

        self._emit("step_start", tier=1, step="build")
        from . import tier1_build
        build_findings, build_summary = tier1_build.run(self.config)
        results.findings.extend(build_findings)
        results.tier1_summary = build_summary
        self._emit("step_end", tier=1, step="build", findings=len(build_findings))

        self._emit("step_start", tier=1, step="cppcheck")
        from . import tier1_cppcheck
        cppcheck_findings = tier1_cppcheck.run(self.config)
        results.findings.extend(cppcheck_findings)
        self._emit("step_end", tier=1, step="cppcheck", findings=len(cppcheck_findings))

        self._emit("step_start", tier=1, step="clang-tidy")
        from . import tier1_clangtidy
        clangtidy_findings = tier1_clangtidy.run(self.config)
        results.findings.extend(clangtidy_findings)
        self._emit("step_end", tier=1, step="clang-tidy", findings=len(clangtidy_findings))

    def _run_tier2(self, results: AuditResults) -> None:
        """Run Tier 2: Pattern scanning."""
        log.info("=== Tier 2: Pattern Scanning ===")
        from . import tier2_patterns
        pattern_findings = tier2_patterns.run(self.config)
        results.findings.extend(pattern_findings)

    def _run_tier3(self, results: AuditResults) -> None:
        """Run Tier 3: Changed file analysis."""
        log.info("=== Tier 3: Changed Files ===")
        from . import tier3_changes
        results.change_summary = tier3_changes.run(self.config)

    def _run_tier4(self, results: AuditResults) -> None:
        """Run Tier 4: Statistics collection."""
        log.info("=== Tier 4: Statistics ===")
        from . import tier4_stats
        results.audit_data = tier4_stats.run(self.config)

    def _run_tier5(self, results: AuditResults) -> None:
        """Run Tier 5: Online research."""
        log.info("=== Tier 5: Research ===")
        from . import tier5_research
        results.research_results = tier5_research.run(self.config, results.findings)

    def build_report(self, results: AuditResults) -> str:
        """Generate the markdown report from results."""
        builder = ReportBuilder(self.config)
        return builder.build(
            findings=results.findings,
            tier1_summary=results.tier1_summary,
            change_summary=results.change_summary,
            audit_data=results.audit_data,
            research_results=results.research_results,
            tiers_run=results.tiers_run,
            duration=results.duration,
        )
