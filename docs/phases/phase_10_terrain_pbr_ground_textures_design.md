# Phase 10 — Meadow Realism A: PBR Ground Textures on Terrain

**Roadmap:** 3D_E-0031 (Meadow realism A). Fixture/benchmark: 3D_E-0027 meadow.
**Status:** SIGNED OFF (2026-07-11); **approach revised 2026-07-17** — the §4.4
roughness→specular Workbench fit was verified infeasible and replaced with the
engine's real Cook-Torrance GGX (§13, decision + re-run cold-eyes). A1 + A2
(part 1, textured path) shipped; A2 (part 2, real GGX specular) in progress.
**Author:** in-session 2026-07-11 (user-requested realism overhaul).

**Contents:** [1 Goal](#1-goal) · [2 Research](#2-research-summary-what-current-practice-recommends) · [3 Current state](#3-current-state-verified-against-source) · [4 Architecture](#4-architecture) · [5 CPU/GPU placement](#5-cpu--gpu-placement-project-rule-7) · [6 Performance](#6-performance-60-fps-is-a-hard-requirement) · [7 Testing](#7-testing) · [8 Assets & licensing](#8-assets--licensing) · [9 Implementation slices](#9-implementation-slices) · [10 Accessibility](#10-accessibility) · [11 Risks](#11-risks--mitigations) · [12 Open questions](#12-open-questions-for-review) · [13 Cold-eyes log](#13-cold-eyes-loop-log)

---

## 1. Goal

Replace the terrain fragment shader's **flat-colour placeholder** — four constant
colours blended by splatmap weight, carrying the literal
`// Simple base colors for each layer (will be replaced with texture arrays later)`
comment (`assets/shaders/terrain.frag.glsl:172`) — with **real tiled PBR ground
materials** (grass / rock / dirt / sand: albedo + normal + roughness, with a
height channel for blending).

The flat, untextured ground is the single largest reason outdoor scenes read
"cartoony" — it fills most of the frame with one flat green. This is the biggest
single realism win, and it is **reusable across every outdoor scene** (the
biblical courtyards, not just the meadow), so it belongs in the terrain renderer,
not the meadow scene.

### 1.1 Non-goals (this phase)

- Grass billboards (→ 3D_E-0032, phase B), trees (→ 3D_E-0033), sky/water polish
  (→ 3D_E-0034). This phase is **ground only**.
- Runtime terrain-material editing UI. The material set is authored in code /
  scene-setup for now; an editor panel is a later enhancement.
- Virtual texturing / megatextures. Out of scope; a texture-array of a small
  fixed layer count is sufficient and portable.
- Parallax occlusion mapping (POM). The height channel is used for **blending**
  this phase; POM displacement is a later, optional enhancement.

---

## 2. Research summary (what current practice recommends)

Terrain texturing is a well-trodden area; the current-practice consensus:

1. **Multi-layer splatting → PBR.** Extend classic texture splatting (a weight
   per layer) so each layer carries a full PBR set (albedo + normal + roughness),
   blended in one shader pass. Minimal overhead vs colour-only splatting because
   the blend is already per-pixel. (Grokipedia "Texture splatting"; Unity
   TerrainLayer model.)

2. **Height/depth-based blending beats linear alpha.** Linear weight blending
   gives unnatural, even cross-fades ("soiling"). Mishkinis's *Advanced Terrain
   Texture Splatting* replaces it with a depth-aware blend using each layer's
   height channel, so e.g. sand fills the cracks between cobbles instead of
   evenly coating them. Canonical formula (two-layer form):

   ```
   ma = max(h1 + w1, h2 + w2) - depth      // depth ≈ 0.2 blend band
   b1 = max(h1 + w1 - ma, 0)
   b2 = max(h2 + w2 - ma, 0)
   rgb = (c1*b1 + c2*b2) / (b1 + b2)
   ```

   Generalises to N layers: `ma = max_i(h_i + w_i) - depth`,
   `b_i = max(h_i + w_i - ma, 0)`, normalise by `Σ b_i`. Cheap (a few ALU ops),
   no extra texture reads beyond the height channel we already pack.

3. **Triplanar for slopes; Whiteout for its normals.** World-XZ (top-down)
   projection stretches on steep faces. Triplanar projects on X/Y/Z and blends by
   the geometric normal. For triplanar *normal maps*, Ben Golus's comparison lands
   on **Whiteout blend** as the desktop sweet spot (RNM is best-quality but
   costlier; UDN flattens past 45°). The engine's terrain shader already has a
   triplanar path (`u_triplanarEnabled/Sharpness/Start/End`) for the flat-colour
   version (with `u_textureTiling` as the world-space UV scale) — we extend it to
   textures.

4. **Break tiling repetition.** Two cheap, composable techniques, both from the
   UE/landscape playbook: **distance-based tiling** (sample the albedo at two
   scales — normal near, ~4× larger far — and lerp by view distance) and **macro
   variation** (multiply by a large-scale low-frequency variation so big patches
   read differently). Stochastic **hex-tiling** (Heitz–Neyret, Mikkelsen's
   contrast-ramp adaptation) fully removes repetition but costs ~3× samples per
   map — **deferred** to a follow-up; Phase A uses distance-tiling + macro
   variation which are near-free.

5. **Storage: 2D texture arrays.** For a fixed small layer count, a
   `GL_TEXTURE_2D_ARRAY` per map type (albedo / normal / material) is the
   portable choice — core in GL 4.5, one sampler + layer index, higher-perf than
   3D textures (no cross-slice filtering), no extension. Bindless textures are a
   3rd option but remain a vendor extension (not core GL) and buy little here.

6. **Channel packing (ORM/ARM → our AO/Rough/Height).** PBR greyscale maps pack
   into one RGB texture — one sample instead of three. The industry precedents are
   **ORM** (glTF/Unreal: R=Occlusion, G=Roughness, B=Metallic) and Poly Haven's
   **ARM** (same channels). Ground is non-metallic, so we do **not** consume the
   pre-packed ARM directly — we **self-repack at asset-prep** from the source
   AO / roughness / displacement maps into our own **R=AO, G=Roughness, B=Height**
   layout (the height-blend of §2 item 2 needs a height channel, which ARM's
   metallic slot doesn't carry). Assets are **CC0** from ambientCG / Poly Haven
   (no attribution required; credited as courtesy).

**Sources:**
- [Advanced Terrain Texture Splatting — Mishkinis (Game Developer)](https://www.gamedeveloper.com/programming/advanced-terrain-texture-splatting)
- [Texture splatting — Grokipedia](https://grokipedia.com/page/Texture_splatting) / [Wikipedia](https://en.wikipedia.org/wiki/Texture_splatting)
- [Normal Mapping for a Triplanar Shader — Ben Golus](https://bgolus.medium.com/normal-mapping-for-a-triplanar-shader-10bf39dca05a)
- [Blending in Detail (RNM) — self-shadow](https://blog.selfshadow.com/publications/blending-in-detail/)
- [Fixing Landscape Texture Tiling (macro/distance variation) — World of Level Design](https://www.worldofleveldesign.com/categories/ue4/landscape-macro-tiling-variation.php) / [80.lv](https://80.lv/articles/tutorial-fixing-landscape-texture-tiling-in-ue4)
- [Stochastic Hex-Tiling (Mikkelsen adaptation)](https://godotshaders.com/shader/stochastic-hex-tiling-mikkelsens-adaptation/) / [mmikk/hextile-demo](https://github.com/mmikk/hextile-demo)
- [Texture Array Terrain Sample — NVIDIA](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/texturearrayterrainsample.htm) / [Bindless Textures — ktstephano](https://ktstephano.github.io/rendering/opengl/bindless)
- [Poly Haven texture standards (ARM packing)](https://docs.polyhaven.com/en/technical-standards/textures) / [ambientCG](https://ambientcg.com/)

---

## 3. Current state (verified against source)

- `assets/shaders/terrain.frag.glsl` — samples `u_normalMap` (macro terrain
  surface normal, RGB16), `u_splatmap` (RGBA weights: **R=grass G=rock B=dirt
  A=sand**), then blends four hard-coded `vec3` colours by weight. Has a working
  triplanar branch for steep slopes over the flat colours, plus `tilingDetail()`
  (value-noise brightness modulation 0.88–1.12) and caustics. Blinn-Phong direct
  + `u_ambientColor` ambient + CSM shadows. (Read 2026-07-11.)
- `engine/renderer/terrain_renderer.cpp::render()` — binds heightmap(unit 0),
  normalMap(1), splatmap(2), CSM(3), caustics(5); sets triplanar uniforms
  (`u_textureTiling=0.1`, sharpness 6, start 0.4, end 0.7). Units 4, 6, 7, 8+ are
  free. (Read 2026-07-11.)
- `Terrain` owns heightmap/normal/splat GL textures + `generateAutoTexture`
  (writes the RGBA splat weights by slope/altitude). Layer semantics fixed at
  grass/rock/dirt/sand. (Verified.)
- **Back-compat constraint:** scenes without a ground-material set (Tabernacle,
  material demo) must keep rendering via the existing flat-colour path unchanged.

---

## 4. Architecture

### 4.1 `TerrainMaterialSet` (new — `engine/environment/terrain_material_set.{h,cpp}`)

A CPU-side description + GPU resources for the ground layers.

```cpp
struct TerrainLayerDesc            // one layer (grass / rock / dirt / sand)
{
    std::string albedoPath;        // sRGB
    std::string normalPath;        // linear, tangent-space (+Z up)
    std::string materialPath;      // linear, packed R=AO G=Roughness B=Height
    float       tiling = 0.15f;    // world-units → UV scale (per layer)
};

class TerrainMaterialSet
{
public:
    bool load(const std::array<TerrainLayerDesc, 4>& layers);  // → GL arrays
    void bind(int albedoUnit, int normalUnit, int materialUnit) const;
    bool isValid() const;          // false → renderer uses flat-colour fallback
    const std::array<float, 4>& tilings() const;
    // GL_TEXTURE_2D_ARRAY handles: albedo (SRGB8_ALPHA8), normal (RGB8),
    // material (RGB8). 4 layers. Mipmapped, anisotropic, wrap = REPEAT.
private:
    GLuint m_albedoArray = 0, m_normalArray = 0, m_materialArray = 0;
    std::array<float, 4> m_tilings{};
    bool m_valid = false;
};
```

- All four layers must share dimensions (array requirement). Loader validates and
  fails soft (`m_valid=false`) → fallback, logged once. Reuses the existing
  `stb_image` **decode** path (`texture.cpp`), but builds the
  `GL_TEXTURE_2D_ARRAY` directly (via `glTextureStorage3D` + per-layer
  `glTextureSubImage3D`) — so the array does **not** inherit `texture.cpp`'s
  per-texture filter/anisotropy setup and the loader must set them **explicitly**:
  `GL_LINEAR_MIPMAP_LINEAR` min / `GL_LINEAR` mag, `GL_TEXTURE_MAX_ANISOTROPY`
  (already used unguarded elsewhere in the engine — inherits that GL-4.6-core
  assumption, no new risk), `REPEAT` wrap, then `glGenerateTextureMipmap`.
  `GL_UNPACK_ALIGNMENT=1` around every layer upload (§4.2).
- Owned by `TerrainRenderer` (rendering concern), set by the scene:
  `terrainRenderer.setGroundMaterials(std::move(set))`. Null/invalid = fallback.

### 4.2 GPU resources

| Array (`GL_TEXTURE_2D_ARRAY`) | Format | Layers | Notes |
|---|---|---|---|
| albedo   | `GL_SRGB8_ALPHA8` | 4 | A unused (reserved) |
| normal   | `GL_RGB8` (linear) | 4 | tangent-space, +Z up |
| material | `GL_RGB8` (linear) | 4 | R=AO, G=Roughness, B=Height |

**Why RGB8 detail normals are fine here** (the macro normal is deliberately
`GL_RGB16` — `terrain.cpp:390` notes 8-bit gave "only ~6 codes"). That lesson is
about a *single* normal spanning the whole 256 m terrain, where 8-bit quantises
the gentle large-scale slope into visible terraces. The layer normals here are
**tiled, high-frequency tangent-space** maps (many repeats across the terrain) —
the standard 8-bit normal-map case, where per-texel precision is ample and the
terracing failure mode doesn't arise. (The B=Height channel is likewise 8-bit
~51 levels inside the 0.2 blend band — watch for stair-stepping during A2's
visual check; bump to a 16-bit material array only if it shows.)

Bound at units **6/7/8** (4 free; leaves 5=caustics). Trilinear + anisotropic
filtering, `REPEAT` wrap, full mip chain (`glGenerateTextureMipmap`).

**RGB8 alignment (project banding lesson).** The normal/material arrays are
3-component (`GL_RGB8`). The recorded engine lesson (`GL_UNPACK_ALIGNMENT`
banding — a 3-component upload at a non-4-aligned row width shears into diagonal
bands) applies. Two guarantees make RGB8 safe here and are **mandatory** in the
loader: (a) committed textures are power-of-two width (1K → 1024×3 = 3072 B/row,
already 4-aligned), and (b) the loader sets `glPixelStorei(GL_UNPACK_ALIGNMENT,
1)` around every array upload regardless (belt-and-suspenders for any future
non-PoT texture). This settles the RGB8-vs-RGBA-padding choice outright — we
commit to RGB8 + alignment-1, not RGBA padding.

### 4.3 Shader changes (`terrain.frag.glsl`)

New uniforms: `sampler2DArray u_albedoArray/u_normalArray/u_materialArray`,
`float u_layerTiling[4]`, `bool u_useGroundTextures`, `vec2 u_distanceTiling`
(the `(nearDist, farDist)` smoothstep range in world metres — the far-scale
*multiplier* is the `FAR_TILING_SCALE` constant, not a uniform). When
`u_useGroundTextures` is false → **existing flat-colour path unchanged**
(fallback / Tabernacle).

Per-pixel algorithm when textures are on:

1. **UVs.** `uv_i = worldPos.xz * u_layerTiling[i]`. For the distance-tiling
   break-up, also `uv_far_i = uv_i * FAR_TILING_SCALE` (FAR_TILING_SCALE ≈ 0.25,
   i.e. ~4× larger tiles far away — an art-directed constant carrying a
   `// TODO: revisit via Formula Workbench` per §4.4 item 2); lerp the **albedo**
   sample only (not material — cheaper, and the visual repetition is carried by
   albedo) between near and far by `smoothstep` over view distance
   (`u_distanceTiling`).
2. **Sample** albedo, normal, material (AO/Rough/Height) for the 4 layers.
3. **Height-blend** by splat weight (Mishkinis, §2 item 2): compute per-layer
   `hw_i = height_i + splat_i`, `ma = max_i(hw_i) - DEPTH` (DEPTH≈0.2),
   `b_i = max(hw_i - ma, 0)`, `w_i = b_i / Σb_i`. Blend albedo, roughness, AO,
   and the detail normal by `w_i`.
4. **Slopes (triplanar).** Reuse the existing steepness → `triBlend` logic. Where
   `triBlend>0`, sample the layers triplanar (X/Y/Z world planes) and blend the
   three projections by geometric-normal weights; blend triplanar normals with
   **Whiteout** (Ben Golus, §2 item 3). `mix(topDown, triplanar, triBlend)`.
5. **Detail normal → world (construct the TBN — the terrain has none).** The
   terrain mesh carries no tangent attribute and the fragment shader receives no
   `v_tangent`/`v_normal` (only `v_terrainUV`, `v_worldPos`, `v_viewDepth`; the
   surface normal is read from `u_normalMap`) — so the frame must be **built in
   the shader**. For the **top-down** projection the UVs are world-XZ, which gives
   a world-aligned tangent frame: `T = (1,0,0)`, `B = (0,0,1)`, geometric `N` =
   the macro normal from `u_normalMap`. (This triad is exactly orthonormal only
   when `N` is vertical; on gentle non-vertical slopes still below the triplanar
   threshold `N` tilts slightly and `T·N ≠ 0` — the small residual skew is
   accepted on meadow-grade terrain.) Apply the blended tangent-space detail
   normal onto `N` with **Whiteout** (perturb along `T`/`B`, keep `N`'s sign) to
   get the world detail normal. For **slope/triplanar** pixels use the
   per-axis triplanar frames of Ben Golus's Whiteout method (§2 item 3), not this
   single frame. The resulting world normal (macro + detail) replaces the
   macro-only normal in lighting.
6. **Shade.** Feed blended albedo + world normal + roughness + AO into the
   lighting. The **textured** path replaces its interim roughness-driven
   Blinn-Phong specular (shipped in A2 part 1) with the **real Cook-Torrance GGX**
   term (§4.4 item 1), driven by the blended roughness (dielectric `F0 = 0.04`,
   `metallic = 0`);
   diffuse stays Lambert, ambient + CSM unchanged. AO multiplies ambient.
   `tilingDetail()` brightness stays as a subtle macro-variation multiplier. The
   flat-colour fallback branch keeps its existing Blinn-Phong specular
   byte-identical (Tabernacle/demo unchanged).

`heightBlendWeights()` is factored as a **pure function** mirrored on CPU
(`terrain_material_blend.h`) for a parity test (§7, project Rule 7).

### 4.4 Specular model & numerical constants (project Rule 6)

Two numerical pieces here deserve a note on their provenance under project Rule 6
— one is the canonical BRDF (no fit), the other is hand-authored art constants:

1. **Roughness → specular: use the real Cook-Torrance GGX BRDF (no fit).** The
   terrain shader was Blinn-Phong (`pow(NdotH, s) * k`), and an earlier draft of
   this design fitted a `roughness → (shininess, scale)` Blinn-Phong mapping in the
   Workbench against a GGX reference. **That approach was verified infeasible and
   dropped (decision 2026-07-17, §13):** Blinn-Phong's `cos^n` lobe cannot
   reproduce GGX's wide, soft shoulder, so even the *theoretical best-case*
   per-roughness fit (fitting shininess + scale freely at each roughness) misses
   the normalised GGX reference by **max-err ≈ 0.245 at roughness 0.8** — squarely
   inside the terrain's own roughness range (the ground layers are rough:
   grass/dirt/rock/sand ≈ 0.6–1.0) — versus the **≤ 0.05** the parity test
   required. No `(shininess, scale)` Blinn-Phong mapping can meet that bar; it is a
   model-capability limit, not a fit-quality one (verified numerically before
   implementation).

   Instead, the textured ground path shades with the **real Cook-Torrance GGX
   specular** the rest of the engine already uses — the `distributionGGX`,
   `geometrySmith`, and `fresnelSchlick` helpers are lifted verbatim from
   `scene.frag.glsl`, together with their dependencies (`geometrySmith` calls
   `geometrySchlickGGX`, and `distributionGGX` needs the `PI` constant — neither
   yet present in `terrain.frag.glsl`), driven by the blended `groundRoughness`
   (dielectric `F0 = 0.04`, `metallic = 0`). This makes "match GGX" **exact by
   construction**: the ground
   lights with the same BRDF as every other surface, there is no approximation to
   fit or validate, and no runtime-cost concern (one directional light's
   Cook-Torrance term is trivial on the RX 6600 — §6). The flat-colour fallback
   branch keeps its `pow(NdotH,64)*0.15` byte-identical. There is therefore **no
   Workbench fit and no second dual-impl** in this phase; the specular is a direct
   reuse of the canonical BRDF, pinned by a consistency test (§7) rather than a
   fit-accuracy test.

2. **Art-directed constants** — the height-blend band `DEPTH` (~0.2), the
   distance-tiling `FAR_TILING_SCALE` (~0.25), and the near/far transition range.
   These have **no reference dataset** (pure look tuning), so per Rule 6 they are
   hand-authored named constants carrying a `// TODO: revisit via Formula
   Workbench` comment at the definition site — the legitimate hand-code path when
   no data exists to fit against. If a look-target dataset is later captured, the
   transition curve becomes a Workbench curve.

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Texture-array assembly, mip/aniso setup, image decode | **CPU**, one-time init | I/O + allocation; not per-frame. |
| Splatmap weight generation (`generateAutoTexture`) | **CPU**, one-time | Already CPU; sparse/decision logic. |
| Per-pixel layer sample + height-blend + normal blend + triplanar | **GPU** (fragment) | Per-pixel, data-parallel, no branching decisions the CPU could hoist. |
| Height-blend math (parity reference) | **CPU** mirror for test only | Dual-impl parity (Rule 7); runtime is GPU. |
| Cook-Torrance GGX specular (§4.4 item 1) | **GPU** (fragment) | Direct reuse of `scene.frag.glsl`'s GGX helpers; per-pixel BRDF eval. No CPU mirror — it is the canonical BRDF, not a fitted approximation. |

No "CPU now, move later" — the runtime path is GPU from the start. The one CPU
mirror (the height-blend pure function) exists solely to lock its formula with a
parity test (§7); the GGX specular needs no mirror — it is the engine's canonical
BRDF, shared verbatim with `scene.frag.glsl`.

---

## 6. Performance (60 FPS is a hard requirement)

Worst-case per-pixel cost (top-down, textures on): 4 layers × 3 maps = 12 array
samples, **+4** for the distance-tiling second albedo sample (albedo only, one
extra tap per layer — §4.3 point 1), + height-blend ALU. On steep triplanar
pixels, ×3 for the extra projections — but triplanar only kicks in above the
steepness threshold (`triBlend>0`), which is a small fraction of a gentle meadow.

**Budget & mitigations:**

- **Frame target:** the full meadow (this + grass + props + water + CSM) holds
  **≥60 FPS at 1080p on the RX 6600** dev GPU (16.6 ms frame).
- **Per-pass target:** the terrain opaque pass costs **≤ 3.0 ms** of that 16.6 ms
  frame at High (its share of the budget); the before/after comparison is against
  this ceiling, not just "before/after".
- **How it's measured (today):** the automated `--profile-log` CSV gate does
  **not exist yet** — the flag parses into `EngineConfig::profileLogPath` (with
  unit tests) but has **no consumer**: there is no `profile_log.{h,cpp}` and
  `engine.cpp` never reads the field. That logger is meadow slice **S6** of
  3D_E-0027, and the CSV *parsing/regression* gate is a separate future item
  (3D_E-0030). So Phase A's perf check is a **manual maintainer read** of the
  editor **Performance panel** (registered in the `PanelRegistry`, drawn in the
  editor dockspace; its GPU tab lists per-pass `GpuTimer` →
  `GpuPassTiming{name,timeMs}` and FPS from the live `PerformanceProfiler`). The
  terrain cost is the **named `beginPass("Terrain")` GPU scope** (`engine.cpp`),
  read on the 3D_E-0027 meadow against the ≤3.0 ms ceiling; FPS against ≥60. Once
  S6 lands, the same numbers come from the CSV. *(Note: do **not** rely on F12 to
  open the panel — F12 is bound to toggle-fullscreen (`engine.cpp:392`,
  `README.md`); the panel's own `shortcut()=="F12"` is unwired display metadata, a
  code/code collision to reconcile separately.)*
- The **≤ 3.0 ms** terrain-pass figure is an **initial target** (roughly a fifth
  of the 16.6 ms frame, leaving room for grass + water + shadows + props + post),
  not a derived bound. A5 records the **current flat-colour terrain-pass cost as
  the baseline** at first measurement, then refines the ceiling against it.
- **Quality tiers — a graphics setting, NOT `FormulaQualityManager`.** These tiers
  *toggle shader features* (drop the distance-tiling second sample, drop detail
  normals, disable triplanar textures), which is a different axis from
  `FormulaQualityManager`'s `FULL / APPROXIMATE / LUT` (that enum selects a
  *formula's evaluation accuracy* — not a fit for feature gating, and this phase
  has no fitted formula to accuracy-gate anyway). The feature tiers are a global
  **graphics-quality `Setting`** (High/Medium/Low) driving shader `#define`s /
  a uniform:
  - **High** — all layers, distance-tiling + macro variation, detail normals,
    height-blend.
  - **Medium** — drop the distance-tiling second sample; keep detail normals +
    height-blend.
  - **Low** — albedo + AO only (no detail normal), linear weight blend, no
    triplanar textures (flat-colour on slopes). Roughly the current cost.
- Mip + anisotropy are mandatory (minification without mips is both ugly *and*
  slow via cache thrash).
- **Tier selection is a manual graphics `Setting`** (default **High**). There is
  **no automatic GPU-capability detection or frame-time watchdog this phase**.
  Auto-detecting a lower GPU and dropping a tier by default is explicitly
  **future work** (it also relates to the shipped-game graphics-settings menu,
  3D_E-0035), not part of Phase A. If a later phase adds such an automatic clamp,
  it is logged per project Rule 5.

---

## 7. Testing

- **Height-blend parity (unit).** `terrain_material_blend.h` pure
  `heightBlendWeights(std::array<float,4> h, std::array<float,4> w, float depth)`;
  test that it matches the GLSL formula on hand-picked cases (single dominant
  layer → weight 1; equal heights+weights → equal split; a high-height low-weight
  layer still peeks through), and that weights sum to 1 and are non-negative.
  **Divisor safety.** The normalisation divisor is `Σb_i`; because
  `ma = max_i(h_i+w_i) − depth`, the peak layer has `b_peak = depth`, so
  `Σb_i ≥ depth`. Therefore **any `depth > 0` guarantees a positive divisor** — no
  runtime epsilon is needed, because `DEPTH` is a positive constant (~0.2; valid
  authored range ~[0.05, 0.5]). The parity test asserts safety at the **smallest
  supported depth (0.05)**, not at `depth = 0`. `depth = 0` is **out of the valid
  range** (there every `b_i = 0` → `Σb = 0`, a genuine 0/0); the function's
  contract is `depth > 0`, and a debug assert guards it — the test verifies that
  assert fires, rather than expecting a defined blend at zero.
- **GGX specular consistency (unit).** The terrain shader now shades with the
  canonical Cook-Torrance GGX (§4.4 item 1), not a fitted approximation, so the
  test pins **reuse**, not fit accuracy: a unit test evaluates the GGX NDF
  transcribed into `terrain.frag.glsl` (`distributionGGX`) against the engine's
  reference GGX — the `ggx_distribution` Formula-Library definition
  (`physics_templates.cpp`), whose `source` field annotates it as *matching*
  `scene.frag.glsl`'s helper — over an NdotH × roughness grid. The reference is
  parameterised by the coefficient **`alpha = roughness²`** (not roughness
  directly), so the test drives it with `alpha = r*r` at each grid roughness `r`
  and keeps `r ≥ 0.04` to respect the shader helper's `roughness = max(roughness,
  0.04)` clamp (the FormulaDefinition has none); with those two substitutions the
  closed forms are identical and must agree to floating-point tolerance. That is
  the Rule-7 lock that stops the terrain copy from silently drifting from the
  shared BRDF. **Objective compile check:** the shader links and a frame renders
  **GL-error-free** (`glGetError`).
- **Material-set load (unit).** Two failure modes, both → `isValid()==false`
  (fallback), not a crash: (a) mismatched layer dimensions, and (b) a
  missing/corrupt file (stb_image decode returns null — a distinct code path from
  the dimension check). A valid 4-layer set → three non-zero array handles.
- **Fallback (unit/behavioural).** With no material set, the terrain still renders
  (flat-colour path) — `u_useGroundTextures=false`. Tabernacle/material-demo
  unaffected.
- **Visual (`--visual-test`) — maintainer inspection, not an automated assert.**
  There is no golden-image diff in-tree; `--visual-test` captures viewpoint
  screenshots (pond_overview / pond_shore / open_meadow) that the **maintainer
  eyeballs**: ground reads as textured grass (not flat mint), no diagonal banding
  (RGB8 arrays uploaded with `GL_UNPACK_ALIGNMENT=1` and PoT width — §4.2), no
  stretching on the pond-bowl banks.
- **Performance — manual read, not an automated gate.** See §6 for the method
  (Performance-panel `Terrain` GPU scope ≤ 3.0 ms, FPS ≥ 60 at High on the RX 6600;
  automated CSV gate is future work — 3D_E-0030).

---

## 8. Assets & licensing

- **Source:** ambientCG / Poly Haven, **CC0** (no attribution required; credited
  as courtesy in `THIRD_PARTY_NOTICES.md`, rows in `ASSET_LICENSES.md`).
- **Layers:** grass (lush lawn), rock/cliff, dirt/soil, sand — each albedo +
  normal + a repacked **R=AO G=Roughness B=Height** material map, generated at
  asset-prep from the source `_diff` / `_nor_gl` / `_ao` / `_rough` / `_disp`
  maps.
- **Resolution: 1K** committed (albedo as JPG, normal + material as PNG); ~4
  layers × ~1.2 MB ≈ **~5 MB** total — above the `ASSET_LICENSES.md` soft >1 MB
  line but consistent with tracked precedent (the 2K brick/plank sets + the 1K
  HDRI). **Meadow design §6 carries the full >1 MB-precedent rationale — not
  repeated here.** Terrain delta: per the repo's externalization direction (4K
  originals live in the `VestigeAssets` side-repo, `ASSET_LICENSES.md:126`),
  higher-res terrain variants stay out and drop in via a git-ignored override
  (mirrors `nature_local/`).
- Committed under `assets/textures/terrain/`. `copy_assets` globs `assets/` so no
  CMake change (same as the meadow model/HDRI subdirs).

---

## 9. Implementation slices

1. **A1 — `TerrainMaterialSet`** + GL array loader + parity-tested
   `heightBlendWeights` pure header. *Verify:* unit tests green (load, fallback,
   blend parity).
2. **A2 — Shader top-down path + real GGX specular.** Add array samplers +
   uniforms; textured top-down albedo/roughness/AO with height-blend; keep
   flat-colour fallback branch (**part 1, shipped**). Replace the interim
   Blinn-Phong specular with the **canonical Cook-Torrance GGX** (§4.4 item 1):
   lift `distributionGGX` / `geometrySmith` / `fresnelSchlick` (plus their deps
   `geometrySchlickGGX` + the `PI` constant) from `scene.frag.glsl`, drive by
   blended roughness (dielectric F0=0.04) (**part 2**).
   *Verify:* GGX-consistency test green (§7) + GL-error-free frame; maintainer
   inspection of `--visual-test` — meadow ground reads as textured grass with a
   soft roughness-driven sheen, Tabernacle unchanged.
3. **A3 — Detail normals + triplanar (Whiteout).** Detail normal → world via the
   shader-built TBN (§4.3 point 5); triplanar textures on slopes. *Verify:*
   **objective** — shader compiles + links and a frame renders with **zero GL
   errors** (`glGetError`), and a unit test on the extracted pure
   `whiteoutBlend(macroN, detailN)` helper makes a **directional** assertion (a
   detail normal tilted +X raises the blended world normal's X vs the macro
   normal; a −X tilt lowers it — so a wrong-axis or flipped-sign blend fails, not
   merely "differs"), and the result is unit-length; **plus**
   maintainer inspection of `--visual-test` (bumps light correctly; no stretching
   on the pond-bowl banks).
4. **A4 — Tiling break-up.** Distance-tiling (albedo, §4.3 point 1) + macro
   variation. *Verify:* **objective** — a unit test on the pure distance-blend
   helper asserts the near/far mix follows the `smoothstep` over the
   `u_distanceTiling` range (near→far endpoints + a midpoint); **plus** maintainer
   inspection of `open_meadow` (no obvious repetition across the 256 m field).
5. **A5 — Assets + wire-up + quality tiers + perf.** Commit the CC0 layer set;
   the meadow sets it; graphics-`Setting` tier plumbing (default High). **Fixture
   precondition:** A5's perf/visual gates run on the 3D_E-0027 meadow, which
   already renders today (slices S1–S5 shipped) — only its S6 CSV logger is
   pending, which is why the perf read is manual (§6). *Verify:* maintainer reads
   the Performance panel's `Terrain` GPU scope on the meadow — terrain pass
   ≤ 3.0 ms and ≥ 60 FPS at High on RX 6600 (automated CSV path waits on S6 — §6);
   CHANGELOG + ASSET_LICENSES + THIRD_PARTY_NOTICES rows added.

Each slice is committed locally; the phase pushes when A5 lands green (§6 push
cadence — public repo, batch push).

---

## 10. Accessibility

- Ground detail must not rely on hue alone for legibility (colour-blind); value
  contrast between layers (grass vs dirt vs rock) carries the read. No flashing /
  motion — static ground.
- The Low quality tier doubles as the low-end-GPU / reduced-detail accessibility
  path; exposed in settings alongside the existing quality controls.

---

## 11. Risks & mitigations

- **VRAM / repo size** from the arrays → 1K, packed material map, single set.
- **Tiling still visible** despite distance+macro → hex-tiling is the escalation
  (deferred, noted §2 item 4); design keeps the sample sites isolated so it can
  slot in.
- **Perf regression** → manual quality tiers (default High) + the Performance-panel
  perf read (§6); no automatic clamp this phase, so no Rule-5 clamp-logging obligation
  yet — Rule 5 applies if a future phase adds an auto-tier-drop.
- **Back-compat break** for flat-colour scenes → explicit `u_useGroundTextures`
  branch + fallback tests.

---

## 12. Open questions for review

- Layer count fixed at 4 (matches splatmap RGBA) — sufficient for the meadow;
  revisit only if a scene needs >4 ground materials (would need a second
  splatmap or an index-map approach).
- Height source: use the source `_disp` map where available, else derive height
  from albedo luminance at asset-prep. (Confirm per-layer during A5.)

---

## 13. Cold-eyes loop log

_(Findings + resolutions per loop. Design is not implemented until a pass
converges with no substantive findings.)_

**Loop 1 (2026-07-11)** — 3 independent cold reviewers (accuracy/conflicts;
completeness/testability; project-rule compliance). Tally (deduped):
CRITICAL 0 · HIGH 3 · MEDIUM 6 · LOW 6 · INFO 3 (all actionable verified against
source; all fixed). All load-bearing code citations verified correct.

- **Workbench roughness→specular fit had no slice owner + no parity test, and §5
  denied a second dual-impl** (all 3 reviewers) → assigned to slice A2 (§9), added
  a GGX-parity test (§7, R²≥0.99 / max-err≤0.05), listed as the 2nd dual-impl (§5).
- **60 FPS "assert" implied an automated gate that doesn't exist** — the
  `--profile-log` CSV logger is meadow S6 (unbuilt; only `profileLogPath` field),
  the regression gate is 3D_E-0030 → rewrote §6/§7/§9 to a manual F12
  Performance-panel read; added a **≤3.0 ms terrain-pass** ceiling.
- **Auto quality-tier fallback ("dropped on lower GPUs") had no mechanism** →
  stated tier is a manual setting (default High); auto-detection is future work.
- **`§2.2/§2.3/§2.4` phantom cross-refs** (§2 is a flat list) → "§2 item N".
- **RGB8 array banding risk vs project lesson** → §4.2 commits RGB8 + mandatory
  `GL_UNPACK_ALIGNMENT=1` + PoT-width guarantee; reconciled the §7 hedge.
- **Sample-count "+4" arithmetic / far-scale magic `0.25`** → distance-tiling is
  albedo-only (+4); named `FAR_TILING_SCALE` with a Rule-6 TODO.
- **"2K HDRI" precedent wrong** → cite 2K brick/plank + the 1K HDRI (~1.5 MB);
  de-duped the asset-precedent prose against meadow design §6.
- Also: added a TOC; quoted the terrain TODO verbatim; added the height-blend
  `depth→0` degenerate test + Σb≥depth>0 invariant; marked all `--visual-test`
  checks as maintainer inspection (no golden-image diff in-tree); trimmed §2 item
  6 to note self-repacking.
- **Surfaced to maintainer (code-side, not doc fixes):** `terrain.h:129`
  doc-comment says the normal map is "RGB8" but `terrain.cpp:403` creates it
  `GL_RGB16` — the design's §3 "RGB16" is correct; the header comment is the stale
  one (fix during implementation).
- INFO (not actioned): height-source `_disp`-vs-luminance choice (owned by A5).

**Loop 2 (2026-07-11)** — 3 independent cold reviewers (same lanes, not briefed
on loop-1 fixes). Tally (deduped): CRITICAL 0 · HIGH 2 · MEDIUM 5 · LOW 6 ·
INFO 3. Loop-1 fixes all held (no regressions raised); new findings:

- **`FormulaQualityManager` reuse doesn't fit** (all 3 reviewers) — its enum is
  `FULL/APPROXIMATE/LUT` (per-formula *accuracy*), not a render-feature toggle →
  the High/Med/Low feature tiers are now a global graphics `Setting` (shader
  `#define`s), NOT FQM; only the §4.4 roughness formula could live in FQM.
- **Detail-normal step assumed a TBN the terrain mesh lacks** (R1) — the terrain
  has no tangent attribute → §4.3 point 5 now specifies the shader-built
  world-XZ tangent frame (top-down) + per-axis triplanar frames (slopes).
- **`depth→0` test was self-contradictory** (2 reviewers — my loop-1 error) →
  reworded: `depth>0` is the contract (safe divisor `Σb≥depth`), test the smallest
  supported depth (0.05) + assert the debug guard fires at 0, not a defined blend.
- **A3/A4 had eyeball-only verify** (R2) → added objective checks (GL-error-free
  compile + `whiteoutBlend` unit test for A3; distance-blend `smoothstep` unit
  test for A4) alongside the visual inspection.
- LOW: made anisotropy/filter setup explicit in the loader contract (§4.1);
  reworded the `profileLogPath` "unwired field" → "parsed, no consumer"; noted the
  F12/README fullscreen conflict + `Window → Performance` fallback; marked ≤3.0 ms
  as an initial target; trimmed §8 precedent + noted the 4K-externalization trend.
- **Scope decision (user, 2026-07-11):** keep the Workbench GGX roughness→specular
  fit (§4.4 item 1) as the full second dual-impl + GGX parity test — the principled,
  reusable path — over a simpler inline curve. Design unchanged.
- **Surfaced (code-side):** `terrain.h:129` "RGB8" + `engine.h:117-125` present-
  tense `ProfileLog` doc-comments are stale (fix at implementation).

**Loop 3 (2026-07-11) — CONVERGED.** 3 independent cold reviewers. Tally
(deduped): **CRITICAL 0 · HIGH 0** · MEDIUM 5 · LOW 8 · INFO — every finding
**polish/precision**, none structural/mechanical/architectural. Loop-2 fixes all
held (FQM split, TBN construction, depth→0, A3/A4 objective verify — none
resurfaced). Polish applied this pass:

- Perf-verify access reworded — the Performance panel is reached via the
  `PanelRegistry`/dockspace and its named `beginPass("Terrain")` GPU scope, **not**
  F12 (F12 is toggle-fullscreen; the panel's `shortcut()=="F12"` is unwired
  metadata — a code/code collision surfaced for separate reconciliation).
- Added the **decode-failure/missing-file** loader test (distinct from
  dim-mismatch); made the **A3 whiteout test directional** (wrong-axis/flipped
  blend now fails); added a **tangent-frame skew caveat** (top-down frame exact
  only for vertical N); clarified §4.3 pt6 (Blinn-Phong *structure* stays, its
  constants become roughness-driven); reconciled **RGB8 detail vs RGB16 macro**
  normal (tiled high-freq vs whole-terrain precision regimes); added `u_texture
  Tiling` to §2 item 3; noted the **3D_E-0027 fixture precondition** (S1–S5 render
  today) + the flat-colour **baseline** to record at A5; trimmed §8 precedent to a
  cross-ref; fixed the `engine.h:117-125` line range.
- Dismissed as **unverified**: `ASSET_LICENSES.md` "122-124" (direct grep confirms
  the 2K brick/plank rows are 121-123 — kept). Re-raised GGX-fit scope resolved by
  the user decision above (kept).

**Convergence:** a pass with zero structural/mechanical/architectural findings —
only polish, now fixed. **Design signed off for implementation (2026-07-11).**

---

### Approach revision — Blinn-Phong fit → real GGX (2026-07-17)

**Supersedes the loop-2 "Scope decision (user, 2026-07-11)" above** (which kept the
Workbench roughness→specular Blinn-Phong fit as the second dual-impl).

During A2 (part 2) implementation the fit was **verified numerically before coding**
and found infeasible: Blinn-Phong's `k·cos^n` lobe cannot reproduce GGX's wide soft
shoulder. Even the *theoretical-best* per-roughness fit (shininess + scale fit
freely at each roughness against the peak-normalised GGX NDF) gives max-abs-error
**≈ 0.029 @ r=0.4, ≈ 0.069 @ r=0.6, ≈ 0.245 @ r=0.8** — the worst case sits inside
the terrain's own roughness range — versus the fit-era §7 bar of ≤ 0.05. This is a
model-capability limit, so **no** `(shininess, scale)` mapping (however fitted)
could pass the parity test.

**Decision (user, 2026-07-17):** drop the Workbench fit; shade the textured ground
with the engine's **real Cook-Torrance GGX** (reuse `scene.frag.glsl`'s
`distributionGGX` / `geometrySmith` / `fresnelSchlick`). "Match GGX" becomes exact
by construction; the parity test becomes a **consistency** lock (terrain's GGX NDF
vs the `ggx_distribution` reference), not a fit-accuracy lock. §4.3/§4.4/§5/§6/§7/§9
updated accordingly. Re-run through cold-eyes on this revision (log below).

**Cold-eyes re-run — Loop 1 (2026-07-17).** 2 independent cold reviewers
(consistency/stale-refs; doc-vs-code GGX accuracy), not briefed on author intent.
Tally: CRITICAL 0 · HIGH 0 · MEDIUM 6 · LOW 3 · INFO 1 (all verified against
source; all fixed). Core question — does any active section still tell the
implementer to build the dropped Blinn-Phong/Workbench fit — came back **clean**.
Fixes applied:

- **§4.3 pt6 attributed the wrong constant** — said the textured path replaces
  `pow(NdotH,64)*0.15`, but that is the *fallback* branch's specular; the textured
  branch's interim is the A2-part-1 roughness-driven Blinn-Phong → reworded.
- **"Lifted verbatim" understated the dependencies** — `distributionGGX` needs the
  `PI` constant (absent from `terrain.frag.glsl`) and `geometrySmith` calls a 4th
  helper `geometrySchlickGGX` → both now named in §4.4 item 1 + §9 A2.
- **§7 "documented source" overstated** — `physics_templates.cpp`'s `ggx_distribution`
  only carries a one-way `source:"matches scene.frag.glsl distributionGGX()"`
  annotation → reworded to "annotated as matching."
- **§7 test hid the `alpha = roughness²` reparameterization** — the reference
  definition's free knob is coefficient `alpha`, not roughness; test must drive it
  `alpha = r*r` and keep `r ≥ 0.04` for the shader's roughness clamp → both stated.
- **§4.4 intro framing stale** — "these go through the Workbench (author→fit→
  validate→export), not hand-coded constants" contradicted both its own sub-items
  (item 1 = no fit; item 2 = hand-authored) → reframed to "provenance note"; §4.4
  heading retitled "Specular model & numerical constants."
- **§11 stale "F12-panel"** perf read contradicted §6's loop-3 correction (panel via
  PanelRegistry, F12 is toggle-fullscreen) → "Performance-panel."
- LOW: substantiated "r≈0.8 inside the terrain roughness range" (ground layers
  ≈ 0.6–1.0); trimmed the spurious §1.1 from the revision-note's updated-sections
  list. INFO (not actioned): PI literal precision differs (`…59` vs `…f`) but both
  round to the same float32 — no test impact.

**Cold-eyes re-run — Loop 2 (2026-07-17) — CONVERGED.** 2 independent cold
reviewers (same lanes, not briefed on loop-1 fixes). Tally: CRITICAL 0 · HIGH 0 ·
MEDIUM 0 · LOW 2 · INFO 3 — every verified finding **polish**, none
structural/mechanical/architectural. Loop-1 fixes all held (the wrong-constant
attribution, helper deps, `alpha=roughness²` reparameterization, "annotated as
matching", and stale framings did not resurface). Both lanes confirmed clean on
the core question — the GGX sections are accurate against source
(`scene.frag.glsl`/`terrain.frag.glsl`/`physics_templates.cpp`) and no active
section still specifies the dropped fit. Polish applied this pass:

- Reconciled the worst-case figure to **≈ 0.245** in both §4.4 and §13 (was 0.24 /
  0.245); tightened §4.2's cross-ref (the §7 "padded to RGBA" hedge it pointed at
  was already reconciled away) and §13's "the §7 bar of ≤0.05" → "the fit-era §7
  bar" (§7 is now a consistency test, no accuracy bar).
- Surfaced (code-side, for the implementer — not doc fixes): `terrain.frag.glsl:292`
  still comments "Replaced by the Formula-Workbench GGX fit … next A2 step" (stale
  after this revision; the whole interim block is replaced in A2 part 2), and the
  textured branch must add `NdotV`/`HdotV` dot-products + the inline `D*G*F/(4·NdotV·
  NdotL)` assembly (all source vectors already present).

**Convergence:** a pass with zero substantive findings — only polish, now fixed.
**Revision re-signed-off for implementation (2026-07-17).**
