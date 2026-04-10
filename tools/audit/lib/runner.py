"""AuditRunner — orchestrates all audit tiers and produces the final report."""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field
from typing import Any

from .config import Config
from .findings import AuditData, ChangeSummary, Finding, ResearchResult
from .report import ReportBuilder

log = logging.getLogger("audit")


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

    def __init__(self, config: Config, verbose: bool = False):
        self.config = config
        self.verbose = verbose

    def run(self) -> AuditResults:
        """Run all enabled audit tiers."""
        results = AuditResults()
        tiers = self.config.enabled_tiers
        start = time.monotonic()

        log.info("Starting audit: %s (tiers: %s)", self.config.project_name, tiers)

        # Tier 1: Build & Static Analysis
        if 1 in tiers:
            results.tiers_run.append(1)
            self._run_tier1(results)

        # Tier 2: Pattern Scanning
        if 2 in tiers:
            results.tiers_run.append(2)
            self._run_tier2(results)

        # Tier 3: Changed File Analysis
        if 3 in tiers:
            results.tiers_run.append(3)
            self._run_tier3(results)

        # Tier 4: Statistics & Data Collection
        if 4 in tiers:
            results.tiers_run.append(4)
            self._run_tier4(results)

        # Tier 5: Online Research
        if 5 in tiers:
            results.tiers_run.append(5)
            self._run_tier5(results)

        results.duration = time.monotonic() - start
        log.info("Audit complete in %.1fs — %d findings", results.duration, len(results.findings))

        return results

    def _run_tier1(self, results: AuditResults) -> None:
        """Run Tier 1: Build, tests, cppcheck, clang-tidy."""
        log.info("=== Tier 1: Build & Static Analysis ===")

        # Build and tests
        from . import tier1_build
        build_findings, build_summary = tier1_build.run(self.config)
        results.findings.extend(build_findings)
        results.tier1_summary = build_summary

        # cppcheck
        from . import tier1_cppcheck
        cppcheck_findings = tier1_cppcheck.run(self.config)
        results.findings.extend(cppcheck_findings)

        # clang-tidy
        from . import tier1_clangtidy
        clangtidy_findings = tier1_clangtidy.run(self.config)
        results.findings.extend(clangtidy_findings)

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
