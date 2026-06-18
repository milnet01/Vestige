# Phase 10 — Fog, Mist & Volumetric Lighting (Design Doc)

**Status:** ✅ Signed off for implementation (2026-06-18). Cold-eyes looped to clean (3 loops; sign-off delegated per session standing instruction). See the Cold-eyes loop log at the foot of this doc.
**Amended 2026-06-18:** slice 11.6 has since shipped (see §0; §4 is now the design-of-record for it); slice 11.7 was evaluated and **dropped** (§7); the slice 11.8 design was added (§11). The amendment was re-reviewed cold — see the loop log (§12).
**Research:** See `docs/phases/phase_10_fog_research.md` for citations and derivations.
**Scope:** Deferred-pipeline fog for the Vestige engine. The non-volumetric layers (distance fog, exponential height fog, sun-inscatter lobe, composite shader integration, accessibility transform) **have shipped**. **Slice 11.6 (froxel-based volumetric fog, single-scatter, no temporal) has since shipped** (see §0; §4 documents that shipped architecture). The remaining volumetric work this doc specifies is **density noise (11.8, §11)**, **god rays (11.5)**, **placeable mist / ground-fog volumes (11.11)**, and the editor panel (11.10).

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

**Update 2026-06-18 — slice 11.6 (volumetric froxel foundation) has also shipped**, end-to-end and user-visible: `engine/renderer/volumetric_fog.{h,cpp}` + `volumetric_fog_pass.{h,cpp}`, the three compute passes (`assets/shaders/volumetric_{inject,scatter,integrate}.comp.glsl`), the composite's froxel sampler (`screen_quad.frag.glsl`, unit 17), and `tests/test_volumetric_fog.cpp` + `test_volumetric_fog_gpu.cpp` + `test_fog_benchmark.cpp`. §4 below documents that shipped architecture (kept as the design-of-record); the *remaining* volumetric work is slices 11.8 (density noise, §11), 11.11, 11.5, and 11.10.

The earlier draft of this doc specified only slice 11.1; that draft is superseded. **No code in §4 changes the shipped non-volumetric layers** — the volumetric work is additive.

---

## 1. Goals (remaining work)

- Ship the three remaining ROADMAP bullets: **volumetric fog**, **volumetric god rays**, **mist / ground fog**.
- Stay inside the **2.0 ms / frame** GPU budget on RX 6600 at 1080p for the *full* fog stack at the High preset (research §7) — measured, not assumed (hard 60 FPS floor).
- Layer cleanly on the shipped composite: the volumetric pass produces a froxel-integrated `(inscatter, transmittance)` 3D texture that the existing `screen_quad.frag.glsl` composite samples, **replacing** the per-pixel distance/height term when volumetrics are enabled.
- ~~Route the Schlick approximation to Henyey-Greenstein through the Formula Workbench (slice 11.7).~~ **Dropped 2026-06-18 after pre-implementation verification (Rule 13) — see §7.** The scatter pass keeps the exact analytic HG phase. Summary: the Schlick fit cannot meet any useful accuracy bar against HG over the needed anisotropy range, there is no performance pressure to replace HG, and the fit would require a cross-formula Workbench capability that does not ship (now tracked in §9).
- Extend the shipped accessibility transform with a `volumetricFogEnabled` master toggle (distance/height fog stay authored-on under the safe preset; only the moving volumetric layer is disabled).

### Scope decision — Phase 10 ships *basic* volumetrics; the froxel + temporal *upgrade* is Phase 13

This is the load-bearing scope call and it resolves a genuine self-contradiction in the source docs, so it is stated explicitly:

- The research doc's own Phase-10 recommendation (research §3 line 99, §7 line 206) is a **single 160×90×64 froxel grid, three compute dispatches (inject / scatter / integrate), one directional sun light with CSM shadow sampling per froxel, Schlick phase, and *no temporal reprojection*.**
- ROADMAP line 1659 confirms the boundary: *"Basic god rays and volumetric fog land in Phase 10 … this Phase 13 item covers the froxel-volume + temporal-reprojection rendering upgrade."*
- The Phase-10 ROADMAP bullet's sub-bullets list temporal reprojection and multi-light, but those contradict both the research recommendation and the Phase-13 note. **We follow the research + Phase-13 boundary:** temporal reprojection, multi-light scattering, and higher-res grids are **deferred to Phase 13**. Phase 10 = single-scatter sun-only froxel fog, no temporal.

