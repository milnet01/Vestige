# Phase 10 — Fog, Mist & Volumetric Lighting (Design Doc)

**Status:** Draft — awaiting user sign-off before implementation.
**Research:** See `docs/phases/phase_10_fog_research.md` for citations and derivations.
**Scope:** Deferred-pipeline fog for the Vestige engine — distance fog, exponential height fog, volumetric fog (froxel-based), and god rays. Delivered as a series of small slices that match the Phase 10 audio cadence.

---

## 1. Goals

- Ship every `Fog, Mist, and Volumetric Lighting` roadmap bullet in Phase 10.
- Stay inside a **2.0 ms / frame** GPU budget on RX 6600 at 1080p for the full fog stack at the default quality preset (section 7 of the research doc).
- Integrate with the existing HDR composite pipeline (`screen_quad.frag.glsl`) without breaking bloom, tonemap, colour grading, or the accessibility filter.
- Expose accessibility toggles symmetrical to `depthOfFieldEnabled` / `motionBlurEnabled` (research §6).
- Route the one fittable formula in the whole fog scope — the **Schlick approximation to the Henyey-Greenstein phase function** — through the Formula Workbench, per CLAUDE.md Rule 11.
- Take the opportunity, while standing up that fit, to close three Workbench feature gaps identified during the research (section 9 of this doc).

---

## 2. Scope Split — what ships when

Phase 10 fog is too big for a single commit. The slices mirror the Phase 10 audio cadence (10.1 – 10.10):

| Slice | Title | Complexity | Ships |
|-------|-------|------------|-------|
| **11.1** | Distance fog primitives (pure-function) | S | `engine/renderer/fog.{h,cpp}` + 15+ unit tests. No shader wiring yet. |
| **11.2** | Distance fog shader integration | M | `screen_quad.frag.glsl` fog uniforms + depth-reconstruction helper + renderer plumbing. Opaque-only. |
| **11.3** | Exponential height fog | M | Adds `FogHeightParams` + analytic Quílez integral to CPU core and GLSL. |
| **11.4** | Sun-direction inscatter lobe | S | Cosine-lobe directional scattering add (UE "DirectionalInscatteringColor" pattern). |
| **11.5** | Screen-space god rays (Mitchell) | M | Separate post-process pass. Cheap fallback for Low / Medium preset. |
| **11.6** | Volumetric fog foundation | L | Froxel grid + density injection compute pass + Beer-Lambert accumulation. No temporal. |
| **11.7** | Volumetric fog phase function | M | **Workbench-fit Schlick `k(g)`** swapped in for HG. See §5 of this doc. |
| **11.8** | Volumetric fog temporal reprojection | M | Halton jitter + 5 %/95 % EMA blend. Quality toggle. |
| **11.9** | Accessibility toggles | S | `PostProcessAccessibilitySettings` extension + UITheme hooks. |
| **11.10** | Editor FogPanel | M | Mirror the AudioPanel four-tab pattern (Distance / Height / Volumetric / Debug). |

This doc specifies **11.1** in full and sketches the integration path for the rest.

---

## 3. API design — slice 11.1 (Distance fog primitives)

### File surface

- `engine/renderer/fog.h` — pure-function primitives (no GL types).
- `engine/renderer/fog.cpp` — implementation.
- `tests/test_fog.cpp` — unit coverage.

### Types

```cpp
namespace Vestige
{

enum class FogMode
{
    None,                 // Pass-through. factor = 1.0 always.
    Linear,               // OpenGL GL_LINEAR: factor = (end - d)/(end - start)
    Exponential,          // OpenGL GL_EXP:    factor = exp(-density * d)
    ExponentialSquared,   // OpenGL GL_EXP2:   factor = exp(-(density * d)^2)
};

struct FogParams
{
    glm::vec3 colour = {0.55f, 0.60f, 0.65f}; // Linear-RGB inscattering colour
    float start   = 20.0f;   // Linear mode only
    float end     = 200.0f;  // Linear mode only; must be > start
    float density = 0.02f;   // EXP/EXP2 modes; must be >= 0
};

float computeFogFactor(FogMode mode, const FogParams& params, float distance);
glm::vec3 applyFog(const glm::vec3& surfaceColour,
                   const glm::vec3& fogColour,
                   float factor);
const char* fogModeLabel(FogMode mode);

}
```

