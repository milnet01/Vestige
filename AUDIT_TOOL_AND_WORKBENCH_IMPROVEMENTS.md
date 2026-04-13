# Tool Improvements from Full Manual Audit (2026-04-13)

Tracking file for improvements to the **audit tool** (`tools/audit/`) and **formula workbench** (`tools/formula_workbench/`) identified while performing the full manual codebase audit requested on 2026-04-13.

**Scope of this file:** things the manual audit did that either (a) the automated audit tool should learn to do, or (b) the formula workbench could do better — with emphasis on **intelligence** and **false-positive reduction**.

**Status:** populated incrementally during Phases 1–3 of the manual audit. Implementation deferred until the audit is complete and the FIXPLAN is approved.

**Predecessor:** `tools/audit/IMPROVEMENTS_FROM_9E_AUDIT.md` (2026-04-13, Phase 9E-1/9E-2 audit). This file supplements that one; do not duplicate those items — add cross-references where they reinforce an existing finding.

---

## Audit tool — candidate improvements

### From Phase 1 (Discovery)

#### AT-D1. `NodeEditor.json` missing from `.gitignore`
- **What I did:** noticed the untracked `NodeEditor.json` at repo root; compared against `.gitignore` which already ignores `imgui.ini` (sibling runtime-generated layout file from Dear ImGui).
- **Automatable?** Yes — pattern-detectable. When a file matches `^[A-Z][A-Za-z]+\.json$` at repo root, sits untracked across multiple runs, and has a sibling tool's `.ini` / `.json` in .gitignore, surface as a gitignore-candidate finding.
- **FP rate if automated:** low if scoped to "untracked for ≥2 audits, root-level, runtime-layout-looking files."

#### AT-D2. Trend snapshot growth is unbounded
- **What I did:** noticed 5 `docs/trend_snapshot_*.json` files tracked in git across 2 days.
- **Automatable?** Yes — within the audit tool itself (self-maintenance). Add `--keep-snapshots N` to auto-prune old ones, or suggest the top-N-snapshots-committed-to-git is within a user-configurable limit.
- **FP rate:** none — it's the tool's own output.

### From Phase 2 (Audit)

*Populated as findings are made. For each finding where I catch something the automated tool missed, note:*
- *What I did manually*
- *Whether it's pattern-automatable, requires semantic analysis, or needs external tooling*
- *Expected false-positive rate if automated naively*

#### AT-A1. cppcheck `--inline-suppr` flag missing — 4 known false-positives re-reported every audit
- **What I did:** verified the 4 pre-existing HIGH cppcheck findings (`engine/formula/node_graph.cpp:495`, `engine/scene/entity.cpp:60,79`, `tests/test_command_history.cpp:244-246`). All four **already have inline `// cppcheck-suppress <rule>` comments in the source** explaining why they're false positives. The audit tool re-surfaces them anyway because `tools/audit/lib/tier1_cppcheck.py:30` builds the cppcheck command without `--inline-suppr`, and `audit_config.yaml` doesn't include it in `static_analysis.cppcheck.args`.
- **Fix:** add `--inline-suppr` to the default cppcheck args in `audit_config.yaml` (and `auto_config.py` for new projects). Also consider honoring cppcheck XML `<suppressed>` attribute in `_parse_xml` if/when we support that.
- **Automatable?** This IS the automated tool. One-line config fix.
- **FP rate after fix:** these 4 findings go to zero per audit. Net reduction: 6 HIGH findings per run.
- **Cross-reference:** reinforces `IMPROVEMENTS_FROM_9E_AUDIT.md §1` (severity calibration / noise reduction).

