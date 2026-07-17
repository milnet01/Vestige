# Phase 10 — Automated Performance-Regression Gate (design)

Roadmap item: **3D_E-0030** (Rendering Enhancements). Kind: investigate → tool.
Depends on: 3D_E-0027 meadow benchmark scene + the profiler CSV logger
(`engine/profiler/profile_log.{h,cpp}`, meadow §8.1 — **landed**, commit 9507430).

## Contents

1. Goal
2. Non-goals
3. The core finding: where the gate runs (CI vs GPU hardware)
4. Verified context (citations)
5. Detailed design
6. CPU / GPU placement (project Rule 7)
7. Performance & flakiness methodology
8. Accessibility
9. Implementation slices
10. Testing & verification
11. Invariants
12. Cold-eyes loop log
13. Sources

---

## 1. Goal

Turn the deterministic meadow benchmark into a **guard-rail**: after a run writes
a profiler CSV (`--profile-log`), a tool compares the run's key timings against a
**committed baseline** and reports OK / WARN / FAIL per metric, exiting non-zero on
FAIL so a human (or a release/self-hosted-GPU job) is told when a change made the
engine slower. This directly serves the project's hard *"60 FPS minimum"* rule by
catching silent slowdowns before they ship.

**Layman:** make the computer automatically warn us if a change makes the engine
slower.

## 1.1 Non-goals

- **No new engine/runtime code.** The profiler CSV logger already exists; this item
  is a *consumer* of that CSV. Zero changes to `engine/`.
- **No per-PR gate on generic GitHub CI.** See §3 — GPU timings are meaningless on
  the GPU-less shared runner, so a live perf gate there would be pure noise.
- **No auto-refresh of the baseline.** The baseline is committed and refreshed
  deliberately by a human when an intentional perf change lands (§5.4).
- **No statistical significance testing (t-test / Mann-Whitney).** A single warm run
  reduced by median is sufficient at this project's scale; distribution testing is a
  later refinement if false positives appear.

## 2. (folded into §1)

## 3. The core finding: where the gate runs (CI vs GPU hardware)

This is the "investigate" crux the roadmap flagged ("pick metrics, thresholds, and a
baseline-refresh policy that avoid flakiness on shared CI runners").

**Finding:** the *live* perf gate must run on **real GPU hardware** (the dev RX 6600,
or a self-hosted GPU runner / a release-time local step), **not** on generic GitHub
CI. Two independent reasons:

1. **The engine's CI runs GPU-less** (Mesa llvmpipe software renderer — see the
   local-ci renderer-parity note and `shader_lint.py`'s own rationale). The
   profiler's `GpuTimer` only emits per-pass rows once its timestamp queries are warm
   (`profile_log.cpp:162`, `gpu.hasResults()`); on llvmpipe those GPU timings are
   absent or reflect software rasterization, not the shipping GPU path. Frame time on
   llvmpipe is orders of magnitude off and unrelated to real-hardware FPS.
2. **Shared-runner noise.** Published CI-benchmark work shows a standard GitHub runner
   needs a ~7 % gate to hold a <1 % false-positive rate; only isolated/bare-metal
   runners reach ~1.5 % (CodSpeed; Android `benchmark` CI). A GPU perf gate on shared
   virtualised runners would either miss real regressions or cry wolf.

**Consequence — the split that makes this both safe and CI-testable:**

- **Gate LOGIC** (parse CSV → reduce → compare → verdict) is pure text/number
  processing. It is **unit-tested in CI** over committed fixture CSVs (valid on a
  GPU-less runner — no GPU involved) exactly like `localization_audit.py` /
  `shader_lint.py`.
- **Gate EXECUTION against the real baseline** (capture a fresh meadow CSV, compare to
  the committed RX 6600 baseline) is a **developer / release-time step on GPU
  hardware**, documented as a one-liner (§5.5). It is *not* wired into the generic CI
  matrix.

So tonight's deliverable is complete and verifiable headlessly: the comparator + its
tests. The committed baseline numbers are captured later on the user's RX 6600 (the
tool ships with an `--update-baseline` mode and a synthetic placeholder baseline so
the machinery is exercised without real hardware).

## 4. Verified context (citations)