Consequence for accessibility: with no temporal reprojection in Phase 10, the volumetric layer has no inter-frame "background movement" shimmer, so `reduceMotionFog` (already shipped) only needs to clamp the sun-lobe — exactly its current behaviour. The `volumetricFogEnabled` toggle still disables the whole volumetric layer for users who find any haze motion (from animated density noise) uncomfortable.

---

## 2. Open-questions resolution (from the prior draft's §10)

The prior draft left five questions for sign-off. All five now resolve from shipped reality + the research doc; recorded here for the audit trail:

1. **Scope of slice 11.1** — *moot.* 11.1 shipped, bundled with the 11.2 composite, so the first fog commit already produced a visible feature.
2. **Height fog in the initial run** — *moot.* 11.3 shipped.
3. **Volumetric fog commitment** — **Yes**, ship basic froxel volumetrics in Phase 10 (no temporal — see §1 scope decision). Research projects ~1.2 ms on RX 6600, comfortably inside 2.0 ms.
4. **Workbench improvements (§9)** — the three prerequisites this question asked about (max-abs-error metric, weighted loss, multi-axis sweeps) all exist in Workbench 1.17.0. **Update 2026-06-18:** this readiness check missed a *different* prerequisite — sourcing fit targets from a separate reference formula (Schlick fitted against HG). The harness has no such path (§7 reason 3), and slice 11.7 was dropped pre-implementation for independent reasons anyway (infeasible accuracy bar + no perf need — §7). The genuine cross-formula gap is now tracked in §9.
5. **Accessibility default** — **distance + height fog stay authored-on under `safeDefaults()`** (disabling them produces a harsh fog-horizon cutoff — visually worse). The new `volumetricFogEnabled` has struct default `true`; `safeDefaults()` sets it `false`. `reduceMotionFog` has struct default `false` and is set `true` by the shipped `safeDefaults()` (it is not a bare struct default).

---

## 3. Remaining slice plan

Slice numbers follow the shipped `CHANGELOG.md` ledger (line 6732: *"non-volumetric fog slices: 11.5 (screen-space god rays) and 11.10 (editor FogPanel). Volumetric slices 11.6 – 11.8 are the heavy-lift"*). Temporal reprojection was never assigned a Phase-10 slice in that ledger; the prior design draft's tentative "11.8 = temporal" is dropped (temporal → Phase 13), and 11.8 is density noise — consistent with the ledger's 11.6–11.8 volumetric grouping. Mist volumes are the one genuinely new slice (11.11).