### Guarantees (= what tests enforce)

- **Stable labels** — `fogModeLabel` returns the exact enum identifier; regressions here break the editor.
- **`None` is a pass-through** — factor == 1.0 at any distance.
- **Below-camera / sub-start distances** — factor == 1.0 (fog never projects behind the camera; guards linearised-depth reconstruction noise in the shader).
- **Linear at knees** — factor == 1.0 at `d <= start`, factor == 0.0 at `d >= end`, factor == 0.5 at the midpoint.
- **EXP at known points** — `density * d = ln 2` gives factor == 0.5 exactly.
- **EXP2 at knees** — factor == 1.0 at `d = 0`, factor == `exp(-1)` ≈ 0.368 at `density * d = 1`.
- **Monotonic decay** — factor is non-increasing with distance for Linear / EXP / EXP2.
- **Degenerate params handled without NaN** — `end == start` (zero span) returns 1.0 rather than dividing by zero; `density < 0` clamps to 0.
- **`applyFog` matches GLSL `mix(fogColour, surfaceColour, factor)`** — verified byte-for-byte in tests against the shader-side formula, clamped to `[0, 1]`.

No coefficients fitted — these are textbook canonical forms (research §1).

### Integration stub (slice 11.2, not in 11.1)

The eventual shader surface, documented here for context:

```glsl
// screen_quad.frag.glsl — inserted AFTER contact shadows, BEFORE bloom add.
uniform int       u_fogMode;       // 0/1/2/3 matching FogMode
uniform vec3      u_fogColour;
uniform float     u_fogStart;
uniform float     u_fogEnd;
uniform float     u_fogDensity;
uniform sampler2D u_depthTexture;  // Linear eye-space depth

float fogFactor = computeFogFactor(u_fogMode, ..., eyeDistance);
color = mix(u_fogColour, color, fogFactor);
```

The CPU-side `applyFog` is identical to this shader line — tests that exercise the CPU path act as a spec for the GPU path.

---

## 4. HDR composition order (all slices)

Current `screen_quad.frag.glsl` order is SSAO → contact shadows → bloom-add → exposure → tonemap → LUT → colour-vision → gamma. Fog inserts **after contact shadows, before bloom**:

```
HDR scene  -->  SSAO  -->  contact shadows
           -->  [fog mix with fogColour]      <-- new, slice 11.2
           -->  bloom add
           -->  exposure
           -->  tonemap (ACES/Reinhard/none)
           -->  LUT
           -->  colour-vision filter
           -->  gamma
```

Rationale (research §1 & §5): fog is a radiance contribution, so it must be in linear HDR. Bloom must sample the *fogged* radiance so bright fog haze blooms correctly; this also matches UE and HDRP.

Volumetric fog (slice 11.6+) writes its own `(inscatter, transmittance)` 3D texture before the composite runs, and the composite samples it at each pixel's linearised-depth froxel coordinate, replacing the height/distance term with the integrated value for that pixel.

---

## 5. Formula Workbench use — slice 11.7 (Schlick phase function)

### What gets fit

The volumetric light-scattering pass evaluates the Henyey-Greenstein phase function per froxel per light. The exact HG form involves `pow(x, 1.5)`, which is expensive on every GPU. The textbook Schlick approximation replaces it with:

```
p(cos θ, g) ≈ (1 - k²) / (4π (1 - k · cos θ)²)   where  k = k(g)
```

The textbook `k(g) = 1.55g - 0.55g³` is widely cited but has **no published error bound** against true HG over the anisotropy range we actually use (`g ∈ [0.1, 0.95]`, atmospheric haze through clouds).

### Workbench procedure