#### AT-A2. `shell=True` is the default for `run_cmd` — command-injection-safe tool should detect this in its own codebase
- **What I did:** read `tools/audit/lib/utils.py:13-30` and found `shell=True` is the default in the tool's own subprocess wrapper. This is exactly the pattern the tool's Tier 2 rules should flag as CRITICAL in any Python codebase. Yet the tool's Tier 2 patterns for Python don't include `subprocess.*shell=True` or `os.system`.
- **Fix:** add Python-specific Tier 2 patterns: `subprocess\.(run|Popen|call|check_output).*shell=True`, `os\.system\(`, `os\.popen\(`. Severity: HIGH when file matches `*.py`.
- **Automatable?** Yes — straightforward regex addition. The pattern library already has a Python section; just extend it.
- **FP rate:** moderate — legitimate uses of `shell=True` with static command strings exist, but those are rare enough that a HIGH-severity flag with clear description is the right trade-off. Suggest the finding include "consider `shlex.split` + `shell=False`" in the description.

#### AT-A3. No hardcoded-secret scan catches UUID-shaped API keys in YAML
- **What I did:** found `tools/audit/audit_config.yaml:272` contains an NVD API key as a literal string. Audit tool v2.0.0 added secret scanning (per CHANGELOG: hardcoded_password, github_token, generic_api_key patterns). None of those regexes match a bare UUID in a `api_key: "..."` YAML line.
- **Fix:** extend `generic_api_key` pattern to include UUID shape in config-file contexts, e.g., `(?i)api_key\s*[:=]\s*[\"']?[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}[\"']?`. Also add YAML-specific matching: scan `*.yaml`/`*.yml` for keys in `(api_key|apikey|secret|token|password):`.
- **Automatable?** Yes — another pattern-library extension.
- **FP rate:** low — a UUID under a key-like config path is almost always a credential.

#### AT-A4. Path-traversal-to-read scan missing for Flask routes
- **What I did:** audited Flask web UI (`tools/audit/web/app.py`) and found GET `/api/report?path=`, GET `/api/config?path=`, POST `/api/init` all accept paths without `relative_to()` containment. The tool's own patterns don't flag these.
- **Fix:** add pattern category `flask_security`: match `request\.args\.get\(` or `request\.get_json\(\).*\[.*path.*\]` feeding into `Path(…)`, `open(`, `.read_text(`, `.write_text(`, `subprocess`. Flag as HIGH when a containment check isn't in the same function.
- **Automatable?** Needs lightweight intra-function control-flow analysis, not pure regex. Reasonable shape: tree-sitter Python grammar + "variable flows from request.* into dangerous sink without passing through Path.resolve().relative_to()".
- **FP rate:** medium — heuristic-based. Recommend: start with "any request-derived value used as a Path arg in a route handler" and let reviewers triage.

#### AT-A5. ReDoS pattern-complexity check for user-supplied regexes
- **What I did:** noticed `tier2_patterns.py:48,57` compiles regex strings straight from YAML. Some existing patterns have nested `.*` with lookbehinds that are ReDoS-prone. User-supplied patterns could be worse and hang the web UI.
- **Fix:** on config load, run a lightweight ReDoS-heuristic check (nested quantifiers, adjacent repetition, backref+repeat) and refuse to load patterns that fail. Alternatively run regex in a subprocess with wall-clock timeout per file.
- **Automatable?** Yes — `rxxr`/`redos` libraries exist; or Python's `regex` module has `timeout=`.
- **FP rate:** low if the heuristic is conservative.

#### AT-A6. SARIF schema validation step missing
- **What I did:** read `sarif_output.py:72` — emits `"uriBaseId": "%SRCROOT%"` which is SARIF-1 syntax and rejected by SARIF-2.1.0 validators (GitHub Advanced Security, VS Code SARIF viewer).
- **Fix:** after generating, validate against the official SARIF 2.1.0 JSON schema (bundle it, run `jsonschema.validate`). Fail the tool run if validation fails.
- **Automatable?** Yes — one-time schema inclusion + validation call.
- **FP rate:** none — deterministic.

