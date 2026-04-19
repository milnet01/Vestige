# Formula Workbench — Self-Learning & Self-Improvement Design

Status: **Implementation in flight** (originally proposed 2026-04-17; see
`docs/SELF_LEARNING_ROADMAP.md` for the shipped-vs-queued ledger). The six
original mechanisms W1–W6 plus the extended W2a/W2b/W2c split, W7 and W8
have shipped in Workbench 1.9.0–1.15.0 (2026-04-18 and 2026-04-19):
W1 async-worker driver, W2a/W2b/W2c PySR Discover panel + import,
W3 markdown rendering, W4 pin-this-fit toggle, W5 + cont. reference-case
harness (10 specs), W6 confidence-weighted meta-feature seeding,
W7 LLM per-call cost log, W8 self-benchmark LM seeding.

Pairs with the audit-tool self-learning conversation on the same day — the
two tools share a lot of mechanism even though their domains (static
analysis vs numerical fitting) look unrelated at first glance.

## 1. Executive summary

**Yes, self-learning applies directly to the Formula Workbench.** The
Workbench today is a one-shot fitter: you pick a formula template, hit
"Fit", inspect R²/RMSE/AIC/BIC, and either accept or try a different
template. Every fit is amnesic — nothing carries forward. Six concrete
mechanisms, cheapest first, turn that workflow into an accumulating
library of hard-won numerical intuition:

