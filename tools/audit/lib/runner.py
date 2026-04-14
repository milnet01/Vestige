"""AuditRunner — orchestrates all audit tiers and produces the final report."""

from __future__ import annotations

import logging
import threading
import time
from concurrent.futures import ThreadPoolExecutor, Future
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
        keep_snapshots: int | None = None,
    ):
        self.config = config
        self.verbose = verbose
        self._progress_cb = progress_callback
        # AUDIT.md §L9 / FIXPLAN I5: None = unlimited (legacy); positive
        # integer = rolling retention of trend snapshot files.
        self.keep_snapshots = keep_snapshots

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

        # Phase 1: Tier 1 must run first (build must succeed before analysis)
        if 1 in tiers:
            if cancel_event and cancel_event.is_set():
                self._emit("cancelled")
                log.info("Audit cancelled by user")
            else:
                results.tiers_run.append(1)
                self._emit("tier_start", tier=1, name="Build & Static Analysis")
                self._run_tier1(results)
                self._emit("tier_end", tier=1, findings_count=len(results.findings))

        # Phase 2: Tiers 2, 3, 4, 6 can run in parallel (independent of each other).
        # D4 (2.6.0): Tier 6 (feature coverage) is filesystem-only, no cross
        # dependencies on other tiers, so it joins the parallel group.
        parallel_tiers = [t for t in [2, 3, 4, 6] if t in tiers]
        if parallel_tiers and not (cancel_event and cancel_event.is_set()):
            self._run_parallel_tiers(results, parallel_tiers, cancel_event)

        # Phase 3: Tier 5 needs findings from earlier tiers for research context
        if 5 in tiers and not (cancel_event and cancel_event.is_set()):
            results.tiers_run.append(5)
            self._emit("tier_start", tier=5, name="Online Research")
            self._run_tier5(results)
            self._emit("tier_end", tier=5, findings_count=len(results.findings))

        # Pipeline ordering:
        #   1. deduplicate    — collapse same-dedup_key entries
        #   2. corroborate    — tag/promote/demote based on multi-source signal (D2, 2.4.0)
        #   3. overrides      — user's explicit severity choices win
        #   4. verified tags  — apply human-review signal (D3, 2.5.0), tag only (no filter)
        #   5. suppressions   — last-mile filter
        # Dedup is idempotent; ReportBuilder.build() calling it again is
        # harmless but avoids regressions if future callers bypass the
        # runner pipeline.
        from .findings import apply_severity_overrides, deduplicate
        from .corroboration import corroborate
        results.findings = deduplicate(results.findings)
        corr_cfg = self.config.get("corroboration", default={}) or {}
        results.findings = corroborate(results.findings, corr_cfg)

        severity_overrides = self.config.get("severity_overrides", default=[])
        if severity_overrides:
            results.findings = apply_severity_overrides(results.findings, severity_overrides)
            log.info("Applied %d severity override rules", len(severity_overrides))

        # D3 (2.5.0): tag verified findings so reviewers can distinguish
        # reviewed-real-still-needs-fixing from not-yet-looked-at. This
        # runs before suppressions because suppression is a last-mile
        # filter — if a finding is both verified and suppressed, the
        # suppression takes effect (reviewer chose to hide it).
        from .verified import load_verified, apply_verified_tags
        verified_keys = load_verified(self.config.root)
        if verified_keys:
            verified_count = apply_verified_tags(results.findings, verified_keys)
            log.info("Tagged %d findings as [VERIFIED] from %d keys",
                     verified_count, len(verified_keys))

        # Apply finding suppressions before reporting
        from .suppress import load_suppressions, filter_suppressed
        suppressed = load_suppressions(self.config.root)
        if suppressed:
            results.findings, suppressed_count = filter_suppressed(results.findings, suppressed)
            log.info("Suppressed %d findings (%d active)", suppressed_count, len(results.findings))

        results.duration = time.monotonic() - start
        log.info("Audit complete in %.1fs — %d findings", results.duration, len(results.findings))
        self._emit("audit_end", duration=round(results.duration, 1),
                   total_findings=len(results.findings))

        # Save trend snapshot after each run. D5 (2.2.0): also pass
        # tier1_summary, audit_data, and duration so the snapshot carries
        # baseline metadata for the next run's Baseline Comparison
        # section. tier1_summary may be empty when Tier 1 wasn't run;
        # save_snapshot handles that gracefully.
        try:
            from .trends import save_snapshot
            report_dir = self.config.report_path.parent
            report_dir.mkdir(parents=True, exist_ok=True)
            save_snapshot(
                report_dir,
                results.findings,
                keep=self.keep_snapshots,
                tier1_summary=results.tier1_summary,
                audit_data=results.audit_data,
                duration=results.duration,
            )
        except Exception as e:
            log.warning("Failed to save trend snapshot: %s", e)

        return results

    def _run_tier1(self, results: AuditResults) -> None:
        """Run Tier 1: Build, cppcheck, clang-tidy in parallel."""
        log.info("=== Tier 1: Build & Static Analysis ===")

        def run_build() -> tuple[list[Finding], dict]:
            """Execute build step and return (findings, summary)."""
            self._emit("step_start", tier=1, step="build")
            from . import tier1_build
            findings, summary = tier1_build.run(self.config)
            self._emit("step_end", tier=1, step="build", findings=len(findings))
            return findings, summary

        def run_cppcheck() -> list[Finding]:
            """Execute cppcheck step and return findings."""
            self._emit("step_start", tier=1, step="cppcheck")
            from . import tier1_cppcheck
            findings = tier1_cppcheck.run(self.config)
            self._emit("step_end", tier=1, step="cppcheck", findings=len(findings))
            return findings

        def run_clangtidy() -> list[Finding]:
            """Execute clang-tidy step and return findings."""
            self._emit("step_start", tier=1, step="clang-tidy")
            from . import tier1_clangtidy
            findings = tier1_clangtidy.run(self.config)
            self._emit("step_end", tier=1, step="clang-tidy", findings=len(findings))
            return findings

        with ThreadPoolExecutor(max_workers=3, thread_name_prefix="tier1") as executor:
            build_future = executor.submit(run_build)
            cppcheck_future = executor.submit(run_cppcheck)
            clangtidy_future = executor.submit(run_clangtidy)

            # Collect build results
            try:
                build_findings, build_summary = build_future.result()
                results.findings.extend(build_findings)
                results.tier1_summary = build_summary
            except Exception as e:
                log.error("Build step failed: %s", e)

            # Collect cppcheck results
            try:
                cppcheck_findings = cppcheck_future.result()
                results.findings.extend(cppcheck_findings)
            except Exception as e:
                log.error("cppcheck step failed: %s", e)

            # Collect clang-tidy results
            try:
                clangtidy_findings = clangtidy_future.result()
                results.findings.extend(clangtidy_findings)
            except Exception as e:
                log.error("clang-tidy step failed: %s", e)

    def _run_parallel_tiers(
        self,
        results: AuditResults,
        tier_nums: list[int],
        cancel_event: threading.Event | None,
    ) -> None:
        """Run tiers 2-4, 6 in parallel using threads. Each returns its data independently."""
        tier_names = {
            2: "Pattern Scanning",
            3: "Changed Files",
            4: "Statistics",
            6: "Feature Coverage",
        }

        def run_tier(tier_num: int) -> dict:
            """Execute a single tier and return its results."""
            if cancel_event and cancel_event.is_set():
                return {"tier": tier_num, "findings": [], "data": None}

            self._emit("tier_start", tier=tier_num, name=tier_names[tier_num])

            if tier_num == 2:
                from . import tier2_patterns
                findings = tier2_patterns.run(self.config)
                return {"tier": 2, "findings": findings}
            elif tier_num == 3:
                from . import tier3_changes
                change_summary, diff_findings = tier3_changes.run(self.config)
                return {"tier": 3, "findings": diff_findings, "change_summary": change_summary}
            elif tier_num == 4:
                from . import tier4_stats
                audit_data, complexity_findings = tier4_stats.run(self.config)
                return {"tier": 4, "findings": complexity_findings, "audit_data": audit_data}
            elif tier_num == 6:
                # D4 (2.6.0): Tier 6 feature coverage — subsystems without tests
                from . import tier6_coverage
                findings = tier6_coverage.run(self.config)
                return {"tier": 6, "findings": findings}
            return {"tier": tier_num, "findings": []}

        with ThreadPoolExecutor(max_workers=3, thread_name_prefix="tier") as executor:
            futures: dict[int, Future] = {}
            for t in tier_nums:
                results.tiers_run.append(t)
                futures[t] = executor.submit(run_tier, t)

            for tier_num, future in futures.items():
                try:
                    tier_result = future.result()
                    results.findings.extend(tier_result.get("findings", []))
                    if "change_summary" in tier_result:
                        results.change_summary = tier_result["change_summary"]
                    if "audit_data" in tier_result:
                        results.audit_data = tier_result["audit_data"]
                except Exception as e:
                    log.error("Tier %d failed: %s", tier_num, e)
                finally:
                    self._emit("tier_end", tier=tier_num, findings_count=len(results.findings))

    def _run_tier5(self, results: AuditResults) -> None:
        """Run Tier 5: Online research."""
        log.info("=== Tier 5: Research ===")
        from . import tier5_research
        results.research_results = tier5_research.run(self.config, results.findings)

    def build_report(self, results: AuditResults, diff=None) -> str:
        """Generate the markdown report from results."""
        # Load trend data if available. The just-saved snapshot is
        # included in the load (snapshots[0]); snapshots[1] is the
        # immediately preceding run that the Baseline Comparison
        # contrasts against.
        trend_report = None
        baseline_comparison = None
        try:
            from .trends import (
                compute_baseline_comparison,
                compute_trends,
                load_snapshots,
            )
            report_dir = self.config.report_path.parent
            snapshots = load_snapshots(report_dir)
            if len(snapshots) >= 2:
                trend_report = compute_trends(snapshots)
                baseline_comparison = compute_baseline_comparison(snapshots)
        except Exception as e:
            log.warning("Failed to load trend data: %s", e)

        builder = ReportBuilder(self.config)
        return builder.build(
            findings=results.findings,
            tier1_summary=results.tier1_summary,
            change_summary=results.change_summary,
            audit_data=results.audit_data,
            research_results=results.research_results,
            tiers_run=results.tiers_run,
            duration=results.duration,
            diff=diff,
            trend_report=trend_report,
            baseline_comparison=baseline_comparison,
        )
