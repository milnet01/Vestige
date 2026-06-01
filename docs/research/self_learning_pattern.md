# Observe → Act: A Unified Self-Learning Pattern for Tool Subsystems

*Vestige developer notes — 2026-06-01*

---

## 1. Overview

Two Vestige tool subsystems — the static-analysis **Audit Tool** and the
**Formula Workbench** — independently arrived at the same architecture for
making a tool smarter over time. Neither was planned as a template for the
other; the convergence emerged from solving similar problems in different
domains. Writing the pattern down once makes it available as a starting point
for the next subsystem (e.g. renderer performance autotuning) and prevents
two-tool knowledge from being rediscovered for a third.

The pattern has two moving parts:

1. **Observe**: at the end of every run, append a small, schema-versioned
   record to a persistent file describing *what happened* in terms the next
   run can act on — not a log (for humans to read later) but a signal file
   (for the tool to read next time).
2. **Act**: at the start of the next run, read the signal file and
   automatically adjust tool behaviour — changing default selections, demoting
   noisy outputs, proposing config changes — before handing control to the
   user. "Automatic" means *without asking*, not *without telling*: every
   adjustment is logged (see invariant 4).

The key discipline: the two phases are separated by at least one run boundary.
This avoids the feedback loop collapsing into same-run self-amplification and
lets the user see the raw behaviour before the tool reacts to it.

---

## 2. The Two Implementations

### 2.1 Audit Tool

**Domain**: Static-analysis false-positive management.

**Signal file**: `.audit_stats.json` — schema-versioned JSON with per-rule
cumulative `{hits, verified, suppressed}` counters and a rolling run history.

**Three-phase progression:**

| Phase | Verb | Trigger | Action |
|-------|------|---------|--------|
| 1 | Observe | every run | Append per-rule counters; `--self-triage` surfaces the noise ranking |
| 2 | Act (demote) | next run, if ≥90 % noise over ≥10 hits with zero verified | Lower severity of pure-noise rule by one step (logged per rule) |
| 3 | Act (propose) | next run, if ≥50 % noise over ≥10 hits and ≥3 min_runs | Mine FP matched-text for a common identifier; write `.audit_propose_fixes.md` |