#### AT-A7. Subprocess output-size cap
- **What I did:** `capture_output=True` is used in `run_cmd` with no size cap. Cppcheck on a 5k-file tree can emit 100+MB of XML; the tool buffers all of it in memory then parses.
- **Fix:** either stream via `Popen` with per-line parsing, or cap captured output at ~64 MB with a truncation warning.
- **Automatable?** Fix belongs in the tool. Detection pattern: `subprocess\.(run|check_output)\(.*capture_output=True` without adjacent size cap.
- **FP rate:** moderate — most uses are small commands. Low signal as a general pattern; better to just fix the tool's own wrapper.

#### AT-A8. Git-ref validator for any `*_ref` config value passed to subprocess
- **What I did:** traced that `base_ref` (web UI) flows into `run_cmd("git diff … {base_ref}")` via `tier3_changes.py`. No validation anywhere.
- **Fix:** config loader validates any field ending in `_ref` matches `^[A-Za-z0-9._/~^-]{1,64}$`. Refuse otherwise.
- **Automatable?** Yes — config-schema rule.
- **FP rate:** low.

#### AT-A9. CWE tagging per Tier-2 pattern (feeds SARIF taxa)
- **What I did:** when triaging findings, I consistently wanted a CWE number to sort severity by class (memory vs injection vs resource). Most patterns already include it in `description`; none expose it as a structured field.
- **Fix:** add optional `cwe:` key per pattern; propagate to SARIF `result.taxa[]` and to markdown report.
- **Automatable?** Yes — schema + report changes only.
- **FP rate:** none.

### From Phase 3 (Research)

*(none yet)*

---

## Formula Workbench — candidate improvements

Captured from the formula-subsystem audit agent (semantic review of curve_fitter, codegen, node_graph, expression_eval, workbench.cpp). Heavily focused on **intelligence (catch more real bugs)** and **false-positive reduction (stop fitting garbage and not warning)**.

### Intelligence / signal

#### FW-I1. Symbolic domain-probe pre-fit
- **What:** before LM starts, sample the expression at training `x` values and detect built-in safety kicks (`/0 → 0`, `log(x≤0) → 0`, `sqrt(negative) → sqrt(|x|)` in the evaluator). Warn: *"7 of 100 training points hit the `log(x≤0)` safe-math guard — the fit will converge to a non-physical optimum."*
- **Why it matters:** directly addresses the **H12 evaluator/codegen mismatch** (fit looks good, shader ships black). Visibility alone prevents silent deployment of broken formulas.
- **FP rate:** none — deterministic signal.

#### FW-I2. Degenerate-data detector (Jacobian rank proxy)
- **What:** pre-fit, compute finite-diff Jacobian at initial coefficients and check column norms. If any column ≈ 0, that coefficient is unobservable from the data; refuse fit and name the coefficient.
- **Why it matters:** today LM burns 100 iterations and reports "singular normal equations." User has no idea why. Would also surface H13 class of bugs proactively.
- **FP rate:** low if threshold is chosen conservatively.

#### FW-I3. GLSL round-trip validator
- **What:** after "Copy as GLSL", background-compile the snippet in a hidden compute shader (engine already links GL). Surface compile errors inline in the workbench.
- **Why it matters:** catches identifier collisions with GLSL reserved words (`sample`, `input`, `output`, `common`, `centroid`), missing overloads, precision qualifier mismatches — and would have caught any injection attempt per H11.
- **FP rate:** none.

#### FW-I4. Codegen / evaluator parity test
- **What:** on every fit completion, resample 32 random points, run through the interpreter AND a JIT-compiled version of the codegen output, diff the results.
- **Why it matters:** directly surfaces H12 mismatches. Tagline: *"Codegen differs from evaluator at X=%.3f — possibly a safe-math path triggered."*
- **FP rate:** requires JIT infrastructure (`libgccjit` or `tcc`). Non-trivial; optional feature.

#### FW-I5. Condition-number meter
- **What:** display `cond(JᵀJ)` after fit. Values > 1e8 mean coefficients are not independently identifiable.
- **Why it matters:** users currently see "R² = 0.99" but refit gives wildly different coefficients; condition number explains why.
- **FP rate:** none.

