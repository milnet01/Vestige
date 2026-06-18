# Phase 10 — Fog, Mist & Volumetric Lighting (Design Doc)

**Status:** ✅ Signed off for implementation (2026-06-18). Cold-eyes looped to clean (3 loops; sign-off delegated per session standing instruction). See the Cold-eyes loop log at the foot of this doc.
**Research:** See `docs/phases/phase_10_fog_research.md` for citations and derivations.
**Scope:** Deferred-pipeline fog for the Vestige engine. The non-volumetric layers (distance fog, exponential height fog, sun-inscatter lobe, composite shader integration, accessibility transform) **have shipped**. This doc now specifies the **remaining** volumetric work: froxel-based volumetric fog (single-scatter, no temporal), god rays, and placeable mist / ground-fog volumes.

---

## 0. What has already shipped (reality check, 2026-06)

These ROADMAP "Fog, Mist, and Volumetric Lighting" bullets are `[x]` and live in `engine/renderer/fog.{h,cpp}` + `assets/shaders/screen_quad.frag.glsl` + `tests/test_fog.cpp`:

| Done | Slice | What |
|------|-------|------|
| ✅ | 11.1 | Distance fog primitives — `FogMode` (None/Linear/Exponential/ExponentialSquared), `FogParams`, `computeFogFactor`. |
| ✅ | 11.2 | Composite shader integration — fog composed in linear HDR after contact shadows, before bloom; world pos reconstructed from reverse-Z depth via `u_fogInvViewProj`; sky pixels skip fog; `composeFog(...)` CPU mirror pins the GLSL. |
| ✅ | 11.3 | Height fog — `HeightFogParams` + Quílez 2010 analytic integral `computeHeightFogTransmittance` (CPU uses `std::expm1` for horizontal-ray stability; GLSL uses the `1-exp(-tau)` equivalent). |
| ✅ | 11.4 | Sun-inscatter lobe — `SunInscatterParams` + `computeSunInscatterLobe`. |
| ✅ | 11.9 | Accessibility transform — `applyFogAccessibilitySettings(authored, settings) → effective`. Master disable + intensity scale + reduce-motion. |

