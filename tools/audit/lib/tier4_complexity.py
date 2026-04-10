"""Tier 4: Cyclomatic complexity analysis via lizard."""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")

try:
    import lizard
    HAS_LIZARD = True
except ImportError:
    HAS_LIZARD = False


@dataclass
class ComplexityResult:
    """Results of cyclomatic complexity analysis."""
    hotspots: list[dict[str, Any]] = field(default_factory=list)
    avg_complexity: float = 0.0
    max_complexity: int = 0
    functions_analyzed: int = 0
    files_analyzed: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "hotspots": self.hotspots[:20],
            "avg_complexity": round(self.avg_complexity, 1),
            "max_complexity": self.max_complexity,
            "functions_analyzed": self.functions_analyzed,
            "files_analyzed": self.files_analyzed,
        }


def analyze_complexity(config: Config) -> tuple[ComplexityResult | None, list[Finding]]:
    """Run lizard complexity analysis. Returns (result, findings)."""
    findings: list[Finding] = []

    if not config.get("tier4", "complexity", "enabled", default=True):
        return None, findings

    if not HAS_LIZARD:
        log.warning("lizard not installed — skipping complexity analysis. "
                     "Install with: pip install lizard")
        return None, findings

    threshold = config.get("tier4", "complexity", "threshold", default=15)
    max_findings = config.get("tier4", "complexity", "max_findings", default=50)

    # Collect source files
    source_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    log.info("Complexity analysis: scanning %d files (threshold=%d)...",
             len(source_files), threshold)

    result = ComplexityResult()
    total_complexity = 0
    total_functions = 0
    all_complex: list[dict[str, Any]] = []

    for src_file in source_files:
        try:
            file_info = lizard.analyze_file(str(src_file))
        except Exception as e:
            log.debug("lizard failed on %s: %s", src_file, e)
            continue

        result.files_analyzed += 1

        for func in file_info.function_list:
            total_functions += 1
            total_complexity += func.cyclomatic_complexity

            if func.cyclomatic_complexity >= threshold:
                rel = relative_path(src_file, config.root)
                all_complex.append({
                    "name": func.name,
                    "file": rel,
                    "line": func.start_line,
                    "complexity": func.cyclomatic_complexity,
                    "tokens": func.token_count,
                    "params": len(func.parameters),
                })

    # Sort by complexity descending
    all_complex.sort(key=lambda f: -f["complexity"])
    result.hotspots = all_complex
    result.functions_analyzed = total_functions
    result.max_complexity = all_complex[0]["complexity"] if all_complex else 0
    result.avg_complexity = total_complexity / total_functions if total_functions else 0.0

    # Generate findings for complex functions
    for func in all_complex[:max_findings]:
        sev = Severity.HIGH if func["complexity"] >= 25 else Severity.MEDIUM
        findings.append(Finding(
            file=func["file"],
            line=func["line"],
            severity=sev,
            category="complexity",
            source_tier=4,
            title=f"High cyclomatic complexity: {func['name']} (CC={func['complexity']})",
            pattern_name="lizard",
        ))

    log.info("Complexity: %d functions analyzed, %d above threshold, max CC=%d",
             total_functions, len(all_complex), result.max_complexity)

    return result, findings
