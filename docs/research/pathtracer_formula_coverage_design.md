# Path-Tracer Formula Coverage — Design

**Status:** Signed off (2026-06-17) — cleared for implementation. Cold-eyes: 4 loops to convergence (see §9).
**ROADMAP:** Formula Pipeline (Cross-Cutting Infrastructure) → "Path-tracer formula coverage (DOOM_Ants Workbench requests, 2026-06-16)", IDs `3D_E-0006`..`3D_E-0012`
**Source of requests:** `DOOM_Ants_Feedback.md` (gitignored; authored by the DOOM_Ants Vulkan path-tracer project while scoping the Vestige Formula Workbench as its coefficient-authoring tool)
**Subsystem touched:** `engine/formula/` + `tools/formula_workbench/` only. **Not** the Vestige runtime engine.

---

## 1. Context & goal

DOOM_Ants is an external GPL-v2 project building a Vulkan path tracer. It wants to
author every tuning curve (BRDF terms, sampling PDFs, denoiser weights, tonemap,
colour transforms) in the Vestige Formula Workbench, export them as GLSL, and
commit the generated `.glsl` into its shader tree — no runtime dependency on the
tool. The Workbench already ships the BRDF *evaluation* half (Fresnel, GGX D,
Schlick G, Beer-Lambert, Henyey-Greenstein-via-Schlick, ACES). The seven requests
fill the *path-tracer-specific* gaps.

This doc covers all seven as one change because they share one subsystem, one
test harness, and one export path. It is deliberately a single design note rather
than seven — each item is a small, well-specified addition to a **COMPLETE**
subsystem following an established pattern (`PhysicsTemplates::createXxx()` +
a `reference_cases/*.json`).

The seven:

| ID | Kind | Item |
|----|------|------|
| 3D_E-0006 | feature | Importance-sampling / PDF templates (GGX-VNDF pdf, cosine-hemisphere pdf, MIS power heuristic) |
| 3D_E-0007 | feature | Real-time-RT denoiser / temporal-accumulation blend templates (SVGF family) |
| 3D_E-0008 | feature | Russian-roulette survival-probability template |
| 3D_E-0009 | feature | Documented headless GLSL-export path for an external Vulkan project |
| 3D_E-0010 | feature | `srgb_to_linear` / `linear_to_srgb` output-transform pair |
| 3D_E-0011 | enhancement | `--self-benchmark` over a directory of datasets |
| 3D_E-0012 | enhancement | Provenance comment in generated GLSL |

---

## 2. The central design decision: scalar-AST scope

The formula expression AST (`engine/formula/expression.h`) is **scalar-valued**.
Node types are `LITERAL`, `VARIABLE`, `BINARY_OP` (`+ - * / pow min max dot`,
plus `mod` — unused here), `UNARY_OP` (`sin cos sqrt abs exp log floor ceil
negate saturate`, plus `sign` — unused here), and `CONDITIONAL` (ternary). There is **no vector constructor, no swizzle, no cross
product, and the tree-walking evaluator returns a `float`**. `FormulaValueType`
has `VEC2/3/4` for *declaring* I/O types, but no expression can *build* a vector
result — codegen emits `return <scalar-expr>;`.

Consequence, stated plainly so we do not over-promise to DOOM_Ants:

- **What ships as Workbench formulas:** the scalar **PDFs, weights, and
  transfer functions**. These are closed-form scalars, fittable/validatable and
  GLSL-exportable exactly like the existing BRDF terms.
- **What does NOT:** the **vector sample-direction routines** — GGX-VNDF
  `(u1,u2) → microfacet normal` (needs an orthonormal basis transform and vec3
  algebra), and the cosine-hemisphere Malley disk-lift `(u1,u2) → direction`.
  These are deterministic algorithms with **no coefficients to fit**, so they do
  not belong in a *fitting* workbench anyway. They stay as hand-written GLSL in
  the consumer. We document the canonical mapping in §3.1 as a reference comment,
  but we do **not** force vec3 algebra into the scalar AST — that would be an
  architecture change far out of proportion to the request (violates
  simplicity-first), and the evaluator/codegen could not honour it.

This is the honest read of request #1's own fallback: *"Even just shipping the
canonical expressions as 0-coefficient FULL-tier formulas … would remove a large
class of hand-typed, hard-to-verify sampling code."* We ship the PDFs (the part
that is a scalar and that the BRDF estimator divides by); the sampling mappings
remain code.

