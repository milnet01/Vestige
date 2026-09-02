# Phase 10 — Fog, Mist & Volumetric Lighting (Design Doc)

**Status:** ✅ Signed off for implementation (2026-06-18). Cold-eyes looped to clean (3 loops; sign-off delegated per session standing instruction). See the Cold-eyes loop log at the foot of this doc.
**Amended 2026-06-18:** slice 11.6 has since shipped (see §0; §4 is now the design-of-record for it); slice 11.7 was evaluated and **dropped** (§7); the slice 11.8 design was added (§11). The amendment was re-reviewed cold — see the loop log (§12).
**Research:** See `docs/phases/phase_10_fog_research.md` for citations and derivations.
**Scope:** Deferred-pipeline fog for the Vestige engine. The non-volumetric layers (distance fog, exponential height fog, sun-inscatter lobe, composite shader integration, accessibility transform) **have shipped**. **Slice 11.6 (froxel-based volumetric fog, single-scatter, no temporal) has since shipped** (see §0; §4 documents that shipped architecture). **Every slice this doc specifies has now shipped** — density noise (11.8, §11), god rays (11.5, §5), placeable mist / ground-fog volumes (11.11, §6) and the editor FogPanel (11.10, §12) all landed after the 2026-06-18 amendment. §§4–6 and §§11–12 are kept as the design-of-record for shipped code, not as work still to do. *(Corrected 2026-08-21, 3D_E-0616: this listed all four as remaining while §5 had read "✅ SHIPPED" since 2026-06-18.)*

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

**Update 2026-06-18 — slice 11.6 (volumetric froxel foundation) has also shipped**, end-to-end and user-visible: `engine/renderer/volumetric_fog.{h,cpp}` + `volumetric_fog_pass.{h,cpp}`, the three compute passes (`assets/shaders/volumetric_{inject,scatter,integrate}.comp.glsl`), the composite's froxel sampler (`screen_quad.frag.glsl`, unit 17), and `tests/test_volumetric_fog.cpp` + `test_volumetric_fog_gpu.cpp` + `test_fog_benchmark.cpp`. §4 below documents that shipped architecture (kept as the design-of-record); the volumetric work that remained *at that date* — slices 11.8 (density noise, §11), 11.11, 11.5 and 11.10 — has **since shipped in full** (see §Scope above).

The earlier draft of this doc specified only slice 11.1; that draft is superseded. **No code in §4 changes the shipped non-volumetric layers** — the volumetric work is additive.

---

## 1. Goals — ✅ all met (record, not a queue)