1. **Feedback-driven formula ranking** — persist AIC/BIC + user-accepted
   rank per (formula, data-shape) bucket. Next time similar data shows
   up, default the template picker to the historically best-ranking
   formula. (Analog of audit's "rule demotion based on verified hits".)

2. **Learned initial guesses & bounds** — cache the fitted coefficient
   values per formula and use them as Levenberg-Marquardt starting
   points on the next fit. LM's global convergence is notoriously
   sensitive to initial guess; a well-chosen start often separates
   "converged in 8 iterations" from "hit the iteration cap and returned
   garbage". (Analog of "learn exclude patterns from suppressions".)

3. **`--self-benchmark` subcommand** — after each fit, auto-fit every
   compatible library formula in parallel and emit a leaderboard
   ranked by AIC/BIC. Currently the user must click through one
   formula at a time; a batch-benchmark command makes the comparison
   decision-ready. (Direct analog of the `--self-triage` audit
   subcommand proposed earlier today.)

4. **Reference-dataset regression harness** — keep a small corpus of
   canonical datasets (e.g. `drag_coefficient_sphere.csv`,
   `shadow_softness_vs_distance.csv`). CI re-fits every library
   formula against its reference and compares R²/RMSE against the
   baseline snapshot. A drop of more than ε fails the build. (Analog
   of the audit-side "known-clean reference repo" harness.)

5. **Symbolic regression tier** — add a PySR-backed mode that
   *discovers* novel formulas when no library template reaches an
   acceptable AIC/BIC. The Workbench already exports C++/GLSL — the
   generated PySR expression goes through the same export path. See
   §5 for integration design. (Analog of "AST over grep" — shifts
   from enumerating templates to searching expression space.)

6. **LLM-guided hypothesis ranking** — for domain-constrained fits
   (e.g. "this is a damped oscillator — find the best formula"), a
   cheap LLM call pre-ranks which library formulas are physically
   plausible before burning LM iterations on all of them. LASR
   ([Grayeli et al. 2024](https://arxiv.org/pdf/2409.09359))
   demonstrates this pattern working well in symbolic regression;
   our use is more modest (template picking, not formula discovery).
   (Analog of "LLM pre-triage as a tier".)

Mechanisms 1–3 are local, deterministic, and require no new
dependencies. They can land first and independently. Mechanism 4
formalises what the engine's numerical regression tests already do
informally. Mechanism 5 is a 1–2 week project and a dependency
addition; it's worth doing but should be scoped separately.
Mechanism 6 is the highest-risk, highest-reward option.

## 2. Current state of the Workbench

See `tools/formula_workbench/workbench.h` and `README.md` for the
authoritative description. Summary:

- **Template browser.** ~N physics formulas, hand-curated in
  `engine/formula/formula_library.{h,cpp}`. Each has a name,
  category, expression template, coefficient metadata (ranges,
  initial guesses).
- **Fitter.** Levenberg-Marquardt with optional per-coefficient
  bounds. Reports iterations, residual norm, R², RMSE, AIC, BIC.
- **Validator.** Train/test split, overfitting detection.
- **Export.** `FormulaLibrary` JSON or generated C++/GLSL.

Key limitation for this proposal: **no cross-session state**. Every
fit starts from cold defaults. The on-disk `FormulaLibrary` JSON
captures the *output* of a good fit but not the *metadata* that made
it a good fit — data shape, alternative formulas considered,
AIC/BIC gaps, convergence behaviour.

## 3. Mechanism detail

### 3.1 Feedback-driven formula ranking

**What.** Add `tools/formula_workbench/.fit_history.json`, one entry
per fit session:

```jsonc
{
  "timestamp":      "2026-04-17T10:32:00Z",
  "formula_name":   "stokes_drag",
  "data_hash":      "sha256:…",            // for de-duplication
  "data_meta": {
    "n_points":     42,
    "domain":       { "x": [0.01, 10.0] },
    "variance":     0.087
  },
  "fit": {
    "aic":          -134.2,
    "bic":          -129.8,
    "r_squared":    0.9981,
    "rmse":         0.0043,
    "iterations":   12,
    "converged":    true,
    "coefficients": { "mu": 0.00181, "A": 6.28, … }
  },
  "user_action":    "exported"              // exported|discarded|iterated
}
```

**Why.** With this log, the template browser can show a "recommended"
badge on the formula that historically scored best for data with
similar meta-features (point count, domain, variance). Materials-
science AutoML (Auto-MatRegressor, [Feng et al. 2025](https://link.springer.com/article/10.1007/s41060-025-00808-w)) uses exactly this
shape — meta-feature vector → algorithm-performance regression → rank.

**How.** `FormulaLibrary::rankForData(meta) -> list<FormulaRank>`
computes distance-weighted historical AIC/BIC for each candidate.
Tier in Levenberg-Marquardt terms: AIC/BIC < 2 apart = indistinguishable,
rank lexicographically; gap > 2 = clear winner.

**Effort.** ~1 day. Additive — no existing behaviour changes.

### 3.2 Learned initial guesses and bounds

**What.** Store the last successful coefficient vector per formula
in the same `.fit_history.json`. When the user selects that formula
again, seed LM with the stored vector instead of the library's
default initial guess. Same for bounds: if a fit repeatedly bumps a
coefficient against its upper bound, widen it automatically.

**Why.** LM's convergence is heavily initial-guess sensitive. The
standard introduction ([Origin's
theory of nonlinear curve fitting](https://www.originlab.com/doc/origin-help/nlfit-theory))
is blunt about it: bad starting values "may cause the algorithm to
converge to a local minimum rather than the global one, or to fail
to converge at all". The workbench currently asks the library author
to predict a good initial guess once, statically. That's the wrong
authority — each user's data has its own natural scale.

**How.** Extend `FitRequest` with `initial_guess_source: "library" |
"last_fit" | "user"`. Default to `last_fit` when history exists for
this formula. UI: tiny badge next to the coefficients column saying
"seeded from 2026-04-12 fit (r² 0.99)".

**Effort.** ~1 day. Risk: history poisoning — a bad fit from an
outlier session biases future seeds. Mitigation: only persist fits
the user explicitly exported (`user_action: "exported"`).

### 3.3 `--self-benchmark` subcommand

**What.** New CLI flag that, given a CSV of data, fits every library
formula in the matching category against it in parallel and writes a
Markdown leaderboard:

```markdown
| Formula             | R²      | RMSE     | AIC    | BIC    | Δaic  |
| ------------------- | ------- | -------- | ------ | ------ | ----- |
| smoothstep_damped   | 0.9992  | 0.00118  | -201.4 | -197.6 |  0.0  |
| smoothstep          | 0.9981  | 0.00214  | -188.2 | -186.3 | 13.2  |
| linear_ramp         | 0.8870  | 0.01981  |  -44.7 |  -43.8 |156.7  |
```

**Why.** Matches how statisticians actually pick models: compute
AIC/BIC for every candidate, rank by ΔAIC, treat gaps <2 as a tie
and gaps >10 as decisive ([Number Analytics 2025](https://www.numberanalytics.com/blog/comparing-aic-bic-model-selection)).
The Workbench GUI supports this workflow one formula at a time —
which means in practice the user fits two or three and stops. A batch
command removes that friction.

**How.** Extract the fit loop from the GUI into a reusable
`batch_fit()` function, wire it to CLI args. PySR does this already —
returns "a set of candidate equations, each one a compromise between
complexity and fitting" ([Cranmer 2023](https://arxiv.org/abs/2305.01582))
— we want the same output shape but over our library, not over a
symbolic-search space.

**Effort.** ~2 days. The fit loop is already in `workbench.cpp`; it
needs to be lifted out of the GUI-specific call site.

### 3.4 Reference-dataset regression harness

**What.** `tools/formula_workbench/reference_datasets/`:

```
drag_coefficient_sphere.csv
drag_coefficient_sphere.expected.json   ← formula=stokes_drag, R²>0.995, rmse<0.01
shadow_softness_vs_distance.csv
shadow_softness_vs_distance.expected.json
...
```

CI job re-fits each reference dataset against its expected formula
and asserts the metrics still meet the bound. Gradual drift in the
formula library (e.g. a bug fix to `stokes_drag`'s template)
immediately shows up as a regression against the canonical data.

**Why.** [Kevin Shoemaker's NRES-746 notes](https://kevintshoemaker.github.io/NRES-746/LECTURE8.html) spell out
the practical workflow — "Create a table summarizing AIC, BIC, delta
scores, and model weights". A CI harness institutionalises that
table. It's the numerical-code equivalent of Ants' existing
`tests/audit_fixtures/<rule-id>/{good,bad}.*` pattern.

**How.** Extend the existing formula-library test to iterate the
`reference_datasets/` directory. Each `.csv` + `.expected.json` pair
is one test case. New assertions: `fit_R² >= expected.r_squared_min`,
`fit_RMSE <= expected.rmse_max`, `coefficients` within ±10% of stored
values.

**Effort.** ~2 days to scaffold + populate 8–10 datasets. The
initial dataset curation is the gating cost; once in place, each new
formula in the library gains a harness entry cheaply.

### 3.5 Symbolic regression tier (PySR)

**What.** Add a "Discover" mode alongside "Fit". When no library
formula produces AIC/BIC within an acceptable window, the user can
hand the data to PySR; PySR evolves a symbolic expression; the
expression lands back in the workbench's export pipeline so it ends
up as `FormulaLibrary` JSON + generated C++/GLSL like any other.

**Why.** PySR is "the most versatile and accurate" symbolic-
regression tool across recent benchmarks ([Cranmer 2023](https://arxiv.org/abs/2305.01582),
[arxiv 2508.20257](https://arxiv.org/html/2508.20257v1)).
AI Feynman — the Tegmark et al. physics-inspired predecessor —
"discovers all 100" Feynman Lectures equations, against 71 for prior
systems ([Udrescu & Tegmark 2020](https://www.science.org/doi/10.1126/sciadv.aay2631)).
For cases like fitting an empirical camera-shake curve or a novel
shader attenuation term where no textbook formula fits, symbolic
regression is the right tool.

**How.** The Workbench is C++/ImGui, PySR is Python/Julia. Three
integration paths:

1. **Shell-out.** Write CSV + config, spawn `pysr` subprocess, read
   back the equation. Cheap (pybind11-free), but awkward UX
   (blocking spawn, no progress signal).
2. **pybind11 embed.** Link against `libpython3`, call PySR via its
   Python API, run in a worker thread. Better UX, adds a substantial
   build dependency.
3. **HTTP wrapper.** Run a small Flask service that wraps PySR;
   Workbench POSTs a CSV and polls. Works across multiple users,
   survives PySR version bumps independently.

Recommendation: **option 1 first.** PySR runs take minutes; blocking
behind a modal with cancellable `QProcess`-style cleanup is
acceptable, and avoids committing to a Python build dependency
before the feature proves out.

**Effort.** ~1–2 weeks:
- 2 days to get CSV → PySR → fitted-expression round-tripping.
- 3 days to graft the generated expression into the existing
  template-browser model so it exports like any other formula.
- 3 days to build the "does the library have a good enough formula?"
  decision layer that decides when to auto-trigger the PySR step vs
  hand back a library choice.
- 2 days docs + tests.

Risk: PySR's output isn't always clean — sometimes 7 terms of nested
`sin(log(x^2))`. The current library has a deliberate simplicity
ceiling (per engine rule #6, "start simple"). Guard with a
"complexity budget": reject PySR expressions that exceed N
operations, force a re-run with tighter parsimony weight.

### 3.6 LLM-guided hypothesis ranking (LASR-style)

**What.** Before the batch fit (§3.3), consult a cheap LLM with a
prompt like: "Here's an 80-point dataset that looks like a decay
curve with period ≈ 0.7s. Which of these 14 library formulas is
most physically plausible?" The LLM returns a ranked shortlist; the
batch fit runs against the shortlist first, promotes the full list
only if no shortlist candidate clears the AIC threshold.

**Why.** LASR ([Grayeli et al. 2024](https://arxiv.org/pdf/2409.09359))
shows that "a library of abstract, reusable and interpretable textual
concepts" biases the search towards better hypotheses. [Nature
Sci-Reports 2026](https://www.nature.com/articles/s41598-026-35327-6)
pushes further — pre-trained LLMs as priors for physics-informed
symbolic regression. Both papers' takeaway: the LLM is terrible at
*doing* the regression, excellent at *narrowing the search*.

**How.** One Anthropic API call per fit session, templated prompt,
response parsed into a priority list over formula names. Cost:
~$0.001/call with Haiku; runs in <2s; entirely optional (feature
flag + offline fallback).

**Effort.** ~3 days. Thin wrapper over the existing `FormulaLibrary`
metadata.

Risk: LLM hallucinations surfacing as confident nonsense rankings.
Mitigate by keeping it strictly advisory — the batch fit still runs
on all formulas if time permits, and the LLM's shortlist is
surfaced in the UI as a "suggestions" panel, never hardcoded.

## 4. Phasing

| Phase | Mechanism | Dep | Weeks |
|------:|-----------|-----|-------|
| 1 | §3.1 fit history + ranking | none | 0.2 |
| 1 | §3.2 learned initial guesses | §3.1 | 0.2 |
| 1 | §3.3 `--self-benchmark` | §3.1 | 0.4 |
| 2 | §3.4 regression harness | §3.3 | 0.4 |
| 3 | §3.5 PySR tier | libtorch/python | 1.5 |
| 4 | §3.6 LLM hypothesis ranking | anthropic-sdk | 0.6 |

Phase 1 lands without new dependencies and gives the tool a memory.
Phase 2 protects that memory against accidental regressions. Phase
3 adds genuine discovery. Phase 4 adds guidance. Ship in order — each
phase is independently valuable and the later ones can be deferred
without breaking the earlier investment.

## 5. Open questions

- **Where does `.fit_history.json` live?** Options: per-project
  (alongside the CSVs), per-user (`~/.config/vestige/workbench/`),
  per-repo-checkout. Per-project is most shareable but bloats
  repos; per-user is invisible to teammates. Recommend per-project,
  gitignored by default, with an opt-in `commit-history.json`
  manifest that curates which fits are worth sharing.
- **Privacy in the LLM prompt.** If `.fit_history.json` is ever
  committed, the prompt in §3.6 could leak coefficient values.
  Non-issue for an open engine; flag loudly in docs so downstream
  users of the workbench (game studios, data scientists) opt in
  knowingly.
- **Should symbolic regression output seed the library?** Tempting
  to say yes — every PySR discovery becomes a future template.
  Resist: the library is hand-curated for readability and physical
  meaning; auto-filling it from PySR's seven-term mutants dilutes
  that curation. Keep PySR results in a sidecar `.discovered.json`.

## 6. Relationship to the audit-tool self-learning work

The two designs are sibling mechanisms applied to different domains.
Where the audit tool's self-triage looks at the rule × finding
matrix to demote noisy rules, the workbench's self-triage looks at
the formula × data matrix to promote well-fitting formulas. Both
mechanisms land the same architectural moves: persist metadata per
decision, compute per-rule (per-formula) quality summaries, feed the
summary back into default selection. Implementing one builds the
conceptual and code-level scaffolding for the other — the
`FitHistory` shape in this doc mirrors `.audit_verified.local`; the
`--self-benchmark` subcommand mirrors `--self-triage`.

## 7. References

- Cranmer, M. (2023). "Interpretable Machine Learning for Science
  with PySR and SymbolicRegression.jl." [arxiv.org/abs/2305.01582](https://arxiv.org/abs/2305.01582)
- Udrescu, S.-M. & Tegmark, M. (2020). "AI Feynman: A physics-
  inspired method for symbolic regression." Science Advances.
  [science.org/doi/10.1126/sciadv.aay2631](https://www.science.org/doi/10.1126/sciadv.aay2631)
- Grayeli, A., Sehgal, A., Costilla-Reyes, O., Cranmer, M.,
  Chaudhuri, S. (2024). "Symbolic Regression with a Learned Concept
  Library." [arxiv.org/pdf/2409.09359](https://arxiv.org/pdf/2409.09359)
- "Interpretable scientific discovery with symbolic regression: a
  review" (2023). [link.springer.com/article/10.1007/s10462-023-10622-0](https://link.springer.com/article/10.1007/s10462-023-10622-0)
- "Discovering equations from data: symbolic regression in
  dynamical systems" (2025). [arxiv.org/html/2508.20257v1](https://arxiv.org/html/2508.20257v1)
- "Discovering physical laws with parallel symbolic enumeration"
  Nature Computational Science (2025).
  [nature.com/articles/s43588-025-00904-8](https://www.nature.com/articles/s43588-025-00904-8)
- "Knowledge integration for physics-informed symbolic regression
  using pre-trained large language models" Scientific Reports (2026).
  [nature.com/articles/s41598-026-35327-6](https://www.nature.com/articles/s41598-026-35327-6)
- "Comparing AIC and BIC for Model Selection."
  [numberanalytics.com/blog/comparing-aic-bic-model-selection](https://www.numberanalytics.com/blog/comparing-aic-bic-model-selection)
- Zhang, J. (2023). "Information criteria for model selection."
  WIREs Computational Statistics.
  [wires.onlinelibrary.wiley.com/doi/10.1002/wics.1607](https://wires.onlinelibrary.wiley.com/doi/10.1002/wics.1607)
- "Origin: Theory of Nonlinear Curve Fitting."
  [originlab.com/doc/origin-help/nlfit-theory](https://www.originlab.com/doc/origin-help/nlfit-theory)
- "Adaptive Design of Experiments Based on Gaussian Processes."
  [researchgate.net/publication/289539781](https://www.researchgate.net/publication/289539781_Adaptive_Design_of_Experiments_Based_on_Gaussian_Processes)
- "Foundation Model for Chemical Process Modeling: Meta-Learning
  with Physics-Informed Adaptation." [arxiv.org/html/2405.11752v1](https://arxiv.org/html/2405.11752v1/)
- "Enhancing Classification Algorithm Recommendation in AutoML."
  [techscience.com/CMES/v142n2/59372](https://www.techscience.com/CMES/v142n2/59372)
