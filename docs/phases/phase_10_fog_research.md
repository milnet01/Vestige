# Phase 10 — Fog & Atmospheric Scattering Research

> **Pairs with `phase_10_fog_design.md`.** This document is the research that informed the design choices; the design doc is the actual implementation plan. Read both together when working on fog, or read just the design doc if you only need the chosen approach. Phase 15 atmospheric rendering is the future-rendering home for the volumetric / weather upgrades; this Phase 10 work is the production fog primitives the engine ships today.

**Scope:** Inform Phase 10 design decisions for distance fog, exponential height fog, froxel-based volumetric fog, and god-ray / crepuscular-ray techniques in the Vestige engine (C++17, OpenGL 4.5, deferred pipeline, 60 FPS floor on an AMD RX 6600 / RDNA2-class GPU).

**Method:** Primary-source review. Where a primary source could not be retrieved verbatim (binary PDFs the fetch tool rejected, paywalled docs), the citation is still anchored to the primary artefact and only supplemented with a secondary explanatory source.

---

## 1. Canonical Distance Fog (Linear, EXP, EXP2)

These are the three textbook fog modes inherited from the fixed-function era (OpenGL 1.x, Direct3D 7–9) and still used as the cheap fallback everywhere.

**Formulas (fog factor `f` in `[0, 1]`, `d` = view-space distance to fragment):**

    linear :  f = clamp( (end - d) / (end - start),  0, 1 )
    exp    :  f = exp(  -density * d )
    exp2   :  f = exp( -(density * d)^2 )

**Composition (blend toward fog colour as `f -> 0`):**

    C_out = f * C_fragment + (1 - f) * C_fog

Direct3D 9 documents these three forms identically under `D3DFOG_LINEAR`, `D3DFOG_EXP`, `D3DFOG_EXP2`, with the same blend equation ([Microsoft, 2018](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas)). The OpenGL fixed-function pipeline used the same three modes, selected by `glFogi(GL_FOG_MODE, ...)`; modern GL programs replicate the math in the fragment shader.

**Pipeline placement — per-vertex vs per-pixel:** Fixed-function GL computed `f` per-vertex and interpolated it (cheap but visibly wrong on large triangles). All modern engines evaluate fog *per-pixel* in a fragment shader, using linearised depth reconstructed from the depth buffer.

**Scene shader vs composite pass:** In a deferred pipeline, fog is almost always applied in a *screen-space composite pass* after the lighting resolve, because (a) the G-buffer already gives you depth and (b) skybox fragments need the same treatment as opaques. For Vestige this means `assets/shaders/screen_quad.frag.glsl` is the natural home, reading the scene HDR target and the depth target.

