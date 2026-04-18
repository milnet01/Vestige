# Self-learning roadmap — audit tool + Formula Workbench

Consolidated handoff doc as of 2026-04-18. Lists everything that's
shipped and everything that's tracked-but-not-started for the
self-learning loops in both tools. Pairs with
`docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md` (the design that
scoped this work).

## Shipped

### Audit tool (`tools/audit/`)

| Ver | Date | What |
|-----|------|------|
| 2.9.0  | 2026-04-17 | Phase 1 — `.audit_stats.json` cumulative per-rule counters (hits / verified / suppressed); `--self-triage` ranked markdown; `--stats-show` / `--stats-reset` CLI. |
| 2.10.0 | 2026-04-17 | Phase 2 — feedback-driven severity demotion. Rules with ≥10 hits, ≥90 % noise, 0 verified get auto-demoted one severity tier on the next run. User `severity_overrides` win first. Six config knobs under `auto_demote`. |
| 2.11.0 | 2026-04-18 | Triage close-out (A1 + A2 + A3): `project.exclude_file_patterns` honoured + built-in skip for `auto_config.py`; `skip_comments` now strips Python `#` comments and triple-quoted strings (and shell / yaml / toml siblings); `utils.enumerate_files` prefers `git ls-files` when available. 21 new tests. |

Also shipped 2026-04-17: `ants-terminal/src/auditdialog.cpp` fixed
four addFindCheck rule-shape bugs that were producing 110 false
positives when `ants-audit` scanned the Vestige tree (file_perms,
header_guards, binary_in_repo, dup_files).

### Formula Workbench (`tools/formula_workbench/`)

| Ver | Date | What |
|-----|------|------|
| 1.4.0 | 2026-04-17 | §3.1 `.fit_history.json` — persists every exported fit (timestamp, formula, data hash + meta, R²/RMSE/AIC/BIC, coefficients, user_action). Per-formula cap MAX_ENTRIES_PER_FORMULA = 20. |
| 1.5.0 | 2026-04-17 | §3.2 learned LM initial guesses — `selectFormula()` seeds coefficients from `lastExportedCoeffsFor()`; blue "seeded from fit @ TIMESTAMP" badge in the UI so seeding is never silent. |
| 1.6.0 | 2026-04-17 | §3.3 `--self-benchmark <csv>` CLI — batch-fits every library formula to the same dataset, emits markdown leaderboard ranked by AIC ascending with ΔAIC column. Headless, CI-safe. |
| 1.7.0 | 2026-04-17 | §3.4 reference-case regression harness (parameterised GTest auto-discovers `reference_cases/*.json`); §3.5 `--symbolic-regression <csv>` shells out to `scripts/pysr_driver.py`; §3.6 `--suggest-formulas <csv>` + `--dump-library` shell out to `scripts/llm_rank.py`. Both Python drivers are optional deps with clean graceful-degrade messaging. |
| 1.8.0 | 2026-04-18 | §3.6 GUI — in-Workbench Suggestions panel with Run button + inline markdown result. |
| 1.9.0 | 2026-04-18 | Roadmap close-outs W4 + W5 + W7 + W8: pin-this-fit toggle on export (history-poisoning mitigation); two more reference cases (`hooke_spring`, `exponential_fog`, 5 total); LLM per-call JSONL cost log; `--self-benchmark` now seeds LM from `.fit_history.json`. |

Related: `docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md` is the
original six-mechanism plan.

## Outstanding

### Audit tool

All triage items (A1, A2, A3) closed out in audit 2.11.0. Nothing
left on the audit-tool side from the 2026-04-16 report.

### Formula Workbench

| # | Item | Priority | Size | Notes |
|---|------|---------:|-----:|-------|
| W1 | Async-worker pattern for Python driver calls | medium | medium | Prereq for W2. Run `runDriverCaptured` on a `std::thread`, poll from the render loop, surface progress in the panel. One pattern we'd reuse for both Suggestions and PySR. |
| W2 | §3.5 GUI — "Discover via PySR" button | medium | large | Needs W1 (PySR runs take minutes). Panel UX: button + progress bar + leaderboard of discovered expressions + "Import as library formula" action. The library-import side is its own challenge — parsing a PySR string like `sin(log(x^2))` into an `ExprNode` means either writing an expression parser or handing the user a "save as template" file dialog. |
| W3 | Markdown rendering in the Suggestions panel | low | small | Panel shows raw markdown today (`\| Rank \| Formula \| ... \|`). Perfectly legible for tables of ≤10 rows but would look nicer rendered. ImGui Markdown extensions exist. |
| W5 (cont.) | Keep adding reference cases as the library grows | low | small | Five specs shipped (`beer_lambert`, `exponential_fog`, `fresnel_schlick`, `hooke_spring`, `stokes_drag`). Library has ~27 formulas total; adding a spec per formula over time gives broader regression coverage. Each spec ~25 lines of JSON, auto-discovered by the test. |
| W6 | Confidence-weighted meta-feature matching (design §3.1 advanced) | low | medium | `lastExportedCoeffsFor` picks the absolute-latest exported fit regardless of data similarity. A data-shape-aware ranker would prefer the most recent fit whose `data_meta` matches the current dataset's meta-features — avoiding the case where a user fits two very different datasets on the same formula and the newer fit seeds the older one badly. |

### Cross-cutting

| # | Item | Priority | Size | Notes |
|---|------|---------:|-----:|-------|
| X1 | Phase 3 of the audit loop | low | medium | `AUDIT_TOOL_IMPROVEMENTS.md`-style proactive suggestion engine: after N runs, emit a markdown file flagging rules that *would benefit* from a Workbench-style approach (e.g. "this rule's FP set has an exclude-pattern signature you haven't applied yet — here's the suggested regex"). Would close the audit loop with a "propose-fix" layer matching the Workbench's §3.6 for ranking. |
| X2 | Document the unified pattern | low | small | A blog-style doc summarising "how we built self-learning into two different tools with the same 2-phase (observe → act) structure" — useful for the other projects that might want to pick up the pattern. Also a solid README for the open-source repo. |

## How to pick something up

1. Pick a row from the tables above.
2. Read `docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md` §3.x (for
   W-numbered items) or `tools/audit/lib/stats.py` + 
   `tools/audit/CHANGELOG.md` (for A-numbered items) for the
   surrounding context.
3. Relevant test files: `tests/test_fit_history.cpp`,
   `tests/test_benchmark.cpp`, `tests/test_reference_harness.cpp`,
   `tools/audit/tests/test_stats.py`.
4. CHANGELOG entries + a version bump are mandatory per
   `scripts/check_changelog_pair.sh` for any change under
   `tools/audit/` or `tools/formula_workbench/`.