Test coverage for the shipped layers lives in `tests/test_fog.cpp`: the **`Fog`** suite (29 tests: distance/height/sun primitives, knees, monotonicity, degenerate params), **`FogComposite`** (7 tests), and **`FogAccessibility`** (12 tests) — 48 in total. (Slices 11.1/11.3/11.4 all share the `Fog` suite, which is why per-slice counts don't sum cleanly.)

The earlier draft of this doc specified only slice 11.1; that draft is superseded. **No code below changes the shipped layers** — the volumetric work is additive.

---

## 1. Goals (remaining work)

- Ship the three remaining ROADMAP bullets: **volumetric fog**, **volumetric god rays**, **mist / ground fog**.
- Stay inside the **2.0 ms / frame** GPU budget on RX 6600 at 1080p for the *full* fog stack at the High preset (research §7) — measured, not assumed (hard 60 FPS floor).
- Layer cleanly on the shipped composite: the volumetric pass produces a froxel-integrated `(inscatter, transmittance)` 3D texture that the existing `screen_quad.frag.glsl` composite samples, **replacing** the per-pixel distance/height term when volumetrics are enabled.
- Route the one fittable formula in the whole scope — the **Schlick approximation to Henyey-Greenstein** — through the Formula Workbench (CLAUDE.md Rule 6). The Workbench already has every capability this fit needs (multi-axis sweeps, max-abs-error acceptance, weighted loss — see §9), so slice 11.7 is reference-case authoring, **not** a tooling change.
- Extend the shipped accessibility transform with a `volumetricFogEnabled` master toggle (distance/height fog stay authored-on under the safe preset; only the moving volumetric layer is disabled).

### Scope decision — Phase 10 ships *basic* volumetrics; the froxel + temporal *upgrade* is Phase 13

This is the load-bearing scope call and it resolves a genuine self-contradiction in the source docs, so it is stated explicitly:

- The research doc's own Phase-10 recommendation (research §3 line 99, §7 line 206) is a **single 160×90×64 froxel grid, three compute dispatches (inject / scatter / integrate), one directional sun light with CSM shadow sampling per froxel, Schlick phase, and *no temporal reprojection*.**
- ROADMAP line 1657 confirms the boundary: *"Basic god rays and volumetric fog land in Phase 10 … this Phase 13 item covers the froxel-volume + temporal-reprojection rendering upgrade."*
- The Phase-10 ROADMAP bullet's sub-bullets list temporal reprojection and multi-light, but those contradict both the research recommendation and the Phase-13 note. **We follow the research + Phase-13 boundary:** temporal reprojection, multi-light scattering, and higher-res grids are **deferred to Phase 13**. Phase 10 = single-scatter sun-only froxel fog, no temporal.

Consequence for accessibility: with no temporal reprojection in Phase 10, the volumetric layer has no inter-frame "background movement" shimmer, so `reduceMotionFog` (already shipped) only needs to clamp the sun-lobe — exactly its current behaviour. The `volumetricFogEnabled` toggle still disables the whole volumetric layer for users who find any haze motion (from animated density noise) uncomfortable.

---

## 2. Open-questions resolution (from the prior draft's §10)

The prior draft left five questions for sign-off. All five now resolve from shipped reality + the research doc; recorded here for the audit trail:

1. **Scope of slice 11.1** — *moot.* 11.1 shipped, bundled with the 11.2 composite, so the first fog commit already produced a visible feature.
2. **Height fog in the initial run** — *moot.* 11.3 shipped.
3. **Volumetric fog commitment** — **Yes**, ship basic froxel volumetrics in Phase 10 (no temporal — see §1 scope decision). Research projects ~1.2 ms on RX 6600, comfortably inside 2.0 ms.
4. **Workbench improvements (§9)** — **all three prerequisites already exist.** `max_abs_error_max` metric (§9.2, `reference_harness.cpp:116`) and weighted-loss curve fitting (§9.3, `curve_fitter.h` `fitWeighted`) shipped in Workbench 1.17.0 (commit `1cb553b`). The "multi-input 2D grid" (§9.1) was **never a gap**: `sweepRecurse` already builds an N-dimensional Cartesian product over every key in `input_sweep`, and shipped cases already use 2–3 keys (`terminal_velocity`, `aerodynamic_drag`). The `(g, cosθ)` phase-function grid is authored by declaring two `input_sweep` keys — no tooling change, no version bump.
5. **Accessibility default** — **distance + height fog stay authored-on under `safeDefaults()`** (disabling them produces a harsh fog-horizon cutoff — visually worse). The new `volumetricFogEnabled` has struct default `true`; `safeDefaults()` sets it `false`. `reduceMotionFog` has struct default `false` and is set `true` by the shipped `safeDefaults()` (it is not a bare struct default).

---

## 3. Remaining slice plan

Slice numbers follow the shipped `CHANGELOG.md` ledger (line 6673: *"non-volumetric fog slices: 11.5 (screen-space god rays) and 11.10 (editor FogPanel). Volumetric slices 11.6 – 11.8 are the heavy-lift"*). Temporal reprojection was never assigned a Phase-10 slice in that ledger; the prior design draft's tentative "11.8 = temporal" is dropped (temporal → Phase 13), and 11.8 is density noise — consistent with the ledger's 11.6–11.8 volumetric grouping. Mist volumes are the one genuinely new slice (11.11).

| Slice | Title | Complexity | Ships |
|-------|-------|------------|-------|
| **11.6** | Volumetric fog foundation | L | Froxel grid + 3 compute passes (inject / scatter / integrate), single directional sun + CSM sampling, Beer-Lambert accumulation, HG phase (literal, pre-Workbench). Extends the shipped composite to sample the 3D texture. No temporal, no noise yet. |
| **11.7** | Workbench-fit Schlick phase | M | Author `phase_schlick_hg` reference case (two-key `input_sweep` over `(g, cosθ)`, weighted loss, `max_abs_error_max ≤ 0.02`) → fit Schlick `k(g)` → exported GLSL replaces the literal HG in the scatter pass. Reference-case authoring only — no Workbench code change. |
| **11.8** | Fog density noise | S/M | 3D Perlin-Worley density modulation in the inject pass for non-uniform haze. Animated via a scroll offset (no temporal-reprojection dependency). |
| **11.11** | Mist / ground-fog volumes | M | Box + sphere density-injection sources with soft-edge falloff, fed into the 11.6 inject pass. Animated density reuses the 11.8 Perlin-Worley field. |
| **11.5** | Screen-space god rays (Mitchell) | M | Radial-blur post-process fallback for Low/Medium presets and when volumetric fog is disabled. (High-preset god rays come *free* from 11.6's shadow-mapped inscattering.) Slice number matches the shipped CHANGELOG ledger. |
| **11.10** | Editor FogPanel | M | Mirror the AudioPanel four-tab pattern (Distance / Height / Volumetric / Debug). |

Implementation order: **11.6 → 11.7 → 11.8 → 11.11 → 11.5 → 11.10.** Each is a self-contained commit with its own tests, matching the Phase 10 audio cadence.

---

## 4. Volumetric froxel architecture (slice 11.6) — the core

### 4.1 Froxel grid

A view-frustum-aligned 3D texture ("froxels" = frustum voxels). Default **160 × 90 × 64** = 921,600 froxels, RGBA16F ≈ 14 MB (research §3). Screen-tile × depth-slice; depth distributed **non-linearly** (exponential mapping) so near-camera froxels are small and far froxels coarse:

```
froxel_z(slice) = near * pow(far / near, (slice + 0.5) / numSlices)   // exponential slice distribution
```

Grid dimensions and the slice-distribution exponent are artist-tuned knobs (research §10), not Workbench-fit — they carry `// TODO: revisit via Formula Workbench once reference data is available` only where measured atmospheric data would apply.

### 4.2 Three compute passes (OpenGL 4.5 compute shaders)

1. **Inject** (`volumetric_inject.comp`) — writes per-froxel `(scattering_rgb, extinction)` into a 3D texture. Base values from the height/distance fog params (reusing the shipped CPU formulas' GLSL form), plus density-noise modulation (11.8) and mist-volume contributions (11.11). One thread per froxel.
2. **Scatter** (`volumetric_scatter.comp`) — per froxel, evaluate single-scatter inscattering from the directional sun:
   `L_scatter = scattering * shadow(froxel, sun) * phase(cosθ, g) * sunRadiance + ambientProbe`
   CSM shadow map sampled per froxel (this is the dominant cost — research §7 line 203). `phase` is literal HG in 11.6, swapped for Workbench Schlick in 11.7.
3. **Integrate** (`volumetric_integrate.comp`) — front-to-back ray-march along each froxel column accumulating scattering + transmittance (Beer-Lambert), writing `(rgb = inscatter-so-far, a = transmittance-so-far)` per froxel. One thread per screen tile, marching the 64 slices.

Slice 11.6 **extends** the shipped composite (`screen_quad.frag.glsl` — which today contains only the distance/height/sun path, no 3D-texture sampler) to sample this froxel texture at each opaque pixel's coordinate: `C_out = T * C_scene + S`. When `volumetricFogEnabled` is false, the composite keeps its current per-pixel distance/height path byte-for-byte (equivalence test in §8).

### 4.3 Why compute, why this layout

The Forward+ pipeline currently has no G-buffer; the froxel approach is G-buffer-independent (it only needs the depth buffer + shadow maps, both already produced). It also keeps fog cost decoupled from screen resolution (froxel count is fixed) and from overdraw. References: Wronski SIGGRAPH 2014, Hillaire/Frostbite SIGGRAPH 2015 (research §3 refs).

### 4.4 CPU / GPU placement (slice 11.6, per CLAUDE.md Rule 7)

| Concern | CPU | GPU | Reason |
|---------|-----|-----|--------|
| Inject / scatter / integrate passes | | ✅ | Per-froxel work → GPU compute default. |
| Froxel grid sizing, slice-distribution exponent, uniform/SSBO upload | ✅ | | Per-frame setup / I/O → CPU. |
| Shadow-map + camera matrices feeding the passes | ✅ produce | ✅ consume | Already produced CPU-side for the shadow pass; consumed per-froxel on GPU. |

All three compute passes are GPU-only; the CPU drives them by uploading params + the active mist-volume SSBO each frame. No dual CPU/GPU impl is needed for the passes themselves (they have no reference dataset); the one CPU-spec/GPU-runtime parity pair in this scope is `fogVolumeDensity` (§6.4).

---

## 5. God rays (slice 11.5)

Two paths, by preset (research §4):

- **High preset:** god rays are a **free byproduct** of slice 11.6 — shadow-mapped inscattering through the froxel volume *is* light shafts (the Tabernacle tent-entrance beam). No separate pass.
- **Low / Medium preset (and whenever volumetric fog is disabled):** a screen-space radial-blur pass (Kenny Mitchell, GPU Gems 3 ch. 13) — project the sun to screen space, ray-march N taps (64–128) toward it accumulating an occlusion mask. ~0.3–0.6 ms. This is the cheap bolt-on that gives the visual payoff without the froxel grid.

`assets/shaders/god_rays_radial.frag.glsl` — new post-process pass, gated by preset + a `godRaysEnabled` toggle. Sits in the composite chain alongside fog (before bloom, so shafts bloom correctly).

---

## 6. Mist / ground-fog volumes (slice 11.11) — *new coverage, absent from the prior draft*

Localized, placeable fog volumes (ROADMAP 465): morning mist around the Bronze Laver, dust near the altar.

### 6.1 Data model

```cpp
namespace Vestige
{

enum class FogVolumeShape { Box, Sphere };

struct FogVolume
{
    FogVolumeShape shape       = FogVolumeShape::Box;
    glm::vec3      center      = {0.0f, 0.0f, 0.0f};
    glm::vec3      halfExtents = {1.0f, 1.0f, 1.0f}; // Box: per-axis half-size; Sphere: .x = radius
    glm::vec3      colour      = {0.6f, 0.62f, 0.65f}; // linear-RGB scattering tint
    float          density     = 0.5f;   // added extinction at the volume core
    float          edgeSoftness = 0.2f;  // 0..1 fraction of extent over which density falls to 0
    float          animSpeed   = 0.0f;   // turbulence scroll speed (0 = static)
};

// Pure-function falloff — CPU spec that pins the GLSL inject contribution.
// Returns density multiplier in [0,1] for a world-space sample point.
float fogVolumeDensity(const FogVolume& v, const glm::vec3& worldPos, float time);

}
```

### 6.2 Soft-edge falloff

Box: per-axis `smoothstep` from the inner core to the outer extent, multiplied across axes. Sphere: radial `smoothstep(radius, radius*(1-edgeSoftness), |p - center|)`. Animated density adds a slow Perlin-Worley modulation (the same 3D noise field as slice 11.8), scrolled by `time * animSpeed`. These are canonical forms — **no coefficients fitted** (matches the shipped distance-fog primitives' approach).

### 6.3 Integration

`fogVolumeDensity` is evaluated per froxel in the **inject** pass (4.2 step 1) — each volume adds to that froxel's extinction/scattering. CPU-side `fogVolumeDensity` mirrors the GLSL byte-for-byte (same discipline as the shipped `composeFog`), so a CPU unit test is the spec for the GPU path. Volumes are uploaded as a small SSBO (cap ~32 active volumes; over-cap volumes are dropped with a one-line `log()` per CLAUDE.md "no silent caps").

### 6.4 CPU / GPU placement (per CLAUDE.md Rule 7)

| Concern | CPU | GPU | Reason |
|---------|-----|-----|--------|
| Volume list management, culling vs frustum | ✅ | | Branching / sparse / I/O — CPU heuristic default. |
| Per-froxel density evaluation | | ✅ | Per-froxel (per-voxel) → GPU default. |
| `fogVolumeDensity` falloff math | ✅ spec | ✅ runtime | Dual impl pinned by a parity test, per Rule 7. |

---

## 7. Phase function (slice 11.7)

A reference-case authoring task — every Workbench capability it needs already ships (§9):

- Fit Schlick `k(g) ≈ a₀ + a₁g + a₂g² + a₃g³` so `p(cosθ,g) ≈ (1-k²) / (4π(1-k·cosθ)²)` matches analytic HG to **max-abs error ≤ 2 %** over `g ∈ [0.1, 0.95]`, `cosθ ∈ [-1, 1]`, forward-scatter weighted 4× (uses the shipped weighted-loss fitter, §9.3).
- The 2D `(g, cosθ)` reference dataset is authored as a `phase_schlick_hg.json` case with **two `input_sweep` keys** — `sweepRecurse` already produces the Cartesian product (no tooling change; shipped cases like `terminal_velocity` already use 3-key sweeps).
- Acceptance uses the shipped `max_abs_error_max` metric (§9.2).
- Export: `engine/renderer/volumetric_phase_schlick.glsl` (generated, checked in), inlined into `volumetric_scatter.comp`, regenerated via the reference case.

---

## 8. Performance targets & test strategy

Budgets (research §7), enforced by a benchmark harness:

| Layer | Budget | Technique |
|-------|--------|-----------|
| Distance + height (shipped) | < 0.1 ms | single `exp` + divide per pixel |
| Sun inscatter (shipped) | < 0.1 ms | single `pow` per pixel |
| God rays, screen-space (Low/Med) | 0.3–0.6 ms | 64–128 taps |
| Volumetric, 160×90×64, no temporal (High) | **~1.2 ms** | 3 compute dispatches, Schlick phase, CSM per froxel |
| **Stack total, High preset** | **~1.4 ms** | inside 2.0 ms budget |

Tests:
- **11.6** — benchmark harness (`tests/test_fog_benchmark.cpp`, `VESTIGE_BENCHMARK_ONLY=1`); correctness smoke (offscreen render vs reference image); **"volumetric off" equivalence** — output must match the shipped height/distance path byte-for-byte when `volumetricFogEnabled=false`.
- **11.7** — `phase_schlick_hg` reference-case regression: must converge, `max_abs_error ≤ 0.02`.
- **11.8 / 11.11** — pure-function `fogVolumeDensity` / noise unit tests (falloff knees, soft-edge monotonicity, static-vs-animated, over-cap drop) + GLSL parity.
- **11.5** — screen-space god-ray smoke + "god rays off" equivalence.

---

## 9. Workbench improvement status (was §9 in the prior draft)

| Gap | Status |
|-----|--------|
| §9.1 — multi-input 2D reference cases | ✅ **Never a gap** — `sweepRecurse` already builds N-dimensional Cartesian products over multi-key `input_sweep`; shipped cases use 2–3 keys. |
| §9.2 — `max_abs_error_max` metric | ✅ **Shipped** in Workbench 1.17.0 (`reference_harness.cpp:116`, commit `1cb553b`). |
| §9.3 — weighted-loss fitting | ✅ **Shipped** in Workbench 1.17.0 (`curve_fitter.h` `fitWeighted` overload, commit `1cb553b`). |

---

## 10. Accessibility extension (slice 11.6 / 11.9-delta)

Add to `PostProcessAccessibilitySettings` (the shipped struct):

```cpp
bool volumetricFogEnabled = true;  // independent of fogEnabled; safeDefaults() -> false
```

`safeDefaults()` sets it `false`. The shipped `applyFogAccessibilitySettings` gains one line gating the volumetric layer; distance + height fog behaviour is unchanged (they stay authored-on). `reduceMotionFog` (set `true` by `safeDefaults()`) continues to clamp the sun-lobe; with no temporal reprojection in Phase 10 it has no froxel-shimmer to suppress. **Note:** the shipped `reduceMotionFog` header comment still advertises "disables temporal reprojection in volumetric fog (when that feature ships)" — since temporal is now deferred to Phase 13, that clause should be updated when 11.6 lands so the flag's Phase-10 role reads as sun-lobe clamp only. WCAG 2.2 SC 2.3.1 / 2.3.3, Xbox AG 117/118 (research §6).

---

## 11. Cold-eyes loop log

Per CLAUDE.md Rule 14 — loop until a cold pass returns zero verified actionable findings; loops 2+ run cold with no prior-loop briefing.

- **Loop 1** (fresh reviewer): 3 findings. 1 HIGH (wrong commit hash `02c0414` for shipped Workbench features → corrected to `1cb553b`), 2 LOW (per-slice test counts didn't reconcile to the 29-test `Fog` suite → cited suite totals; an `expm1` claim that was a *verified non-issue* — CPU does use `std::expm1`, GLSL uses the `1-exp` equivalent — dropped explicitly). All verified against disk before fixing.
- **Loop 2** (fresh reviewer, cold): 8 findings, none a repeat of Loop 1 (Loop-1 fixes held). 1 CRITICAL: §9.1 "input_grid" was a fictional gap — `sweepRecurse` already does N-dimensional sweeps, so slice 11.7 needs no tooling/version bump (corrected). 2 HIGH: §4.2 claimed the shipped composite already samples the froxel texture (reworded to future-tense "extends"); god-rays slice renumbered 11.12→11.5 to match the shipped CHANGELOG ledger. 2 MEDIUM: `reduceMotionFog` default wording (struct default is `false`, set `true` by `safeDefaults()`); lingering header-comment temporal note flagged for update at 11.6. 2 LOW: 11.8 reuse clarified; noise basis standardised to Perlin-Worley. 1 INFO: added §4.4 CPU/GPU placement for the froxel core. All verified against disk before fixing.
- **Loop 3** (fresh reviewer, cold): **CLEAN — zero actionable findings.** All load-bearing claims re-verified against disk (shipped symbols, `sweepRecurse` N-dim sweeps, `reference_harness.cpp:116`, commit `1cb553b`, slice numbering vs CHANGELOG ledger, scope decision vs research §3/§7 + ROADMAP 1657, budget arithmetic). 1 INFO (Phase-13-vs-Phase-15 naming reconciled by ROADMAP 1657 — doc lands on the correct phase) left for follow-up per Rule 14. **Convergence reached → signed off.**