1. **Reference data.** Generate a dense `(g, cos θ)` grid (`g ∈ [0.1, 0.95]` step 0.01 = 86 values × `cos θ ∈ [-1, 1]` step 0.02 = 101 values = 8,686 samples) and evaluate analytic HG on the CPU side.
2. **Fit target.** A rational form for `k(g)` — start with cubic `a₀ + a₁g + a₂g² + a₃g³`, check whether a quadratic is sufficient.
3. **Bounds.** `a_i ∈ [-5, 5]` (loose — LM should converge inside them).
4. **Loss.** Weighted L2 on `p(cos θ, g)` — weight forward scatter (`cos θ > 0.5`) 4× because that's where the sun-direction artefact lives.
5. **Acceptance.** Max-abs error over `p(cos θ, g)` ≤ **2 %** over the full grid; AIC/BIC lower than the textbook `k = 1.55g - 0.55g³` baseline.
6. **Export.** GLSL snippet — a single `k_of_g(g)` function inlined into the volumetric scatter compute shader.

### Engine integration

`engine/renderer/volumetric_phase_schlick.glsl` — generated artefact, checked in, regenerated via `formula_workbench --reference-case phase_schlick_hg`.

### Reference case JSON

A new `tools/formula_workbench/reference_cases/phase_schlick_hg.json` — format matches the existing `fresnel_schlick.json` / `exponential_fog.json` cases, with the HG-on-a-grid reference inline rather than on an `input_sweep` single variable (see §9 — this requires a small Workbench extension).

---

## 6. Accessibility (slice 11.9)

Extend `engine/accessibility/post_process_accessibility.{h,cpp}` — follows the existing DoF / motion-blur pattern:

```cpp
struct PostProcessAccessibilitySettings
{
    // ... existing fields ...
    bool  fogEnabled             = true;   // Default on; safeDefaults() -> false? TBD
    float fogIntensityScale      = 1.0f;   // Scales density + inscatter contribution
    bool  volumetricFogEnabled   = true;   // Independent from fogEnabled
    bool  reduceMotionFog        = false;  // Disables temporal reprojection + caps sun-lobe
};
```

`safeDefaults()` (the one-click accessibility preset) flips `volumetricFogEnabled = false` and `reduceMotionFog = true`. Distance fog itself stays on because disabling it produces a harsh fog-horizon cutoff on mountains and long sightlines (visually worse).

WCAG 2.2 SC 2.3.1 / 2.3.3 and Xbox AG 117/118 cited in research §6.

---

## 7. Performance targets (all slices)

Per research §7, the combined fog stack must fit inside **2.0 ms** at the High preset on RX 6600 / 1080p. Per-slice budgets:

| Slice | Budget | Technique |
|-------|--------|-----------|
| 11.1–11.3 (distance + height) | < 0.1 ms | Single `exp` + divide per pixel in `screen_quad.frag.glsl`. |
| 11.4 (sun inscatter lobe) | < 0.1 ms | Single `pow` per pixel. |
| 11.5 (god rays, Low/Med preset) | 0.3–0.6 ms | 64–128 taps along sun vector. |
| 11.6–11.8 (volumetric High preset) | ~1.2–1.5 ms | 160×90×64 froxels, Schlick phase, CSM sampling, temporal on Ultra. |
| **Stack total, High preset** | **~1.5 ms** | Well inside 2.0 ms budget. |

Enforced via benchmark harness (new `tests/test_fog_benchmark.cpp` in slice 11.6) running under `VESTIGE_BENCHMARK_ONLY=1` to stay out of the unit-test wall-clock.

---

## 8. Test strategy

- **Slice 11.1** — pure-function unit tests (formulas, knees, monotonicity, degenerate params, `applyFog` ↔ GLSL-`mix` parity). 15+ cases.
- **Slices 11.2–11.5** — shader integration smoke tests via headless offscreen render; pixel-comparison against a reference image at a known camera pose. (Pattern: same as bloom / SSAO.)
- **Slice 11.6+** — benchmark harness + correctness smoke + "volumetric off" equivalence (when `volumetricFogEnabled = false` the output must match the height-fog-only path byte-for-byte).
- **Slice 11.7** — Workbench reference-case regression: `phase_schlick_hg` must converge and meet `max_abs_error ≤ 0.02`. Added to `tools/formula_workbench/scripts/run_reference_cases.sh` (or wherever the existing reference-case runner lives).

