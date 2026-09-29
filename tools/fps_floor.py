#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Check a profiler CSV against the project's absolute frame-rate floor.

CLAUDE.md's 60 FPS rule: a Release build running
    vestige --demo-flythrough --no-vsync --profile-log=run.csv
must read at least 60 FPS in every one-second `frame,total` sample after
warm-up. An average would hide hitches. tools/perf_gate.py is a different
check: it compares against a committed baseline, not against this floor.

Exit 0 when every sample meets the floor, 1 when one does not, 2 when the
file holds no sample after warm-up.
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
                   help="ignore samples taken before this many seconds")
    args = p.parse_args()

    samples = []
    with args.csv.open(newline="") as f:
        for row in csv.DictReader(f):
            if row["category"] == "frame" and row["name"] == "total":
                t = float(row["time_s"])
                if t >= args.warmup_s:
                    samples.append((t, float(row["fps"])))

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