| Slice | Title | Complexity | Ships |
|-------|-------|------------|-------|
| **11.6** | Volumetric fog foundation | L | ✅ **SHIPPED 2026-06-18.** Froxel grid + 3 compute passes (inject / scatter / integrate), single directional sun + CSM sampling, Beer-Lambert accumulation, HG phase (literal — Schlick swap dropped, §7). Composite samples the 3D texture (unit 17). No temporal, no noise yet. |
| ~~**11.7**~~ | ~~Workbench-fit Schlick phase~~ **— DROPPED** | — | **Evaluated and dropped 2026-06-18 (pre-implementation, Rule 13).** Fitting Schlick to HG cannot hit a useful error bound over `g∈[0.1,0.95]` (HG ≈62 at `g=0.95,cosθ=1`; best fit error ≈67 there, and the realistic `g≤0.6` range still only reaches ≈0.03 abs / ≈9% at the forward glow), there is no perf need (HG `pow(x,1.5)`=`x·√x` is cheap; the fog stack is far inside budget), and the fit needs a cross-formula Workbench capability that does not ship (§9). Scatter keeps the exact HG. **See §7.** |
| **11.8** | Fog density noise | S/M | Procedural **3-octave integer-hash 3D value-noise FBM** density modulation in the inject pass for non-uniform, drifting haze. Animated via domain scroll (no temporal-reprojection dependency). **See §11 for the full design.** |
| **11.11** | Mist / ground-fog volumes | M | Box + sphere density-injection sources with soft-edge falloff, fed into the 11.6 inject pass. Animated density reuses the 11.8 value-noise-FBM field. |
| **11.5** | Screen-space god rays (Mitchell) | M | Radial-blur post-process fallback for Low/Medium presets and when volumetric fog is disabled. (High-preset god rays come *free* from 11.6's shadow-mapped inscattering.) Slice number matches the shipped CHANGELOG ledger. |
| **11.10** | Editor FogPanel | M | Mirror the AudioPanel four-tab pattern (Distance / Height / Volumetric / Debug). |

Implementation order: **11.6 → ~~11.7~~ → 11.8 → 11.11 → 11.5 → 11.10** (11.7 dropped — §7). Each is a self-contained commit with its own tests, matching the Phase 10 audio cadence.

---

## 4. Volumetric froxel architecture (slice 11.6) — the core ✅ SHIPPED 2026-06-18

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
   CSM shadow map sampled per froxel (this is the dominant cost — research §7 line 203). `phase` is the literal HG closed form (kept — the Schlick fit was evaluated and dropped, §7).
3. **Integrate** (`volumetric_integrate.comp`) — front-to-back ray-march along each froxel column accumulating scattering + transmittance (Beer-Lambert), writing `(rgb = inscatter-so-far, a = transmittance-so-far)` per froxel. One thread per screen tile, marching the 64 slices.

Slice 11.6 **extended** the shipped composite (`screen_quad.frag.glsl`, now carrying `u_volumetricEnabled` + a `sampler3D` on unit 17) to sample this froxel texture at each opaque pixel's coordinate: `C_out = T * C_scene + S`. When `volumetricFogEnabled` is false, the composite keeps its per-pixel distance/height path byte-for-byte (equivalence test in §8).

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

Localized, placeable fog volumes (ROADMAP 465–466): morning mist around the Bronze Laver, dust near the altar.

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

Box: per-axis `smoothstep` from the inner core to the outer extent, multiplied across axes. Sphere: radial `smoothstep(radius, radius*(1-edgeSoftness), |p - center|)`. Animated density adds a slow value-noise-FBM modulation (the same 3D noise field as slice 11.8, §11), scrolled by `time * animSpeed`. These are canonical forms — **no coefficients fitted** (matches the shipped distance-fog primitives' approach).

### 6.3 Integration

`fogVolumeDensity` is evaluated per froxel in the **inject** pass (4.2 step 1) — each volume adds to that froxel's extinction/scattering. CPU-side `fogVolumeDensity` mirrors the GLSL byte-for-byte (same discipline as the shipped `composeFog`), so a CPU unit test is the spec for the GPU path. Volumes are uploaded as a small SSBO (cap ~32 active volumes; over-cap volumes are dropped with a one-line `log()` per CLAUDE.md "no silent caps").

### 6.4 CPU / GPU placement (per CLAUDE.md Rule 7)

| Concern | CPU | GPU | Reason |
|---------|-----|-----|--------|
| Volume list management, culling vs frustum | ✅ | | Branching / sparse / I/O — CPU heuristic default. |
| Per-froxel density evaluation | | ✅ | Per-froxel (per-voxel) → GPU default. |
| `fogVolumeDensity` falloff math | ✅ spec | ✅ runtime | Dual impl pinned by a parity test, per Rule 7. |

---

## 7. Phase function (slice 11.7) — evaluated and DROPPED (2026-06-18)

**Decision: keep the exact analytic Henyey-Greenstein phase; do not fit a Schlick approximation.** This was verified *before* implementation (Rule 13). The original plan — fit Schlick `k(g) = a₀+a₁g+a₂g²+a₃g³` so `p(cosθ,g) ≈ (1-k²)/(4π(1-k·cosθ)²)` matches HG to ≤2 % over `g∈[0.1,0.95]`, as a Workbench reference case — does not survive contact with the numbers. Three independent reasons, any one sufficient:

1. **The accuracy target is unreachable.** HG is sharply peaked at strong forward scatter — at `g=0.95, cosθ=1` it evaluates to ≈62 (not ≈1). The stated bound `max_abs_error_max ≤ 0.02` is the harness's *absolute* metric. An offline weighted least-squares fit of the cubic `k(g)` over `g∈[0.1,0.95]` yields a worst-case **absolute error ≈67** — the rational form simply cannot reproduce the peak height. Even restricted to the realistic fog range `g≤0.6`, the best fit reaches only **≈0.032 abs (≈9 % relative)**, and that worst error lands on the bright forward glow where the eye goes. The "≤2 %" wording also conflated relative-% with the absolute `0.02` — against a function peaking at 62 those are wildly different units. No fit meets a useful bar.
2. **There is no performance pressure.** The fog stack already runs far inside the 2.0 ms budget (slice 11.6 benchmark green on the RX 6600). HG's only "expensive" op is `pow(denom, 1.5)` = `denom·sqrt(denom)` — one sqrt + one mul, cheap on RDNA2. Schlick's `denom²` saves a single sqrt per froxel, immeasurable against the per-froxel CSM shadow sample that dominates the scatter pass (research §7).
3. **The fit needs a Workbench capability that does not ship.** Fitting Schlick to a *different* reference formula (HG) is a cross-formula approximation. The reference harness offers only (a) self-recovery — `synthesizeDataset` evaluates the *same* formula being fitted at its canonical coefficients (`reference_harness.cpp:220`) — and (b) evaluation-pinning via `evaluation_points` (no fit). Neither sources fit targets from a different function. §9's "never a gap" line covered multi-axis sweeps, not cross-formula targets; that genuine gap is now recorded in §9 and on the ROADMAP.

**Net:** swapping HG for Schlick would cost visible forward-scatter accuracy, save no measurable time, and need new tooling to hit a bar it still couldn't reach. The scatter pass keeps the exact `henyeyGreenstein` GLSL, pinned to CPU `henyeyGreensteinPhase` by the existing parity test (`tests/test_volumetric_fog_gpu.cpp`). The freed slice effort goes to slice 11.8 (density noise, §11), a real visible-quality gain.

---

## 8. Performance targets & test strategy

Budgets (research §7), enforced by a benchmark harness:

| Layer | Budget | Technique |
|-------|--------|-----------|
| Distance + height (shipped) | < 0.1 ms | single `exp` + divide per pixel |
| Sun inscatter (shipped) | < 0.1 ms | single `pow` per pixel |
| God rays, screen-space (Low/Med) | 0.3–0.6 ms | 64–128 taps |
| Volumetric, 160×90×64, no temporal (High) | **~1.2 ms** | 3 compute dispatches, HG phase, CSM per froxel |
| **Stack total, High preset** | **~1.4 ms** | inside 2.0 ms budget |

Tests:
- **11.6** — ✅ *shipped:* benchmark harness (`tests/test_fog_benchmark.cpp`, Release-gated); CPU-spec + GPU-parity tests (`tests/test_volumetric_fog.cpp`, `tests/test_volumetric_fog_gpu.cpp`); **"volumetric off" equivalence** holds byte-for-byte when `volumetricFogEnabled=false`.
- **11.7** — *dropped (§7).* The scatter pass's existing GLSL `henyeyGreenstein` stays pinned to CPU `henyeyGreensteinPhase` by the shipped parity test in `tests/test_volumetric_fog_gpu.cpp` — no new test.
- **11.8** — CPU unit tests for `fogDensityNoise` (range `m∈[0,2]`, determinism, animation changes the value, `strength=0 → m≡1`); GPU parity (extract GLSL `fogDensityNoise` + hash helpers via `extractGlslFunction`, single-pixel harness vs CPU — integer-hash layer bit-exact, final value within `1e-4 + 1e-3·|cpu|`); `noiseEnabled=false` byte-identical to the uniform medium (full-dispatch readback); benchmark re-run with noise on stays ≤2 ms (60 FPS gate). Full design + test contract in §11.
- **11.11** — pure-function `fogVolumeDensity` unit tests (falloff knees, soft-edge monotonicity, static-vs-animated, over-cap drop) + GLSL parity.
- **11.5** — screen-space god-ray smoke + "god rays off" equivalence.

---

## 9. Workbench improvement status (was §9 in the prior draft)

| Gap | Status |
|-----|--------|
| §9.1 — multi-input 2D reference cases | ✅ **Never a gap** — `sweepRecurse` already builds N-dimensional Cartesian products over multi-key `input_sweep`; shipped cases use 2–3 keys. |
| §9.2 — `max_abs_error_max` metric | ✅ **Shipped** in Workbench 1.17.0 (`reference_harness.cpp:116`, commit `1cb553b`). |
| §9.3 — weighted-loss fitting | ✅ **Shipped** in Workbench 1.17.0 (`curve_fitter.h` `fitWeighted` overload, commit `1cb553b`). |
| §9.4 — **cross-formula fit target** (fit formula A to a *different* reference formula B's curve) | ❌ **Genuine gap, found 2026-06-18.** `synthesizeDataset` (`reference_harness.cpp:220`) only evaluates the formula being fitted (self-recovery), and `evaluation_points` only pins direct evaluation (no fit). Neither sources fit targets from a second reference function. Surfaced by the (now-dropped, §7) Schlick→HG fit. **Logged to ROADMAP** as a future Workbench capability — valuable for approximations where perf *does* matter (the Schlick case had neither perf need nor a reachable bound, so closing the gap was not justified for it). |

---

## 10. Accessibility extension (slice 11.6 / 11.9-delta) — ✅ SHIPPED 2026-06-18

`PostProcessAccessibilitySettings` carries `bool volumetricFogEnabled = true` (`post_process_accessibility.h:104`), with `safeDefaults()` setting it `false` (`post_process_accessibility.cpp`). The volumetric layer is gated **in the renderer**, not inside `applyFogAccessibilitySettings`: `renderer.cpp` computes `volumetricActive = m_postProcessAccessibility.volumetricFogEnabled && m_volumetricFogPass.isInitialized()`, feeding both the froxel dispatch and the `u_volumetricEnabled` composite uniform. `applyFogAccessibilitySettings` operates on the analytic distance/height `FogState` and is unchanged — distance + height fog stay authored-on (disabling them produces a harsh fog-horizon cutoff — visually worse). `reduceMotionFog` (set `true` by `safeDefaults()`) clamps the sun-lobe; with no temporal reprojection in Phase 10 it has no froxel-shimmer to suppress, and its header comment already records that the "disable temporal reprojection" role arrives in Phase 13. **Slice 11.8 adds one branch here** (§11.8): reduce-motion zeroes the noise `windVelocity` so the haze stays static. WCAG 2.2 SC 2.3.1 / 2.3.3, Xbox AG 117/118 (research §6).

---

## 11. Fog density noise (slice 11.8) — full design

Research: `docs/phases/phase_10_fog_research.md` density-noise addendum (Schneider *Nubis* SIGGRAPH 2017; Hillaire/Frostbite 2015–16; Wronski AC4 GDC 2014; Jarzynski & Olano, *Hash Functions for GPU Rendering*, JCGT 9(3) 2020; Gustavson `webgl-noise`; Inigo Quilez fBM).

### 11.1 Goal
Modulate the uniform froxel medium (slice 11.6 writes a constant `(scattering, extinction)`) with a 3D noise field so fog reads as **drifting, non-uniform haze** instead of a flat grey wash — directly addressing the "overcast" look of the flat-field demo. Animated by domain scroll; **no temporal-reprojection dependency** (Phase 10 has none — §1 scope).

### 11.2 Noise basis — procedural integer-hash 3D value-noise FBM (decision)
- **Skip Worley.** Inverted-Worley billowing is a *cloud-silhouette* tool; ground/air haze does not need it. A **3-octave value-noise FBM** (lacunarity 2.0, gain 0.5) is the shortest correct natural field. Worley/clouds are a later feature.
- **Procedural, not a baked 3D texture.** The research's perf case for a 32³ baked texture is real for cloudscapes, but three factors flip it here: **(a) reuse + parity** — the engine already hashes with integer bit-mixing (`cloth_wind_model.cpp:17`; `terrain.cpp:915` is a related value-noise precedent), and an integer-hash value noise is *bit-reproducible* CPU↔GLSL (Jarzynski-Olano; GLSL `uint` is spec-guaranteed 32-bit wrapping = C++ `uint32_t`), so the Rule-7 parity test is tight rather than a "baker parity" problem; **(b) no extra texture unit** — the composite already juggles units 0/9–13/17 and we just fixed a unit-0 clobber, so adding a 3D sampler is a global-state hazard we avoid (the inject pass binds *image* unit 0 only); **(c) headroom** — the inject pass is currently one `imageStore`, and integer-hash value noise has **no transcendentals** (the hash is int add/mul/shift/xor; smoothstep is muls), so 3 octaves × 8-corner trilinear × 920k froxels is sub-0.1 ms ALU on RDNA2. If profiling ever shows otherwise, a baked tiling 3D texture is the documented fallback.
- **Hashing:** integer hash (`lowbias32`-style mixer, `uint` wraparound) — **never `sin`-hashing** (not bit-portable across vendors; the research flags it as structurally non-reproducible). The engine's only existing 3D noise (`snoise`/`curlNoise` in `particle_simulate.comp.glsl:102`) is float-polynomial, GPU-only, and has no CPU mirror, so it cannot meet the bit-exact CPU↔GLSL parity requirement; a new integer-hash value-noise pair is written instead, its hash *construction* mirroring `cloth_wind_model.cpp`'s integer mixer (Rule 3). The 2D in-engine hashes are not reusable for a 3D field.

### 11.3 Data model / API (`engine/renderer/volumetric_fog.{h,cpp}`)
```cpp
struct FogNoiseParams
{
    bool      enabled      = false;             // off until tuned per scene (editor, 11.10)
    float     frequency    = 0.05f;             // cycles per world metre (lower = larger blobs)
    float     strength     = 0.6f;              // 0..1 modulation depth around the mean
    int       octaves      = 3;                 // FBM octaves (clamped 1..5)
    glm::vec3 windVelocity = {0.4f, 0.0f, 0.1f};// world m/s domain scroll (zeroed by reduce-motion)
};

// CPU spec — density multiplier, mean ≈1. Mirrors fogDensityNoise() in
// volumetric_inject.comp.glsl within the parity tolerance (integer-hash layer
// bit-exact). worldPos = froxel-centre world position; time = elapsed seconds.
float fogDensityNoise(const glm::vec3& worldPos, const FogNoiseParams& p, float time);
```
`FogNoiseParams` is carried on `VolumetricFogPass::FrameParams` (`volumetric_fog_pass.h:43`, which already carries the per-frame matrices + sun params) and uploaded by `dispatch()`.

### 11.4 Modulation math
FBM normalised to `n ∈ [0,1]`. Multiplier `m = clamp(1 + strength·(2n−1), 0, 2)`. Applied to **both** scattering and extinction in the inject pass (`sigma *= m`) — physically "more/less medium here," holding the scatter/extinction ratio (single-scatter albedo) constant. Mean `m ≈ 1`, so enabling noise does not change *average* fog density, only its spatial variation. Domain sampled at `worldPos·frequency + windVelocity·time`.

### 11.5 Animation
Domain scroll `+ windVelocity·time` — deterministic, no reprojection. The single low-frequency layer reads as wind-driven drift (not a conveyor belt) for haze; the research's two-divergent-layers upgrade is deferred unless it visibly slides. `time` = engine elapsed seconds via a new `u_elapsed` uniform on the inject pass (the `u_elapsed` compute-shader precedent is `particle_simulate.comp.glsl:73`).

### 11.6 Inject-pass integration (`assets/shaders/volumetric_inject.comp.glsl`)
The inject pass gains froxel→world reconstruction, mirroring the scatter pass's existing code (`volumetric_scatter.comp.glsl`: view-pos reconstruction lines 115–124, `sliceToViewDepth` at line 97, the `worldPos = (u_invView·…)` step at line 73): uniforms `u_invProjection`, `u_invView`, `u_froxelNearFar`, plus `u_elapsed` and noise uniforms (`u_noiseEnabled`, `u_noiseFreq`, `u_noiseStrength`, `u_noiseOctaves`, `u_noiseWind`). Reconstruct froxel-centre view pos (the `sliceToViewDepth` helper is copied per the established compute-shader duplication), `worldPos = (u_invView·vec4(viewPos,1)).xyz`, evaluate `m`, `imageStore(scattering·m, extinction·m)`. **When `u_noiseEnabled` is false the pass writes the uniform medium exactly as today** (byte-for-byte equivalence — §11.9).

### 11.7 CPU / GPU placement (Rule 7)
| Concern | CPU | GPU | Reason |
|---------|-----|-----|--------|
| Per-froxel noise evaluation | | ✅ | Per-voxel → GPU compute. |
| `fogDensityNoise` value-noise math | ✅ spec | ✅ runtime | Dual impl pinned by a parity test; integer hash → tight tolerance. |
| Noise params / wind upload, reduce-motion freeze | ✅ | | Setup / I-O + accessibility branch → CPU. |

### 11.8 Accessibility
Drifting haze is motion (WCAG 2.2 SC 2.3.3; Xbox AG 117). `applyFogAccessibilitySettings` gains one line: when `reduceMotionFog` is true, **zero `windVelocity`** (static noise — still non-uniform, no drift). `volumetricFogEnabled=false` already disables the whole layer. No new accessibility field needed.

### 11.9 Test contract
- **CPU unit tests** (`tests/test_volumetric_fog.cpp` — the froxel CPU-spec home): `m ∈ [0,2]` and `n ∈ [0,1]` across a sample grid; determinism (same args → identical value); animation (different `time` → different value, given non-zero wind); `strength=0 ⇒ m ≡ 1`; more octaves add detail without leaving range.
- **GPU parity** (`tests/test_volumetric_fog_gpu.cpp`): extract GLSL `fogDensityNoise` + its hash/`valueNoise3` helpers via `extractGlslFunction`, run on the single-pixel `ShaderProgram` harness, compare to CPU `fogDensityNoise` — integer-hash conversion bit-exact, final interpolated value within `1e-4 + 1e-3·|cpu|` (the shipped HG-parity tolerance).
- **Equivalence:** `noiseEnabled=false` ⇒ inject output byte-identical to the pre-11.8 uniform medium (full-dispatch readback).
- **Benchmark:** the shipped `tests/test_fog_benchmark.cpp` full-dispatch budget (≤2 ms median, Release-gated) re-run with noise enabled — must stay green (60 FPS gate).

### 11.10 Performance
3-octave integer-hash value noise, no transcendentals: ~24 hashes + trilinear blends per froxel, ALU-only — projected sub-0.1 ms across 920k froxels on the RX 6600, **measured by the benchmark, not assumed.**

---

## 12. Cold-eyes loop log

Per CLAUDE.md Rule 14 — loop until a cold pass returns zero verified actionable findings; loops 2+ run cold with no prior-loop briefing.

- **Loop 1** (fresh reviewer): 3 findings. 1 HIGH (wrong commit hash `02c0414` for shipped Workbench features → corrected to `1cb553b`), 2 LOW (per-slice test counts didn't reconcile to the 29-test `Fog` suite → cited suite totals; an `expm1` claim that was a *verified non-issue* — CPU does use `std::expm1`, GLSL uses the `1-exp` equivalent — dropped explicitly). All verified against disk before fixing.
- **Loop 2** (fresh reviewer, cold): 8 findings, none a repeat of Loop 1 (Loop-1 fixes held). 1 CRITICAL: §9.1 "input_grid" was a fictional gap — `sweepRecurse` already does N-dimensional sweeps, so slice 11.7 needs no tooling/version bump (corrected). 2 HIGH: §4.2 claimed the shipped composite already samples the froxel texture (reworded to future-tense "extends"); god-rays slice renumbered 11.12→11.5 to match the shipped CHANGELOG ledger. 2 MEDIUM: `reduceMotionFog` default wording (struct default is `false`, set `true` by `safeDefaults()`); lingering header-comment temporal note flagged for update at 11.6. 2 LOW: 11.8 reuse clarified; noise basis standardised to Perlin-Worley *(superseded 2026-06-18 — §11.2 selects a value-noise FBM and drops Worley as a cloud-silhouette-only tool)*. 1 INFO: added §4.4 CPU/GPU placement for the froxel core. All verified against disk before fixing.
- **Loop 3** (fresh reviewer, cold): **CLEAN — zero actionable findings.** All load-bearing claims re-verified against disk (shipped symbols, `sweepRecurse` N-dim sweeps, `reference_harness.cpp:116`, commit `1cb553b`, slice numbering vs CHANGELOG ledger, scope decision vs research §3/§7 + ROADMAP 1659, budget arithmetic). 1 INFO (Phase-13-vs-Phase-15 naming reconciled by ROADMAP 1659 — doc lands on the correct phase) left for follow-up per Rule 14. **Convergence reached → signed off.**

### Amendment 2026-06-18 (drop slice 11.7 + add slice 11.8 design) — cold-eyes loops

Per Rule 14 the amendment was re-reviewed cold; loops 2+ ran with no prior-loop briefing.

- **Loop 1** (fresh reviewer): 3 HIGH (stale "swapped for Schlick in 11.7" in §4.2; "Schlick phase" left in the §8 perf table; ROADMAP not yet updated to match the doc's "dropped"/"logged" claims), 3 MEDIUM (Perlin-Worley vs value-noise contradiction across §3/§6.2/log; "first 3D use" false — `snoise` exists in `particle_simulate.comp.glsl`; wrong CHANGELOG ledger line 6673→6732), 2 LOW (imprecise scatter line refs; closer `u_elapsed` precedent at `particle_simulate.comp.glsl:73`). All verified against disk and fixed; ROADMAP edited (11.7 dropped in the progress note, new **FW W9** cross-formula-gap item, value-noise basis).
- **Loop 2** (fresh reviewer, cold): caught that the doc still framed **slice 11.6 as future work** though it shipped 2026-06-18 (§4.2 "no 3D-texture sampler" was false; §0/§1/§3/§4/§8 stale framing), plus MEDIUM (CPU noise tests routed to `test_fog.cpp` instead of the froxel home `test_volumetric_fog.cpp`; `terrain.cpp:915` is value-noise, not a pure integer mixer) and LOW (ROADMAP boundary note 1657→1659; ROADMAP feature bullet still "Perlin/Worley"). One reported HIGH (`FrameParams` "does not exist") was a **false finding** — the reviewer read `volumetric_fog.h`; `FrameParams` is in `volumetric_fog_pass.h:43` (verified on disk). All real findings fixed: re-baselined §0/§1/§3/§4/§8/§10 to shipped reality, corrected the test file + citations, updated the ROADMAP bullet, and fixed the stale scatter-shader header comment (11.7 Schlick → dropped).
- **Loop 3** (fresh reviewer, cold): **no structural / mechanical / architectural defects — only verified polish.** 1 MEDIUM (§10 described the accessibility gate as a future `applyFogAccessibilitySettings` line, but it shipped as a renderer-level gate, and the `reduceMotionFog` comment was already updated), 3 LOW (scatter line refs drifted again 94→97 / 70→73 / 112-120→115-124; §12 historical "ROADMAP 1657" cite; §6 "ROADMAP 465"→465-466), 2 INFO (§11 subsection numbers shadow slice numbers — readability only; a stale `post_process_accessibility.h` "awaiting consumer" comment whose consumer shipped in B2). All polish items fixed, including the stale code comment. Per the session standing instruction (converge once only verified polish remains and no structural fixes are outstanding), **convergence reached** — the doc matches shipped reality and the slice 11.8 design is implementation-ready.
