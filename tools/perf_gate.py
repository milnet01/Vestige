#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Performance-regression gate — compares a profiler CSV to a committed baseline.

Design: docs/phases/phase_10_perf_regression_gate_design.md (3D_E-0030).

The engine's `--profile-log[=PATH]` writes a profiler CSV (`profile_log.cpp`):

    time_s,category,name,depth,ms,fps

rows are ~1 Hz interval averages; the `ms` column holds MB for `mem` rows; `fps`
is filled only on the `frame,total` row. This tool reduces each metric's series
(drop the warm-up interval, take the median), compares it to a committed baseline
JSON, and reports OK / WARN / FAIL / IMPROVED / MISSING / SKIPPED / INCONCLUSIVE
per metric — exiting non-zero when a *gated* metric regressed.

**Where it runs (design §3):** the live gate belongs on real GPU hardware (the
baseline is captured there); on GPU-less CI only this comparator's `--selftest`
runs, over committed fixtures. GPU timings on a software renderer are meaningless.

Exit codes (design §5.5):
    0  pass          — no FAIL and >=1 conclusive gated metric
    1  regression    — a gated metric FAILed (or WARN/MISSING under --strict-warn)
    2  inconclusive  — no gated metric reached a conclusive verdict (re-runnable)
    3  config error  — unreadable/unsupported baseline (fix the file; do NOT retry)