- **CSV schema** — `engine/profiler/profile_log.cpp:96` writes the header
  `time_s,category,name,depth,ms,fps`. Rows are ~1 Hz **interval averages**
  (`WRITE_INTERVAL_SEC = 1.0`, `profile_log.h:105`). Columns:
  - `time_s` — cumulative seconds since `open()` (2 dp).
  - `category` — one of `frame` / `gpu` / `cpu` / `mem` (`profile_log.cpp:19-29`).
  - `name` — pass/scope name, or `total` / `gpu_mb`.
  - `depth` — CPU-scope nesting; `0` for frame/gpu/mem/totals.
  - `ms` — milliseconds (3 dp) for frame/gpu/cpu; **MB (0 dp) for `mem` rows** — the
    same column is reused (`profile_log.cpp:54-59`).
  - `fps` — filled **only** on the `frame,total` row (1 dp), blank elsewhere
    (`profile_log.cpp:62`).
- **Metrics available** (from `profile_log.cpp:156-186`): `frame,total` (+ fps),
  `gpu,total` and `gpu,<pass>`, `cpu,total` and `cpu,<scope>`, `mem,gpu_mb`.
- **Tooling pattern to mirror** — `tools/shader_lint.py` + `tools/localization_audit.py`:
  `#!/usr/bin/env python3`, MIT header, module docstring, `argparse`, `--strict`
  self-test mode, `--root` fixture override, exit 0/1. Gated in `tests/CMakeLists.txt`
  (`find_package(Python3 … QUIET)`; a positive `--strict` test + a `WILL_FAIL`
  negative test against a seeded fixture).

## 5. Detailed design

New files (no `engine/` changes):

- `tools/perf_gate.py` — the comparator (pure stdlib: `argparse`, `csv`, `json`,
  `statistics`).
- `tools/perf_gate/baseline_placeholder.json` — a committed synthetic baseline so the
  tool + ctest work with no GPU. The real `baseline_rx6600.json` is added later by the
  user via `--update-baseline`.
- `tests/fixtures/perf_gate/` — synthetic CSVs (`baseline.csv`, `ok.csv`,
  `regressed.csv`, `short.csv`) + `baseline.json` for the ctest gates.

### 5.1 Parse + reduce

1. Read the CSV. Skip the header. Each row → `(time_s, category, name, depth, value)`.
   `value` = the `ms` column parsed as float (it holds MB for `mem` rows; the unit is
   carried by `category`, matching the writer). A blank `ms` is skipped.
2. Group values by metric key `"{category},{name}"` (e.g. `frame,total`, `gpu,shadow`,
   `mem,gpu_mb`). Depth is not part of the key — gated metrics are all depth-0 totals
   and named GPU passes; two same-named scopes at different depths never occur among
   gated metrics.
3. Each metric now has a time-series (one value per ~1 s interval). **Reduce:**
   - **Drop the first `drop_warmup_intervals` (default 1) intervals** — the first
     second covers shader compile, texture upload, and `GpuTimer` warming
     (`gpu.hasResults()` is false until queries are warm), so its numbers are not
     representative.
   - Take the **median** of the remaining values — robust to an occasional frame spike
     from a background process on a dev machine, unlike the mean.
   - If fewer than `min_usable_intervals` (default 3) remain after the warmup drop, the
     metric is **INCONCLUSIVE** (reported, never a FAIL) and the run is flagged
     "too short" — a truncated run must not manufacture a false regression.

### 5.2 Compare

For each metric present in **both** the baseline and the (reduced) current run and on
the **gate allowlist**:

- `delta_pct = (current - baseline) / baseline * 100`.
- Direction: for `ms` / `mb` metrics, **only increases regress** (a decrease is an
  improvement → `IMPROVED`, never a FAIL). `fps` is *informational only* — it is a
  pure function of `frame,total` ms, so gating it too would double-count the same
  quantity; it is printed for context.
- **Absolute floor** `min_abs` (default 0.05 ms): if the baseline value is below the
  floor, the metric is `SKIPPED` (a +25 % swing on a 0.01 ms pass is noise, not a
  regression). Applies to ms metrics only; `mem_gpu_mb` uses its own `min_abs_mb`
  (default 1 MB).
- Verdict per metric:
  - `FAIL` if `delta_pct >= fail_pct` (default 25).
  - `WARN` if `delta_pct >= warn_pct` (default 10).
  - `IMPROVED` if `delta_pct <= -warn_pct`.
  - else `OK`.
