#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""LLM-guided formula hypothesis ranking — §3.6 of the Workbench
self-learning design.

Usage:
    llm_rank.py <csv_path> < library.json

Reads a dataset CSV and a library metadata JSON (from stdin — the
C++ side pipes its FormulaLibrary dump in), constructs a prompt, and
asks Anthropic's Claude to rank which library formulas are physically
plausible models for the data. Prints a ranked markdown shortlist.

Requires ``ANTHROPIC_API_KEY`` in the environment. Exits 2 when the
key or the anthropic SDK is missing so the C++ CLI can surface a
clear install / config message.

Design intent: the LLM is terrible at *doing* the fit — PySR and LM
beat it by orders of magnitude on that axis. But the LLM is
excellent at *narrowing the search*: "80 points, decay-shaped,
period ≈ 0.7s — which of these 14 formulas is plausible?" LASR
(Grayeli et al. 2024) and the 2026 Nature Sci-Reports LLM-as-prior
paper both make this case. Our use is more modest: library-formula
shortlisting, not novel-expression discovery.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
import sys
from pathlib import Path


PROMPT_TEMPLATE = """You are helping a physics / rendering engineer pick the best model for a dataset.

The dataset has {n_points} rows. Input variables: {input_vars}. Output variable: {obs_name}.
Output-value summary: min {y_min:.4g}, max {y_max:.4g}, mean {y_mean:.4g}, stdev {y_stdev:.4g}.
First 10 rows (input_1, ..., input_n, observed):
{sample_rows}

Here is the FormulaLibrary the engineer can fit against. Each entry is a candidate model:

{library_json}

TASK: Rank the library formulas from MOST to LEAST physically plausible given the data shape.
- Consider whether the formula's inputs match the dataset's variables (same names, similar magnitude ranges).
- Consider whether the formula's expected output shape (monotonic, oscillating, bounded) matches the observations.
- Do NOT try to fit the formula yourself — trust the fitter. Only judge PLAUSIBILITY.

Reply with a markdown table, MOST plausible first, exactly N rows (N = 1..10 as you see fit). Columns:
  | Rank | Formula | Plausibility | Why (one sentence) |

After the table, add a one-paragraph "Caveats" section noting anything suspicious about the dataset
(missing variables, implausible ranges, non-physical values).

Output ONLY the markdown — no preamble, no post-commentary outside the table + caveats section.
"""


def _load_csv(path: Path) -> tuple[list[str], list[list[float]]]:
    with path.open() as f:
        reader = csv.reader(f)
        headers = next(reader)
        rows = []
        for row in reader:
            if not row:
                continue
            try:
                rows.append([float(v) for v in row])
            except ValueError:
                continue
    return headers, rows


def main() -> int:
    ap = argparse.ArgumentParser(
        description="LLM-guided formula hypothesis ranking")
    ap.add_argument("csv_path", type=Path)
    ap.add_argument(
        "--model",
        default="claude-haiku-4-5-20251001",
        help="Anthropic model ID. Default Haiku 4.5 — fast, cheap, "
             "accurate enough for shortlisting.",
    )
    ap.add_argument(
        "--max-tokens",
        type=int,
        default=800,
        help="Response length cap. Ranking + caveats fits comfortably "
             "in 800 tokens for libraries of ~30 formulas.",
    )
    args = ap.parse_args()

    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        sys.stderr.write(
            "error: ANTHROPIC_API_KEY not set in environment.\n"
            "  Get a key at https://console.anthropic.com/\n"
            "  export ANTHROPIC_API_KEY='sk-...'\n",
        )
        return 2

    try:
        from anthropic import Anthropic
    except ImportError:
        sys.stderr.write(
            "error: anthropic SDK not installed.\n"
            "  Install with: pip install anthropic\n",
        )
        return 2

    if not args.csv_path.exists():
        sys.stderr.write(f"error: CSV not found: {args.csv_path}\n")
        return 1

    # Library metadata arrives on stdin (C++ side writes it). If
    # stdin is a TTY, error out — we have no library to rank against.
    if sys.stdin.isatty():
        sys.stderr.write(
            "error: no library metadata on stdin. The C++ CLI pipes "
            "it in; if you're invoking this script directly, do:\n"
            "  formula_workbench --dump-library | "
            "llm_rank.py data.csv\n",
        )
        return 1
    library_raw = sys.stdin.read()

    headers, rows = _load_csv(args.csv_path)
    if len(headers) < 2 or len(rows) < 3:
        sys.stderr.write(
            "error: CSV needs ≥2 columns and ≥3 rows.\n")
        return 1

    input_names = headers[:-1]
    obs_name = headers[-1]
    ys = [r[-1] for r in rows]
    y_stdev = statistics.stdev(ys) if len(ys) > 1 else 0.0

    # Take a representative sample so the prompt doesn't balloon on
    # huge datasets. 10 rows is enough for shape-judgement.
    sample_rows = "\n".join(
        ", ".join(f"{v:.4g}" for v in r) for r in rows[:10])

    prompt = PROMPT_TEMPLATE.format(
        n_points=len(rows),
        input_vars=", ".join(input_names),
        obs_name=obs_name,
        y_min=min(ys),
        y_max=max(ys),
        y_mean=statistics.mean(ys),
        y_stdev=y_stdev,
        sample_rows=sample_rows,
        library_json=library_raw.strip(),
    )

    client = Anthropic(api_key=api_key)
    resp = client.messages.create(
        model=args.model,
        max_tokens=args.max_tokens,
        messages=[{"role": "user", "content": prompt}],
    )
    # Messages API returns a list of content blocks; concatenate any
    # text blocks into the response we print.
    body = "".join(
        block.text for block in resp.content
        if getattr(block, "type", "") == "text"
    )
    print(f"# Formula ranking — {args.csv_path.name}\n")
    print(f"Model: `{args.model}`. Dataset: {len(rows)} points, "
          f"inputs {input_names}, output `{obs_name}`.\n")
    print(body.strip())
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