"""

from __future__ import annotations

import argparse
import csv
import datetime
import json
import statistics
import sys
from pathlib import Path

EXIT_PASS = 0
EXIT_REGRESSION = 1
EXIT_INCONCLUSIVE = 2
EXIT_CONFIG = 3

SCHEMA_VERSION = 1

# Defaults, mirrored into a generated baseline's `reduction` / `thresholds` blocks.
DEFAULT_REDUCTION = {"drop_warmup_intervals": 1, "min_usable_intervals": 3, "stat": "median"}
DEFAULT_THRESHOLDS = {"warn_pct": 10.0, "fail_pct": 25.0, "min_abs_ms": 0.05, "min_abs_mb": 1.0}
# The gate allowlist a fresh --update-baseline writes (present ones only).
DEFAULT_GATE = ["frame,total", "gpu,total", "cpu,total", "mem,gpu_mb"]

# Conclusive verdicts: an actual baseline-vs-current comparison happened (design §5.5).
CONCLUSIVE = {"OK", "WARN", "FAIL", "IMPROVED"}


class ConfigError(Exception):
    """A fix-the-file error (unreadable / unsupported baseline). Maps to exit 3."""


# --- CSV parsing + reduction -------------------------------------------------


def parse_csv(path: Path) -> tuple[dict[str, list[float]], list[float]]:
    """Return ({metric_key: [interval values]}, [frame,total fps series]).

    metric_key is "{category},{name}". `value` is the `ms` column (MB for mem
    rows). A row is skipped — never fatal — if it is short or its `ms` field is
    blank / non-numeric (INV-9); a blank `fps` is normal and never skips a row.
    """
    series: dict[str, list[float]] = {}
    fps_series: list[float] = []
    with path.open(encoding="utf-8", newline="") as fh:
        reader = csv.reader(fh)
        header = next(reader, None)  # discard "time_s,category,name,depth,ms,fps"
        for row in reader:
            if len(row) < 5:
                continue  # too few columns — skip (INV-9)
            category, name, ms_field = row[1], row[2], row[4]
            try:
                value = float(ms_field)
            except ValueError:
                continue  # blank / non-numeric ms — skip (INV-9)
            key = f"{category},{name}"
            series.setdefault(key, []).append(value)
            if key == "frame,total" and len(row) >= 6 and row[5].strip():
                try:
                    fps_series.append(float(row[5]))
                except ValueError:
                    pass  # a blank/garbage fps never invalidates the frame row
    return series, fps_series


def reduce_series(values: list[float], reduction: dict) -> tuple[float | None, int]:
    """Drop warm-up intervals, then median the rest. Returns (median|None, usable).

    None median => fewer than `min_usable_intervals` remain (caller: INCONCLUSIVE).
    """
    drop = int(reduction.get("drop_warmup_intervals", 1))
    min_usable = int(reduction.get("min_usable_intervals", 3))
    usable = values[drop:] if drop > 0 else list(values)
    if len(usable) < min_usable:
        return None, len(usable)
    return statistics.median(usable), len(usable)


# --- Classification (design §5.2 ordered rules) ------------------------------


def _threshold(metrics: dict, key: str, name: str, default_block: dict):
    """Per-metric override else top-level default."""
    entry = metrics.get(key, {})
    if name in entry:
        return entry[name]
    return default_block[name]


def classify(key: str, baseline_entry: dict, current_series: dict[str, list[float]],
             reduction: dict, thresholds: dict, metrics: dict) -> dict:
    """One verdict for one baseline metric, by the ordered classification (§5.2)."""
    is_mem = key.startswith("mem,")
    unit = "mb" if is_mem else "ms"
    baseline_value = baseline_entry.get(unit)

    result = {"metric": key, "baseline": baseline_value, "current": None,
              "delta_pct": None, "verdict": None}

    # Rule 1 — key appears in zero current rows -> MISSING.
    if key not in current_series:
        result["verdict"] = "MISSING"
        return result

    # Rule 2 — present but too few usable intervals -> INCONCLUSIVE.
    current, _usable = reduce_series(current_series[key], reduction)
    if current is None:
        result["verdict"] = "INCONCLUSIVE"
        return result
    result["current"] = current

    # Rule 3 — baseline below the floor -> SKIPPED.
    floor_name = "min_abs_mb" if is_mem else "min_abs_ms"
    floor = _threshold(metrics, key, floor_name, thresholds)
    if baseline_value is None or baseline_value < floor:
        result["verdict"] = "SKIPPED"
        return result

    # Rule 4 — compare. Only increases regress.
    warn_pct = _threshold(metrics, key, "warn_pct", thresholds)
    fail_pct = _threshold(metrics, key, "fail_pct", thresholds)
    delta = (current - baseline_value) / baseline_value * 100.0
    result["delta_pct"] = delta
    if delta >= fail_pct:
        result["verdict"] = "FAIL"
    elif delta >= warn_pct:
        result["verdict"] = "WARN"
    elif delta <= -warn_pct:
        result["verdict"] = "IMPROVED"
    else:
        result["verdict"] = "OK"
    return result


# --- Baseline load + compare -------------------------------------------------


def load_baseline(path: Path) -> dict:
    """Load + validate a baseline JSON. Raises ConfigError (-> exit 3) on any fault."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ConfigError(f"baseline unreadable: {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise ConfigError(f"baseline is not a JSON object: {path}")
    if data.get("schema") != SCHEMA_VERSION:
        raise ConfigError(
            f"baseline schema unsupported (got {data.get('schema')!r}, "
            f"want {SCHEMA_VERSION}): {path}")
    if not isinstance(data.get("metrics"), dict) or not isinstance(data.get("gate"), list):
        raise ConfigError(f"baseline missing 'metrics' or 'gate': {path}")
    return data


def compare(baseline: dict, csv_path: Path, strict_warn: bool = False) -> tuple[list[dict], int]:
    """Compare a CSV against a loaded baseline. Returns (results, exit_code)."""
    reduction = {**DEFAULT_REDUCTION, **baseline.get("reduction", {})}
    thresholds = {**DEFAULT_THRESHOLDS, **baseline.get("thresholds", {})}
    metrics = baseline["metrics"]
    gate = set(baseline["gate"])

    try:
        current_series, _fps = parse_csv(csv_path)
    except OSError as exc:
        raise ConfigError(f"current CSV unreadable: {csv_path}: {exc}") from exc

    results = []
    for key in metrics:
        r = classify(key, metrics[key], current_series, reduction, thresholds, metrics)
        r["gated"] = key in gate
        results.append(r)

    # Exit code from the gated subset only (INV-4).
    gated = [r for r in results if r["gated"]]

    def effective_fail(r: dict) -> bool:
        v = r["verdict"]
        return v == "FAIL" or (strict_warn and v in ("WARN", "MISSING"))

    if any(effective_fail(r) for r in gated):
        code = EXIT_REGRESSION
    elif any(r["verdict"] in CONCLUSIVE for r in gated):
        code = EXIT_PASS
    else:
        code = EXIT_INCONCLUSIVE
    return results, code


# --- update-baseline ---------------------------------------------------------


def update_baseline(csv_path: Path, out_path: Path, hardware: str, scene: str) -> None:
    series, fps_series = parse_csv(csv_path)
    reduced: dict[str, dict] = {}
    for key, values in series.items():
        median, _ = reduce_series(values, DEFAULT_REDUCTION)
        if median is None:
            continue  # too short to characterise — omit
        unit = "mb" if key.startswith("mem,") else "ms"
        entry = {unit: round(median, 3 if unit == "ms" else 0)}
        if key == "frame,total" and fps_series:
            entry["fps"] = round(statistics.median(fps_series[DEFAULT_REDUCTION["drop_warmup_intervals"]:] or fps_series), 1)
        reduced[key] = entry
    gate = [k for k in DEFAULT_GATE if k in reduced]
    doc = {
        "schema": SCHEMA_VERSION,
        "hardware": hardware,
        "captured": datetime.date.today().isoformat(),
        "scene": scene,
        "reduction": dict(DEFAULT_REDUCTION),
        "thresholds": dict(DEFAULT_THRESHOLDS),
        "gate": gate,
        "metrics": reduced,
    }
    out_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")


# --- reporting ---------------------------------------------------------------


def format_report(results: list[dict], code: int) -> str:
    lines = ["  metric              gated  baseline   current    delta%  verdict"]
    for r in sorted(results, key=lambda x: (not x["gated"], x["metric"])):
        base = "" if r["baseline"] is None else f"{r['baseline']:.3f}"
        cur = "" if r["current"] is None else f"{r['current']:.3f}"
        dpct = "" if r["delta_pct"] is None else f"{r['delta_pct']:+.1f}"
        gated = "gate" if r["gated"] else " -  "
        lines.append(f"  {r['metric']:<18}  {gated}  {base:>8}  {cur:>8}  {dpct:>7}  {r['verdict']}")
    verdict = {EXIT_PASS: "PASS", EXIT_REGRESSION: "REGRESSION",
               EXIT_INCONCLUSIVE: "INCONCLUSIVE", EXIT_CONFIG: "CONFIG-ERROR"}[code]
    lines.append(f"  => {verdict} (exit {code})")
    return "\n".join(lines)


# --- self-test (design §10 expected-verdict table) ---------------------------

# Each fixture: expected overall exit, specific gated verdicts to assert, and the
# expected exit under --strict-warn (None = not re-run). Drives fixture contents.
SELFTEST_EXPECT = {
    "ok.csv":        {"exit": EXIT_PASS,        "verdicts": {"frame,total": "OK", "gpu,tiny": "SKIPPED"}, "strict_exit": None},
    "warn.csv":      {"exit": EXIT_PASS,        "verdicts": {"frame,total": "WARN"},                      "strict_exit": EXIT_REGRESSION},
    "regressed.csv": {"exit": EXIT_REGRESSION,  "verdicts": {"frame,total": "FAIL"},                      "strict_exit": None},
    "improved.csv":  {"exit": EXIT_PASS,        "verdicts": {"frame,total": "IMPROVED"},                  "strict_exit": None},
    "missing.csv":   {"exit": EXIT_PASS,        "verdicts": {"gpu,total": "MISSING"},                     "strict_exit": EXIT_REGRESSION},
    "floor.csv":     {"exit": EXIT_PASS,        "verdicts": {"gpu,tiny": "SKIPPED"},                      "strict_exit": None},
    "short.csv":     {"exit": EXIT_INCONCLUSIVE,"verdicts": {"frame,total": "INCONCLUSIVE", "gpu,tiny": "INCONCLUSIVE"}, "strict_exit": None},
}


def run_selftest(root: Path) -> int:
    baseline = load_baseline(root / "baseline.json")
    failures = []
    for fixture, expect in SELFTEST_EXPECT.items():
        csv_path = root / fixture
        results, code = compare(baseline, csv_path, strict_warn=False)
        by_key = {r["metric"]: r["verdict"] for r in results}
        if code != expect["exit"]:
            failures.append(f"{fixture}: exit {code}, expected {expect['exit']}")
        for key, want in expect["verdicts"].items():
            got = by_key.get(key)
            if got != want:
                failures.append(f"{fixture}: {key} = {got}, expected {want}")
        if expect["strict_exit"] is not None:
            _, scode = compare(baseline, csv_path, strict_warn=True)
            if scode != expect["strict_exit"]:
                failures.append(f"{fixture} --strict-warn: exit {scode}, expected {expect['strict_exit']}")
    # INV-4: the non-gated gpu,shadow regresses in ok.csv yet exit stays 0.
    results, code = compare(baseline, root / "ok.csv", strict_warn=False)
    shadow = next((r for r in results if r["metric"] == "gpu,shadow"), None)
    if shadow is None or shadow["gated"]:
        failures.append("ok.csv: gpu,shadow should be present and non-gated (INV-4)")
    elif shadow["verdict"] != "FAIL":
        failures.append(f"ok.csv: gpu,shadow = {shadow['verdict']}, expected FAIL (INV-4 witness)")
    elif code != EXIT_PASS:
        failures.append(f"ok.csv: non-gated regression changed exit to {code} (INV-4)")
    # Config error -> exit 3.
    try:
        load_baseline(root / "bad_schema.json")
        failures.append("bad_schema.json: expected ConfigError, none raised")
    except ConfigError:
        pass

    if failures:
        print("perf_gate --selftest: FAIL", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1
    print(f"perf_gate --selftest: OK ({len(SELFTEST_EXPECT)} fixtures)")
    return EXIT_PASS


# --- CLI ---------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Performance-regression gate (3D_E-0030).")
    p.add_argument("--baseline", type=Path, help="baseline JSON to compare against")
    p.add_argument("--current", type=Path, help="profiler CSV of the run to check")
    p.add_argument("--strict-warn", action="store_true",
                   help="promote WARN and MISSING to FAIL (exit 1)")
    p.add_argument("--json", type=Path, dest="json_out", help="write a machine report")
    p.add_argument("--update-baseline", action="store_true",
                   help="write a baseline JSON from --current instead of comparing")
    p.add_argument("--out", type=Path, help="output path for --update-baseline")
    p.add_argument("--hardware", default="unknown", help="hardware label for --update-baseline")
    p.add_argument("--scene", default="meadow", help="scene label for --update-baseline")
    p.add_argument("--selftest", action="store_true", help="run the built-in fixture self-test")
    p.add_argument("--root", type=Path, help="fixture dir for --selftest")
    args = p.parse_args(argv)

    if args.selftest:
        root = args.root or (Path(__file__).resolve().parent.parent
                             / "tests" / "fixtures" / "perf_gate")
        try:
            return run_selftest(root)
        except ConfigError as exc:
            print(f"perf_gate: config error: {exc}", file=sys.stderr)
            return EXIT_CONFIG

    if args.update_baseline:
        if not args.current or not args.out:
            p.error("--update-baseline requires --current and --out")
        update_baseline(args.current, args.out, args.hardware, args.scene)
        print(f"perf_gate: wrote baseline {args.out}")
        return EXIT_PASS

    if not args.baseline or not args.current:
        p.error("compare mode requires --baseline and --current")
    try:
        baseline = load_baseline(args.baseline)
        results, code = compare(baseline, args.current, strict_warn=args.strict_warn)
    except ConfigError as exc:
        print(f"perf_gate: config error: {exc}", file=sys.stderr)
        return EXIT_CONFIG

    print(format_report(results, code))
    if args.json_out:
        args.json_out.write_text(
            json.dumps({"exit": code, "results": results}, indent=2) + "\n",
            encoding="utf-8")
    return code


if __name__ == "__main__":
    sys.exit(main())
