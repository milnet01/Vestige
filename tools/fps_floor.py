#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Check a profiler CSV against the project's absolute frame-rate floor.

CLAUDE.md's 60 FPS rule: a Release build running
    vestige --demo-flythrough --no-vsync --profile-log=run.csv
must read at least 60 FPS in every one-second `frame,total` sample that
lies wholly after warm-up. An average would hide hitches. frameledger compare
(external/frameledger/frameledger.pyz) is a different check: it compares against a saved
baseline, not against this floor.

Exit 0 when every sample meets the floor, 1 when one does not, 2 when the
file cannot be judged: it holds no sample after warm-up, or a row that cannot
be read (a run killed mid-write leaves one).
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("csv", type=Path, help="profiler CSV from --profile-log")
    p.add_argument("--min-fps", type=float, default=60.0)
    p.add_argument("--warmup-s", type=float, default=2.0,
                   help="ignore samples that cover any time before this many seconds")
    args = p.parse_args()

    # A sample covers the time since the one before it, or since the start
    # for the first. It counts only when all of that is after warm-up: a first
    # sample at 2.3 s is the scene loading, not a frame rate.
    samples = []
    covered_from = 0.0
    with args.csv.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                if row["category"] == "frame" and row["name"] == "total":
                    t = float(row["time_s"])
                    fps = float(row["fps"])
                    if covered_from >= args.warmup_s:
                        samples.append((t, fps))
                    covered_from = t
            except (KeyError, TypeError, ValueError):
                # A missing column reads as None (TypeError) or is absent
                # from the header (KeyError); a cut-off number is a ValueError.
                print(f"line {reader.line_num} of {args.csv} is not a readable "
                      f"profiler row")
                return 2

    if not samples:
        print(f"no frame samples after {args.warmup_s} s in {args.csv}")
        return 2
    worst_t, worst = min(samples, key=lambda s: s[1])
    below = [s for s in samples if s[1] < args.min_fps]
    print(f"{len(samples)} samples, worst {worst:.1f} FPS at {worst_t:.1f} s, "
          f"{len(below)} below {args.min_fps:g}")
    return 1 if below else 0


if __name__ == "__main__":
    sys.exit(main())