---

## 9. Formula Workbench improvements (opportunistic, surfaced by this research)

Three concrete gaps the fog research exposed. All small; each unlocks a class of fit beyond just phase-function work.

### 9.1 — Multi-input reference cases (2D sweeps)

**Problem.** Every existing reference case (`beer_lambert`, `exponential_fog`, `fresnel_schlick`, …) uses `input_sweep: { <single variable>: {min, max, count} }`. HG-vs-Schlick is `p(g, cos θ)` — intrinsically 2D.

**Fix.** Extend the reference-case JSON schema to allow a grid:

```json
"input_grid": {
    "g":        { "min": 0.1, "max": 0.95, "step": 0.01 },
    "cosTheta": { "min": -1.0, "max": 1.0, "step": 0.02 }
}
```

The harness iterates the Cartesian product. No breaking change to existing 1D cases (both keys are accepted; one is required).

**Scope.** ~80 lines in `reference_harness.cpp` + schema docs update. Enables all future 2D+ fits (BRDF lobes, tonemap curves under exposure, etc.).

### 9.2 — Max-absolute-error metric alongside RMSE / R²

**Problem.** The current `expected` block in reference cases accepts `r_squared_min` and `rmse_max`. For rendering-formula fits, the user-visible artefact is *the worst-case error*, not the average — a Schlick approximation with RMSE 0.005 but max-abs 0.15 looks broken at the sun direction even though it passes RMSE. Same applies to tonemap curve approximations, BRDF fits, etc.

**Fix.** Add:

```json
"expected": {
    "r_squared_min": 0.999,
    "rmse_max":      0.002,
    "max_abs_error_max": 0.02,     // NEW
    "must_converge": true
}
```

Compute max-abs error in `reference_harness.cpp` alongside the existing metrics. Backwards-compatible (optional field).

**Scope.** ~20 lines. Touches `fit_history.{h,cpp}` + `reference_harness.cpp`.

### 9.3 — Weighted-loss support (per-sample weight vector)

**Problem.** The phase-function fit needs to weight forward scatter more heavily than back-scatter because that's where the visual artefact sits. Current LM fitter uses uniform weights.

**Fix.** Extend `engine/formula/curve_fitter.{h,cpp}` (+ workbench UI) to accept an optional per-sample weight vector. If absent, behaviour is unchanged. Reference cases declare weights via an optional `"weights"` expression:

```json
"weights": "pow(max(cosTheta, 0.0), 2.0) * 3.0 + 1.0"
```

Evaluated per-sample at reference-dataset generation time.

**Scope.** ~40 lines + UI toggle. Enables weighted fits for any rendering formula where some input regions matter more (tonemap highlight region, BRDF grazing angles, shadow-falloff knee).

### Workbench release plan

If the user greenlights these, they ship as workbench **1.X.0** (minor bump, new features, no breaking changes to existing reference cases) in the commit that introduces the `phase_schlick_hg` reference case — keeping the Workbench CHANGELOG + VERSION in the same commit per CLAUDE.md feedback rule.

---

## 10. Open questions for user sign-off

1. **Scope of slice 11.1.** Ship just the pure-function primitives (Linear / EXP / EXP2) now, or bundle with 11.2 (shader integration) so the first fog commit produces a visible feature?
2. **Do we want height fog (11.3) in the initial run**, or land distance fog alone and circle back?
3. **Volumetric fog commitment.** Are we definitely shipping volumetric fog in Phase 10 (slices 11.6–11.8), or is it a "only if the frame budget holds" aspiration? The research says 1.2–1.5 ms on RX 6600, leaving comfortable headroom, but it's the single biggest Phase 10 rendering item.
4. **Workbench improvements (§9).** Green-light all three, or subset? They're all small, but each is a workbench commit in its own right.
5. **Accessibility default.** Should `fogEnabled` default to `true` (normal) with `safeDefaults()` flipping it off, or is fog innocuous enough to stay on even under the safe preset? I currently propose keeping distance fog on and only disabling volumetric fog in safe mode (research §6 rationale).

---

**Review requested:** please confirm §10 answers before slice 11.1 implementation begins.
