#!/usr/bin/env python3
"""
Vestige Audit Tool — automated code audit with LLM-optimized reporting.

Usage:
    python3 tools/audit/audit.py                       # Full audit with defaults
    python3 tools/audit/audit.py -t 1 2                # Just build + patterns
    python3 tools/audit/audit.py -b v0.9.0             # Diff against a tag
    python3 tools/audit/audit.py -c other_config.yaml  # Different project
    python3 tools/audit/audit.py --json                # Raw JSON output
    python3 tools/audit/audit.py --no-research         # Skip web research
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

# Ensure the lib package is importable
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from lib.config import load_config
from lib.runner import AuditRunner


def find_default_config() -> Path:
    """Look for audit_config.yaml next to this script."""
    default = SCRIPT_DIR / "audit_config.yaml"
    if default.exists():
        return default
    # Also check current working directory
    cwd_config = Path.cwd() / "audit_config.yaml"
    if cwd_config.exists():
        return cwd_config
    return default  # Will error in load_config if missing


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Automated code audit tool with LLM-optimized reporting",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--config", "-c",
        default=None,
        help="Path to audit_config.yaml (default: tools/audit/audit_config.yaml)",
    )
    parser.add_argument(
        "--tiers", "-t",
        nargs="+",
        type=int,
        default=None,
        help="Run only specific tiers (e.g., -t 1 2)",
    )
    parser.add_argument(
        "--output", "-o",
        default=None,
        help="Override report output path",
    )
    parser.add_argument(
        "--base-ref", "-b",
        default=None,
        help="Git ref for Tier 3 diff (e.g., v0.9.0, HEAD~5)",
    )
    parser.add_argument(
        "--project-root", "-r",
        default=None,
        help="Override project root directory",
    )
    parser.add_argument(
        "--no-research",
        action="store_true",
        help="Skip Tier 5 (web research)",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Print progress and timing to stderr",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would run without executing",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        dest="json_output",
        help="Output raw JSON instead of markdown report",
    )
    parser.add_argument(
        "--list-patterns",
        action="store_true",
        help="Print all configured grep patterns and exit",
    )
    parser.add_argument(
        "--init",
        action="store_true",
        help="Auto-detect project settings and generate audit_config.yaml",
    )
    parser.add_argument(
        "--suppress-show",
        action="store_true",
        help="Print current suppressions from .audit_suppress and exit",
    )
    parser.add_argument(
        "--suppress-add",
        metavar="KEY",
        default=None,
        help="Add a dedup_key to .audit_suppress and exit",
    )
    parser.add_argument(
        "--diff",
        action="store_true",
        help="Include differential report comparing against previous audit results",
    )
    parser.add_argument(
        "--ci",
        action="store_true",
        help="CI mode: emit GitHub Actions annotations and set exit code by severity",
    )
    parser.add_argument(
        "--patterns",
        choices=["strict", "relaxed", "security", "performance"],
        default=None,
        help="Use a pattern preset instead of config patterns",
    )
    parser.add_argument(
        "--html",
        action="store_true",
        help="Also generate a self-contained HTML report",
    )
    parser.add_argument(
        "--sarif",
        action="store_true",
        help="Also generate a SARIF 2.1.0 report",
    )
    parser.add_argument(
        "--keep-snapshots",
        type=int,
        default=None,
        metavar="N",
        help="Retain only the N most recent trend_snapshot_*.json files "
             "in docs/ (default: keep all). See AUDIT.md §L9.",
    )

    args = parser.parse_args()

    # Setup logging
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,
        force=True,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )
    log = logging.getLogger("audit")

    # Suppress verbose HTTP/library debug output
    logging.getLogger("httpx").setLevel(logging.WARNING)
    logging.getLogger("httpcore").setLevel(logging.WARNING)
    logging.getLogger("primp").setLevel(logging.WARNING)
    logging.getLogger("duckduckgo_search").setLevel(logging.WARNING)

    # --suppress-show: print current suppressions and exit
    if args.suppress_show:
        from lib.suppress import load_suppressions
        # Determine root: use --project-root if given, else cwd
        root = Path(args.project_root).resolve() if args.project_root else Path.cwd()
        keys = load_suppressions(root)
        if not keys:
            print("No suppressions configured (no .audit_suppress file or it is empty).")
        else:
            print(f"{len(keys)} suppressed finding(s):")
            for k in sorted(keys):
                print(f"  {k}")
        return 0

    # --suppress-add: add a key and exit
    if args.suppress_add:
        from lib.suppress import save_suppression
        root = Path(args.project_root).resolve() if args.project_root else Path.cwd()
        save_suppression(root, args.suppress_add, annotation="added via --suppress-add")
        print(f"Added suppression: {args.suppress_add}")
        return 0

    # --init: auto-generate config and exit
    if args.init:
        from lib.auto_config import generate_config
        project_root = Path(args.project_root).resolve() if args.project_root else Path.cwd()
        output = Path(args.config) if args.config else project_root / "audit_config.yaml"
        generate_config(project_root, output)
        print(f"Config generated: {output}")
        print(f"Review it, then run: python3 {sys.argv[0]}")
        return 0

    # Load config
    config_path = args.config or find_default_config()
    try:
        config = load_config(config_path, project_root=args.project_root)
    except SystemExit:
        return 1

    # Apply CLI overrides
    if args.tiers:
        config.raw["tiers"] = args.tiers
    if args.output:
        config.raw["report"]["output_path"] = args.output
    if args.base_ref:
        config.raw["changes"]["base_ref"] = args.base_ref
    if args.no_research:
        config.raw["research"]["enabled"] = False

    # --patterns preset: override patterns from config
    if args.patterns:
        from lib.config import apply_pattern_preset
        apply_pattern_preset(config.raw, args.patterns, config.language)

    # --list-patterns: print patterns and exit
    if args.list_patterns:
        patterns = config.patterns
        for cat, pats in patterns.items():
            if not isinstance(pats, list):
                continue
            print(f"\n=== {cat} ===")
            for p in pats:
                print(f"  [{p.get('severity', '?')}] {p.get('name', '?')}: /{p.get('pattern', '')}/ — {p.get('description', '')}")
        return 0

    # --dry-run: show config and exit
    if args.dry_run:
        print("=== Dry Run ===")
        print(f"Project: {config.project_name}")
        print(f"Root: {config.root}")
        print(f"Language: {config.language}")
        print(f"Tiers: {config.enabled_tiers}")
        print(f"Report: {config.report_path}")
        print(f"Source dirs: {config.source_dirs}")
        print(f"Exclude dirs: {config.exclude_dirs}")

        sa = config.get("static_analysis", default={})
        print(f"cppcheck: {'enabled' if sa.get('cppcheck', {}).get('enabled') else 'disabled'}")
        print(f"clang-tidy: {'enabled' if sa.get('clang_tidy', {}).get('enabled') else 'disabled'}")
        print(f"Research: {'enabled' if config.get('research', 'enabled') else 'disabled'}")
        return 0

    # Run the audit
    runner = AuditRunner(config, verbose=args.verbose,
                         keep_snapshots=args.keep_snapshots)
    results = runner.run()

    # Compute differential report if requested
    diff = None
    if args.diff:
        from lib.diff_report import load_previous_results, compute_diff
        report_dir = config.report_path.parent
        current_stem = config.report_path.stem
        previous = load_previous_results(report_dir, current_stem)
        if previous is not None:
            diff = compute_diff(previous, results.findings)
            diff.previous_path = str(report_dir)
            log.info("Diff: %d new, %d resolved, %d persistent",
                     len(diff.new_findings), len(diff.resolved_findings),
                     diff.persistent_count)
        else:
            log.info("No previous results found — skipping differential report")

    if args.json_output:
        # JSON output to stdout
        output_data = results.to_dict()
        if diff is not None:
            output_data["diff"] = diff.to_dict()
        print(json.dumps(output_data, indent=2))
    elif args.ci:
        # CI mode: emit annotations
        from lib.ci_output import format_github_annotations, write_step_summary, get_exit_code
        annotations = format_github_annotations(results.findings)
        if annotations:
            print(annotations)
        write_step_summary(results.to_dict())
        # Still generate the markdown report
        runner.build_report(results, diff=diff)
        return get_exit_code(results.findings)
    else:
        # Build markdown report
        report = runner.build_report(results, diff=diff)
        log.info("Done. Report: %s", config.report_path)

    # Generate HTML report if requested
    if args.html and not args.json_output:
        from lib.html_report import generate_html_report
        try:
            from lib.trends import load_snapshots, compute_trends
            report_dir = config.report_path.parent
            snapshots = load_snapshots(report_dir)
            trend_report = compute_trends(snapshots) if len(snapshots) >= 2 else None
        except Exception:
            trend_report = None

        html_content = generate_html_report(results, config, trend_report=trend_report)
        base_path = config.report_path
        timestamp = __import__("datetime").datetime.now().strftime("%Y-%m-%d_%H%M%S")
        html_path = base_path.parent / f"{base_path.stem}_{timestamp}.html"
        html_path.parent.mkdir(parents=True, exist_ok=True)
        html_path.write_text(html_content)
        log.info("HTML report written to %s", html_path)

    # Generate SARIF report if requested
    if args.sarif and not args.json_output:
        from lib.sarif_output import write_sarif
        sarif_path = config.report_path.with_suffix(".sarif")
        write_sarif(results.findings, sarif_path)
        log.info("SARIF report written to %s", sarif_path)

    # Exit code
    if results.has_critical:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
