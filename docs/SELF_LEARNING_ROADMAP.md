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

Related: `docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md` is the
original six-mechanism plan.

## Outstanding

### Audit tool

| # | Item | Priority | Size | Notes |
|---|------|---------:|-----:|-------|
| A1 | Exclude rule-definition files from tier-2 pattern scans | low  | small | Only fires when the audit scans its own rule-source against Python patterns. Ants side already excludes its own rules via `kGrepFileExclSec`. Vestige tier-2 could add a `rule_source_files` skip list. |
| A2 | `skip_comments` for Python `#` comments + triple-quoted docstrings | low  | small | `_classify_line` / `_strip_comments_multiline` currently only handle C/C++ comment syntax. Not a real issue today because scans run against `*.cpp`/`*.h`, not `*.py`. Blocker only if/when Python-project audits become a first-class use case. |
| A3 | Vestige-side `git ls-files` in `utils.enumerate_files` | low  | medium | Currently uses `rglob`; honours `exclude_dirs` but not `.gitignore`. Ants side already uses `git ls-files` for the two rules where the noise was catastrophic. Parity move. |

### Formula Workbench

| # | Item | Priority | Size | Notes |
|---|------|---------:|-----:|-------|
| W1 | Async-worker pattern for Python driver calls | medium | medium | Prereq for W2. Run `runDriverCaptured` on a `std::thread`, poll from the render loop, surface progress in the panel. One pattern we'd reuse for both Suggestions and PySR. |
| W2 | §3.5 GUI — "Discover via PySR" button | medium | large | Needs W1 (PySR runs take minutes). Panel UX: button + progress bar + leaderboard of discovered expressions + "Import as library formula" action. The library-import side is its own challenge — parsing a PySR string like `sin(log(x^2))` into an `ExprNode` means either writing an expression parser or handing the user a "save as template" file dialog. |
| W3 | Markdown rendering in the Suggestions panel | low | small | Panel shows raw markdown today (`\| Rank \| Formula \| ... \|`). Perfectly legible for tables of ≤10 rows but would look nicer rendered. ImGui Markdown extensions exist. |
| W4 | §3.2 history-poisoning mitigation via "pin this fit" | low | small | Today every exported fit poisons future seeds. A UI toggle on the Export dialog — "don't remember this fit as a seed for next time" — would let users recover from an outlier session without `--stats-reset`-style scorched earth. |
| W5 | More `reference_cases/*.json` entries | low | small | Today: `beer_lambert`, `stokes_drag`, `fresnel_schlick`. The library has 27 formulas; adding a spec per formula over time gives broader regression coverage. Each spec is ~25 lines of JSON. |
| W6 | Confidence-weighted meta-feature matching (design §3.1 advanced) | low | medium | `lastExportedCoeffsFor` picks the absolute-latest exported fit regardless of data similarity. A data-shape-aware ranker would prefer the most recent fit whose `data_meta` matches the current dataset's meta-features — avoiding the case where a user fits two very different datasets on the same formula and the newer fit seeds the older one badly. |
| W7 | LLM cost log / per-call audit trail | low | small | `--suggest-formulas` and the GUI panel each trigger one Anthropic API call. A local JSONL log (`.llm_calls.log`) would let a user audit spend without logging into the dashboard. Matches the `.fit_history.json` pattern. |
| W8 | `--self-benchmark` that consults `.fit_history.json` for initial guesses | low | small | Today the CLI benchmark starts every formula from library defaults. For formulas with history, starting from `lastExportedCoeffsFor` would match the GUI's §3.2 seeding and give fairer comparisons on datasets the user has fit before. |

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
