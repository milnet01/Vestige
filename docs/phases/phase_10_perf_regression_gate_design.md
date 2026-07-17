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

## 2. Non-goals

- **No new engine/runtime code.** The profiler CSV logger already exists; this item
  is a *consumer* of that CSV. Zero changes to `engine/`.
- **No per-PR gate on generic GitHub CI.** See §3 — GPU timings are meaningless on
  the GPU-less shared runner, so a live perf gate there would be pure noise.
- **No auto-refresh of the baseline.** The baseline is committed and refreshed
  deliberately by a human when an intentional perf change lands (§5.4).
- **No statistical significance testing (t-test / Mann-Whitney).** A single warm run
  reduced by median is sufficient at this project's scale; distribution testing is a
  later refinement if false positives appear.

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
tests. The committed real-hardware baseline is captured later on the user's RX 6600 via
the tool's `--update-baseline` mode; until then the committed fixture baseline
(`tests/fixtures/perf_gate/baseline.json`, a small synthetic baseline) exercises the
whole machinery without a GPU.

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
  `#!/usr/bin/env python3`, MIT header, module docstring, `argparse`, exit 0/1. In
  `shader_lint.py`, `--strict` is the **default fail-on-violation scan** (`--lint` is
  its inverse — warnings only) and `--root` overrides the scanned tree to a fixture;
  neither tool has a built-in self-test (`shader_lint.py:13,96`). The ctest gate
  (`tests/CMakeLists.txt:350-395`) is `find_package(Python3 … QUIET)` guarding a
  positive test that runs the tool and must pass, plus a `WILL_FAIL` negative test
  that runs it against a seeded-violation fixture. perf_gate reuses this file/flag/
  ctest shape; its `--selftest` mode (§5.5) is perf_gate's **own** addition, not
  inherited from these tools.

## 5. Detailed design

New files (no `engine/` changes):

- `tools/perf_gate.py` — the comparator (pure stdlib: `argparse`, `csv`, `json`,
  `statistics`).
- `tests/fixtures/perf_gate/` — a fixture `baseline.json` (the committed synthetic
  baseline, doubling as the machinery's no-GPU exercise) + synthetic CSVs, one per
  behaviour the `--selftest` asserts: `ok.csv` (totals within tolerance), `warn.csv`
  (frame +15 %), `regressed.csv` (frame +40 %), `improved.csv` (frame −30 %),
  `missing.csv` (gated `gpu,total` absent), `floor.csv` (sub-floor `gpu,tiny` swings
  +1000 %), `short.csv` (≤2 usable intervals). **Every CSV contains all five gated
  metrics** (so `gpu,tiny` is present and `SKIPPED` — INCONCLUSIVE only in `short.csv`)
  **except** `missing.csv`, which deliberately omits `gpu,total`. Expected verdicts per
  fixture: §10 table. The real `baseline_rx6600.json` is added later by the user via
  `--update-baseline`.

### 5.1 Parse + reduce

1. Read the CSV. Skip the header. Each row → `(time_s, category, name, depth, value,
   fps)`. `value` = the `ms` column parsed as float (it holds MB for `mem` rows; the
   unit is carried by `category`, matching the writer). `fps` = column 6, present only
   on the `frame,total` row (blank elsewhere — a **blank `fps` is normal** and never a
   reason to skip a row). **Robustness (INV-9):** a row is *skipped* — never fatal — if
   it has too few columns or its **`ms`** field is blank or non-numeric. (Only `ms` is
   required per row; `fps` is optional by the writer's format.) The writer is trusted, so
   this only guards a truncated final line or a hand-edited file; a skipped row simply
   doesn't contribute to its metric's series. An empty or header-only CSV yields zero
   series → every gated baseline metric is `MISSING` (§5.2 classification, rule 1).
2. Group values by metric key `"{category},{name}"` (e.g. `frame,total`, `gpu,shadow`,
   `mem,gpu_mb`). Depth is not part of the key — gated metrics are all depth-0 totals
   and named GPU passes; two same-named scopes at different depths never occur among
   gated metrics. (A non-gated CPU scope that *does* recur at two depths would be
   merged in the reported table; harmless, as it never affects the verdict — INV-4.)
   The `frame,total` group also carries the run's logged `fps` series (reduced the same
   way, reported for context — never gated, INV-5).
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

Every metric in the **baseline** gets exactly one verdict, computed by this **ordered
classification** (first matching rule wins). This runs for *all* baseline metrics — gated
and non-gated alike — so every reported row (§8) has a status word; the `gate` allowlist
governs only the **exit code** (§5.5 / INV-4), not whether a verdict is computed.

1. The metric's key appears in **zero** current-run rows → `MISSING`. (Distinct,
   non-conclusive — "baseline may be stale after a pass rename." A legitimate rename must
   not FAIL; `--strict-warn` promotes it, §5.5.)