- ✅ **Shipped** — the three ROADMAP bullets this doc was written for: **volumetric fog**, **volumetric god rays**, **mist / ground fog**. *(Corrected 2026-08-21, 3D_E-0616: this section read "Goals (remaining work)" and listed all three as unbuilt, contradicting §Scope.)*
- Stay inside the **2.0 ms / frame** GPU budget on RX 6600 at 1080p for the *full* fog stack at the High preset (research §7) — measured, not assumed (hard 60 FPS floor).
- Layer cleanly on the shipped composite: the volumetric pass produces a froxel-integrated `(inscatter, transmittance)` 3D texture that the existing `screen_quad.frag.glsl` composite samples, **replacing** the per-pixel distance/height term when volumetrics are enabled.
- ~~Route the Schlick approximation to Henyey-Greenstein through the Formula Workbench (slice 11.7).~~ **Dropped 2026-06-18 after pre-implementation verification (Rule 13) — see §7.** The scatter pass keeps the exact analytic HG phase. Summary: the Schlick fit cannot meet any useful accuracy bar against HG over the needed anisotropy range, there is no performance pressure to replace HG, and the fit would require a cross-formula Workbench capability that does not ship (now tracked in §9).
- Carry a `volumetricFogEnabled` master toggle on `PostProcessAccessibilitySettings`, **gated in the renderer** rather than inside `applyFogAccessibilitySettings` (§10) — distance/height fog stay authored-on under the safe preset; only the moving volumetric layer is disabled.

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
| **11.8** ✅ | Fog density noise | S/M | Procedural **3-octave integer-hash 3D value-noise FBM** density modulation in the inject pass for non-uniform, drifting haze. Animated via domain scroll (no temporal-reprojection dependency). **See §11 for the full design.** |
| **11.11** ✅ | Mist / ground-fog volumes | M | Box + sphere density-injection sources with soft-edge falloff, fed into the 11.6 inject pass. Animated density reuses the 11.8 value-noise-FBM field. |
| **11.5** ✅ | Screen-space god rays (Mitchell) | M | Radial-blur post-process fallback for Low/Medium presets and when volumetric fog is disabled. (High-preset god rays come *free* from 11.6's shadow-mapped inscattering.) Slice number matches the shipped CHANGELOG ledger. |
| **11.10** ✅ | Editor FogPanel | M | Mirror the AudioPanel four-tab pattern (Distance / Height / Volumetric / Debug). |

Implementation order **as built** — every slice below has shipped: **11.6 → ~~11.7~~ → 11.8 → 11.11 → 11.5 → 11.10** (11.7 dropped — §7). Each landed as a self-contained commit with its own tests, matching the Phase 10 audio cadence. This is a record of what was done, not a queue to work through.

---

## 4. Volumetric froxel architecture (slice 11.6) — the core ✅ SHIPPED 2026-06-18

### 4.1 Froxel grid

A view-frustum-aligned 3D texture ("froxels" = frustum voxels). Default **160 × 90 × 64** = 921,600 froxels. The core allocates **two** RGBA16F volumes — the scattering/extinction volume and the integrated result — at 8 B/froxel each, so ≈ 7.4 MB apiece and **≈ 14 MB together** (research §3). *(Corrected 2026-08-21, 3D_E-0616: this attached the 14 MB figure to a single texture, which is 7.4 MB.)* Screen-tile × depth-slice; depth distributed **non-linearly** (exponential mapping) so near-camera froxels are small and far froxels coarse:

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

## 5. God rays (slice 11.5) ✅ SHIPPED 2026-06-18

Two paths, by preset (research §4):

- **High preset / volumetric on:** god rays are a **free byproduct** of slice 11.6 — shadow-mapped inscattering through the froxel volume *is* light shafts (the Tabernacle tent-entrance beam). No separate pass.
- **Volumetric off (Low / Medium preset, or fog toggled off):** a screen-space radial-blur pass (Kenny Mitchell, *GPU Gems 3* ch. 13) — project the sun to screen space, gather N taps toward it accumulating a sky-light buffer. The cheap bolt-on that gives the visual payoff without the froxel grid.

### 5.1 Algorithm (Mitchell radial gather)

For each output pixel `uv`, step toward the sun's screen position `sunUV` in `N` taps, accumulating a *light buffer* `L`:

```
delta = (uv - sunUV) / N * density      // step toward the sun
coord = uv ; illum = 1 ; accum = L(uv)
repeat N-1 times:
    coord -= delta
    accum += L(coord) * illum * weight
    illum *= decay                       // exponential shaft falloff
result = accum * exposure
```

`density` (shaft length, ≈0.9), `decay` (≈0.95), `weight` (per-tap, ≈0.5), `exposure` (≈0.3) and `N` (64) are **provisional look constants** inlined in the shader with a `TODO 11.10 / Formula Workbench` marker — purely aesthetic, no reference data to fit (CLAUDE.md Rule 6), exposed per-scene by the editor panel (11.10). The *GPU Gems 3* originals are the starting point.

### 5.2 Light buffer (folded into the gather — no pre-pass)

The occlusion/light buffer `L(c)` is computed *inside* the gather (one pass, not a separate masking pass): sample the resolved depth at `c` and the pre-bloom HDR scene at `c`; contribute the scene colour only where the pixel is **sky** (reverse-Z depth ≤ a small epsilon — sky clears to the far plane = 0.0 in reverse-Z, matching `contact_shadows.frag.glsl`'s `depth < 0.0001` sky test), else 0. Geometry therefore *occludes* the shafts, which is what makes them crepuscular. The bright sun disk in the sky dominates `L`, so the shafts emanate from it. The depth is **point-sampled** (`texelFetch` / nearest) for the sky test — the gather runs at half-res over a full-res reverse-Z depth buffer, and bilinear-filtering a non-linear reverse-Z depth across silhouettes would mis-classify edge pixels (soft sky halo). Each tap costs two texture samples (depth + scene); 64 taps at half-res stays inside the fog-stack budget (§8).

### 5.3 Sun projection + screen fade (CPU)

The sun is a directional light (a point at infinity). Project its *toward-sun* direction to clip space and test it is in front of the camera:

```
sunDirView = mat3(view) · (−light.direction)      // toward-sun, view space (matches the froxel pass)
clip       = projection · vec4(sunDirView, 0)     // w=0: direction, not position
onScreen   = clip.w > 0                            // sun in front of the camera
sunUV      = clip.xy / clip.w · 0.5 + 0.5
```

A scalar `intensity` fades the effect out as the sun leaves the view — 1 inside the frame, smoothly to 0 over a screen-margin band, and 0 when `clip.w ≤ 0` (sun behind the camera) — so shafts don't pop when the sun crosses the frustum edge. `sunUV` and `intensity` are uploaded as uniforms; the per-pixel gather is GPU. The projection + fade is the only CPU math and is unit-tested directly (no GL needed).

### 5.4 Integration (two draws, before bloom)

- New `assets/shaders/god_rays.frag.glsl` (gather) + a minimal `god_rays_combine.frag.glsl` (additive upsample), both on the shared `screen_quad.vert.glsl`.
- **Pass A — gather:** render half-resolution into a new `m_godRaysFbo` (a half-res float `Framebuffer` → `RGBA16F`, `GL_LINEAR`-filtered by the `Framebuffer` default so the half→full upsample in Pass B is smooth), reading the current pre-bloom HDR scene colour and the resolved depth. Note the handles differ: the *colour* is the post-AA `hdrSourceFbo` (the AA-resolved scene) while the *depth* is `m_resolveDepthFbo` (the pre-AA-resolved depth the contact-shadow / SSAO passes already use). Same resolution and registration, so the sky test lines up; the depth is **point-sampled** for the sky classification.
- **Pass B — combine:** an additive (`GL_ONE, GL_ONE`) full-res draw that adds the half-res `m_godRaysFbo` (linear-upsampled) into the HDR scene FBO.
- **Insertion point — immediately *before* the bloom downsample block.** The composite order is **bloom → auto-exposure → contact shadows → volumetric dispatch → final composite**; bloom reads the HDR scene FBO as its mip-0 source, and auto-exposure blits it for luminance. So to make the shafts bloom *and* feed auto-exposure, both god-ray draws must complete *before* the bloom block — **not** near the contact-shadow / volumetric passes (those run after bloom). Pass A reads the HDR scene FBO that is current at that point: note SMAA/TAA reassign which FBO holds the resolved scene, so the pass must read whichever `hdrSourceFbo` the bloom block is about to read, not a hard-coded handle.
- **Texture units:** Pass A binds the scene + depth at its own draw (the composite re-binds its own units afterward); Pass B binds `m_godRaysFbo`. Use **low/free units** — units 9–13 and 17–23 are spoken for (bloom 9, SSAO 10, contact 11, depth 12, LUT 13, SH-probe grid 17–23, froxel volume 17). Binding scene at 0 + depth at a free low unit for the gather, and `m_godRaysFbo` at a free low unit for the combine, avoids all of them.
- `m_godRaysFbo` is created in `initialize()` and recreated on window resize (immutable storage, same as the bloom mips).

### 5.5 CPU / GPU placement (per CLAUDE.md Rule 7)

| Concern | CPU | GPU | Reason |
|---------|-----|-----|--------|
| Sun screen projection + on-screen fade | ✅ | | One matrix-vector mul + a branch per frame — sparse/decision → CPU; unit-tested directly. |
| Per-pixel radial gather + occlusion test | | ✅ | Per-pixel, N-tap → GPU default. |
| Additive combine into the HDR buffer | | ✅ | Per-pixel blend → GPU. |

### 5.6 Gating

`godRaysEnabled` **and** `!volumetricActive`, both read through the shipped predicates in `volumetric_fog.{h,cpp}`: `isGodRaysActive(godRaysEnabled, volumetricFogEnabled, qualityHeavyPostEnabled, volumetricPassInitialized)`, which delegates to `isVolumetricFogPassActive(volumetricFogEnabled, qualityHeavyPostEnabled, volumetricPassInitialized)` — **three** terms, not two. The froxel path already produces god rays when volumetric fog is on, so the screen-space fallback runs only when volumetric isn't contributing them, avoiding double shafts. The preset→quality mapping is **shipped, not future**: `settings_apply.cpp` sets `heavyPost = false` on Low and Medium, so those tiers run no froxel fog and the fallback engages there. *(Corrected 2026-08-21, 3D_E-0616: this gave the two-term form `volumetricFogEnabled && m_volumetricFogPass.isInitialized()`, which omits the quality term — precisely the defect 3D_E-0617 fixed, where the fallback was suppressed on every tier below High.)* Also skipped when there is no directional light or the sun is behind the camera.

The `godRaysEnabled` flag is plumbed exactly like `volumetricFogEnabled`, which touches **five** sites that must all be updated or the flag silently fails to persist / compare: (1) the `PostProcessAccessibilitySettings` struct field; (2) that struct's hand-written `operator==` (a field-by-field list — omitting it makes two configs differing only in god-rays compare equal, a change-detection bug); (3) `safeDefaults()` in `post_process_accessibility.cpp`, which sets `godRaysEnabled = false` — volumetric is disabled there too, so nothing self-gates, and the screen-space shafts would otherwise engage and sweep as the camera pans past the sun, which is exactly what the safe preset exists to prevent; (4) the persisted `Settings` mirror in `settings.{h,cpp}` (its `operator==`, `to_json`, `from_json`); (5) the wire→renderer transfer in `settings_apply.cpp`. The renderer then reads the flag at the composite gate.

### 5.7 Test contract

- **CPU `godRaysSunScreenInfo()` unit tests** (the only CPU math, GL-free — a pure function in `volumetric_fog.{h,cpp}`): sun dead-ahead → `sunUV ≈ (0.5,0.5)`, `intensity = 1`, visible; sun behind camera (`clip.w ≤ 0`) → not visible, `intensity = 0`; sun at the frame edge → `intensity` in (0,1); sun well off-screen (past the fade margin) → `intensity = 0`; `edgeMargin = 0` → hard cut at the frame boundary.
- **GPU shader smoke** (on the headless GL fixture): `god_rays.frag.glsl` and `god_rays_combine.frag.glsl` compile + link against `screen_quad.vert.glsl` — catches GLSL/uniform regressions in CI.
- **"God rays off" equivalence is structural, not a test:** the whole pass is behind the `godRaysEnabled && !volumetricActive && sun-in-front` gate, so when off it never runs and `hdrSourceFbo` is untouched by construction.

(A spatial behavioural test — "shafts brightest near `sunUV`, occluded frame → 0" — would need a multi-pixel FBO + synthetic depth/scene textures. The *parity* harness in `test_volumetric_fog_gpu.cpp` is still 1×1 scalar-uniform only, but `GodRayPassUnderBudget` (§8) now builds exactly that multi-pixel FBO and those synthetic textures, so the stated blocker no longer applies to the suite as a whole (3D_E-0616). The sky-gating + intensity-gate logic is simple and the CPU projection is the bug-prone part, which *is* unit-tested; a behavioural GPU harness is a possible follow-up.)

### 5.8 Performance

Half-res gather (one quarter the pixels), 64 taps × 2 samples, plus a full-res additive combine. Gated off entirely when volumetric fog is on (the common shipped path), so it adds nothing to the default frame.

**Benchmarked since 3D_E-0616** (`tests/test_fog_benchmark.cpp`, `GodRayPassUnderBudget`). This section previously said the pass had "no separate micro-benchmark" because it has no standalone subsystem class — it lives inline in the renderer composite (§ 5.4). The benchmark reconstructs the shipped pair of draws from the same two fragment shaders and the same screen-quad vertex stage the renderer links, so the absence of a class was never a reason the cost could not be measured. Budget and measured figures: § 8.

---

## 6. Mist / ground-fog volumes (slice 11.11) — ✅ SHIPPED (design-of-record)

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

The spatial falloff returns 1 at the volume core and 0 at its outer extent, via a shared helper `coreFade(x, inner, outer) = 1 − smooth01(inner, outer, x)`, where `smooth01` is the canonical cubic `t·t·(3−2t)` (and degenerates to a hard step when `inner ≥ outer`, so `edgeSoftness = 0` and zero-extent axes stay finite and parity-stable). Box: per-axis `coreFade(|p−center|ₐ, halfExtentsₐ·(1−edgeSoftness), halfExtentsₐ)`, multiplied across axes. Sphere: `coreFade(|p−center|, radius·(1−edgeSoftness), radius)` with `radius = halfExtents.x`.

Animated density (`animSpeed ≠ 0`) multiplies the falloff by a value-noise-FBM turbulence term `fbm3(worldPos·F_turb + (0, time·animSpeed, 0))` ∈ [0,1] — the *same* 3-octave integer-hash field as slice 11.8 (§11), so mist churns and rises (vertical scroll) and reads as wispy rather than a solid blob. `F_turb` (turbulence frequency, provisional **0.15 cyc/m**) and the octave count (3) are **inlined literals in both the CPU and GLSL `fogVolumeDensity`** so the function is self-contained for the parity-test extractor; they are look constants exposed per-scene by the editor panel (slice 11.10), carrying a `TODO 11.10 / Formula Workbench` marker (purely aesthetic — no reference data to fit, per CLAUDE.md Rule 6). All other forms are canonical — **no coefficients fitted** (matches the shipped distance-fog primitives' approach).

### 6.3 Integration

`fogVolumeDensity` is evaluated per froxel in the **inject** pass (4.2 step 1), *after* the 11.8 base-medium noise — each volume adds `density·fogVolumeDensity(...)` to that froxel's extinction and `colour·density·fogVolumeDensity(...)` to its scattering (the volume tints its own inscatter). CPU-side `fogVolumeDensity` mirrors the GLSL structurally (same discipline as the shipped `composeFog`), so a CPU unit test is the spec for the GPU path. **Parity is bit-exact only while a volume is static.** With `animSpeed != 0` the density is multiplied by the same `fbm3` field slice 11.8 uses, so it inherits §11.9's tolerance — integer-hash layer bit-exact, final value within `1e-4 + 1e-3·|cpu|`. A test asserting `EXPECT_FLOAT_EQ` on an animated volume is the failure this sentence exists to prevent (3D_E-0616).

Volumes are uploaded as a `std430` SSBO (binding 1), one element per volume packed into 4×`vec4` (64 B): `centerShape` (xyz center, w = shape as float), `halfExtentsDensity` (xyz extents, w density), `colourEdge` (xyz colour, w edgeSoftness), `animMisc` (x animSpeed, yzw pad). The buffer is allocated once at `MAX_FOG_VOLUMES = 32` capacity and `glNamedBufferSubData`-updated per frame; `u_volumeCount` gates the per-froxel loop. Over-cap volumes are dropped, logged **once when the over-cap count changes** (not per frame — avoids log spam) per CLAUDE.md "no silent caps". When `u_volumeCount == 0` the loop never runs and the inject output is byte-identical to the noise-only path. The turbulence FBM is skipped wherever the spatial falloff is already 0 (froxel outside the volume) — `0·turb = 0`, so the result and CPU parity are unchanged, only the cost is avoided. Reduce-motion accessibility zeroes each volume's `animSpeed` before upload (same rule as the noise drift, §10): the spatial mist still renders, it just stops churning.

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
| Distance fog (shipped) | < 0.05 ms | one `exp` + one mad per pixel |
| Height fog, exponential analytic (shipped) | < 0.1 ms | one `exp` + one divide per pixel |
| Sun inscatter (shipped) | < 0.1 ms | single `pow` per pixel |
| God rays, screen-space — RX 6600 reference, `renderScale` 1.0 | **0.6–1.2 ms** | ½-res gather (¼ the pixels) + full-res combine, 64–128 taps × **2** samples each |
| God rays, screen-space — **Low/Med tier budget** | **1.75 ms** | the same two draws, with every scene-res object at that tier's `renderScale` (3D_E-0624) |
| Volumetric, 160×90×64, no temporal (High) | **~1.2 ms** | 3 compute dispatches, HG phase, CSM per froxel |
| **Stack total, High preset** | **~1.45 ms** | the three always-on layers + volumetric; inside 2.0 ms |
| **Stack total, Low/Med preset** | **≤ 2.0 ms** | the three always-on layers + the 1.75 ms row; no froxel pass at these tiers |

**Why the god-ray row is double research § 7's figure (3D_E-0616).** Research § 7 estimates 0.3–0.6 ms for "64–128 samples × **one tap each**" — the classic Mitchell shape, where a separate pre-pass builds the occlusion buffer and each gather tap then costs one texture sample. Vestige does not ship that shape: § 5.2 deliberately folds the light buffer *into* the gather ("one pass, not a separate masking pass"), so every tap costs **two** samples (depth `texelFetch` + scene fetch), as § 5.8 also states. The imported figure therefore described a different shader than the one that shipped, and doubling it is what makes the row describe this one. The trade is intentional — double the per-tap cost buys the removal of a full-res pre-pass — and it is not a regression to fix.

**The ×2 is one of three shape differences, and the band's real warrant is the measurement.** Research § 7's figure is a *full-resolution* per-frame cost; the shipped pass gathers at **half** res (¼ the pixels, § 5.4 / § 5.8) and adds a full-res combine draw that the research figure does not cost at all. Those two push in the opposite direction to the ×2, so the arithmetic does not in fact reconcile 0.3–0.6 to 0.6–1.2 — what anchors the band is empirical: 0.69 ms measured on the shipped pass, and 1.21 ms when `NUM_SAMPLES` is mutated to its 128-tap ceiling. **Read the Technique cells as the cost model: resolution is an input, not just tap count.** A change to `godRaysConfig`'s ½ — including the quarter-res gather this section names as a remedy below — moves the cost without moving a tap count, and must move the budget with it.

Measured on the RX 6600 at 1080p, the shipped 64-tap variant medians **0.69 ms** with the whole frame sky (every tap pays both samples — the most expensive frame the shader can be handed, and a real one, since the pass only runs with the sun on screen) and **0.48–0.55 ms** with no sky in view (the depth sample alone). Harness sync overhead is 10.6 µs, so these are GPU cost and not measurement noise.

Both sit inside the corrected band, but note the all-sky figure is ~15% above the band's *64-tap end*. The residual is optimism in research § 7's own extrapolation, independent of the sample-count correction: it predicted 0.3 ms for the one-sample case that measures 0.48–0.55 ms. That extrapolation scaled Hillaire's PS4 compute figures by FP32 throughput, and this pass is texture-bandwidth-bound, not compute-bound. **The benchmark therefore gates the band's upper bound (1.2 ms), not its 64-tap end** — the same choice the volumetric gate makes in gating the 2.0 ms stack total rather than the ~1.2 ms pass figure. It catches a raised tap count: mutating `NUM_SAMPLES` to its 128-tap ceiling measures **1.21 ms** and goes red (verified, 3D_E-0616). That works because the benchmark loads `god_rays.frag.glsl` from disk, so the shader is the shipped one. This row is timed at `renderScale` 1.0; what the gather resolution is and is not pinned against is covered once below, under *What the god-ray gate still does NOT cover*.

**The Low/Med tier budget, and where 1.75 ms comes from (3D_E-0624).** The reference row above it is an RX 6600 figure at `renderScale` 1.0 — which is *High's* resolution, and High and Ultra are the two presets that never run this pass (both carry `heavyPost = true`, `settings_apply.cpp`). That is the right number for a dev-rig regression gate and the wrong number for the tiers the technique actually serves. The tier budget is derived from the frame instead. **Two of its three inputs are re-scoped here, and the re-scoping is a decision taken by this section rather than a citation** — § 1 and research § 7 both state their figures for the RX 6600, and neither grants a weak tier anything:

- **The frame budget is 16.6 ms at every tier**, and this one is a citation, not a decision: the scalability strategy § 2 states it identically for all four hardware targets. A weak box earns 60 FPS by rendering less, never by being allowed a longer frame.
- **The fog stack's 2.0 ms is re-scoped from a figure to a share.** § 1 pins it "on RX 6600 at 1080p … at the High preset". What carries to other tiers is not that measurement but the *share of a 60 FPS frame* the project is willing to spend on air — 2.0 / 16.6 ≈ **12%**, at every tier. That is a decision recorded here; § 1 does not make it and should not be read as authority for it.
- **The three always-on layers are read as ceilings, not as measurements** — `< 0.05 + < 0.1 + < 0.1 = < 0.25 ms`. They are the only rows in the table written with `<` rather than `~` or a band, and what they cost is one `exp` + mad, one `exp` + divide and one `pow` per pixel folded into the composite — roughly three orders of magnitude less work than the gather's 64 taps × 2 fetches, so the ceiling holds on a weak part for the same reason it holds on a strong one. **None has been measured on weak hardware.** If they breach 0.25 ms combined, the row that fails is the *stack total*, not the god-ray row, and that measurement is owed separately.
- Low and Medium run **no froxel pass at all** (Tier-1 design § 4.1 / § 4.2, implemented in `settings_apply.cpp`), so the god-ray pass is the only heavy consumer of the remainder.

**2.0 − 0.25 = 1.75 ms.** No step in that chain is a measurement of *this pass* on any box, which is the property that matters: the number would have read 1.75 ms before a weak GPU was ever measured, so a measurement can now *fail* it. A budget fitted to the machine it polices can only ever pass — the objection that made this bullet's predecessor vacuous, and it applies to a budget fitted to a GTX 1050 exactly as it applied to one fitted to an RX 6600.

**Every scene-res object scales, not merely the two draws.** Tier-1 design § 3.3 puts the god-ray buffers among those that scale — they are "½ of the internal res", and internal res is the play-mode size × `renderScale`. The timed region covers two draws: pass A gathers at half the internal res, pass B combines additively at the **full** internal res (§ 5.4). But this pass is texture-bandwidth-bound (see above), and its bandwidth is dominated by what the gather *samples*, not by what it writes — 64 taps, each a depth `texelFetch` plus a scene fetch. In the shipped frame those sources scale too: `resizeRenderTarget` resizes the resolve-depth, resolve and TAA-scene targets to the already-scaled internal res, and the combine viewport is that same res. **So a faithful tier timing scales all of it — gather target, combine target, and the depth and scene source textures the gather reads.** Timing a 633×356 gather against 1080p sources would leave a working set 2.3× larger than any tier draws, and inflate the result against a budget derived for the scaled one. Low renders at 0.66 and Medium at 0.75 (§ 4.1, `settings_apply.cpp`), so the scaled pass carries **0.436×** and **0.563×** the pixels of the `renderScale` 1.0 arrangement the reference gate times.

**What this predicts — a prediction, and a weak one.** The GTX 1050 measures **3.1 ms** for the two draws at `renderScale` 1.0 with 1080p sources (3D_E-0624 step (a)). Scaled by pixel count that is **~1.35 ms at Low** and **~1.74 ms at Medium**, i.e. 8% and 10% of a 60 FPS frame rather than the 19% the uncorrected figure suggests. Low fits with room. **Medium lands at ~1.74 ms against 1.75 ms — a 0.6% margin, which is no margin at all: on this budget Medium is expected to go red, not to pass.** Treat the scaling as indicative only, and for two reasons: per-draw overhead does not scale with pixel count, and the 3.1 ms measurement was taken with *unscaled sources*, so the bandwidth term that dominates this pass changes once they scale — in a direction no arithmetic here settles. The figure that decides it is the preset-aware gate run on that box. **If Medium goes red the answer is a cheaper god-ray configuration at that tier** — fewer taps, or a quarter-res gather, either of which must move the budget row with it per the cost-model note above — and **not** a raised budget; 3D_E-0616's do-not-reopen ruling covers the RX 6600 figure sitting inside *its* budget and says nothing about this one.

**MEASURED (2026-08-21, 3D_E-0624 step (c)) — the prediction held and the verdict is "no headroom".** Six runs of the shipped tier gate on the GTX 1050 at Medium, `renderScale` 0.75, 720×405 gather + 1440×810 combine, via `scripts/wintest.sh`:

`1696.0 · 1704.8 · 1704.8 · 1728.2 · 1806.5 · 1952.3 µs` — **median 1716.5 µs**, against the 1750 µs budget.

Three things this settles, and one it does not. **The scaling model was sound**: 1716.5 µs measured against ~1740 µs predicted is within 2%, even though the prediction scaled a figure taken with *unscaled* source textures. **The budget is doing its job**: it was written and published before this measurement, so it was capable of failing, and it very nearly does. **And the pass has no headroom on the tier it exists for** — 2% under a ceiling that is itself 12% of the frame, on the hardware least able to give any of it back. What it does NOT settle is a verdict, because **the gate exceeds its budget on 2 runs of 6**. A gate that red-flags a third of its runs cannot adjudicate a 2% margin, and that is a defect in the measurement rather than in the budget. Same instrument on the RX 6600 spans 526–1097 µs at `renderScale` 1.0 depending only on how warm the GPU is. Both consequences are filed rather than absorbed here: a cheaper Low/Med god-ray configuration, and the harness's reproducibility. **Do not respond to the flap by raising the budget** — the budget is the one part of this that is derived rather than measured.

**RE-MEASURED (2026-09-02, 3D_E-0626) — the flap was the harness, and Medium has headroom.** The block above was taken with a timing scheme that sampled inside the GPU's start-up transient: three warm-up frames, then the median of eight. The GTX 1050 ramps its clocks under sustained load for roughly half a second, so every timed frame landed in the ramp. The gate now warms until the clocks settle and asserts the *uncontended* cost — the minimum of a long timed run — with the reasoning recorded beside the helper in `tests/test_fog_benchmark.cpp`.

Six runs of the shipped tier gate on the GTX 1050 at Medium, same box and same binary, via `scripts/wintest.sh`:

`1391.7 · 1394.9 · 1402.2 · 1412.0 · 1415.6 · 1447.0 µs` — a 3.9% spread, against the 1750 µs budget, **red on none**.

**This withdraws the "no headroom" verdict, and nothing else.** Medium sits about 20% inside its ceiling. The block above stays as the record of what that run found, and two of its statements still hold: the budget was published before any weak-GPU measurement so it remains capable of failing, and it is still not to be raised. What does not hold is the scaling prediction's apparent accuracy — ~1.74 ms predicted against ~1.40 ms measured is pessimistic by about a quarter, for the reason that paragraph already gives, that it scaled a figure taken with *unscaled* source textures.

**The three gate figures, re-taken on the RX 6600 under the same harness (2026-09-02, six runs each).** These are uncontended cost, not the medians the older figures in this section quote:

| Gate | Measured | Budget | Run-to-run spread |
|---|---|---|---|
| God rays, `renderScale` 1.0 | ~0.52 ms | 1.2 ms | 2.3% |
| Volumetric froxel dispatch | ~0.40 ms | 2.0 ms | 4.0% |
| GI inject dispatch | ~0.10–0.19 ms | 0.4 ms | 39% |

The GI inject spread is a residual recorded rather than fixed. At roughly a tenth of a millisecond, drained every frame, that pass never holds boost clocks and its whole distribution shifts between runs — its minimum tracks its median, so this is power state and not outliers. Its headroom against the 0.4 ms budget is wide enough that the gate still adjudicates, and a tighter figure would need a different measurement shape. **Every other per-pass figure in this section and in § 11.2 predates this harness and is a single sample; re-taking them is 3D_E-0657.**

**How the two budgets are selected, and what must NOT change.** `budgetsApplyToThisMachine` asks whether the box is the hardware class every figure in the benchmark was measured on. **It stays exactly as it is and keeps its meaning**, and the volumetric and GI gates keep using it unchanged — inverting it or redefining it would silently move those two. The god-ray gate gains a tier branch *ahead* of it rather than replacing it, so the reference row is still protected by the hardware-class question it was measured under:

| Declared preset | Timed at | Asserts |
|---|---|---|
| Low, Medium | that preset's `renderScale`, read back from the shipped `applyQualityPreset` | the 1.75 ms tier row |
| High, Ultra — **and an unset or unrecognised value, which resolve to High** | `renderScale` 1.0 | the 1.2 ms reference row |
| Custom | `renderScale` 1.0 — the reference resolution | *(nothing — reports its cost)* |

`Custom` times at 1.0 rather than skipping before the draws, for two reasons: the file's existing contract is that the path always runs so a crash is still caught, and a cost reported at an unstated resolution could not be compared with the 0.69 ms and 3.1 ms figures this project set its budgets from. Unset resolving to High is deliberate: `applyQualityPreset` writes no `renderScale` for `Custom`, so 1.0 is the only defined choice, and an undeclared box is not claiming to be a weak tier.

**The selection splits across the timed region, and this is the one ordering that works.** The resolution half must be resolved **above** the draws — the framebuffer and source-texture sizes are chosen before the timing lambda, so the tier's `renderScale` has to be in hand there. The budget-and-assert half sits **below** the existing Debug and software-renderer guards, whose order is unchanged. Putting the whole tier branch below the guards would time the pass at `renderScale` 1.0 and then judge it against the tier row — 3.1 ms against 1.75 ms on the GTX 1050, permanently red on the one box the tier branch exists for. Hoisting the guards above the timing instead would lose the property the benchmark's header promises, that the path always runs and so still proves it does not crash.

**Both guards apply to the tier row too.** The 1.75 ms figure is derived from a frame budget rather than from a GPU, but the thing measured is still a GPU wall-clock — under llvmpipe the draws rasterise on the CPU and under Debug the build is unoptimised, so both remain meaningless against either budget.

**Where each budget actually gets enforced.** The tier row has an automated home: `scripts/wintest.sh` defaults `VESTIGE_QUALITY_PRESET=medium` and builds Release, so neither guard swallows it on the GTX 1050. **The reference row has none.** `scripts/local-ci.sh` exports `LIBGL_ALWAYS_SOFTWARE=1` unconditionally, so the software-renderer guard skips it in the local mirror exactly as it does on GitHub's GPU-less runners — the 1.2 ms assertion fires only on a hand-run `ctest` against a real driver on the dev rig, which is how 3D_E-0616's 128-tap red was obtained. Recorded because a gate nothing runs automatically looks identical to one that passes.

**What the god-ray gate still does NOT cover.** The `renderScale` half of the resolution is specified above — **it is not yet built; the preset-aware gate is the second half of 3D_E-0624 step (c)**. Even once it is, the benchmark still hard-codes the 1920 × 1080 play-mode base and `godRaysConfig`'s ½: `renderer.cpp` open-codes the half at two sites with no named constant, and the base is not in the renderer at all — it is `Editor::m_playModeWidth/Height`, changeable at runtime. So a `godRaysConfig` changed to full-res would still be timed at half-res and stay green. Same two-copy drift shape as 3D_E-0617 one layer up, narrowed rather than closed, and recorded here because a benchmark that silently misses a regression is worse than no benchmark.

Tests:
- **11.6** — ✅ *shipped:* benchmark harness (`tests/test_fog_benchmark.cpp`, Release-gated); CPU-spec + GPU-parity tests (`tests/test_volumetric_fog.cpp`, `tests/test_volumetric_fog_gpu.cpp`); **"volumetric off" equivalence** holds byte-for-byte when `volumetricFogEnabled=false`.
- **11.7** — *dropped (§7).* The scatter pass's existing GLSL `henyeyGreenstein` stays pinned to CPU `henyeyGreensteinPhase` by the shipped parity test in `tests/test_volumetric_fog_gpu.cpp` — no new test.
- **11.8** — CPU unit tests for `fogDensityNoise` (range `m∈[0,2]`, determinism, animation changes the value, `strength=0 → m≡1`); GPU parity (extract GLSL `fogDensityNoise` + hash helpers via `extractGlslFunction`, single-pixel harness vs CPU — integer-hash layer bit-exact, final value within `1e-4 + 1e-3·|cpu|`); `noiseEnabled=false` byte-identical to the uniform medium (full-dispatch readback); benchmark re-run with noise on stays ≤2 ms (60 FPS gate). Full design + test contract in §11.
- **11.11** — pure-function `fogVolumeDensity` unit tests (falloff knees, soft-edge monotonicity, static-vs-animated) + GLSL parity. **Over-cap drop is a separate test on the upload path**, not on the pure function: `fogVolumeDensity` takes one `FogVolume` and knows nothing of `MAX_FOG_VOLUMES`, so the cap needs its own case — a 33rd volume is dropped, and logged once per change in the over-cap count (§6.3).
- **11.5** — screen-space god-ray smoke, plus the GPU benchmark (`GodRayPassUnderBudget`) gating the shipped pair of draws. **Two budgets, selected three ways** (3D_E-0616, 3D_E-0624) — the selection table above is the contract, together with the two paragraphs after it on what scales and where the split falls relative to the timed region. This bullet is a pointer, not a restatement: build from those. **"God rays off" equivalence is structural, not a test** (§5.7) — do not write one.

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

`PostProcessAccessibilitySettings` carries `bool volumetricFogEnabled = true` (`post_process_accessibility.h:104`), with `safeDefaults()` setting it `false` (`post_process_accessibility.cpp`). The volumetric layer is gated **in the renderer**, not inside `applyFogAccessibilitySettings`: `renderer.cpp` gates on `isVolumetricFogPassActive(volumetricFogEnabled, m_qualityHeavyPostEnabled, m_volumetricFogPass.isInitialized())` — all **three** terms — feeding both the froxel dispatch and the `u_volumetricEnabled` composite uniform. *(Corrected 2026-08-21, 3D_E-0616: this gave the pre-3D_E-0617 two-term form.)* `applyFogAccessibilitySettings` operates on the analytic distance/height `FogState` and is unchanged — distance + height fog stay authored-on (disabling them produces a harsh fog-horizon cutoff — visually worse). `reduceMotionFog` (set `true` by `safeDefaults()`) clamps the sun-lobe; with no temporal reprojection in Phase 10 it has no froxel-shimmer to suppress, and its header comment already records that the "disable temporal reprojection" role arrives in Phase 13. Reduce-motion zeroes the noise `windVelocity` so the haze stays static — but it does so **at the per-frame build site, not here** (§11.8, §12.4). WCAG 2.2 SC 2.3.1 / 2.3.3, Xbox AG 117/118 (research §6).

---

## 11. Fog density noise (slice 11.8) — ✅ SHIPPED (design-of-record)

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
    int       octaves      = 3;                 // FBM octaves — see the clamp note below
    glm::vec3 windVelocity = {0.4f, 0.0f, 0.1f};// NOISE-space scroll, cycles/s (see §11.4)
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
Drifting haze is motion (WCAG 2.2 SC 2.3.3; Xbox AG 117). When `reduceMotionFog` is true the **per-frame `FrameParams` build zeroes `windVelocity`** (static noise — still non-uniform, no drift), *after* the panel's authored values, per §12.2/§12.4 — `renderer.cpp` does this at the build site. It is **not** a line in `applyFogAccessibilitySettings`, which operates on the analytic distance/height `FogState` and carries no `FogNoiseParams` to zero. *(Corrected 2026-08-21, 3D_E-0616.)* `volumetricFogEnabled=false` already disables the whole layer. No new accessibility field needed.

### 11.9 Test contract
- **CPU unit tests** (`tests/test_volumetric_fog.cpp` — the froxel CPU-spec home): `m ∈ [0,2]` and `n ∈ [0,1]` across a sample grid; determinism (same args → identical value); animation (different `time` → different value, given non-zero wind); `strength=0 ⇒ m ≡ 1`; more octaves add detail without leaving range.
- **GPU parity** (`tests/test_volumetric_fog_gpu.cpp`): extract GLSL `fogDensityNoise` + its hash/`valueNoise3` helpers via `extractGlslFunction`, run on the single-pixel `ShaderProgram` harness, compare to CPU `fogDensityNoise` — integer-hash conversion bit-exact, final interpolated value within `1e-4 + 1e-3·|cpu|` (the shipped HG-parity tolerance).
- **Equivalence:** `noiseEnabled=false` ⇒ inject output byte-identical to the pre-11.8 uniform medium (full-dispatch readback).
- **Benchmark:** the shipped `tests/test_fog_benchmark.cpp` full-dispatch budget (≤2 ms median, Release-gated) re-run with noise enabled — must stay green (60 FPS gate).

### 11.10 Performance
3-octave integer-hash value noise, no transcendentals: ~24 hashes + trilinear blends per froxel, ALU-only — projected sub-0.1 ms across 920k froxels on the RX 6600, **measured by the benchmark, not assumed.**

---

## 12. Slice 11.10 — Editor FogPanel — ✅ SHIPPED (design-of-record)

**Goal.** A single dockable ImGui panel exposing every per-scene fog knob the renderer already owns, plus the volumetric look-constants that slices 11.5 / 11.8 / 11.11 left inlined with `TODO 11.10` markers. It mirrors the shipped `AudioPanel` four-tab shape (Distance / Height / Volumetric / Debug) and the Ed5 `IPanel` / `PanelRegistry` togglable-panel convention. **No new GPU work** — the panel only *authors* parameters consumed by passes already placed on the GPU.

### 12.1 What the panel drives (single source of truth = the Renderer)

The renderer is the authority for fog state; the panel is a thin view that reads the current value into each widget and writes it back on edit (the `NavigationPanel` / `AudioPanel` precedent). Existing renderer getters/setters already cover the non-volumetric layers:

| Tab | Drives | Renderer API (already present) |
|---|---|---|
| Distance | `FogMode` + `FogParams` (colour, start, end, density) | `setFogMode` / `getFogMode`, `setFogParams` / `getFogParams` |
| Height | height-fog enable + `HeightFogParams`; sun-inscatter enable + `SunInscatterParams` | `setHeightFogEnabled` / `isHeightFogEnabled`, `setHeightFogParams` / `getHeightFogParams`, `setSunInscatterEnabled` / `isSunInscatterEnabled`, `setSunInscatterParams` / `getSunInscatterParams` |

The **Volumetric** tab needs two small pieces of renderer state that today are hardcoded literals in the per-frame `FrameParams` build (`renderer.cpp` ≈ 1285–1300 for the medium/noise) — this slice lifts them into authored structs (§12.2). **The god-ray half is no longer a literal**: `GodRayParams` and its accessors have shipped, and the god-ray block reads `m_godRayParams.edgeMargin` / `.intensity` (3D_E-0616):

| Tab | Drives | New renderer API (this slice) |
|---|---|---|
| Volumetric | froxel medium (`scattering`, `extinction`, `anisotropy`) + density-noise (`FogNoiseParams`) | `setVolumetricFogParams` / `getVolumetricFogParams` over a new `VolumetricFogParams` |
| Volumetric | god-ray artist gain + screen-edge margin | `setGodRayParams` / `getGodRayParams` over `GodRayParams` — **already present** (`renderer.h:130-131`, member `:666`) |
| Volumetric | mist / ground-fog volume list (slice 11.11) | `setFogVolumes` / `fogVolumes` (already present) |

### 12.2 Lifting the inlined volumetric constants (resolves the `TODO 11.10` markers)

Two new POD structs join `FogNoiseParams` / `FogVolume` in `engine/renderer/volumetric_fog.h` (their natural home — `volumetric_fog.h` is already included by the renderer, so no new include juggling):

```cpp
struct VolumetricFogParams
{
    glm::vec3      scattering = glm::vec3(0.005f);  // sigma_s, 1/m (≈4× thinner than neutral → haze)
    float          extinction = 0.005f;             // sigma_t, 1/m
    float          anisotropy = 0.3f;               // Henyey-Greenstein g (soft forward bloom)
    // enabled, frequency, strength, octaves, windVelocity — declaration order.
    // windVelocity is 0.15 on Z (NOT the FogNoiseParams struct default of 0.1) to
    // reproduce the renderer's current inlined literal byte-for-byte.
    FogNoiseParams noise = FogNoiseParams{ true, 0.03f, 0.5f, 3, glm::vec3(0.4f, 0.0f, 0.15f) };
};

struct GodRayParams
{
    float intensity  = 1.0f;  // artist gain multiplied into the per-pixel shaft weight
    float edgeMargin = 0.3f;  // sun fades over this fraction of screen past the frame edge
};
```

Their **defaults reproduce the current inlined literals byte-for-byte**, so the lift is behaviour-preserving when the panel is untouched (a parity expectation pinned by test, §12.5). The per-frame build reads the struct instead of the literal; the reduce-motion `windVelocity`-zero and `animSpeed`-zero accessibility transforms stay *at the build site* — they override the authored values each frame (§12.4). God-ray gain multiplies the existing edge-visibility (`u_intensity = sun.intensity * params.intensity`); `edgeMargin` **replaced** the `GOD_RAYS_EDGE_MARGIN` constant, which no longer exists — do not go looking for it, and do not declare a second `GodRayParams`.

**Scope decision (logged per CLAUDE.md Rule 5).** The radial-blur *sampling* constants — `NUM_SAMPLES (64)`, `DENSITY (0.9)`, `DECAY (0.95)`, `WEIGHT (0.5)`, `EXPOSURE (0.3)` — stay inlined in `god_rays.frag.glsl` with their existing `TODO 11.10 / Formula Workbench` markers. Exposing them is five per-tap uniforms with marginal authoring value and a real shader cost; `intensity` is the one shaft knob an artist actually reaches for. The `F_turb` / octave look-constants inside `fogVolumeDensity` likewise stay inlined — they **must** remain literals for the CPU↔GPU parity extractor (§6.2; on how exact that parity is, see §6.3). Both deferrals are recorded in CHANGELOG.

### 12.3 Panel structure (mirrors AudioPanel + Ed5)

`engine/editor/panels/fog_panel.{h,cpp}`. `class FogPanel : public IPanel` — `displayName()` = `"Fog"`, `isOpen` / `setOpen` over an `m_open` bool, default-closed (matching the other Window-menu panels). `void draw(Renderer* renderer)` opens one ImGui window with a four-item tab bar; a null `renderer` early-returns so headless tests never touch GL.

Editor-owned state (the only state the panel holds, mirroring `AudioPanel`'s reverb-zone list):
- `std::vector<FogVolume> m_volumes` + `int m_selectedVolume = -1` — the working set the Volumetric tab edits. On the first frame after the panel opens it is **seeded from `renderer->fogVolumes()`** (a one-shot latch reset each frame the panel is closed) so volumes authored by scene loading are adopted, not clobbered by an empty set; thereafter edits **push back on change** via `renderer->setFogVolumes(m_volumes)` (no per-frame vector copy while the tab merely sits open).
- Pure, GL-free management methods so the panel is unit-testable without an ImGui context (`AudioPanel` / `NavigationPanel` precedent): `int addVolume(const FogVolume&)` (returns the new index, selects it), `bool removeVolume(int)` (range-checked; shifts selection down exactly like `AudioPanel::removeReverbZone`), `void selectVolume(int)`, `const std::vector<FogVolume>& volumes() const`, `int selectedVolume() const`.

Tabs:
- **Distance** — `FogMode` combo (labels from `fogModeLabel`), colour picker, start / end / density drags.
- **Height** — enable checkbox + ground density / falloff / height / colour / maxOpacity; sun-inscatter enable + colour / exponent / start.
- **Volumetric** — a read-only status line (is the froxel path active? — gated by Settings → Accessibility `volumetricFogEnabled`, with a hint that the master toggle lives there, §12.4); scattering / extinction / anisotropy drags; noise enable + frequency / strength / octaves; god-ray gain + edge margin; a volume list (Add Box / Add Sphere / Remove / select) with an editor for the selected volume's shape, center, half-extents, colour, density, edge softness, anim speed.
- **Debug** — froxel grid dims (`m_volumetricFogPass.config()` via a renderer getter), active volume count vs `MAX_FOG_VOLUMES`, and the over-cap indicator.

### 12.4 Accessibility

The panel authors *look* only. The master fog / volumetric / god-ray enables and the reduce-motion freeze already live in `PostProcessAccessibilitySettings` (slices 11.6 / 11.5 / 11.9) and remain the single authority — the panel shows them read-only with a "set in Settings → Accessibility" hint rather than forking a second toggle surface. Reduce-motion's per-frame `windVelocity` / `animSpeed` zeroing is applied *after* the panel's authored values in the `FrameParams` build, so a motion-sensitive user is never overridden by a scene author.

### 12.5 Test contract

Headless (no GL / ImGui), mirroring `tests/test_audio_panel.cpp`:
- `FogPanel` defaults closed; `setOpen` / `isOpen` round-trip; `displayName() == "Fog"` (IPanel surface).
- `addVolume` returns a growing index and selects it; `removeVolume` range-checks, shifts selection down, and clears selection when the selected row is removed; an out-of-range remove is a no-op (mirrors the reverb-zone tests).
- A **parity guard**: `VolumetricFogParams` / `GodRayParams` default values equal the literals they replace (an `EXPECT` against the documented constants — `0.005` / `0.005` / `0.3`, noise `{true, 0.03, 0.5, 3, (0.4,0,0.15)}`, god-ray `{1.0, 0.3}` — so a future edit to one site can't silently desync the other).

The ImGui `draw()` dispatch isn't headless-testable (same precedent as Ed5 `drawMenuToggle` and Pe1 `beginBatch2D`) — verified visually at editor launch.

### 12.6 CPU / GPU placement (per CLAUDE.md Rule 7)

Entirely **CPU** — UI event handling, parameter authoring, and a small POD vector. Branching / IO / decision work, the CPU side of the heuristic. The authored parameters feed the already-GPU-placed froxel compute passes (11.6 / 11.8 / 11.11) and the god-ray fragment shader (11.5); this slice adds **no** GPU work and **no** new parity surface (the `fogVolumeDensity` parity from 11.11 is untouched — its constants stay inlined, §12.2; on its exactness see §6.3).

### 12.7 Editor wiring

One `FogPanel m_fogPanel` member on `Editor`; `m_panelRegistry.registerPanel(&m_fogPanel)` in `Editor::initialize`; `m_panelRegistry.drawMenuToggle(m_fogPanel)` in the Window menu; `m_fogPanel.draw(renderer)` in the panel-draw block (the `renderer` handle is already in scope there, e.g. `m_hdriViewerPanel.draw(renderer)`). One inherit + one `displayName()` + one register + one toggle — the Ed5 contract.

**Octave clamp ownership (3D_E-0616).** The clamp to `1..5` is applied by the **CPU** mirror alone — `fogDensityNoise` in `volumetric_fog.cpp` opens with `std::clamp(params.octaves, 1, 5)`. The GLSL twin takes `int octaves` and passes it straight to `fbm3` unclamped, and `volumetric_fog_pass.cpp` uploads `params.noise.octaves` raw. **So §11.9's parity tolerance holds only inside `1..5`**; a sweep past 5 compares a CPU-clamped value against an unclamped GPU one and diverges by far more than `1e-4 + 1e-3·|cpu|`. Do not answer that by loosening the tolerance. The divergence is a live code asymmetry, surfaced rather than fixed here because this is a document review.

**Sources.** Internal precedent only — `AudioPanel` (four-tab editor panel), `NavigationPanel` (GL-free testable panel state), and the Ed5 `IPanel` / `PanelRegistry` convention (ROADMAP **Ed5**, shipped 2026-05-16). No external research: this slice wires existing renderer APIs to an existing UI pattern.

---

## 13. Cold-eyes loop log

Per CLAUDE.md Rule 14 — loop until a cold pass returns zero verified actionable findings; loops 2+ run cold with no prior-loop briefing.

- **Loop 1** (fresh reviewer): 3 findings. 1 HIGH (wrong commit hash `02c0414` for shipped Workbench features → corrected to `1cb553b`), 2 LOW (per-slice test counts didn't reconcile to the 29-test `Fog` suite → cited suite totals; an `expm1` claim that was a *verified non-issue* — CPU does use `std::expm1`, GLSL uses the `1-exp` equivalent — dropped explicitly). All verified against disk before fixing.
- **Loop 2** (fresh reviewer, cold): 8 findings, none a repeat of Loop 1 (Loop-1 fixes held). 1 CRITICAL: §9.1 "input_grid" was a fictional gap — `sweepRecurse` already does N-dimensional sweeps, so slice 11.7 needs no tooling/version bump (corrected). 2 HIGH: §4.2 claimed the shipped composite already samples the froxel texture (reworded to future-tense "extends"); god-rays slice renumbered 11.12→11.5 to match the shipped CHANGELOG ledger. 2 MEDIUM: `reduceMotionFog` default wording (struct default is `false`, set `true` by `safeDefaults()`); lingering header-comment temporal note flagged for update at 11.6. 2 LOW: 11.8 reuse clarified; noise basis standardised to Perlin-Worley *(superseded 2026-06-18 — §11.2 selects a value-noise FBM and drops Worley as a cloud-silhouette-only tool)*. 1 INFO: added §4.4 CPU/GPU placement for the froxel core. All verified against disk before fixing.
- **Loop 3** (fresh reviewer, cold): **CLEAN — zero actionable findings.** All load-bearing claims re-verified against disk (shipped symbols, `sweepRecurse` N-dim sweeps, `reference_harness.cpp:116`, commit `1cb553b`, slice numbering vs CHANGELOG ledger, scope decision vs research §3/§7 + ROADMAP 1659, budget arithmetic). 1 INFO (Phase-13-vs-Phase-15 naming reconciled by ROADMAP 1659 — doc lands on the correct phase) left for follow-up per Rule 14. **Convergence reached → signed off.**

### Amendment 2026-06-18 (drop slice 11.7 + add slice 11.8 design) — cold-eyes loops

Per Rule 14 the amendment was re-reviewed cold; loops 2+ ran with no prior-loop briefing.

- **Loop 1** (fresh reviewer): 3 HIGH (stale "swapped for Schlick in 11.7" in §4.2; "Schlick phase" left in the §8 perf table; ROADMAP not yet updated to match the doc's "dropped"/"logged" claims), 3 MEDIUM (Perlin-Worley vs value-noise contradiction across §3/§6.2/log; "first 3D use" false — `snoise` exists in `particle_simulate.comp.glsl`; wrong CHANGELOG ledger line 6673→6732), 2 LOW (imprecise scatter line refs; closer `u_elapsed` precedent at `particle_simulate.comp.glsl:73`). All verified against disk and fixed; ROADMAP edited (11.7 dropped in the progress note, new **FW W9** cross-formula-gap item, value-noise basis).
- **Loop 2** (fresh reviewer, cold): caught that the doc still framed **slice 11.6 as future work** though it shipped 2026-06-18 (§4.2 "no 3D-texture sampler" was false; §0/§1/§3/§4/§8 stale framing), plus MEDIUM (CPU noise tests routed to `test_fog.cpp` instead of the froxel home `test_volumetric_fog.cpp`; `terrain.cpp:915` is value-noise, not a pure integer mixer) and LOW (ROADMAP boundary note 1657→1659; ROADMAP feature bullet still "Perlin/Worley"). One reported HIGH (`FrameParams` "does not exist") was a **false finding** — the reviewer read `volumetric_fog.h`; `FrameParams` is in `volumetric_fog_pass.h:43` (verified on disk). All real findings fixed: re-baselined §0/§1/§3/§4/§8/§10 to shipped reality, corrected the test file + citations, updated the ROADMAP bullet, and fixed the stale scatter-shader header comment (11.7 Schlick → dropped).
- **Loop 3** (fresh reviewer, cold): **no structural / mechanical / architectural defects — only verified polish.** 1 MEDIUM (§10 described the accessibility gate as a future `applyFogAccessibilitySettings` line, but it shipped as a renderer-level gate, and the `reduceMotionFog` comment was already updated), 3 LOW (scatter line refs drifted again 94→97 / 70→73 / 112-120→115-124; §13 historical "ROADMAP 1657" cite; §6 "ROADMAP 465"→465-466), 2 INFO (§11 subsection numbers shadow slice numbers — readability only; a stale `post_process_accessibility.h` "awaiting consumer" comment whose consumer shipped in B2). All polish items fixed, including the stale code comment. Per the session standing instruction (converge once only verified polish remains and no structural fixes are outstanding), **convergence reached** — the doc matches shipped reality and the slice 11.8 design is implementation-ready.

### Amendment 2026-06-18 (slice 11.11 mist-volume design finalized + shipped) — cold-eyes loop

§6.2/§6.3 were finalized with the concrete falloff helper (`coreFade`/`smooth01`), the turbulence math + provisional `F_turb`/octave constants, the `std430` 4×`vec4` SSBO layout, the over-cap throttled-log rule, and the `falloff > 0` FBM-skip + reduce-motion clauses — then implemented and reviewed cold.

- **Loop 1** (fresh reviewer, no authoring context): **CLEAN — zero actionable findings.** The reviewer diffed `fogVolumeDensity` branch-by-branch between `volumetric_fog.cpp` and `volumetric_inject.comp.glsl` (`smooth01`, `coreFade`, the box product / sphere radial branches, the turbulence guard + vector form, the inlined `0.15`/3-octave constants, and the shared `fbm3`/`valueNoise3`) and found no divergence; verified every §6 claim against disk (struct fields + defaults, `MAX_FOG_VOLUMES = 32`, SSBO binding 1 / 64-B packing, `u_volumeCount`-gated byte-identical path, noise-then-volumes order, additive `density·fd` / `colour·density·fd`, reduce-motion `animSpeed` zeroing, the over-cap throttle, and all wiring); confirmed the falloff math ("1 at core → 0 at extent", hard step at `edgeSoftness=0`, sphere `radius = halfExtents.x`); and confirmed the parity test exercises the animated branch (2 of 4 cases have `anim≠0`, times swept). 2 INFO (the `FogVolumeShape` 0/1 mapping relies on declaration order — correct as written; no internal contradictions). Per the session standing instruction (converge once only verified polish/INFO remains and no structural fixes are outstanding), **convergence reached** on the clean pass — the doc matches the shipped implementation.

### Amendment 2026-06-18 (slice 11.5 god-rays design) — cold-eyes loops

§5 was expanded from the two-bullet sketch into an implementation-ready design (algorithm §5.1, folded light buffer §5.2, CPU sun projection + fade §5.3, two-pass integration §5.4, CPU/GPU split §5.5, gating §5.6, test contract §5.7, perf §5.8), then reviewed cold *before* implementation (Rule 1).

- **Loop 1** (fresh reviewer, no authoring context): **1 CRITICAL + companions.** The insertion point was wrong against the real composite order — I wrote "after the contact-shadow pass," but the actual order is **bloom → auto-exposure → contact shadows → volumetric → composite**, so bloom runs *before* contact shadows; placing god rays there would have left the shafts *unbloomed* (the opposite of the stated payoff) and contradicted the doc's own "before bloom" sentence. Also flagged: the SH-probe grid occupies units 17–23 (don't grab a high unit); the half-res gather over a full-res reverse-Z depth needs point-sampled depth (silhouette aliasing); set `GL_LINEAR` on the god-rays FBO for the upsample. Verified sound: reverse-Z sky test, sun projection math (matches the froxel pass's `−direction` toward-sun convention), gather direction, gating against the real `volumetricActive`, resize pattern. **Fixed:** insertion moved to *before the bloom block* (reading the live `hdrSourceFbo`, which SMAA/TAA reassign), point-sampled depth, linear FBO filter, unit guidance (0–8 / 14–16 free); the premature "✅ SHIPPED" header removed.
- **Loop 2** (fresh reviewer, cold, no prior-loop briefing): **no CRITICAL — the insertion order, sky test, sun math, and unit map all verified correct against disk.** 1 HIGH (the "wired like `volumetricFogEnabled`" one-liner hides **five** plumbing sites — struct field, hand-written `operator==`, `safeDefaults()`, the `Settings` JSON mirror's `operator==`/`to_json`/`from_json`, and the `settings_apply` wire transfer — omitting any silently breaks persistence/equality), 1 MEDIUM (name the handles: post-AA colour `hdrSourceFbo` paired with pre-AA-resolved depth `m_resolveDepthFbo`), LOW/INFO (shader double-negate transcription risk — caught by the §5.7 smoke test; resize must delete+recreate the FBO; reverse-Z projection assumed in the CPU test). **Fixed:** §5.6 now enumerates the five plumbing sites, §5.4 names both handles. No structural/architectural defects remain — the design is implementation-ready; the HIGH/MEDIUM are an implementation checklist, carried into the code and re-checked by the post-implementation cold review.
- **Loop 3 — post-implementation** (fresh reviewer, cold, against the shipped code): **no CRITICAL / HIGH / MEDIUM.** Verified correct: the pass sits after the SMAA/TAA `hdrSourceFbo` reassignment and before the bloom block (shafts bloom + feed auto-exposure); no read-while-write hazard and no `glTextureBarrier` needed (matches the SSAO→blur / SMAA-chain render-then-sample pattern — the barrier in bloom is only for same-texture mip read/write); additive blend enabled then disabled so it doesn't leak into bloom; the half-res viewport doesn't leak (bloom sets its own); `m_resolveDepthFbo` is resolved early (step 2) so the gather reads current-frame depth; reverse-Z sky test, `texelFetch` clamps + out-of-frame guard, loop direction, and `u_intensity` early-out all correct; uniform parity exact (no orphan/missing); `godRaysSunScreenInfo` math hand-traced (the partial-fade test: uv.x=1.15 → intensity 0.5); **all five settings plumbing sites present** (struct field, `operator==`, `safeDefaults`, the `Settings` mirror's `operator==`/`to_json`/`from_json`, the wire transfer); FBO half-res RGBA16F linear + half-res resize. 1 LOW (a header comment claimed `safeDefaults()` leaves god-rays on, but the cpp correctly turns them off — fixed the comment), 2 INFO (sky test `<=`→`<` to exactly match `contact_shadows.frag.glsl` — tightened; §5.4 prose said `R11F_G11F_B10F` but the code uses `RGBA16F` — corrected the prose). **Convergence reached** — implementation matches the design and is committed.

### Amendment 2026-06-19 (slice 11.10 editor FogPanel design) — cold-eyes loop

§12 was added (the new FogPanel design + the lift of the inlined volumetric/god-ray constants into authored `VolumetricFogParams` / `GodRayParams` structs), then reviewed cold *before* implementation (Rule 1).

- **Loop 1** (fresh reviewer, no authoring context): **no CRITICAL / HIGH / MEDIUM.** Verified against disk: all 14 "already present" renderer getters/setters (`renderer.h:107,110,301–328`); the byte-for-byte literals — scattering/extinction `0.005`, anisotropy `0.3` (`renderer.cpp:1285–1287`), noise `{true,0.03,0.5,3}` (`1296–1299`), `windVelocity (0.4,0,0.15)` (`1302`, confirming it differs from the `FogNoiseParams` struct default `0.1` at `volumetric_fog.h:118` — the warning the doc flags), `GOD_RAYS_EDGE_MARGIN 0.3` (`renderer.cpp:1061`); the `FogNoiseParams` field order `{enabled,frequency,strength,octaves,windVelocity}` so the positional aggregate-init is correct; C++17 confirmed (so designated initializers correctly avoided); `u_intensity = sun.intensity` today (`renderer.cpp:1076`) so the artist-gain multiply is a valid minimal change; the `IPanel` four-method surface; the `AudioPanel::removeReverbZone` shift-down semantics the doc says FogPanel mirrors; and the full editor wiring (`m_panelRegistry` member, `registerPanel` in `initialize`, `drawMenuToggle` block, `renderer` in scope at the draw site). 2 LOW (both citation fixes — §12.1 cited `≈1111` for the god-ray margin which is actually `1061`; the F_turb/octave-inlined claim cited §6.3 but that content lives in §6.2). Both verified and fixed. Per the session standing instruction (converge once only verified non-structural polish remains), **convergence reached** — the design is implementation-ready and the byte-for-byte parity contract is sound.
- **Loop 2 — post-implementation** (fresh reviewer, cold, against the shipped code + diff): **no CRITICAL / HIGH / MEDIUM.** Verified byte-for-byte against the diff that the lift changed only indirection, not values (scattering/extinction `0.005`, anisotropy `0.3`, noise `{true,0.03,0.5,3,(0.4,0,0.15)}`, god-ray gain `1.0`, margin `0.3`); reduce-motion still zeroes `windVelocity` *on top of* the authored value and the per-volume `animSpeed` zeroing is untouched; `GOD_RAYS_EDGE_MARGIN` fully removed (no orphan refs); the `BeginDisabled`/`EndDisabled` pairs balanced on every path; the `FogVolumeShape` Box=0/Sphere=1 combo mapping correct; `removeVolume` a byte-identical mirror of `AudioPanel::removeReverbZone`; editor wiring reachable; the three remaining `TODO 11.10` markers are the *intentionally* deferred sampling/`F_turb` look-constants (§12.2). 1 LOW (the panel never seeded `m_volumes` from `renderer.fogVolumes()`, so opening it would clobber scene-loaded volumes with the empty set) + 1 INFO (the `volEdited` flag was dead — the push ran unconditionally). **Both fixed:** added a one-shot seed-on-open latch (§12.3) and gated the push on actual change. Full debug regression green after the fix. **Convergence reached** — implementation matches the design and is committed.

### Amendment 2026-08-21 (3D_E-0624 — Low/Med god-ray tier budget) — cold-eyes loops

§ 8 gained a Low/Med tier budget row, its derivation, and the contract for a
preset-aware gate. Gated under CLAUDE.md rule 14: a run's cap bounds that run,
not the document, so this authoring edit owed a fresh gate. Genre pinned `spec`
(cap 2). Deterministic layer: `doc_integrity` over this file and the Tier-1
design, clean on every run.

- **Loop 1** (2 fresh lanes, cold — one against the cited documents, one against
  the code): **Q1 1 · Q2 3 · Q3 4 · Q4 0 — 8 verified, 8 fixed, 0 dismissed.**
  **Both lanes independently found five of the eight**, the run's strongest
  signal. The most consequential: the derivation cited § 1 as authority for a
  tier-independent 2.0 ms fog allowance, and § 1 double-scopes it "on RX 6600 at
  1080p … at the High preset" — the cited passage did not carry the proposition
  the whole 1.8 ms chain hung on. Also converged on: the section claimed the
  preset-aware gather "**is now** derived from the preset" when nothing was
  built, so a reader would have closed step (c) without writing the branch;
  every scaling sentence named only the *gather* while the timed region is two
  draws and pass B is full-res, so an implementer could have left the combine at
  1080p and red-flagged a pass inside budget; "any box declaring neither …
  reports its median" contradicted the shipped `if (!env || !*env) return
  QualityPreset::High;`, and building it would have retired the only assertion
  that currently fires; and Tier-1 § 4.1 still quoted the **0.3–0.6 ms** figure
  3D_E-0616 superseded, while naming § 8 as owner — a live 3× contradiction that
  would have produced a `600.0` constant, red on the RX 6600's own 0.69 ms. That
  last was fixed by deleting the figure from the Tier-1 table rather than
  updating it: it had already drifted twice, and a pointer cannot drift. Lane B
  alone found the two the implementer would otherwise have invented — that
  `budgetsApplyToThisMachine` documents itself as a hardware-class check and the
  new bullet gave `VESTIGE_QUALITY_PRESET` two meanings in one file, and that
  the Debug/software-renderer guards' placement relative to the tier assertion
  was unstated, one choice being permanently red under llvmpipe and the other
  unreachable. Lane A alone found that the 0.2 ms subtrahend was itself an
  RX 6600 figure with no stated tier scope.
- **Loop 2** (2 fresh lanes, cold, briefed identically — no prior-loop list):
  **Q1 3 · Q2 2 · Q3 3 · Q4 0 — 8 verified, 8 fixed, 0 dismissed. Cap reached;
  the run files nothing further and exits.** **Six of the eight landed in text
  loop 1 added**, the pattern this log has recorded on every prior run. The best
  finding moved the headline number: research § 7 publishes **three** always-on
  analytic rows (`< 0.05` distance, `< 0.1` height, `< 0.1` inscatter = 0.25 ms),
  and § 8's table had merged distance and height into one row carrying the
  *height* row's budget — under-counting by 0.05 ms since before this amendment.
  The honest subtraction is **2.0 − 0.25 = 1.75 ms**, which takes Medium's
  predicted margin from 3% to 0.6% and changes the section's own verdict from
  "fits, barely" to "expected to go red". The deepest finding was lane B's: the
  fix for loop 1's combine-draw finding said "BOTH draws scale" and still left
  the gather's **source** textures at 1080p — and this pass is
  texture-bandwidth-bound on exactly those, so a 633×356 gather sampling 1080p
  sources carries a 2.3× working set, the same inflation loop 1's fix had just
  forbidden. It also makes the 1.74 ms prediction weaker than stated, since the
  3.1 ms measurement was taken with unscaled sources. Both lanes converged on the
  `Custom` row promising a median while its *Timed at* cell said "nothing" — three
  implementations fit, and only `renderScale` 1.0 keeps the reported median
  comparable with the 0.69 ms and 3.1 ms figures the project sets budgets from.
  Three more: loop 1's "the god-ray gate stops using `budgetsApplyToThisMachine`"
  left the row labelled *RX 6600 reference* asserting on any box declaring High
  (fixed by adding the tier branch **ahead** of that predicate rather than
  replacing it); "the one preset that never runs this pass" is two, since Ultra
  shares High's `heavyPost = true` row, and a literal reading routes Ultra to the
  looser budget; and the claim that the dev rig and CI are "where the 1.2 ms
  assertion actually fires" is false in both halves — CI is llvmpipe and
  `local-ci.sh` exports `LIBGL_ALWAYS_SOFTWARE=1` unconditionally, so that gate
  fires only on a hand-run `ctest`. Lane A also found the § 8 cost model carried
  no resolution term while the section proposes a quarter-res gather as its own
  remedy, so that change would have moved the cost without moving the budget.

### Amendment 2026-08-21 (3D_E-0616 — god-ray benchmark + § 8 budget correction) — cold-eyes loops

§ 8's god-ray budget row was corrected and a GPU benchmark added for the pass
(`GodRayPassUnderBudget`). Gated under CLAUDE.md rule 14 because the row is what
a perf gate is written against, so a conformer would now write a different
number. Genre pinned `spec` (cap 2). Deterministic layer: `doc_integrity`, clean
on every run — it covers anchors, links, TOC, heading sequence and tool grants,
and does **not** cover quoted-fragment staleness, cited-symbol existence or
census counts, which the lanes carried.

- **Loop 1** (2 fresh lanes, cold): **Q1 5 · Q2 3 · Q3 0 · Q4 0 — 8 verified, 8
  fixed, 0 dismissed.** **Both lanes independently found the same two**, which is
  the run's strongest signal. (a) § Scope, § 0 and § 3 listed 11.5, 11.8, 11.10
  and 11.11 as remaining work when **all four had shipped** — an implementer
  following § 3's order would have built a *second* screen-space god-ray pass,
  i.e. the double shafts § 5.6's gate exists to prevent. Fixed in seven places,
  including ✅ on §§ 6, 11 and 12's headings, which §§ 4 and 5 already carried.
  (b) § 12 presented `GodRayParams` / `setGodRayParams` / `getGodRayParams` as new
  API when `renderer.h:130-131` and `:666` already ship them, and pointed at a
  `GOD_RAYS_EDGE_MARGIN` constant that no longer exists. Also fixed: § 5.6 and
  § 10 both described the gate as the **two-term** `volumetricFogEnabled &&
  isInitialized()` form — precisely the defect 3D_E-0617 fixed — and § 5.6 called
  the preset→quality mapping "future" when `settings_apply.cpp` ships it; § 5.6
  site (3) claimed `safeDefaults()` may leave god rays on and that they self-gate,
  when `post_process_accessibility.cpp:41` sets them **off** and volumetric is off
  there too, so nothing self-gates (an accessibility claim, not a cosmetic one);
  § 6.3's "byte-for-byte" parity, which is false once a volume animates through
  `fbm3`; and § 11.3's `windVelocity` "world m/s" comment, when the shipped
  formula adds wind in *noise* space.
- **Loop 2** (2 fresh lanes, cold, briefed identically — no prior-loop list):
  **Q1 2 · Q2 4 · Q3 1 · Q4 1 — 8 verified, 8 fixed, 0 dismissed.** Both lanes
  again converged on two: § 1 still read "Goals (remaining work)" and listed the
  three bullets as unbuilt (loop 1 fixed § Scope and § 0 and never touched § 1),
  and § 8's 11.5 bullet listed "god rays off equivalence" as a test that § 5.7
  says deliberately is not one. The run's best single finding was against **loop
  1's own new text**: § 8 claimed the benchmark would catch a full-res gather, and
  it would not — the test hard-codes `1920/2 × 1080/2` as a copy of
  `godRaysConfig` rather than reading it, the same two-copy drift shape as
  3D_E-0617 one layer up. Narrowed in § 8 and in the test header rather than left
  implied. Also fixed: § 11.8 put the reduce-motion `windVelocity` zeroing inside
  `applyFogAccessibilitySettings`, when `renderer.cpp:1370` does it at the
  per-frame build site and that struct carries no `FogNoiseParams` to zero; § 1's
  accessibility-toggle goal contradicted § 10's "gated in the renderer"; § 8
  assigned "over-cap drop" to a pure function that takes one `FogVolume` and
  cannot express it; § 4.1 attached ≈ 14 MB to a single RGBA16F volume, which is
  7.4 MB (the core allocates two); and § 11.3 never said which side clamps
  octaves.
- **Cap reached (2, spec) — filed the tail and shipped, no loop 3.** A moderate
  cap rather than a calm or a violent one: **3 of loop 2's 8 findings landed on
  text this run wrote**, checked against loop 1's fixes rather than recalled.
  Per rule 14 a spec at its cap ships and lets the build be the third reviewer.

**Deferred tail — surfaced, not fixed, because all three are code and this was a
document review.** (1) The **octave clamp is CPU-only**: `volumetric_fog.cpp`
clamps `1..5`, the GLSL twin and the upload do not, so § 11.9's parity tolerance
holds only inside that range. (2) **Texture unit 17 is double-booked in the
shipped code** — `SHProbeGrid::FIRST_TEXTURE_UNIT = 17` with
`SH_TEXTURE_COUNT = 7` claims 17–23, and `renderer.cpp:1561` binds the froxel
volume to 17. § 5.4 records both and is **accurate**; whether the two are ever
live in one draw was not established here. (3) The benchmark's **gather
resolution copy** is ungated; pinning it needs the renderer to expose the
`godRaysConfig` derivation.