**HDR composition order — the important bit:** Fog must run in *linear HDR* space *before* tonemap, because (a) fog colour is a radiance value that needs to add/replace radiance in the same units as the rest of the scene, and (b) tonemapping is a non-linear curve that will crush fog into banded artefacts if applied first. For bloom, the canonical order is **fog -> bloom -> exposure -> tonemap -> LUT/grade -> accessibility -> gamma**: bloom samples fogged radiance (so a bright fog haze blooms correctly), then the combined HDR image is exposed and tonemapped. The EA/Frostbite and Unreal deferred pipelines both follow this ordering; see the HDR-pipeline discussion in [Kosmonaut, 2017](https://kosmonautblog.wordpress.com/2017/03/26/designing-a-linear-hdr-pipeline/) for the general rationale (fog omitted from his diagram but the "stay linear until tonemap" principle is explicit).

**Citations:**
- [Microsoft Learn: "Fog Formulas (Direct3D 9)", 2018](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas) — the three canonical formulas and the `C = f*C_src + (1-f)*C_fog` blend, stated verbatim. Matters because it is the most accessible primary-source statement of the math; the OpenGL 1.x spec gives the same equations, and every modern engine's "classic fog" path reduces to these.
- [Kosmonaut, "Designing A Linear HDR Rendering Pipeline", 2017](https://kosmonautblog.wordpress.com/2017/03/26/designing-a-linear-hdr-pipeline/) — explains why everything that contributes radiance must run before tonemap.

**Formula Workbench applicability:** None. Linear / EXP / EXP2 have no fittable coefficients; `density` and `start/end` are artist-authored, not curve-fitted.

---

## 2. Exponential Height Fog (Unreal Engine pattern)

Height fog makes density a function of world-space altitude `y`, so valleys look soupy and mountaintops stay clear. Unreal's "Exponential Height Fog" actor is the canonical artist-facing reference.

**Density model (Iñigo Quílez's derivation, widely adopted):**

    d(y) = a * exp( -b * (y - fogHeight) )

`a` is the ground-level density; `b` is `FogHeightFalloff` (larger `b` = tighter vertical transition). The view-ray integral from camera position `ro` in direction `rd` over distance `t` is *analytically integrable*:

    fogAmount(t) = (a / b) * exp( -b * (ro.y - fogHeight) )
                  * ( 1 - exp( -b * rd.y * t ) ) / rd.y

    f_transmit   = exp( -fogAmount(t) )
    C_out        = f_transmit * C_scene + (1 - f_transmit) * C_inscatter

`rd.y == 0` needs a degenerate-case branch (`fogAmount = a * t * exp(-b*(ro.y - fogHeight))`). Because the integral is closed form there is no ray-march, so cost is essentially one `exp`, one divide, one add — per-pixel, single-pass ([Quílez, 2010](https://iquilezles.org/articles/fog/)).

**Unreal parameter surface (5.x actor):**
`FogDensity` (= `a`), `FogHeightFalloff` (= `b`), `FogInscatteringColor`, `FogMaxOpacity`, `StartDistance`, `FogCutoffDistance`, a *second fog layer* with its own density/falloff/height, `DirectionalInscatteringColor` + `DirectionalInscatteringExponent` + `DirectionalInscatteringStartDistance` (a sun-direction Henyey-Greenstein-ish lobe that brightens fog toward the sun), and the `VolumetricFog` sub-checkbox that promotes it to a full froxel-based solution (section 3). The documentation does not publish the exact closed-form; it is substantially the Quílez integral plus a per-layer sum and a sun-lobe add. The UE5 actor docs describe each parameter qualitatively ([Epic Games, "Exponential Height Fog", UE 5.x docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine)). HDRP uses the equivalent parameterisation but in "mean free path / attenuation distance" units: at `attenuationDistance` metres the fog has absorbed/out-scattered 63% (= `1 - 1/e`) of light, and at `maximumHeight` vertical distance the density has decayed to `1/e` of ground level ([Unity, "Fog Override" HDRP docs](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Fog.html)).

**Citations:**
- [Quílez, "Better Fog / Colored Fog", 2010](https://iquilezles.org/articles/fog/) — derives the analytic line integral for `d(y) = a*exp(-b*y)`. This is the only reference Vestige actually needs to reproduce the math; UE and HDRP are layered artist UIs on top of the same closed form.
- [Epic Games, "Exponential Height Fog in Unreal Engine", UE 5.x documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine) — canonical parameter list (second layer, directional inscatter, start/cutoff distances, max opacity).
- [Wenzel, "Real-Time Atmospheric Effects in Games", SIGGRAPH 2006 course](https://advances.realtimerendering.com/s2006/Chapter6-Real-time%20Atmospheric%20Effects%20in%20Games.pdf) — Crytek's earlier industrial treatment of height fog + atmospheric scattering, predates UE's version and informed it.

**Formula Workbench applicability:** `a`, `b`, `fogHeight` are textbook parameters the artist wants to dial live — no reference data to fit against. The `DirectionalInscatteringExponent` (a cosine-lobe exponent) is an aesthetic parameter, also not fitted. Nothing in §2 goes through the Workbench.

---

## 3. Volumetric Fog (Froxel-Based, Compute-Shader)

The state of the art is Wronski's *Assassin's Creed 4* technique (SIGGRAPH 2014) and Hillaire's *Frostbite* refinement (SIGGRAPH 2015). Both Unity HDRP and Godot 4's volumetric fog are direct descendants.

**Algorithmic shape (four-pass):**

1. **Density / material injection.** A compute shader writes per-froxel `(scattering, extinction, emissive, phase_g)` into a view-frustum-aligned 3D texture (the *V-buffer* / "froxels" = frustum voxels). Dimensions are screen-tile × depth-slice; depth is distributed *non-linearly* (usually an exponential / reciprocal mapping) so that near-camera froxels are small and far froxels are large.
2. **Light scattering.** A second compute pass evaluates inscattering per-froxel: for each light, `L_scatter = sigma_s * visibility(froxel, light) * phase(cos_theta, g) * L_light`, plus an ambient / probe term. Shadow maps are sampled per-froxel.
3. **Temporal integration (reprojection).** Jittered Halton samples across frames are blended with the previous frame's reprojected froxel (Hillaire uses ~5% new, 95% history via an exponential moving average).
4. **Ray-march accumulation.** A third compute pass integrates scattering and transmittance along the view ray through the froxel stack: at each slice `i`,

       T_i = T_{i-1} * exp( -sigma_t_i * ds_i )     // Beer-Lambert
       S_i = S_{i-1} + T_{i-1} * (1 - exp(-sigma_t_i*ds_i)) * L_scatter_i / sigma_t_i

    producing a final 3D texture where each froxel stores `(rgb = inscattered radiance so far, a = transmittance so far)`. The composite pass samples this texture at each opaque pixel's froxel coordinate and does `C_out = T * C_scene + S` ([Hillaire, 2015, slide "Ray-marching integration"](https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934); [Wronski, 2014](https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf)).

**Froxel-grid dimensions seen in the wild:**
- **Frostbite default:** `8×8` screen tiles with `64` depth slices (so at 1080p that's `240 × 135 × 64`). Adjustable to `4×4` or `16×16` tiles. PS4 cost at 900p: **~2.95 ms total** (0.45 material + 2.00 light scattering + 0.40 final accumulation + 0.10 reprojection) ([Hillaire, 2015](https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934)).
- **Unity HDRP default (1080p):** `240 × 135 × 64` (exactly Frostbite's 8×8 tiling) ([Unity, HDRP Fog docs](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Fog.html)).
- **Godot 4 default:** `64 × 64 × 64`, cited explicitly as "based on Bart Wronski's 2014 SIGGRAPH presentation" ([Godot, "Volumetric fog and fog volumes"](https://docs.godotengine.org/en/stable/tutorials/3d/volumetric_fog.html)).

**Henyey-Greenstein phase function** (the *one* fittable curve in the whole pipeline):

    p(cos_theta, g) = (1 - g^2) / ( 4 * pi * (1 + g^2 - 2*g*cos_theta)^(3/2) )

with `g ∈ (-1, 1)`: `g = 0` isotropic, `g > 0` forward (atmospheric haze ~0.7–0.8, clouds ~0.85–0.99), `g < 0` back-scatter ([Henyey-Greenstein phase function, Wikipedia](https://en.wikipedia.org/wiki/Henyey%E2%80%93Greenstein_phase_function), citing Henyey & Greenstein, *ApJ* 93, 1941). The HG form is already analytic — there is nothing to "fit" — but the cheaper **Schlick approximation** substitutes a polynomial `k = 1.55*g - 0.55*g^3` and evaluates `p ≈ (1 - k^2) / (4*pi*(1 - k*cos_theta)^2)`, trading a small angular error for removing the `^(3/2)` `pow`. That *coefficient-vs-error trade-off* is a legitimate Formula Workbench target: fit the Schlick-style rational to HG over `g ∈ [0.1, 0.95]`, `cos_theta ∈ [-1, 1]`, minimising max-abs angular error, then export the fitted coefficients as GLSL constants.

**Minimal viable implementation for Vestige Phase 10 (MVP):**
A single `160 × 90 × 64` froxel grid (= 921,600 froxels, ~14 MB at RGBA16F), three compute dispatches (inject / scatter / integrate), no temporal reprojection initially, Schlick phase function, and a single directional light with CSM shadow sampling per froxel. On RDNA2 (RX 6600 is ~5–6× PS4 throughput) this should land ~1–1.5 ms — comfortably within a 16.6 ms / 60 FPS budget. Temporal reprojection can be added as a quality toggle; it halves effective sample count (so halves cost) at the price of ghosting.

**Citations:**
- [Wronski, "Volumetric Fog: Unified Compute Shader-Based Solution to Atmospheric Scattering", SIGGRAPH 2014 Advances in Real-Time Rendering](https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf) — the foundational paper. Introduces the frustum-aligned 3D texture, compute-shader passes, temporal reprojection, and the AC4 cost numbers.
- [Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015 Advances in Real-Time Rendering](https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite) (slides mirrored at [SlideShare](https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934)) — adds the unified material V-buffer, the four-pass structure as documented above, Halton-jitter temporal integration, and concrete PS4 timings (2.95 ms total at 900p, 8×8×64).
- [Henyey & Greenstein, "Diffuse radiation in the Galaxy", *Astrophysical Journal* 93, 1941](https://en.wikipedia.org/wiki/Henyey%E2%80%93Greenstein_phase_function) — the original phase function; Wikipedia gives the modern normalised form used above.
- [Godot Engine, "Volumetric fog and fog volumes"](https://docs.godotengine.org/en/stable/tutorials/3d/volumetric_fog.html) — open-source reference implementation explicitly based on Wronski 2014; useful for reading real shader code.

**Formula Workbench applicability:** **Yes, for the Schlick phase-function approximation.** Vestige should fit a small rational approximation to HG over the working range of `g` and export the coefficients. Everything else (froxel dimensions, slice distribution exponents, temporal blend factor) is an artist-tuned knob with no reference data.

---

## 4. God Rays / Crepuscular Rays

Two coexisting techniques in modern engines:

**(a) Screen-space radial blur (Kenny Mitchell, GPU Gems 3 ch. 13).** Project the sun onto screen space; ray-march `N` samples along the screen-space vector from each pixel toward the sun position, accumulating attenuated samples from an occlusion mask (the scene rendered with sky = white, everything else = black):

    for i in 0..N:
        sample_pos  = pixel_pos + delta * i
        c          += occlusion(sample_pos) * decay^i * weight
    C_out = C_scene + exposure * c

Parameters: `exposure`, `decay ∈ [0,1]`, `density` (step length scale), `weight`, `NUM_SAMPLES` (typically 64–128). Single post-process pass, fixed cost per pixel regardless of scene complexity ([Mitchell, 2008](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process)). Fails gracefully when the sun is off-screen (just produces no rays) and is cheap (~0.3 ms at 1080p on RDNA2 extrapolating from Mitchell's "minimal overhead" characterisation and the fact that it is one texture fetch plus one multiply per sample).

**(b) Integration with volumetric fog.** Once you have a froxel grid (section 3), god rays come *for free*: shadow-mapped inscattering through participating media produces genuine light shafts. This is how UE's Volumetric Fog, HDRP's volumetric lighting, and Godot 4's volumetric fog all do it — no separate god-ray pass exists. The cost is already paid for by the volumetric-fog system.

**Trade-off for Vestige:** If Phase 10 ships volumetric fog, god rays are a byproduct and (a) is redundant. If Phase 10 ships only distance/height fog, (a) is a cheap bolt-on that gives the visual payoff without froxel-grid complexity; worth keeping as a fallback quality setting for low-end hardware where volumetric fog is disabled.

**Citations:**
- [Mitchell, "Volumetric Light Scattering as a Post-Process", *GPU Gems 3* ch. 13, NVIDIA 2008](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process) — the canonical cheap god-ray technique; the formula given is Equation 4 in the chapter.
- [Hillaire, 2015](https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite) — demonstrates god rays emerging from shadowed volumetric inscattering with no extra pass.

**Formula Workbench applicability:** None. Mitchell's `decay`, `density`, `weight` are aesthetic knobs, not fitted.

---

## 5. Integration With Deferred Rendering

Where the major engines inject fog in their deferred pipelines:

| Engine | Height/distance fog | Volumetric fog | Relative to tonemap | Relative to bloom | Skybox fogged? |
|---|---|---|---|---|---|
| Unreal 5 | Composite pass after lighting, before transparency composite | Separate froxel system, sampled by both opaque and transparent passes | Before tonemap (HDR) | Before bloom | Yes, via `FogMaxOpacity` and distance |
| Unity HDRP | "Fog" override, composite after opaque lighting | Froxel grid `240×135×64`, sampled in composite | Before tonemap (HDR) | Before bloom | Yes, "Sky Color" fog mode blends with skybox |
| Godot 4 | Fog shader callback in scene composite | Froxel grid `64×64×64`, compute-shader atomics | Before tonemap | Before bloom | Yes |

Across all three, the invariants are:
- **Fog is linear-HDR.** Applied on scene HDR radiance, before tonemap.
- **Fog is before bloom.** Bloom should sample fogged radiance so that bright fog haze blooms.
- **Fog is before DOF / motion blur.** DOF samples *the final visible colour* including fog; motion blur smears the post-fog image.
- **Fog is after opaque lighting, before/during transparency.** Transparent surfaces usually sample the same fog function per-vertex (or per-pixel with a cheaper formulation) to composite correctly.
- **Skybox is fogged.** Otherwise you get a sharp "fog horizon" where world geometry fades but sky doesn't — visually disastrous on hills and mountains. Both UE and HDRP clamp fog on the sky by `FogMaxOpacity` / `MaxFogDistance` so the sky doesn't completely vanish.

For Vestige, this means extending `screen_quad.frag.glsl` to evaluate fog *before* bloom is added and before exposure/tonemap. The existing ordering (SSAO -> contact shadows -> bloom-add -> exposure -> tonemap -> LUT -> accessibility -> gamma) becomes **SSAO -> contact shadows -> fog -> bloom-add -> exposure -> tonemap -> LUT -> accessibility -> gamma**. Volumetric fog (if shipped) writes its froxel-integrated `(inscatter, transmittance)` texture *before* the screen quad runs; the screen quad then samples it at each pixel's linearised depth and replaces the height/distance-fog term.

**Citations:**
- [Unity HDRP, "Forward and Deferred Rendering"](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Forward-And-Deferred-Rendering.html) — pipeline stage ordering.
- [Lagarde & de Rousiers, "Moving Frostbite to Physically Based Rendering", SIGGRAPH 2014 / 2018 HDRP talk](https://advances.realtimerendering.com/s2018/Siggraph%202018%20HDRP%20talk_with%20notes.pdf) — explicit HDR pipeline order including fog placement.

---

## 6. Accessibility

Major engines do *not* publish fog-specific accessibility guidance. The relevant standards:

- **[Game Accessibility Guidelines](https://gameaccessibilityguidelines.com/full-list/)** — under "Vision / Intermediate": *"Provide an option to turn off / hide background movement."* Volumetric fog with temporal reprojection is background movement; this guideline directly applies. Under "Vision / Basic": *"Avoid VR simulation sickness triggers"* — moving fog can be one.
- **[WCAG 2.2 SC 2.3.1 Three Flashes](https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html)** and **[SC 2.3.3 Animation from Interactions](https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html)** — web-standard but apply by analogy: volumetric fog with high-frequency flicker from under-sampled shadow light shafts can trigger photosensitivity; vestibular users can experience nausea from dense swirling fog under camera motion.
- **[Xbox Accessibility Guideline 117 (motion)](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117)** and **[118 (photosensitivity)](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/118)** — require ability to reduce camera shake, motion blur, and flashing. Dense volumetric fog with rapid directional changes can contribute to both triggers.

**Concrete requirements for Vestige's `PostProcessAccessibilitySettings`:**

- `fogEnabled: bool` — hard off switch.
- `fogIntensityScale: float in [0, 1]` — scales `FogDensity` and inscatter contribution (so a low-vision user can see at distance without disabling the look entirely).
- `volumetricFogEnabled: bool` — independent toggle. Temporal reprojection should be *off* whenever this is off (no hidden flicker).
- `reduceMotionFog: bool` — when true, disable temporal reprojection (eliminates shimmer) *and* clamp the god-ray / directional-inscatter sun-lobe intensity to prevent flashing as the camera pans past the sun.
- All four must be live-tunable; no restart / reload.

These mirror the existing `depthOfFieldEnabled` / `motionBlurEnabled` toggles. Photosensitivity-safe clamps already in the engine should extend to `FogInscatteringColor` luminance (cap at a user-configurable nits ceiling).

**Citations:**
- [Game Accessibility Guidelines, "Full List"](https://gameaccessibilityguidelines.com/full-list/)
- [W3C WAI, "Understanding SC 2.3.3: Animation from Interactions"](https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html)
- [Microsoft, "Xbox Accessibility Guideline 118: Photosensitivity"](https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/118)

---

## 7. Performance Targets (RDNA2 / RX 6600)

RX 6600 at 1080p has roughly 5–6× the raw compute throughput of a base PS4 (1.8 TFLOPs vs 8.9 TFLOPs FP32, cache differences aside). Using Hillaire's published PS4 figures as a floor and scaling conservatively:

| Technique | Per-frame GPU cost (1080p, RDNA2 estimate) | Notes |
|---|---|---|
| Linear / EXP / EXP2 distance fog | **< 0.05 ms** | One `exp`, one mad in screen-quad fragment shader. Effectively free. |
| Exponential height fog (analytic) | **< 0.1 ms** | One `exp` + one divide per pixel. |
| Directional-inscatter sun lobe | **< 0.1 ms** | One `pow` per pixel. |
| Screen-space god rays (Mitchell) | **0.3–0.6 ms** | 64–128 samples × one tap each; scales linearly with sample count. |
| Volumetric fog, `160×90×64`, no temporal | **~1.2 ms** | Extrapolating Hillaire 2.95 ms / PS4 at 900p with one light + CSM. |
| Volumetric fog, `240×135×64`, + Halton temporal reprojection | **~1.8–2.4 ms** | Matches HDRP default quality. |
| Volumetric fog, `240×135×128` high-quality | **~3.5–4.5 ms** | Eats >20% of a 16.6 ms budget — reserve for "Ultra" preset only. |

**What makes volumetric fog expensive:**
1. **Sample count per froxel** — light-scattering pass evaluates every light per froxel; cost scales with `froxels × lights × shadow_taps`.
2. **3D-texture bandwidth** — a `240×135×64` RGBA16F is ~33 MB; every integration pass touches it twice.
3. **Shadow-map sampling per froxel** — the real bottleneck. Frostbite's 2.0 ms "light scattering" line is dominated by cascade shadow lookups at 64 depth slices × 32,400 tiles.
4. **Temporal accumulation** — cuts effective sample count and so cost, at the cost of ~1 frame of ghosting on fast-moving geometry.

**Budget allocation for Vestige:** Phase 10 targets a combined fog cost of **≤ 2.0 ms** on RX 6600 at 1080p at the "High" preset (distance + height + volumetric, no temporal), leaving >14 ms for the rest of the frame.

**Citations:**
- [Hillaire, 2015 — Frostbite PS4 timings](https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934) (2.95 ms total, 8×8×64, 900p, PS4).
- [Unity HDRP docs — froxel grid defaults and quality-vs-cost guidance](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Fog.html).

---

## 8. Formula Workbench Applicability Summary

Per CLAUDE.md Rule 11, only formulas with fittable coefficients should go through the Workbench. The survey:

| Technique | Formula | Fittable? | Through Workbench? |
|---|---|---|---|
| Linear fog | `f = (end-d)/(end-start)` | No (artist-authored range) | No |
| EXP fog | `f = exp(-density*d)` | No | No |
| EXP2 fog | `f = exp(-(density*d)^2)` | No | No |
| Exponential height fog | `fog = (a/b)*exp(-b*(ro.y-h0))*(1-exp(-b*rd.y*t))/rd.y` | No (closed-form analytic) | No |
| Directional inscatter (cosine lobe) | `I = pow(max(cos_theta,0), exponent)` | No (aesthetic) | No |
| Volumetric fog integration | `T_i = T_{i-1}*exp(-sigma_t*ds)` | No (Beer-Lambert) | No |
| **Henyey-Greenstein phase** | `p(θ,g) = (1-g²)/(4π(1+g²-2g·cosθ)^1.5)` | Exact — not fitted | No |
| **Schlick approximation to HG** | `p ≈ (1-k²)/(4π(1-k·cosθ)²)`, `k = f(g)` | **Yes** | **Yes** — fit `k(g)` rational to HG over `g ∈ [0.1, 0.95]`, `cosθ ∈ [-1, 1]`; export GLSL |
| God-ray decay | `decay^i` per sample | No (aesthetic) | No |

**One Workbench deliverable.** The Schlick `k(g)` fit (currently the textbook `k = 1.55*g - 0.55*g^3`, a cubic with unclear error bound against the true HG over the range we care about) is the single formula in Phase 10 worth routing through the Workbench. Load HG as the reference curve over a dense `(g, cos θ)` grid, fit a rational form with Levenberg-Marquardt, record max-abs error and R², export as `volumetric_phase_schlick.glsl`. This is a small but real performance win (replace `pow(..., 1.5)` with a single reciprocal) and lines up with Rule 11's "performance optimisation via Workbench-fit approximation" clause.

Every other Phase 10 constant (froxel dimensions, temporal blend, `FogDensity`, `FogHeightFalloff`, `FogMaxOpacity`, `decay/density/weight` for god rays) is artist-tuned and stays out of the Workbench, with `// TODO: revisit via Formula Workbench once reference data is available` comments only where the engine eventually acquires measured atmospheric data (e.g. matching a photo reference scene).

---

## Citation List (consolidated)

**Primary sources:**

1. Wronski, B. "Volumetric Fog: Unified Compute Shader-Based Solution to Atmospheric Scattering." *SIGGRAPH 2014 Advances in Real-Time Rendering in Games course.* <https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf>
2. Hillaire, S. "Physically Based and Unified Volumetric Rendering in Frostbite." *SIGGRAPH 2015 Advances in Real-Time Rendering in Games course.* EA Frostbite: <https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite>. Slides: <https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934>
3. Mitchell, K. "Volumetric Light Scattering as a Post-Process." *GPU Gems 3*, Chapter 13. NVIDIA, 2008. <https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process>
4. Wenzel, C. "Real-Time Atmospheric Effects in Games." *SIGGRAPH 2006 course chapter.* <https://advances.realtimerendering.com/s2006/Chapter6-Real-time%20Atmospheric%20Effects%20in%20Games.pdf>
5. Microsoft. "Fog Formulas (Direct3D 9)." Win32 documentation, revision 2018. <https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas>
6. Quílez, I. "Better Fog / Colored Fog." 2010. <https://iquilezles.org/articles/fog/>
7. Henyey, L. & Greenstein, J. "Diffuse radiation in the Galaxy." *Astrophysical Journal* 93, 1941, pp. 70–83. Summary and modern form: <https://en.wikipedia.org/wiki/Henyey%E2%80%93Greenstein_phase_function>
8. Lagarde, S. et al. "HDRP: Render Pipeline." *SIGGRAPH 2018 talk.* <https://advances.realtimerendering.com/s2018/Siggraph%202018%20HDRP%20talk_with%20notes.pdf>

**Engine documentation (primary, living):**

9. Epic Games. "Exponential Height Fog in Unreal Engine." UE 5.x documentation. <https://dev.epicgames.com/documentation/en-us/unreal-engine/exponential-height-fog-in-unreal-engine>
10. Epic Games. "Volumetric Fog in Unreal Engine." UE 5.x documentation. <https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine>
11. Unity Technologies. "Fog Override", High Definition Render Pipeline docs. <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Fog.html>
12. Unity Technologies. "Local Volumetric Fog", HDRP docs. <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Local-Volumetric-Fog.html>
13. Godot Engine. "Volumetric fog and fog volumes." <https://docs.godotengine.org/en/stable/tutorials/3d/volumetric_fog.html>
14. Godot Engine. "Fog Volumes arrive in Godot 4.0." <https://godotengine.org/article/fog-volumes-arrive-in-godot-4/>

**Accessibility standards:**

15. Game Accessibility Guidelines. Full list. <https://gameaccessibilityguidelines.com/full-list/>
16. W3C WAI. "Understanding Success Criterion 2.3.3: Animation from Interactions (WCAG 2.2)." <https://www.w3.org/WAI/WCAG22/Understanding/animation-from-interactions.html>
17. W3C WAI. "Understanding Success Criterion 2.3.1: Three Flashes or Below Threshold (WCAG 2.2)." <https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html>
18. Microsoft. "Xbox Accessibility Guideline 117 (motion)." <https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117>
19. Microsoft. "Xbox Accessibility Guideline 118 (photosensitivity)." <https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/118>

**Supporting:**

20. Kosmonaut. "Designing A Linear HDR Rendering Pipeline." 2017. <https://kosmonautblog.wordpress.com/2017/03/26/designing-a-linear-hdr-pipeline/>
