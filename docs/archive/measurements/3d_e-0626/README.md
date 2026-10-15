# Preserved measurement data

Raw series kept because they are the evidence for constants that now ship, and
re-taking the GTX 1050 half needs that physical box awake.

## 3D_E-0626 — GPU benchmark ramp probe (2026-09-02)

Produced by a TEMPORARY patch to `tests/test_fog_benchmark.cpp` (reverted before
commit) that printed `[RAMP] <frame> <elapsed_ms> <micros>` for 600 consecutive
god-ray timings, replacing the normal warm-up + sample.

- `rx6600_run{1..5}.txt` — RX 6600 (dev rig), preset High, renderScale 1.0.
- `gtx1050_run1.txt` — GTX 1050 via `scripts/wintest.sh`, preset Medium.

What they establish, and what shipped because of it:

- The transient is short on AMD (settles ~frame 15, from 4749 us down to ~554)
  and LONG on NVIDIA (clocks ramp for ~450-500 ms: 1684 us in the first 100 ms,
  1481 us at 400-500 ms, flat ~1460 us after). Hence a TIME-bounded warm-up
  (`kBenchWarmupMillis = 600`), not a frame count.
- Cross-run spread of candidate statistics over the five RX 6600 runs:
  minimum 1.9%, median-of-128 4.7%, p90 67.7%. Hence the gate asserts the
  MINIMUM — a high percentile gates on compositor interference, not the pass.

Summary figures are in `docs/phases/phase_10_fog_design.md` § 8 and in the
3D_E-0626 roadmap bullet; these files are the underlying samples.