#### FW-I6. Graph-to-expression lossiness lint
- **What:** when converting `FormulaDefinition` → `NodeGraph`, pre-scan the expression tree for constructs the graph can't represent (CONDITIONAL per M10). Emit a list: *"This formula contains 2 conditionals that will be lost on import."*
- **Why it matters:** directly fixes the silent drop in M10.
- **FP rate:** none.

### False-positive reduction

#### FW-F1. Bound-active indicator per coefficient
- **What:** when a fitted coefficient sits at its user-set bound, the fit reports "converged" — but the optimum is constrained, not true. Flag per-coefficient "bound-active" and surface in the result panel.
- **Why it matters:** a key class of "fit looks great but isn't" — reducing user false-confidence.

#### FW-F2. AIC vs. null-model comparison
- **What:** already compute AIC. Also compute AIC for a constant-mean model and display ΔAIC = AIC_model − AIC_null.
- **Why it matters:** today AIC is shown as an absolute; users don't know if "500" is good. ΔAIC > 10 vs. null is the usual cutoff for "this formula actually predicts better than the mean."

#### FW-F3. Identifier-shape lint on import/edit
- **What:** when a FormulaDefinition is loaded or a new variable/operation is entered in the UI, validate against `[A-Za-z_][A-Za-z0-9_]*`. Current UI lets users type arbitrary strings that silently break codegen (H11).
- **Why it matters:** defense-in-depth against codegen injection.

#### FW-F4. CSV schema preview
- **What:** before import, show detected columns + first 3 rows; let the user confirm which column is "observed" and which are variables.
- **Why it matters:** current assumption "last column = observed" silently swaps meaning if the CSV is reordered. Catches reversed-axes fits instantly.

#### FW-F5. Residual-plot filter consistency check
- **What:** fix H10 (m_residuals / m_dataX size mismatch). After fix, add an assertion `residuals.size() == dataX.size()` that panics if they drift again.
- **Why it matters:** the workbench's residual plot is a key trust signal. Silently showing wrong data is the worst possible failure mode.

### UX / workflow

#### FW-U1. "Why didn't it converge?" explanation card
- **What:** when `converged=false` after `max_iterations`, show a card ranking likely causes: NaN residuals (H13), singular Jacobian (FW-I2), ill-conditioned problem (FW-I5), step size rejected repeatedly, bounds too tight. Link to relevant docs per cause.

#### FW-U2. Popen buffer fix for long paths
- **What:** fix L2 — `popen` read buffer is 512 B; long symlink paths silently truncate and then fail the subsequent open. Loop on `fgets`.

#### FW-U3. One-click "save as" respects last-used dir
- Not audit-driven; noted in passing — useful QoL.

---

## Cross-references to existing `IMPROVEMENTS_FROM_9E_AUDIT.md`

When an item in this file reinforces an existing entry in the 9E improvements doc, cross-reference it here so the combined priority ranking is clearer when it's time to implement.

| This file | 9E doc | Relationship |
|---|---|---|
| AT-A1 (`--inline-suppr`) | §1 (severity calibration / noise) | AT-A1 is a concrete one-line fix that reinforces the §1 "cut report noise" theme. Ship AT-A1 first. |
| AT-A3 (UUID api_key scan) | §1a (per-check severity overrides) | AT-A3 extends the secret-scan pattern library; §1a extends severity mapping. Complementary. |
| AT-A4 (Flask-security flow scan) | §3 (semantic gaps — lambda ref-capture, JSON size caps) | Same class of gap: patterns can't catch flow-sensitive issues without control-flow analysis. |
| AT-A5 (ReDoS) | (new class) | Not in 9E doc. |
| AT-A6 (SARIF validation) | (new class) | Not in 9E doc. |
| FW-I1–FW-I6 (workbench intelligence) | §8a–8c | Reinforces existing workbench section; expands with 6 more concrete items. |

---

## Priority ranking (to be populated at end of audit)

*Filled in during Phase 4 fix planning.*
