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
    runner = AuditRunner(config, verbose=args.verbose)
    results = runner.run()

    if args.json_output:
        # JSON output to stdout
        print(json.dumps(results.to_dict(), indent=2))
    else:
        # Build markdown report
        report = runner.build_report(results)
        log.info("Done. Report: %s", config.report_path)

    # Exit code
    if results.has_critical:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