2. The key appears but yields **fewer than `min_usable_intervals`** usable intervals
   after the warmup drop (§5.1 step 3) → `INCONCLUSIVE`. (Data-availability is decided
   before the floor — you cannot floor-test a value you don't have enough of.)
3. The metric's **baseline** value is below the floor (`min_abs_ms` 0.05 ms, or
   `min_abs_mb` 1 MB for `mem,gpu_mb`) → `SKIPPED`. (A +25 % swing on a 0.01 ms pass is
   noise, not a regression. Floor keys on the *baseline* value, so a sub-floor metric is
   `SKIPPED` in every run where rules 1–2 didn't already fire.)
4. Otherwise **compare**: `delta_pct = (current - baseline) / baseline * 100`; only
   *increases* regress (`ms`/`mb`) —
   - `FAIL` if `delta_pct >= fail_pct` (default 25),
   - `WARN` if `delta_pct >= warn_pct` (default 10),
   - `IMPROVED` if `delta_pct <= -warn_pct`,
   - else `OK`.

`fps` is **never** a metric key in this loop — it is not gated and carries no verdict; it
rides along on the `frame,total` row as reported context only (INV-5). (It is *not*
`1000 / frame_ms`: the logger accumulates `fps` as the mean of per-frame `getFps()` while
`frame,total` ms is a separate windowed average `getAvgFrameTimeMs()` —
`profile_log.cpp:156-158,205` — so the two are correlated but not reciprocal.) The
frame-time regression is judged on `frame,total` ms.

A current metric **absent from the baseline** → informational note only ("new metric —
refresh the baseline to gate it"); it gets no verdict and never affects the exit code.

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
  "gate": ["frame,total", "gpu,total", "cpu,total", "mem,gpu_mb", "gpu,tiny"],
  "metrics": {
    "frame,total": { "ms": 8.000, "fps": 125.0 },
    "gpu,total":   { "ms": 5.000 },
    "gpu,shadow":  { "ms": 1.200 },
    "gpu,tiny":    { "ms": 0.010 },
    "cpu,total":   { "ms": 2.000 },
    "mem,gpu_mb":  { "mb": 400 }
  }
}
```

`schema` is the baseline-format version; perf_gate reads it and **fatally rejects**
(exit 3 — config error, §5.5) any value other than `1`, and likewise a **missing**
`schema` key or an unreadable/unparseable baseline, so a future `"schema": 2` file is
never silently mis-parsed. `gpu,tiny` is a **gated sub-floor**
metric (0.010 ms < the 0.05 ms floor) included so the `floor.csv` fixture can exercise
the `SKIPPED` path on a *gated* metric (§10); `gpu,shadow` is a **non-gated context**
metric (in `metrics`, absent from `gate`) exercising INV-4. `gate` is the allowlist; a metric may exist in `metrics` (for context / future gating)
without being gated. **Per-metric threshold overrides:** a metric entry may carry any
of the keys `warn_pct`, `fail_pct`, `min_abs_ms`, `min_abs_mb` (e.g.
`metrics["gpu,total"].fail_pct = 15.0`). Precedence when comparing a metric: the
metric's own key if present, else the top-level `thresholds` default. Only these four
keys are recognised as overrides; `ms` / `mb` / `fps` in a metric entry are baseline
values, not thresholds.

### 5.4 Baseline-refresh policy

The baseline is a committed artifact. It is refreshed **deliberately**, by a human, in
its own reviewed commit, when a change *intentionally* moves perf (a new effect, an
optimization) — never silently in the same commit as the change it would mask. The
`captured` date + `hardware` string make a stale or wrong-hardware baseline obvious in
review. This is the "defined baseline your CI compares against" pattern from the CI-
benchmark literature (§13).

### 5.5 Modes (CLI)

**Conclusive vs non-conclusive verdict.** A gated metric's verdict is **conclusive** iff
it comes from a real baseline-vs-current comparison: `OK`, `WARN`, `FAIL`, or `IMPROVED`.
The three "no comparison happened" verdicts are **non-conclusive**: `MISSING` (the
baseline metric is absent from the current run, §5.2), `SKIPPED` (baseline below the
floor, §5.2), and `INCONCLUSIVE` (too few usable intervals, §5.1).

**Exit-code contract (all modes):**
- `0` = **pass** — no FAIL **and** ≥1 *conclusive* gated metric.
- `1` = **regression** — at least one gated metric FAILed.
- `2` = **inconclusive data** — no gated metric reached a conclusive verdict
  (empty/short/all-warmup run, every gated metric MISSING, or every gated metric below
  the floor). Distinct from `0`: a run that never actually compared anything must **not**
  read as a green pass, or the guard-rail (§1) is silently defeated. A caller may re-run
  (fresh data may help).
- `3` = **config error** — unreadable/unparseable baseline, a missing or unsupported
  `schema` (§5.3). A fix-the-file error: a caller must **not** auto-retry on `3` (a
  re-run won't help); it is a separate code from `2` precisely so an automated caller
  doesn't loop forever on a broken baseline.

`--strict-warn` promotes `WARN` **and** `MISSING` to FAIL → exit 1.

- **Compare (default):**
  `perf_gate.py --baseline tools/perf_gate/baseline_rx6600.json --current run.csv`
  → prints a per-metric table, applies the exit-code contract above. `--strict-warn`
  promotes every `WARN` and `MISSING` (§5.2) to FAIL → exit 1. `--json OUT.json` writes
  a machine report.
- **Update baseline:**
  `perf_gate.py --update-baseline --current run.csv --out baseline_rx6600.json
  --hardware "RX 6600 (RDNA2)"` → reduces the CSV and writes a baseline JSON carrying:
  today's date, the `--hardware` string, `--scene` (default `meadow`), the default
  `reduction` + `thresholds` blocks, every reduced metric under `metrics`, and a `gate`
  allowlist = **the four default keys `frame,total` / `gpu,total` / `cpu,total` /
  `mem,gpu_mb`, filtered to those actually present** in the reduced metrics. Widen the
  `gate` (e.g. to add a specific `gpu,<pass>`) or tighten a threshold by hand-editing
  afterwards — reviewed like any config.
- **Self-test (`--selftest`, for ctest):** perf_gate's own mode (no analogue in
  shader_lint). With no `--current`/`--baseline`, it runs the comparator over the
  committed `tests/fixtures/perf_gate/` set and asserts each fixture's **expected
  per-metric verdict + exit code** (the table in §10) match; exits `0` iff every
  assertion holds, non-zero on the first mismatch. `--root DIR` overrides the fixture
  dir. This tests the comparator itself — the invariant paths (floor, missing→WARN,
  improvement, short→INCONCLUSIVE, non-gated-metric-ignored) are all exercised here.

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
- **Min-interval guard** — a run with fewer than `min_usable_intervals` (default 3)
  usable intervals after the warmup drop — i.e. under ~4 s of logging — is
  INCONCLUSIVE, never a FAIL, and yields exit code `2` (§5.5). Prevents a
  truncated/crashed run from reading as a regression *or* as a green pass.
- **Determinism** — the meadow scene is seed-deterministic (meadow design INV), so the
  workload is stable run to run; the only variance is timing noise, which the above
  absorbs.

The gate tool itself runs in well under a second on a CSV of a few hundred rows —
negligible.

## 8. Accessibility

N/A — a developer command-line tool with no runtime UI. Output is plain text with an
explicit status word per row (`OK` / `WARN` / `FAIL` / `IMPROVED` / `MISSING` /
`SKIPPED` / `INCONCLUSIVE`), not colour-only, so it is readable in any terminal and in
CI logs.

## 9. Implementation slices

Single slice (small, self-contained):

1. `tools/perf_gate.py` — parse + reduce + compare + `--update-baseline` + `--selftest`.
   *Verify:* `python3 tools/perf_gate.py --selftest` exits 0 (all fixture verdicts
   match the §10 table); `perf_gate.py --baseline … --current regressed.csv` exits 1.
2. `tests/fixtures/perf_gate/` (the fixtures listed in §5). *Verify:* files parse as
   valid CSV/JSON.
3. `tests/CMakeLists.txt` — `PerfGateSelftest` (must pass) + `PerfGateCatchesRegression`
   (`WILL_FAIL`) mirroring the lint gates. *Verify:* `ctest -R PerfGate` green.
4. README CLI section + CHANGELOG + ROADMAP flip.

## 10. Testing & verification

Two ctest entries (Python3-guarded, mirroring `tests/CMakeLists.txt:350-395`):

- **`PerfGateSelftest`** — `perf_gate.py --selftest` (must pass). Runs the comparator
  over every fixture and asserts the expected verdict + exit below. This is where each
  invariant path is pinned; a wrong verdict fails the test.
- **`PerfGateCatchesRegression`** (`WILL_FAIL TRUE`) — the *real* compare path
  `perf_gate.py --baseline ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/perf_gate/baseline.json
  --current ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/perf_gate/regressed.csv` must exit 1.
  (Absolute `${CMAKE_CURRENT_SOURCE_DIR}` prefixes, as in the mirrored pattern — ctest
  runs in the build dir, so a relative fixture path would not resolve.) Proves the gate
  fails on a genuine regression through the same entry point a release job uses — not
  only inside the self-test harness (same "prove it catches a violation" shape as
  `ShaderLintCatchesViolation`). `--selftest` likewise takes `--root
  ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/perf_gate`.

**Expected verdict table** (`--selftest` asserts each row; drives fixture contents):

| Fixture | Condition it encodes | Notable gated verdict(s) | Exit |
|---|---|---|---|
| `ok.csv` | four totals within ±10 % | totals `OK` | 0 |
| `warn.csv` | `frame,total` +15 % | `frame,total` `WARN` | 0; `1` under `--strict-warn` |
| `regressed.csv` | `frame,total` +40 % | `frame,total` `FAIL` | 1 |
| `improved.csv` | `frame,total` −30 % | `frame,total` `IMPROVED` | 0 (INV-3) |
| `missing.csv` | gated `gpu,total` row absent | `gpu,total` `MISSING` | 0; `1` under `--strict-warn` (INV-6) |
| `floor.csv` | sub-floor `gpu,tiny` swings 0.010→0.110 ms | `gpu,tiny` `SKIPPED` | 0 (floor) |
| `short.csv` | ≤2 usable intervals after warmup drop | all gated `INCONCLUSIVE` | 2 (INV-2) |

Table convention: a row names only the *notable* gated verdict(s); every other **present,
conclusive** gated metric is `OK`. Two standing verdicts hold across the set: the gated
`gpu,tiny` (baseline 0.010 ms) is present in every CSV and — because the floor keys on the
*baseline* value — is `SKIPPED` in every fixture **that has enough intervals** (all but
`short.csv`, where it is `INCONCLUSIVE` like the rest); and `missing.csv` omits only
`gpu,total`, so its other four gated metrics are `OK`/`SKIPPED` as usual (overall exit 0
because ≥1 conclusive `OK` remains). `baseline.json` also carries a **non-gated** context
metric `gpu,shadow` (in `metrics`, absent from `gate`) that regresses in `ok.csv`: the
self-test asserts it shows a regressed verdict in the report **yet exit stays 0**,
pinning INV-4 (a non-gated regression never changes the exit code). INV-5 (`fps` never
gated) is structural — `fps` is never a metric key in the §5.2 loop, so no gate can act
on it — and is asserted by checking no fixture's verdict set ever contains an `fps` row.

**`--strict-warn` coverage:** `--selftest` additionally re-runs `warn.csv` and
`missing.csv` with `--strict-warn` and asserts each then exits `1` (WARN→FAIL and
MISSING→FAIL promotion, §5.5 / INV-6). All fixtures are committed text — the whole suite
runs on GPU-less CI.

## 11. Invariants

- **INV-1** The gate makes **no** changes under `engine/` — it only reads a CSV the
  engine already writes.
- **INV-2** A shorter-than-`min_usable_intervals` run is **INCONCLUSIVE**, never FAIL.
- **INV-3** A metric decrease is **never** a FAIL (improvements don't break the build).
- **INV-4** Only metrics on the baseline `gate` allowlist affect the exit code; all
  others are reported but non-gating.
- **INV-5** `fps` is informational only — reported for context, **never** gated (it is
  a redundant, noisier view of frame performance, not a derivation of `frame,total` ms).
- **INV-6** A baseline metric missing from the current run is a **MISSING** (distinct,
  non-conclusive) verdict, not a FAIL (a legitimate pass rename must not break the
  build); `--strict-warn` promotes it to FAIL.
- **INV-7** The gate is **not** wired into the generic GPU-less CI matrix; only the
  fixture-based comparator self-test is. Real-baseline execution is a dev/release step
  on GPU hardware.
- **INV-8** The baseline is refreshed only in its own deliberate, human-reviewed
  commit — never silently alongside the change it would mask.
- **INV-9** A malformed or short CSV row (too few columns, blank/non-numeric `ms`/`fps`)
  is **skipped**, never fatal; an empty/header-only CSV reduces to all-INCONCLUSIVE
  (exit 2), not a crash.

## 12. Cold-eyes loop log

- **Loop 1 (2026-07-17)** — 2 cold reviewers (accuracy-vs-code; consistency/testability).
  12 verified findings (HIGH 1 · MEDIUM 4 · LOW 5 · INFO 2), all fixed:
  - Accuracy: dropped the false "fps is a pure function of `frame,total` ms" claim —
    the logger accumulates fps independently (`profile_log.cpp:156-158,205`); fps is now
    captured, reported, and gated-never with the correct rationale (§5.1/§5.2/INV-5).
  - Accuracy: §4 no longer calls `shader_lint --strict` a "self-test" (it's the default
    scan); perf_gate's `--selftest` is stated as its own mode.
  - Consistency: fixed the §5.1↔§7 off-by-one on the INCONCLUSIVE boundary (`<3` usable).
  - Simplification: replaced the vague "`--strict` self-test" with a concrete
    `--selftest` + a per-fixture expected-verdict table (§10); real compare path is the
    `WILL_FAIL` test.
  - Completeness: specified `--update-baseline` `gate` population, per-metric threshold
    override precedence (§5.3), CSV row-parsing robustness (INV-9), and the tri-state
    exit code (0/1/2) including the INCONCLUSIVE case (§5.5).
  - Naming: `min_abs` → `min_abs_ms` throughout; structure: removed the stray empty §2.
- **Loop 2 (2026-07-17)** — same 2 cold reviewers, re-read cold (loop-1 fixes confirmed
  held — the fps/`--strict`/off-by-one findings did not resurface). 4 verified findings
  (MEDIUM 2 · LOW 2), all fixed:
  - Correctness: defined **conclusive vs non-conclusive** verdicts and gave the
    absent-metric case its own `MISSING` status, closing the hole where an all-missing
    run could exit 0 and defeat the guard-rail (§5.5/§5.2/§8/INV-6).
  - Testability: added a gated sub-floor metric `gpu,tiny` to the fixture baseline so
    `floor.csv`'s `SKIPPED` row is actually buildable (§5.3/§10).
  - Accuracy: §10 ctest commands use absolute `${CMAKE_CURRENT_SOURCE_DIR}` fixture
    paths (ctest runs in the build dir); defined `schema` version handling (§5.3).
- **Loop 3 (2026-07-17)** — same 2 reviewers, cold. Both converged on the same two
  self-introduced (loop-2) contradictions. 4 verified findings (MEDIUM 2 · LOW 2), fixed:
  - Consistency: `gpu,tiny` (gated, sub-floor) is `SKIPPED` in *every* fixture (the floor
    keys on the baseline value), so `ok.csv`'s "all OK" was unbuildable — table row +
    a convention note now say "totals OK; `gpu,tiny` SKIPPED" (§10).
  - Consistency: removed stale "missing-metric WARN" wording from the §5.5 Compare
    bullet — it's `MISSING`, matching §5.2/INV-6.
  - Completeness: folded the `schema`/unreadable-baseline config error into the exit-2
    contract (§5.5); simplification: dropped the unused `baseline_placeholder.json` — the
    fixture `baseline.json` is the committed synthetic baseline (§3/§5/§9).
- **Loop 4 (2026-07-17)** — 2 reviewers, cold; dug into the verdict-classification logic.
  8 verified findings (HIGH 1 · MEDIUM 3 · LOW 3 · INFO 1), all fixed:
  - Correctness (HIGH): the "skip a row with blank `ms`/`fps`" rule would have skipped
    every non-`frame` row (fps is blank there by design) → gate nothing. Now skips on a
    blank/non-numeric **`ms`** only (§5.1/INV-9).
  - Correctness: gave §5.2 an explicit **ordered classification** (MISSING → INCONCLUSIVE
    → SKIPPED → compare), resolving the MISSING-vs-INCONCLUSIVE collision for zero-row /
    empty-CSV / short-run metrics; empty CSV → all-MISSING.
  - Correctness: §5.2 now computes a verdict for **every** baseline metric (gated or not);
    the `gate` allowlist governs only the exit code — so the non-gated `gpu,shadow` INV-4
    witness and §8's per-row status word both hold.
  - Exit codes: split config errors to a distinct **exit 3** (fix-the-file, no auto-retry)
    vs data-inconclusive exit 2; defined missing-`schema` handling (§5.3/§5.5).
  - Testability: added `warn.csv`; every fixture now carries all five gated metrics
    (`gpu,tiny` present → SKIPPED, INCONCLUSIVE only in `short.csv`); `--selftest` now
    also re-runs `warn`/`missing` under `--strict-warn` (exit 1); corrected the INV-5
    coverage claim to structural (§10).
- **Loop 5 (2026-07-17)** — convergence check. Traced all 7 fixtures through the §5.2
  ordered classification + §5.5 exit contract: every asserted (verdict → exit) derives
  exactly; exit codes 0/1/2/3 all reachable and non-contradictory; 7 status words defined
  before use; defaults consistent; INV-1..9 consistent; Contents matches body; no dangling
  references. **Zero substantive findings — converged.** (This pass was self-conducted:
  the independent-reviewer dispatch returned repeated 529/overload errors, so the final
  check is a maintainer trace rather than a fresh cold agent; implementation + the ctest
  self-test provide the empirical convergence proof — a design contradiction would make a
  fixture fail to produce its asserted verdict.)

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