- A baseline metric **absent from the current run** → `WARN` ("metric missing —
  baseline may be stale after a pass rename"), not FAIL: renaming/removing a pass is a
  legitimate code change, and failing on it would punish refactors.
- A current metric **absent from the baseline** → informational note ("new metric —
  refresh the baseline to gate it").

### 5.3 Gate allowlist + thresholds

Defaults live in the baseline JSON so they are reviewed and versioned with the
numbers, and are overridable per-metric there:

```json
{
  "schema": 1,
  "hardware": "synthetic-placeholder",
  "captured": "2026-07-17",
  "scene": "meadow",
  "reduction": { "drop_warmup_intervals": 1, "min_usable_intervals": 3, "stat": "median" },
  "thresholds": { "warn_pct": 10.0, "fail_pct": 25.0, "min_abs_ms": 0.05, "min_abs_mb": 1.0 },
  "gate": ["frame,total", "gpu,total", "cpu,total", "mem,gpu_mb"],
  "metrics": {
    "frame,total": { "ms": 8.000, "fps": 125.0 },
    "gpu,total":   { "ms": 5.000 },
    "gpu,shadow":  { "ms": 1.200 },
    "cpu,total":   { "ms": 2.000 },
    "mem,gpu_mb":  { "mb": 400 }
  }
}
```

`gate` is the allowlist; a metric may exist in `metrics` (for context / future gating)
without being gated. Per-metric threshold overrides: `metrics["gpu,total"].fail_pct`
etc. override the top-level defaults when present.

### 5.4 Baseline-refresh policy

The baseline is a committed artifact. It is refreshed **deliberately**, by a human, in
its own reviewed commit, when a change *intentionally* moves perf (a new effect, an
optimization) — never silently in the same commit as the change it would mask. The
`captured` date + `hardware` string make a stale or wrong-hardware baseline obvious in
review. This is the "defined baseline your CI compares against" pattern from the CI-
benchmark literature (§13).

### 5.5 Modes (CLI)

- **Compare (default):**
  `perf_gate.py --baseline tools/perf_gate/baseline_rx6600.json --current run.csv`
  → prints a per-metric table, exits `1` on any FAIL else `0`. `--strict-warn`
  promotes WARN→FAIL. `--json OUT.json` writes a machine report.
- **Update baseline:**
  `perf_gate.py --update-baseline --current run.csv --out baseline_rx6600.json
  --hardware "RX 6600 (RDNA2)"` → reduces the CSV and writes a baseline JSON (carrying
  today's date, hardware, the default thresholds, and every reduced metric). Threshold
  editing afterwards is a hand-edit — reviewed like any config.
- **Self-test (`--strict`, for ctest):** with no `--current`/`--baseline`, runs the
  built-in fixture comparison (`--root` overrides the fixture dir) and exits non-zero
  if the *expected* verdicts don't match — i.e. it tests the comparator itself, the
  same self-gating shape as `shader_lint.py --strict`.

The one-liner the user runs on the RX 6600 to (a) create the real baseline and (b)
guard a change is documented in the tool `--help` and the README CLI section.

## 6. CPU / GPU placement (project Rule 7)

Entirely **CPU / offline**. The gate is a Python text tool that runs after the engine
exits; it does no per-pixel/per-vertex work. The *data* it consumes was produced on the
GPU (per-pass timestamp queries) and reduced on the CPU by the profiler. Rule 7's
"branching / sparse / decision / I/O → CPU" applies squarely: this is file I/O +
comparison logic. No dual CPU/GPU impl, no parity test needed.

## 7. Performance & flakiness methodology

- **Median over mean** — robust to single-interval spikes (background load, a GC-like
  hitch) without discarding the run.
- **Warmup drop** — the first interval is unrepresentative (compile/upload/warm-up);
  dropping it is standard benchmark hygiene (Android `benchmark`, §13).
- **Generous default gate (warn 10 %, fail 25 %)** — matched to warm-but-not-isolated
  dev-GPU variance. The literature shows even a shared *GitHub* runner needs ~7 % for a
  <1 % false-positive rate; on the dev RX 6600 (dedicated, no virtualization noise) 25 %
  is comfortably above run-to-run jitter while still catching the regressions that
  matter (a 60→45 FPS drop is +33 % frame time → FAIL). The user can tighten per-metric
  once real baselines exist.
- **Absolute floor** — sub-0.05 ms passes are noise-dominated; a relative gate on them
  is meaningless, so they are skipped.
- **Min-interval guard** — a run shorter than ~4 s (≤3 usable intervals) is
  INCONCLUSIVE, never a FAIL — prevents a truncated/crashed run from reading as a
  regression.
- **Determinism** — the meadow scene is seed-deterministic (meadow design INV), so the
  workload is stable run to run; the only variance is timing noise, which the above
  absorbs.

The gate tool itself runs in well under a second on a CSV of a few hundred rows —
negligible.

## 8. Accessibility

N/A — a developer command-line tool with no runtime UI. Output is plain text with an
explicit status word per row (`OK`/`WARN`/`FAIL`/`IMPROVED`/`SKIPPED`/`INCONCLUSIVE`),
not colour-only, so it is readable in any terminal and in CI logs.

## 9. Implementation slices

Single slice (small, self-contained):

1. `tools/perf_gate.py` — parse + reduce + compare + `--update-baseline` + `--strict`
   self-test. *Verify:* `python3 tools/perf_gate.py --strict` exits 0 on the good
   fixtures; a seeded-regression fixture makes it exit 1.
2. `tests/fixtures/perf_gate/` + `tools/perf_gate/baseline_placeholder.json`.
   *Verify:* files parse as valid CSV/JSON.
3. `tests/CMakeLists.txt` — `PerfGateStrict` (must pass) + `PerfGateCatchesRegression`
   (`WILL_FAIL`) mirroring the lint gates. *Verify:* `ctest -R PerfGate` green.
4. README CLI section + CHANGELOG + ROADMAP flip.

## 10. Testing & verification

- **ctest `PerfGateStrict`** — the comparator on the good fixtures yields the expected
  verdicts (all OK/IMPROVED, exit 0).
- **ctest `PerfGateCatchesRegression`** (`WILL_FAIL TRUE`) — the comparator on a
  `regressed.csv` (frame time +40 %) exits 1. Pins that the gate actually fails on a
  real regression (the same "prove it catches a violation" shape as
  `ShaderLintCatchesViolation`).
- **Self-test coverage inside `--strict`** asserts, over fixtures: median reduction,
  warmup drop, the absolute floor (a tiny-value pass with a huge % swing stays OK), the
  missing-metric→WARN path, the improvement path, and the short-run→INCONCLUSIVE path.
- All fixtures are committed text — the whole suite runs on GPU-less CI.

## 11. Invariants

- **INV-1** The gate makes **no** changes under `engine/` — it only reads a CSV the
  engine already writes.
- **INV-2** A shorter-than-`min_usable_intervals` run is **INCONCLUSIVE**, never FAIL.
- **INV-3** A metric decrease is **never** a FAIL (improvements don't break the build).
- **INV-4** Only metrics on the baseline `gate` allowlist affect the exit code; all
  others are reported but non-gating.
- **INV-5** `fps` is informational only (a function of `frame,total` ms) — never
  independently gated.
- **INV-6** A baseline metric missing from the current run is a **WARN**, not a FAIL
  (a legitimate pass rename must not break the build).
- **INV-7** The gate is **not** wired into the generic GPU-less CI matrix; only the
  fixture-based comparator self-test is. Real-baseline execution is a dev/release step
  on GPU hardware.
- **INV-8** The baseline is refreshed only in its own deliberate, human-reviewed
  commit — never silently alongside the change it would mask.

## 12. Cold-eyes loop log

- _Loop 1: pending — dispatched via `/cold-eyes` after this draft._

## 13. Sources

- Chris Craik, "Fighting regressions with Benchmarks in CI", Android Developers —
  warmup handling, median/thresholds, dropping unrepresentative early iterations.
  https://medium.com/androiddevelopers/fighting-regressions-with-benchmarks-in-ci-6ea9a14b5c71
- CodSpeed, "Benchmarks in CI: Escaping the Cloud Chaos" — shared-runner noise
  (noisy neighbours through virtualization); ~7 % gate on standard GitHub runners for
  <1 % false positives vs ~1.5 % on isolated bare-metal; the committed-baseline
  pattern. https://codspeed.io/blog/benchmarks-in-ci-without-noise
- Engine sources: `engine/profiler/profile_log.{h,cpp}` (CSV schema),
  `tools/shader_lint.py` + `tests/CMakeLists.txt` (the Python-tool ctest-gate pattern),
  `docs/phases/phase_10_meadow_benchmark_scene_design.md` §8.1 (profiler CSV logging).