---

## 3. New formula templates

All new templates are added in `engine/formula/physics_templates.cpp` via a
`createXxx()` function returning a `FormulaDefinition`, registered by appending
`all.push_back(createXxx());` in `PhysicsTemplates::createAll()`. Each gets a
`tools/formula_workbench/reference_cases/<name>.json` regression case.

**Coefficients are canonical defaults, not fitted-in-our-repo values.** These
expressions are reference math; the DOOM_Ants consumer re-fits the tunable ones
(edge-stopping sigmas, temporal alpha floor, adaptive-count gain) against its own
captured per-pixel data. So the tunable coefficients **stay named** (a literal-
inlined constant could not be re-fit) — and we regression-lock the **expression
shape + default coefficient values** with **evaluation-regression** reference
cases (golden input→output points), not fit-regression. Rationale: there is no
captured reference dataset in *this* repo to fit against; synthesising-then-
refitting a zero-noise dataset would only test the fitter, not the formula; and
several of these formulas (clamps, `max`, `pow` with a 128 exponent) are
ill-conditioned for Levenberg-Marquardt (flat/zero-gradient regions), so
fit-regression would be flaky for exactly the coefficient-bearing templates.

**Harness enhancement required (part of this change).** The existing
evaluation-regression path (`reference_harness.cpp:279-316`) builds the
evaluator's variable map from the golden point's `inputs` **only** — it was
written for *zero-coefficient* formulas (its own comment, line 274), so a
coefficient-bearing formula throws `"Undefined variable: <coeff>"`. Fix: merge
`formula->coefficients` (the library **defaults**, not the case's
`canonical_coefficients` — that field is a fit-regression input and is unused on
this path) into the var map **first**, then apply the golden `inputs` so inputs
win on key-collision. This uses the same coefficients-as-variables mechanism as
mode-1 `synthesizeDataset` (`reference_harness.cpp:242-244`, "the evaluator
treats variables and coefficients identically") but in the **reverse order** —
mode-1 applies inputs then coefficients (coefficients win); mode-2 applies
coefficients then inputs (inputs win), so a golden point *may* probe a
non-default coefficient while normally supplying only true inputs.
This locks each formula at its **shipped default coefficients** — change a
default and the golden test fails — while the coefficients remain re-fittable by
the consumer. One added line; it also lets the *existing* coefficient-bearing
templates use mode 2 in future.

Golden outputs are computed with an independent trusted calculator (Python
`math`) at authoring time so the C++ evaluator is the thing under test, not the
source of its own truth.

`accuracy` (R²) is set to `1.0` for the canonical/exact closed-form formulas
(they are not approximations of reference data — R²=1.0 here reads "exact closed
form, not a fit", a convention noted in §4.3) and the `source` string carries the
citation. The one non-canonical template (`adaptive_sample_count`, §3.2) keeps
R²=1.0 but its `source` string flags it as a tunable template, not a citation.

### 3.1 Sampling PDFs (3D_E-0006) — category `sampling`

**`cosine_hemisphere_pdf`** — diffuse-bounce solid-angle PDF.
- Inputs: `cosTheta` (FLOAT, `N·omega`).
- FULL: `cosTheta * (1/pi)` → `binOp("*", var("cosTheta"), lit(0.31830989))`.
  (Encoded as a literal `1/pi`; the AST has no `pi` constant. Documented in the
  description.)
- Domain: upper hemisphere; caller guarantees `cosTheta >= 0` (PDF is 0 below,
  but the formula is the positive branch — the consumer masks the lower
  hemisphere, same as PBRT's helper which assumes `cosTheta >= 0`).
- Source: *Pharr, Jakob & Humphreys, Physically Based Rendering, 4th ed (2023),
  Appendix A.5.3 "Cosine-Weighted Hemisphere Sampling"; pdf = cosθ/π.*

**`ggx_vndf_pdf`** — PDF of the reflected direction when sampling the GGX visible
normal distribution.
- Inputs: `G1` (FLOAT, Smith monodirectional masking for V), `D` (FLOAT, GGX NDF
  value at H), `NdotV` (FLOAT, `N·V`).
- FULL: `G1 * D / (4 * NdotV)` → `binOp("/", binOp("*", var("G1"), var("D")),
  binOp("*", lit(4.0), var("NdotV")))`. The `/` codegens to `safeDiv` (guards
  `NdotV == 0`).
- Derivation note in description: `D_visible(H) = G1·max(0,V·H)·D / (N·V)`; the
  reflection Jacobian `dω_h/dω_i = 1/(4|V·H|)` cancels the `(V·H)` term, leaving
  `pdf(L) = G1·D/(4·N·V)`. Inputs are `G1`, `D`, `N·V` (consumer supplies these
  from its existing `ggx_distribution` / `schlick_geometry` evaluations — direct
  reuse of shipped templates).
- Caveat: reflection lobe, single-scattering, isotropic. Roughness remap
  `α = perceptualRoughness²` is the caller's, not part of this PDF.
- Source: *Heitz, "Sampling the GGX Distribution of Visible Normals", JCGT 7(4),
  2018 (D_visible, Eq. 3); Walter et al., "Microfacet Models for Refraction
  through Rough Surfaces", EGSR 2007 (the 1/(4|V·H|) reflection Jacobian — Heitz
  gives the sampling pdf only as a code comment).*

**`mis_power_heuristic`** — two-strategy, single-sample, β=2 weight.
- Inputs: `pdfA` (FLOAT), `pdfB` (FLOAT).
- FULL: `pdfA² / (pdfA² + pdfB²)` → `safeDiv(pow(pdfA,2), pow(pdfA,2)+pow(pdfB,2))`.
- APPROXIMATE: balance heuristic (β=1) `pdfA / (pdfA + pdfB)`.
- Caveat: weights for the two strategies must sum to 1; vanishes where its pdf is
  0. β=2 is Veach's empirical recommendation, not a derived optimum (balance is
  the proven-near-optimal baseline → hence APPROXIMATE tier).
- Source: *Veach & Guibas, "Optimally Combining Sampling Techniques for Monte
  Carlo Rendering", SIGGRAPH '95, Eq. 14 (power), §3.3 (balance).*

*Reference comment only (not a formula — see §2):* GGX-VNDF sample-direction and
cosine-hemisphere Malley mapping are documented in the design doc and as a
`// canonical sample mapping (hand-written, vec3 — not a Workbench formula)`
comment block emitted nowhere automatically; the consumer copies it from here.

### 3.2 Denoiser / temporal-accumulation (3D_E-0007) — category `denoise`

SVGF family (Schied et al. 2017). Defaults σ_z=1, σ_n=128, σ_l=4 are the paper's
recommended values (retunable by the consumer).

**`temporal_alpha`** — EMA blend factor with a floor (running-average warmup form
the request asked for; SVGF's fixed α=0.2 is the consumer's choice of `aMin`).
- Inputs: `n` (FLOAT, history length / accumulated frame count).
- Coefficients: `aMin` (default `0.2`).
- FULL: `max(aMin, 1/(n+1))` → `binOp("max", var("aMin"), safeDiv(1, n+1))`.
  At `n=0` → `max(aMin,1)=1` (full reset on first/disoccluded sample); decays
  toward `aMin` as history grows.
- Source: *Schied et al., "Spatiotemporal Variance-Guided Filtering", HPG '17,
  §4.1 (temporal integration; disocclusion → α=1 reset).*

**`edge_stopping_depth`** — w_z.
- Inputs: `dz` (FLOAT, `|z_p − z_q|`), `gradTerm` (FLOAT, `|∇z·(p−q)|`).
- Coefficients: `sigmaZ` (1.0), `eps` (1e-3).
- FULL: `exp( -dz / (sigmaZ*gradTerm + eps) )`.
- Source: *Schied et al. 2017, Eq. 3.*

**`edge_stopping_normal`** — w_n.
- Inputs: `ndot` (FLOAT, `n_p·n_q`).
- Coefficients: `sigmaN` (128.0).
- FULL: `pow( max(0, ndot), sigmaN )`.
- Source: *Schied et al. 2017, Eq. 4.*

**`edge_stopping_luminance`** — w_l.
- Inputs: `dl` (FLOAT, `|l_p − l_q|`), `gVar` (FLOAT, 3×3-Gaussian-prefiltered
  luminance variance).
- Coefficients: `sigmaL` (4.0), `eps` (1e-3).
- FULL: `exp( -dl / (sigmaL*sqrt(gVar) + eps) )` (`sqrt` → `safeSqrt`).
- Source: *Schied et al. 2017, Eq. 5.*

**`adaptive_sample_count`** — variance-driven spp curve. **Engine template, not a
published formula** — a sensible parametric starting curve the consumer fits to
its own variance/error capture.
- Inputs: `variance` (FLOAT).
- Coefficients: `k` (8.0), `nMin` (1.0), `nMax` (16.0).
- FULL: `min(nMax, max(nMin, floor(k*sqrt(variance) + 0.5)))`.
- Source string: `"engine template — fit k/nMin/nMax to captured variance"`.
- `accuracy` left at default; description flags it as a template, not a citation.

### 3.3 Russian roulette (3D_E-0008) — category `pathtrace`

**`rr_survival`** — survival probability. **Engine variant, explicitly labelled —
not the PBRT text form.** PBRT uses throughput *luminance* and a single floor
(`q = max(0.05, 1−lum)`, survival ≤ 0.95). The request asked for the common
max-channel + two-sided clamp form; we ship that and say so.
- Inputs: `maxThroughput` (FLOAT, `max(throughput.r,g,b)` — the max reduction is
  the caller's, a vec3 op outside the AST).
- Coefficients: `pMin` (0.05), `pMax` (0.95).
- FULL: `min(pMax, max(pMin, maxThroughput))`.
- Caveat in description: "Common engine variant (max-channel + two-sided clamp).
  PBRT §14.5.4 uses luminance + single floor `q=max(0.05,1−lum)`. RR keeps the
  estimator unbiased but increases variance — apply only after early bounces
  (e.g. depth > 3). Survivors rescale throughput by 1/p (caller's job)."
- Source: *Arvo & Kirk, "Particle Transport and Image Synthesis", SIGGRAPH '90;
  PBRT §13.7 / §14.5.4 (note: PBRT form differs — see caveat).*

### 3.4 sRGB transfer functions (3D_E-0010) — category `color`

Exact IEC 61966-2-1 piecewise. Per-channel, R/G/B only (never alpha).

**`srgb_to_linear`** (decode).
- Inputs: `c` (FLOAT, one channel in [0,1]).
- FULL (piecewise): `(c <= 0.04045) ? c/12.92 : pow((c+0.055)/1.055, 2.4)`.
  Encoded with the codebase's scalar-predicate idiom (codegen emits
  `(cond != 0.0 ? then : else)`; the existing template at
  `physics_templates.cpp:1054` uses `max(0, diff)` for a ">0" test). Predicate
  for `c <= 0.04045`: `max(0, 0.04045 - c)` — positive (true) when `c < 0.04045`,
  zero (false) when `c >= 0.04045`. The threshold-equality point is measure-zero
  and lands on the `else` (high) branch, matching the standard's `>` boundary.
- APPROXIMATE: `pow(c, 2.2)` (cheap gamma-2.2; no linear toe near black).
- Source: *IEC 61966-2-1:1999; W3C CSS Color 4 reference code. 12.92·0.0031308 ≈
  0.0404499 (the rounded breakpoint 0.04045). Curve is not perfectly C1 / not an
  exact inverse of the encode — use the rounded IEC constants in production.*

**`linear_to_srgb`** (encode).
- Inputs: `c` (FLOAT, one channel in [0,1]).
- FULL (piecewise): `(c <= 0.0031308) ? 12.92*c : 1.055*pow(c, 1/2.4) - 0.055`.
  Predicate `max(0, 0.0031308 - c)`.
- APPROXIMATE: `pow(c, 1/2.2)`.
- Source: *IEC 61966-2-1:1999; exponent 1/2.4 = 0.41666….*

---

## 4. Tooling changes

### 4.1 `--export-glsl` (3D_E-0009)

New headless CLI verb in `tools/formula_workbench`, dispatched from `main.cpp`
alongside the existing `runBenchmarkCli` / `runDumpLibraryCli` (same
`std::optional<int>` early-return pattern).

```
formula_workbench --export-glsl [<library.json>] --out <dir> [--tier full|approx]
```

- No `<library.json>` → use the built-in `FormulaLibrary` (same source as
  `--dump-library`). With a path → load via the **existing**
  `FormulaLibrary::loadFromFile` (`engine/formula/formula_library.cpp:136`),
  which already routes through `JsonSizeCap::loadJsonWithSizeCap` — so the
  untrusted-input size cap (SECURITY.md §10, CVE-2024-38525 mitigation) is
  inherited, not bypassed by a raw `nlohmann::json::parse`. Note `loadFromFile`
  currently parses in **non-strict** mode (rejects malformed JSON via
  `is_discarded`, but does not surface the structured parse error): its signature
  takes only a path (`engine/formula/formula_library.cpp:136`) and hard-codes the
  `loadJsonWithSizeCap` non-strict default. For the untrusted `--export-glsl`
  path, **add a `strict` parameter (or overload) to `loadFromFile`** that forwards
  `strict=true` to `loadJsonWithSizeCap`, so trailing-garbage / parse errors are
  reported with detail. (Small plumbing step — call it out so it isn't missed.)
- For each formula, write `<dir>/<formula_name>.glsl` (single function, with the
  safe-math prelude) **and** a combined `<dir>/formulas.glsl` (`generateFile` over
  all formulas — one prelude, then every function).
- **Deterministic ordering:** formulas sorted by `name` before emit, so
  regenerated artifacts diff cleanly in the consumer's git.
- Reuses `CodegenGlsl::generateFunction` / `generateFile` unchanged (plus the
  §4.3 provenance header). No new codegen logic.
- Exit non-zero with a clear message on: missing `--out`, unreadable library,
  un-writable dir.

**Input-validation gap this verb exposes (must close — see §4.4).** A
user-supplied `library.json` reaches codegen with its `name` and input `name`
fields **unvalidated** (`engine/formula/formula.cpp:175,185` read them with no
check; the only gate is non-empty at `engine/formula/formula_library.cpp:103`).
Those strings then (a) splice verbatim into the GLSL function/parameter identifier
via `toGlslFunctionName` (`engine/formula/codegen_glsl.cpp:150,159`), which strips
only `_` and rejects nothing — a
`name` like `f(){};void evil(` injects tokens; and (b) become the output filename
`<dir>/<formula_name>.glsl` — a `name` of `../../etc/cron.d/x` is path traversal
(SECURITY.md §2). This is the §H11 codegen-injection class, but the existing AST
hardening covers only `ExprNode.name`/`.op`, **not** `FormulaDefinition`/
`FormulaInput` metadata. §4.4 specifies the fix.

Plus a short doc section **"Consuming Workbench formulas from an external
C++/Vulkan project"** in `docs/engine/formula/` (or appended to the workbench
README): pin a `library.json`, run `--export-glsl` in the consumer's build,
`#include` the generated `.glsl`, and wire the reference harness as a CI/pre-commit
gate so coefficient drift fails the build. (The request offered "verb OR doc"; the
verb is small and the doc makes it usable, so both — the doc is cheap.)

### 4.2 `--self-benchmark` over a directory (3D_E-0011)

Extend the existing `runBenchmarkCli`. Currently `--self-benchmark <csv>` takes a
single file (confirmed: single-file only today). New behaviour: if the path is a
**directory**, iterate its `*.csv` files in sorted order, run `runBenchmark` on
each, and emit **one combined markdown** with a `## <dataset>` section per file
(reusing `renderBenchmarkMarkdown` per dataset). A single file keeps today's
behaviour exactly (back-compatible). Errors on individual files are reported in
the combined output and do not abort the batch; the verb exits non-zero if any
dataset failed to load.

### 4.3 Provenance comment in generated GLSL (3D_E-0012)

Enrich the comment `CodegenGlsl::generateFunction` already emits (currently just
`// <description>`):

```glsl
// <formula_name> — <description>
// source: <source string>   R²: <accuracy>
```

and a file-level banner in `generateFile`:

```glsl
// Generated by Vestige Formula Workbench v<WORKBENCH_VERSION>
// library hash: <fnv1a-64 of the dumped library JSON>
```

- We emit what the `FormulaDefinition` actually carries: `name`, `description`,
  `source`, `accuracy` (R²). **RMSE/AIC are benchmark outputs, not stored on the
  definition** — they are only available on the `--self-benchmark` path, so they
  are out of scope for plain codegen provenance (noted here so the omission reads
  as deliberate, not forgotten). R² is printed as `accuracy`; by the §3 convention
  R²=1.0 on a canonical formula means "exact closed form, not a fit" — the `source`
  string disambiguates the one tunable template.
- **Comment-injection sanitisation (required).** `description`/`source`/`name`
  are free-text from the (possibly untrusted) library JSON and GLSL `//` comments
  run to end-of-line — a newline in `source` terminates the comment and the
  remainder becomes code. Strip/replace `\r` and `\n` (collapse to single line)
  in these fields at the comment-emit site. The current code already emits
  `// <description>` (`engine/formula/codegen_glsl.cpp:147`) with this latent issue; this change
  adds the helper and applies it to all three fields. See §4.4.
- Library hash: **reuse the existing `fnv1a64`** (`fit_history.cpp:36`, currently
  file-local) over the `--dump-library` JSON text — promote it to a small shared
  header rather than re-implementing (project reuse rule). No new dependency.
- `WORKBENCH_VERSION` already exists: `inline constexpr const char* = "1.18.0"`
  at `workbench.h:32`. Reuse it; no new constant.
- **This is always-on** (not flag-gated): self-documenting output is the whole
  point, and a flag would let un-provenanced artifacts slip through. Trade-off:
  it changes generated text — but the codegen tests (`tests/test_formula_compiler.cpp`
  `GenerateFunction`/`GenerateFile`) assert via **substring** `find() != npos`,
  not exact-equality, so adding comment lines does not break them. Verify they
  still pass and **add** new assertions for the provenance line + banner; no
  existing assertion needs rewriting.

---

### 4.4 Input validation & codegen safety (closes the §4.1 gap)

The `--export-glsl <library.json>` verb is the first path that codegens from an
**untrusted** library (the built-in library is trusted; round-trip tests use
valid names). Three fixes, all reuse-not-rewrite:

1. **Validate `FormulaDefinition.name` and every `FormulaInput.name`** with the
   existing `ExprNode::isValidVariableName` (`expression.h:104` — `[A-Za-z_]`
   `[A-Za-z0-9_]*`, ≤128 chars) at load time in `FormulaDefinition::fromJson`
   (`formula.cpp`). Reject the formula otherwise. This is the natural sibling of
   the §H11 hardening already applied to `ExprNode::fromJson`, placed at the same
   layer so *all* load paths benefit (defence in depth). A validated `name`
   **structurally** prevents both the identifier-injection and the path-traversal
   findings — `/`, `.`, `..`, `(`, `;`, whitespace cannot appear, so
   `<dir>/<name>.glsl` cannot escape `<dir>`. Built-in templates (all snake_case)
   pass unchanged.
2. **Sanitise comment text** (`description`/`source`/`name`) for `\r`/`\n` before
   emitting provenance comments (§4.3) so free-text cannot break out of a `//`
   line into emitted GLSL.
3. **Load via `FormulaLibrary::loadFromFile`** (§4.1) to inherit the JSON size
   cap — do not hand-roll a parse.

The FNV-1a-64 library hash is used **for provenance/diff only, never for
integrity or trust decisions** — its non-cryptographic nature is therefore not a
concern (stated so a future reader does not repurpose it).

Coefficient *keys* are **not** an injection vector (confirmed): `emitExpression`
only substitutes a coefficient's float *value*
(`engine/formula/codegen_glsl.cpp:30-38`); the key string is never spliced into
source — so no validation is needed there. Likewise `category` and `unit`
(`formula.cpp:176,187,197`) are read unvalidated but are **deliberately** left so:
they reach only ImGui display, benchmark JSON, and node-editor grouping — never
codegen or the output filename. If a future provenance comment ever emits
`category`, it would need the same `\r`/`\n` sanitisation as `description`/`source`
above.

## 5. CPU / GPU placement

Per project rule 7. **All seven items are authoring-time / build-time CPU work**;
none runs in the Vestige runtime:

- The new formula templates are evaluated on the **CPU** in the Workbench (fitting,
  validation, reference harness) and **codegen'd to GLSL** for the *consumer's*
  GPU. Vestige itself does not execute these at runtime — they exist to be
  exported. So there is no Vestige runtime CPU/GPU choice to make.
- For the **consumer** (DOOM_Ants): every one of these is per-ray / per-pixel /
  per-bounce math → **GPU**, which is exactly why they are exported as GLSL. The
  scalar PDFs/weights/transforms map to single GPU functions; the sampling
  *mappings* (§2) are hand-written GPU code.
- Tooling (`--export-glsl`, batch `--self-benchmark`, provenance) is **CPU**,
  build-time, I/O-bound — correct for branching/file work per the default
  heuristic.

No dual CPU/GPU implementation and therefore no parity test is required: the
GLSL is generated *from* the single CPU expression tree by `CodegenGlsl`, and the
existing safe-math prelude already guarantees evaluator/shader parity (AUDIT
§H12/M11). The reference harness locks the CPU side; codegen parity is the
existing test_formula_compiler coverage.

---

## 6. Performance

The hard 60 FPS rule governs the Vestige runtime; **none of this runs in that
runtime**. Authoring-time concerns only:

- Reference-harness eval-regression for 11 new formulas: a handful of point
  evaluations each → negligible (existing harness runs 27 cases in milliseconds).
- `--export-glsl` over the full library: linear in formula count, one pass,
  build-time. No hot path.
- The generated GLSL the *consumer* ships is single-expression functions with the
  safe-math prelude — branch-light, no loops; the consumer owns its own 60 FPS
  budget (RX 6600 / RDNA2, same reference HW). The PDFs are exactly the cheap
  scalar functions a path tracer divides by per bounce.

## 7. Accessibility

N/A — no user-facing surface. (Recorded explicitly per the design-doc template;
the formulas feed an external renderer's shaders, not Vestige's UI.)

---

## 8. Verify-step plan

1. Extend the mode-2 harness to merge `formula->coefficients` first, then the
   golden `inputs` (inputs override) → **verify:** a coefficient-bearing reference
   case evaluates without `"Undefined variable"`, a case asserting a
   default-coefficient output fails when that default is changed, and a golden
   point that names a coefficient under `inputs` overrides the default.
2. Add 11 formula templates + register in `createAll()` → **verify:** new unit
   assertions that each is registered, has a FULL tier, and codegens to GLSL
   without a `// No expression` fallback.
3. Add 11 `reference_cases/*.json` (evaluation-regression, Python-derived golden
   points) → **verify:** reference harness passes all 11 within tolerance; whole
   suite green (`ctest`).
4. `--export-glsl` verb → **verify:** running it over the built-in library writes
   N+1 files, the combined `formulas.glsl` contains every function once, output is
   byte-identical across two runs (determinism), and a re-run after reordering the
   library leaves the output unchanged (sorted-by-name).
5. Input validation (§4.4) → **verify:** a `library.json` with `name` =
   `"../evil"` or `"f(){};x"` is rejected at load (formula skipped / clear
   error), so no file is written outside `--out` and no token is injected; a
   `source` containing a newline emits a single-line comment.
6. `--self-benchmark <dir>` → **verify:** a directory of 2 CSVs yields one
   markdown with 2 dataset sections; a single CSV path is byte-identical to the
   pre-change output (back-compat).
7. Provenance comments → **verify:** generated GLSL contains name/source/R² per
   function and the hash+version banner; existing `test_formula_compiler`
   substring assertions still pass, new banner/provenance assertions added; the
   library hash is stable across runs and changes when a formula changes.
8. Full build + `ctest` green; audit Tier 1–3 clean on touched files.

---

## 9. Cold-eyes loop log

> Per global rule 14 / project rule 9: fresh reviewer, no authoring context, loop
> until zero verified actionable findings. Loop 2+ runs cold (no briefing on prior
> fixes). Recorded as loops happen.

| Loop | Date | Findings (sev) | Disposition |
|------|------|----------------|-------------|
| 1 | 2026-06-17 | 3 lanes (correctness / cross-doc / security), all verified against source | **C1 (CRITICAL):** mode-2 harness doesn't inject coefficients → coefficient-bearing templates throw. **Fixed** §3 preamble + §8.1: one-line harness merge of `formula->coefficients`, keeping coefficients named. **HIGH×2 (security):** unvalidated `FormulaDefinition.name` → GLSL-identifier injection + filename path-traversal on `--export-glsl`. **Fixed** §4.1+§4.4: validate `name`/input `name` via existing `isValidVariableName` in `fromJson`. **MEDIUM (security):** comment-text newline breakout. **Fixed** §4.3+§4.4: sanitise `\r\n` in `description`/`source`/`name`. **MEDIUM (M1):** over-claimed test rewrites — codegen tests are substring-based. **Fixed** §4.3: verify-still-pass + add assertions. **LOW:** reuse `fnv1a64` (`fit_history.cpp:36`); R² convention for non-fitted template; `WORKBENCH_VERSION="1.18.0"` flat (resolved hedge); citation URLs. **Fixed** §4.3/§3/§References. All formula math, constants, citations, AST-expressibility, and the scalar-only scoping verified correct by the correctness lane (no change needed). |
| 2 | 2026-06-17 | 3 lanes, cold (no loop-1 briefing); zero CRITICAL/HIGH (loop-1 fixes held — none resurfaced) | **MEDIUM:** §3/§8.1 self-contradiction — "mirroring exactly" mode-1's coefficient order vs "inputs override" are opposite (mode-1 applies inputs-then-coefficients). **Fixed** §3/§8.1: mode-2 applies coefficients-then-inputs (reverse of mode-1), inputs win; clarified it uses library defaults not the case's `canonical_coefficients`. **LOW:** §2 op list omitted `mod`/`sign` (**fixed**, noted unused); `codegen_glsl.cpp` cited without dir prefix (**fixed** to `engine/formula/`); `loadFromFile` is non-strict so §4.1 try/catch claim overstated (**fixed**: recommend `strict=true` for untrusted path); `category`/`unit` unvalidated-but-safe not noted (**fixed** §4.4). All math/constants/citations/security-fix-location re-verified correct by all three lanes. |
| 3 | 2026-06-17 | 1 lane (correctness + internal consistency), cold; zero CRITICAL/HIGH/MEDIUM (loop-2 merge-order fix confirmed resolved, no stale language) | **LOW:** §4.1 instructed "pass `strict=true`" but `loadFromFile(path)` has no `strict` parameter — needs a param/overload forwarding to `loadJsonWithSizeCap`. **Fixed** §4.1: named the plumbing step explicitly. All 11 formulas, constants, citations, AST-expressibility, harness merge-order, and all file:line mechanism claims re-verified correct. |
| 4 | 2026-06-17 | 1 lane (correctness + internal consistency), cold | **CLEAN — zero actionable findings.** §4.1 strict-plumbing wording confirmed accurate; whole-doc internally consistent; all 11 formulas/constants/citations, AST expressibility, harness merge-order, and every file:line claim re-verified correct against source. Converged. |

## 10. Sign-off

> Sign-off delegated to the implementing session (user instruction 2026-06-17):
> requires (a) cold-eyes loops run until clean / polish-verified fixes only, and
> (b) genuine confidence in the spec. To be recorded after §9 converges.

- [x] Cold-eyes converged (zero actionable findings) — loop 4, 2026-06-17
- [x] Signed off — 2026-06-17 (sign-off delegated to implementing session;
  4 cold-eyes loops run to convergence, fixes verified against source, genuine
  confidence in the spec). Cleared for implementation.

---

## References

- Pharr, Jakob & Humphreys, *Physically Based Rendering: From Theory to
  Implementation*, 4th ed (2023), App. A.5.3 (cosine-hemisphere pdf), §13.10
  (MIS), §13.7 / §14.5.4 (Russian roulette). <https://pbr-book.org/>
- Heitz, E. "Sampling the GGX Distribution of Visible Normals." *JCGT* 7(4),
  1–13 (2018). <https://jcgt.org/published/0007/04/01/> (D_visible, Eq. 3).
- Walter, Marschner, Li & Torrance. "Microfacet Models for Refraction through
  Rough Surfaces." *EGSR 2007*. DOI 10.2312/EGWR/EGSR07/195-206 (reflection
  Jacobian 1/(4|V·H|) → pdf(L) = G1·D/(4·N·V)).
- Veach, E. & Guibas, L. J. "Optimally Combining Sampling Techniques for Monte
  Carlo Rendering." *SIGGRAPH '95*, 419–428. DOI 10.1145/218380.218498 (power
  heuristic Eq. 14, balance heuristic §3.3).
- Schied et al. "Spatiotemporal Variance-Guided Filtering: Real-Time
  Reconstruction for Path-Traced Global Illumination." *HPG '17*.
  DOI 10.1145/3105762.3105770 (temporal α §4.1; edge-stopping w_z/w_n/w_l
  Eqs. 3–5; σ_z=1, σ_n=128, σ_l=4).
- Arvo, J. & Kirk, D. "Particle Transport and Image Synthesis." *SIGGRAPH '90*,
  63–66. DOI 10.1145/97879.97886 (Russian roulette in graphics).
- IEC 61966-2-1:1999, *Default RGB colour space — sRGB*. Canonical reproduction:
  W3C CSS Color 4 sample code <https://www.w3.org/TR/css-color-4/#color-conversion-code>.