**Key files**:
- `tools/audit/lib/stats.py` — `update_stats()` (Phase 1), `compute_demotions()` (Phase 2), `compute_proposals()` / `render_proposals_markdown()` (Phase 3)
- `tools/audit/lib/runner.py` — the self-learning stages, in pipeline order (they run after the dedup/corroborate/severity-override stages): demotions (on the *prior* run's stats) → verified-tag → stats-update (this run) → proposals → suppress-filter (last). Phase 3 runs before the suppress filter so the false positives' matched text is still present.
- Signal files: `.audit_stats.json`, `.audit_propose_fixes.md`

**Phase 2 is automatic** (changes the run's output without asking, but logs
each demotion — `runner.py` emits an "Auto-demoted N findings…" line per rule).
**Phase 3 is advisory** (writes a file for human review; never edits config).
The separation
maps to risk: severity demotion is reversible and low-stakes; suggesting a
regex change to config is higher-stakes and earns a human checkpoint.

### 2.2 Formula Workbench

**Domain**: Numerical curve-fitting quality.

**Signal files**: `.fit_history.json` (per-formula quality ledger) and the
reference-case JSON specs under `tools/formula_workbench/reference_cases/`.

**Two mechanisms** (the fit-history ledger is one observe→act pair; the
reference harness is a separate CI gate):

| Part | Verb | Trigger | Action |
|------|------|---------|--------|
| History | Observe | every fit | Append AIC/BIC/R²/RMSE + convergence flag per (formula, data-shape) bucket |
| Seeding | Act | next fit of same formula | Use the coefficients from the most data-shape-similar prior *exported* fit (recency breaks ties) as the Levenberg-Marquardt starting point instead of the library defaults |
| Reference harness | Observe + Act | CI / every dev run | Synthesize a dataset from canonical coefficients; fit from the library defaults; assert R²/RMSE/convergence stay within bounds |

**Key files**:
- `tools/formula_workbench/fit_history.{h,cpp}` — `FitHistory::record()` (observe) / `FitHistory::bestSeedFor()` (act; `lastExportedCoeffsFor()` is the shape-agnostic recency-only fallback for callers without a `FitHistoryMeta`)
- `tools/formula_workbench/reference_cases/*.json` — one spec per formula, auto-discovered by the repo-root `tests/test_reference_harness.cpp`
- `tools/formula_workbench/reference_harness.{h,cpp}` — `synthesizeDataset()` / `executeReferenceCase()`

**History seeding** is fully automatic (transparent to the user; warms up
automatically after the first few fits). **Reference-case harness** is a CI
gate — it acts by *blocking the build* when the fitter regresses, which is the
most conservative form of "act."

---

## 3. The Shared Structure

Strip the domain-specific vocabulary and both implementations reduce to:

```
┌─────────────────────────────────────────────────────────────────┐
│ Observe (end of run N)                                          │
│   read_signal()          → load existing records (or empty)     │
│   update_signal(result)  → append this run's outcome           │
│   write_signal()         → persist (schema-versioned)           │
└──────────────────────────────┬──────────────────────────────────┘
                               │   ≥1 run boundary
┌──────────────────────────────▼──────────────────────────────────┐
│ Act (start of run N+1)                                          │
│   read_signal()           → load accumulated records            │
│   compute_action(signal)  → derive a behaviour change           │
│   apply_action(tool)      → adjust (logged) or report           │
└─────────────────────────────────────────────────────────────────┘
```

The run boundary is what makes this *learning* rather than *caching*. A cache
updates in the same call; this pattern defers the update to the next invocation
so a single noisy run can't destabilise the tool's baseline behaviour.

**Five invariants that both implementations honour** — as shared *goals*; the
two tools meet several of them with different idioms (the audit tool is Python,
the Workbench C++), called out per-invariant below:

1. **Schema versioning.** The signal file has a `schema_version` field. On
   mismatch, the load returns an empty/default container instead of crashing or
   silently misreading the data, and the tool degrades gracefully to its
   pre-learning behaviour. The Audit Tool also logs a warning on mismatch
   (`stats.py` `load_stats`); the Workbench's `FitHistory::load()` returns
   `false` silently (no log) — same graceful-degradation outcome, no warning.

2. **Never break the main run.** A bug in the learning subsystem must not block
   the primary operation. The Audit Tool (Python) wraps the whole observe/act
   block in `try/except` (`runner.py`), so a static-analysis run still produces
   its report even if stats persistence fails. The Formula Workbench (C++)
   instead guards JSON parsing inside `FitHistory::load()` with a `try/catch`
   that degrades to an empty history, and `load()`/`save()` return a failure
   bool the caller honours — so a corrupt or unreadable `.fit_history.json`
   leaves the fit itself untouched. Same guarantee, two idioms.

3. **Determinism.** Given the same signal file and the same run inputs, the
   act step produces the same output. No randomness, no wall-clock sensitivity.
   This makes the self-learning behaviour testable with ordinary unit tests.

4. **Transparency.** The act step surfaces what it changed so the user can
   understand *why* tool behaviour shifted across runs without reading the
   signal file. The Audit Tool logs each Phase 2 demotion ("Auto-demoted N
   findings…", `runner.py`). The Workbench surfaces a history seed through the
   returned `SeedMatch.similarity` (a UI badge), not a log line — same intent,
   different channel.

5. **Human override at every gate.** The signal file can be deleted to restart
   from scratch — the Audit Tool exposes `--stats-reset` for this; the Formula
   Workbench has no reset flag, so deleting `.fit_history.json` is its only
   reset. High-stakes acts (Phase 3 config proposals) are advisory — a human
   confirms before anything is applied. Low-stakes acts (severity demotion,
   coefficient seeding) are automatic but logged and reversible.

---

## 4. Applying the Pattern to a New Subsystem

**Candidate**: renderer performance autotuning. The render loop collects frame
time per subsystem per frame; a self-learning layer could recommend adjusting
quality-preset thresholds to maintain 60 FPS on the observed hardware.

Mapping the pattern:

| Step | Concrete instantiation |
|------|----------------------|
| Signal file | `.render_perf_baseline.json` — per-subsystem P95 frame time + hardware fingerprint |
| Observe | Append a run summary after each play session; rolling window of last N sessions |
| Act (low-stakes) | Suggest a quality-preset adjustment in the dev console at startup if the observed P95 is consistently above the target |
| Act (high-stakes) | Write a `.render_autoconfig_proposals.md` suggesting specific tunable values; user applies |

The invariants carry over directly. The two risks to watch:
- **Over-fitting to a single session's behaviour** — the rolling-window cap
  (analogous to `MAX_RUN_HISTORY`) prevents this.
- **Hardware drift** — if the hardware fingerprint changes (driver update,
  new GPU), flush the baseline and restart the observation window.

**Checklist for a new self-learning subsystem:**

- [ ] Define the signal file schema up front (field names, units, schema_version).
- [ ] Pick the act threshold conservatively — start with a higher bar than you
      think you need; you can lower it once you have data.
- [ ] Wrap both observe and act in try/except; test the degraded path.
- [ ] Add a `--signal-reset` (or equivalent) CLI flag so users can restart.
- [ ] For high-stakes acts: write an advisory file, not an automatic edit.
- [ ] For test coverage: write the act function as a pure function of
      (signal, policy) → action so it's unit-testable without running the
      full tool.
- [ ] Document the schema and the act thresholds in the tool's CHANGELOG entry
      so the behaviour is discoverable later.

---

## 5. Why Two Tools Converged

The convergence wasn't accidental. Both tools faced the same structural
problem: **a tool that makes a binary decision on every invocation (flag this
finding / accept this fit) accumulates implicit knowledge in the user's head
rather than in the tool**. Every run the user re-supplies context the tool
threw away. The observe→act pattern is the minimal architecture that lets the
tool accumulate that context without requiring a database, a network
connection, or anything more than a small JSON file in the project root.

The audit-tool and Workbench self-learning designs were developed in tandem
(both 2026-04-17); §6 of
`docs/research/formula_workbench_self_learning_design.md` ("Relationship to the
audit-tool self-learning work") is a short note that contrasts the two
mechanisms (demote-noisy-rules vs promote-well-fitting-formulas) and calls them
siblings — it predates Phase 3 and says nothing about it. The
advisory-vs-automatic distinction Phase 3 later forced is *this* document's own
synthesis: the gate-decision rule — risk vs reversibility — that the rest of
this doc is built around. Low-stakes, reversible acts (severity demotion,
coefficient seeding) run automatically; higher-stakes acts (a config-edit
proposal) earn a human checkpoint.

---

## 6. Cross-references

- `docs/research/formula_workbench_self_learning_design.md` — original design
  with §6 pointing at the audit analogy.
- `tools/audit/lib/stats.py` — all three audit phases in one module.
- `tools/formula_workbench/fit_history.{h,cpp}` — the Workbench observe/act ledger.
- `tools/formula_workbench/reference_harness.{h,cpp}` — `synthesizeDataset()` / `executeReferenceCase()`, run by the repo-root `tests/test_reference_harness.cpp`.
- `tools/audit/CHANGELOG.md` — v2.9.0 (Phase 1), v2.10.0 (Phase 2), v2.19.0
  (Phase 3).
- `tools/formula_workbench/CHANGELOG.md` — the [Unreleased] / 2026-06-01
  FW W5 entry (reference-case backlog completion, 22/22 builtins); v1.17.0 for
  the harness extensions (`max_abs_error_max`, step-based sweeps); earlier
  versions for the `.fit_history.json` ledger.
- `tools/formula_workbench/reference_cases/` — 22 reference specs, one per
  coefficient-bearing builtin formula (5 zero-coefficient builtins are out of
  scope — the harness fits coefficients, and those have none).
