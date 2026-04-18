# Self-learning roadmap — audit tool + Formula Workbench

Consolidated handoff doc as of 2026-04-18 (rev 3). Lists everything that's
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
| 1.10.0 | 2026-04-18 | W1 — async-worker pattern for Python driver calls. New `AsyncDriverJob` (`async_driver.{h,cpp}`) wrapping `runDriverCaptured` in a `std::async` worker; tri-state `Idle/Running/Done` polled from the render loop. Suggestions panel no longer blocks the GUI; elapsed-seconds counter shown while running. Prereq for W2. No cancellation / streaming yet — those land with W2. 7 new tests. |
| 1.11.0 | 2026-04-18 | W2a + W2b — "Discover via PySR" panel with streaming + cancel. Extracted `spawnDriverProcess` low-level helper so the async path can expose the child PID; `AsyncDriverJob` now runs the stdout read loop on its worker thread with `drainStdoutChunk()` for live frame-by-frame output and `cancel()` for SIGTERM (auto-escalates to SIGKILL after 3s grace). New `renderPySRPanel`: Discover + Cancel buttons, niterations / max-complexity sliders, live raw-output pane, sortable leaderboard parsed from the driver's JSON tail. W2c (library-import via PySR expression parser) deferred. 4 new tests (streaming, cancel, SIGKILL escalation, cancel-when-idle). |
| 1.12.0 | 2026-04-18 | W2c — PySR expression-string → `ExprNode` parser wired into the Suggestions leaderboard. New `pysr_parser.{h,cpp}` (recursive-descent, precedence-climbing, supports `+ - * / ^ ** cos sin exp log sqrt abs floor ceil` and unary `-`); everything flows through the `ExprNode::*` factories so the H11 codegen allowlist gates every imported tree. Per-row Import buttons in the leaderboard build a `FormulaDefinition` under category `"imported"` and register it in the live library. 19 new parser tests (grammar + rejection paths). |
| 1.13.0 | 2026-04-18 | W3 — markdown rendering in the Suggestions panel. New `markdown_render.{h,cpp}` (pure parser + ImGui-only renderer) handles the subset `llm_rank.py` emits: headings 1–3, pipe tables, `---` rules, fenced code, inline backtick code spans, `**bold**` stripped. Suggestions panel renders the LLM shortlist as a real ImGui table instead of raw markdown; a "Show raw markdown" toggle keeps copy-paste available. 17 new parser tests. |

Related: `docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md` is the
original six-mechanism plan.

## Outstanding

### Audit tool

All triage items (A1, A2, A3) closed out in audit 2.11.0. Nothing
left on the audit-tool side from the 2026-04-16 report.

### Formula Workbench

| # | Item | Priority | Size | Notes |
|---|------|---------:|-----:|-------|
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
