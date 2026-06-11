#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Phase 10 Localization L6 — string-coverage lint (design § 5.7).

STRICT by default (reviewer decision 4). Fails the build (exit non-zero) on:

  1. A user-visible string literal passed to a known text sink without being
     wrapped in ``tr()`` — a hardcoded, untranslatable string.
  2. A ``tr("key")`` whose key is absent from the reference language
     (``en.json``) — even English-fallback can't resolve it, so it would
     render the raw key.

REPORTS ONLY (never fails the build):

  3. Keys present in the reference but missing from a secondary language
     (he / el / la). Runtime English-fallback (decision 1) makes a
     not-yet-translated secondary string a non-bug, so gating it would block
     incremental translation.

``--strict`` is the default; ``--lint`` downgrades checks 1+2 to warnings for
local pre-commit runs.

Detection is a lightweight regex line-scan (same approach as the existing
tools/audit lint scripts), not a clang-AST pass. Known envelope: it cannot see
a literal that reaches a sink through an intermediate variable
(``std::string s = "hi"; label.text = s;``) — accepted as a documented blind
spot, not chased with data-flow analysis. A trailing ``// i18n-exempt`` comment
on the line suppresses check 1 (debug-only labels, format templates).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# --- Check 1: user-visible text sinks taking a string literal ----------------
# Each pattern requires a string literal immediately after the sink opener, so a
# forward declaration like `void renderText3D(const std::string& text, ...)`
# (no quote after the paren) is not matched.
_SINK_PATTERNS = [
    re.compile(r"""\.text\s*=\s*"\s*"""),                    # label.text = "..."
    re.compile(r"""\.text\s*=\s*\""""),                      # label.text = "..."
    re.compile(r"""\bsetText\s*\(\s*\""""),                  # setText("...")
    re.compile(r"""\brenderText2D\s*\(\s*\""""),             # renderText2D("...")
    re.compile(r"""\brenderText2DOblique\s*\(\s*\""""),      # renderText2DOblique("...")
    re.compile(r"""\brenderText3D\s*\(\s*\""""),             # renderText3D("...")
    re.compile(r"""\b(?:UILabel|UIWorldLabel)\s*\(\s*\""""), # UILabel("...")
]

# An empty string literal ("") is not user-visible text — skip it.
_EMPTY_LITERAL = re.compile(r"""\.text\s*=\s*""\s*;""")

_EXEMPT = "// i18n-exempt"

# --- Check 2: tr("...") literal keys -----------------------------------------
_TR_KEY = re.compile(r"""\btr\s*\(\s*"([^"]*)"\s*\)""")


def load_keys(path: Path) -> set[str]:
    with path.open(encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a flat JSON object")
    return set(data.keys())


def scan_sources(roots: list[Path]) -> tuple[list[str], list[tuple[str, str]]]:
    """Return (hardcoded_violations, tr_key_uses).

    hardcoded_violations: human-readable "file:line: text" strings.
    tr_key_uses: (key, "file:line") pairs for every literal tr() key.
    """
    hardcoded: list[str] = []
    tr_uses: list[tuple[str, str]] = []
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".hpp", ".cc", ".cxx"):
                continue
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except (UnicodeDecodeError, OSError):
                continue
            for n, line in enumerate(lines, start=1):
                loc = f"{path}:{n}"
                for m in _TR_KEY.finditer(line):
                    tr_uses.append((m.group(1), loc))
                if _EXEMPT in line or "tr(" in line:
                    continue
                if _EMPTY_LITERAL.search(line):
                    continue
                if any(p.search(line) for p in _SINK_PATTERNS):
                    hardcoded.append(f"{loc}: {line.strip()}")
    return hardcoded, tr_uses


def main(argv: list[str] | None = None) -> int:
    here = Path(__file__).resolve().parent
    repo = here.parent

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--strict", action="store_true", default=True,
                      help="fail the build on checks 1+2 (default)")
    mode.add_argument("--lint", dest="strict", action="store_false",
                      help="downgrade checks 1+2 to warnings (exit 0)")
    ap.add_argument("--root", action="append", type=Path, default=None,
                    help="source root to scan (repeatable; default: <repo>/engine)")
    ap.add_argument("--reference", type=Path, default=None,
                    help="reference language JSON (default: assets/localization/en.json)")
    ap.add_argument("--secondary", action="append", default=None,
                    help="secondary language JSON for the coverage report (repeatable)")
    args = ap.parse_args(argv)

    roots = args.root or [repo / "engine"]
    reference = args.reference or (repo / "assets" / "localization" / "en.json")
    if args.secondary is not None:
        secondaries = [Path(p) for p in args.secondary]
    else:
        loc_dir = repo / "assets" / "localization"
        secondaries = [loc_dir / f"{c}.json" for c in ("he", "el", "la")]

    if not reference.is_file():
        print(f"localization_audit: reference not found: {reference}", file=sys.stderr)
        return 2
    ref_keys = load_keys(reference)

    hardcoded, tr_uses = scan_sources(roots)
    missing_tr = sorted({k for k, _ in tr_uses if k not in ref_keys})
    missing_locs = {k: loc for k, loc in tr_uses if k not in ref_keys}

    failed = False

    # Check 1.
    if hardcoded:
        failed = True
        label = "ERROR" if args.strict else "warning"
        print(f"[{label}] {len(hardcoded)} hardcoded user-visible string(s) "
              f"(wrap in tr(), or add a trailing `{_EXEMPT}`):")
        for v in hardcoded:
            print(f"  {v}")

    # Check 2.
    if missing_tr:
        failed = True
        label = "ERROR" if args.strict else "warning"
        print(f"[{label}] {len(missing_tr)} tr() key(s) absent from "
              f"{reference.name} (would render the raw key):")
        for k in missing_tr:
            print(f"  {k}  (first use {missing_locs[k]})")

    # Check 3 — report only.
    for sec in secondaries:
        if not sec.is_file():
            continue
        sec_keys = load_keys(sec)
        miss = sorted(ref_keys - sec_keys)
        if miss:
            print(f"[report] {sec.name}: {len(miss)} untranslated key(s) "
                  f"(English fallback applies): {', '.join(miss)}")

    if not hardcoded and not missing_tr:
        print(f"localization_audit: clean — {len(ref_keys)} reference keys, "
              f"{len(tr_uses)} tr() call site(s).")

    # In --lint mode checks 1+2 are advisory; never fail the build.
    if failed and args.strict:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
