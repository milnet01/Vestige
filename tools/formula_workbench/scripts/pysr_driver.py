#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Headless PySR symbolic regression driver — §3.5 of the Workbench
self-learning design.

Usage:
    pysr_driver.py <csv_path> [--niterations N] [--binary-ops OPS] [--unary-ops OPS]

Reads ``<csv_path>`` (first row headers, last column observed), runs
PySR against it, and prints the discovered expressions as markdown
+ JSON to stdout. Exit code 2 when PySR isn't importable; exit code
1 on any other error.

Design intent: this is the data-plane of §3.5. The C++ CLI spawns
this script. Keeping PySR out of the C++ build means:
  - No Python dep in the default Workbench build.
  - Users who want symbolic regression install PySR themselves
    (``pip install pysr``; it pulls Julia too).
  - The CLI degrades gracefully when PySR is missing: the driver
    exits 2, the C++ side reports what to install.

Scope: runs PySR at its defaults. Users who want to tune the search
(operator set, complexity, niterations) can pass flags; anything
more exotic — parsimony weighting, custom operators — goes through
the PySR Python API directly. We're providing a shell-out, not an
API wrapper.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path


def _load_csv(path: Path) -> tuple[list[str], list[list[float]]]:
    """Return (headers, rows). Last column is treated as the observation."""
    with path.open() as f:
        reader = csv.reader(f)
        headers = next(reader)
        rows: list[list[float]] = []
        for row in reader:
            if not row:
                continue
            try:
                rows.append([float(v) for v in row])
            except ValueError:
                # Skip non-numeric rows silently — matches the C++
                # loader's tolerance for mixed headers.
                continue
    return headers, rows


def main() -> int:
    ap = argparse.ArgumentParser(description="PySR symbolic regression driver")
    ap.add_argument("csv_path", type=Path)
    ap.add_argument("--niterations", type=int, default=40)
    ap.add_argument(
        "--binary-ops",
        default="+,-,*,/",
        help="Comma-separated list of binary operators PySR may use.",
    )
    ap.add_argument(
        "--unary-ops",
        default="cos,sin,exp,log,sqrt",
        help="Comma-separated list of unary operators PySR may use.",
    )
    ap.add_argument(
        "--max-complexity",
        type=int,
        default=20,
        help="Upper bound on expression complexity. Guards against the "
             "7-term-nested-sin(log(x²)) mutants PySR sometimes evolves; see "
             "docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md §3.5.",
    )
    args = ap.parse_args()

    try:
        from pysr import PySRRegressor
    except ImportError:
        sys.stderr.write(
            "error: pysr not installed.\n"
            "  Install with: pip install pysr\n"
            "  PySR also pulls Julia (~300 MB the first time).\n",
        )
        return 2

    if not args.csv_path.exists():
        sys.stderr.write(f"error: CSV not found: {args.csv_path}\n")
        return 1

    headers, rows = _load_csv(args.csv_path)
    if len(headers) < 2 or len(rows) < 3:
        sys.stderr.write(
            "error: CSV needs ≥2 columns and ≥3 rows (got "
            f"{len(headers)} / {len(rows)}).\n",
        )
        return 1

    # Last column is the observation; the rest are inputs.
    input_names = headers[:-1]
    obs_name = headers[-1]
    X = [[r[i] for i in range(len(r) - 1)] for r in rows]
    y = [r[-1] for r in rows]

    model = PySRRegressor(
        niterations=args.niterations,
        binary_operators=[op.strip() for op in args.binary_ops.split(",") if op.strip()],
        unary_operators=[op.strip() for op in args.unary_ops.split(",") if op.strip()],
        maxsize=args.max_complexity,
        verbosity=0,
        progress=False,
    )
    model.fit(X, y, variable_names=input_names)

    # PySR's .equations_ frame has complexity, loss, score, equation.
    # Emit both human markdown (top) and machine JSON (bottom) so the
    # caller can pick whichever format fits their downstream use.
    eqs = model.equations_
    out_rows = []
    if eqs is not None:
        for _, row in eqs.iterrows():
            out_rows.append({
                "complexity": int(row.get("complexity", 0)),
                "loss":       float(row.get("loss", 0.0)),
                "score":      float(row.get("score", 0.0)),
                "equation":   str(row.get("equation", "")),
            })

    print(f"# PySR symbolic regression — {args.csv_path.name}\n")
    print(f"Input variables: {', '.join(input_names)}")
    print(f"Output variable: {obs_name}")
    print(f"Iterations: {args.niterations}  /  Max complexity: "
          f"{args.max_complexity}\n")
    if out_rows:
        print("| Complexity | Loss | Score | Equation |")
        print("|-----------:|-----:|------:|----------|")
        for r in out_rows:
            eq = r["equation"].replace("|", "\\|")
            print(f"| {r['complexity']} | {r['loss']:.4g} | "
                  f"{r['score']:.4g} | `{eq}` |")
        print()
    else:
        print("*(PySR produced no candidate equations.)*\n")

    # Machine-readable tail — the C++ side pipes this through JSON
    # parsers when the output is captured programmatically.
    print("```json")
    print(json.dumps({
        "input_names":  input_names,
        "output_name":  obs_name,
        "n_points":     len(rows),
        "niterations":  args.niterations,
        "equations":    out_rows,
    }, indent=2))
    print("```")
    return 0


if __name__ == "__main__":
    sys.exit(main())
