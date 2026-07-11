# Phase 10 — Meadow Realism A: PBR Ground Textures on Terrain

**Roadmap:** 3D_E-0031 (Meadow realism A). Fixture/benchmark: 3D_E-0027 meadow.
**Status:** DRAFT — pending cold-eyes convergence (§13) before implementation.
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
   version — we extend it to textures.

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

Bound at units **6/7/8** (4 free; leaves 5=caustics). Trilinear + anisotropic
filtering, `REPEAT` wrap, full mip chain (`glGenerateTextureMipmap`).

**RGB8 alignment (project banding lesson).** The normal/material arrays are
3-component (`GL_RGB8`). The recorded engine lesson (`GL_UNPACK_ALIGNMENT`
banding — a 3-component upload at a non-4-aligned row width shears into diagonal
bands) applies. Two guarantees make RGB8 safe here and are **mandatory** in the
loader: (a) committed textures are power-of-two width (1K → 1024×3 = 3072 B/row,
already 4-aligned), and (b) the loader sets `glPixelStorei(GL_UNPACK_ALIGNMENT,
1)` around every array upload regardless (belt-and-suspenders for any future
non-PoT texture). This resolves the §7 "or padded to RGBA" hedge — we commit to
RGB8 + alignment-1, not RGBA padding.

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
   a world-aligned tangent frame directly: `T = (1,0,0)`, `B = (0,0,1)`, geometric
   `N` = the macro normal from `u_normalMap`. Apply the blended tangent-space
   detail normal onto `N` with **Whiteout** (perturb along `T`/`B`, keep `N`'s
   sign) to get the world detail normal. For **slope/triplanar** pixels use the
   per-axis triplanar frames of Ben Golus's Whiteout method (§2 item 3), not this
   single frame. The resulting world normal (macro + detail) replaces the
   macro-only normal in lighting.
6. **Shade.** Feed blended albedo + world normal + roughness + AO into the
   existing lighting (Blinn-Phong direct + ambient + CSM). Roughness modulates
   the existing specular term (lower roughness → tighter/stronger highlight); AO
   multiplies ambient. `tilingDetail()` brightness stays as a subtle macro
   variation multiplier.

`heightBlendWeights()` is factored as a **pure function** mirrored on CPU
(`terrain_material_blend.h`) for a parity test (§7, project Rule 7).

### 4.4 Formula Workbench usage (project Rule 6)

Two numerical pieces here are formulas with reference data, not art whims — they
go through `tools/formula_workbench/` (author → fit → validate → export to GLSL),
not hand-coded constants:

1. **Roughness → Blinn-Phong specular (the real Workbench candidate).** The
   terrain shader is Blinn-Phong (`pow(NdotH, s) * k`), but the PBR layers give a
   **roughness** in [0,1]. Rather than guess a `roughness → (shininess s, scale
   k)` mapping, fit it in the Workbench against a **GGX/Cook-Torrance reference**
   (reference dataset = GGX specular lobe sampled over roughness × NdotH; the
   Workbench already ships Fresnel-Schlick and related rendering templates). This
   is exactly Rule 6's endorsed path — a Workbench-fit cheap approximation
   replacing heavier runtime BRDF math — and it makes the terrain specular
   *principled* instead of the current fixed `pow(NdotH,64)*0.15`. Exported as a
   `FormulaDefinition` in the **rendering** category (C++ + GLSL codegen). This is
   a **second dual CPU/GPU implementation** (listed in §5) and is **owned by slice
   A2** (§9); it is pinned by a GGX-reference parity/regression test (§7) before it
   ships.

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
| Roughness → Blinn-Phong specular fit (§4.4 item 1) | **CPU** Workbench fit + GLSL export; **CPU** parity reference | Workbench-authored formula emitted as both C++ and GLSL → dual-impl (Rule 7); runtime is GPU. |

No "CPU now, move later" — the runtime path is GPU from the start. The two CPU
mirrors (height-blend + the roughness→specular fit) exist solely to lock their
formulas with parity tests (§7).

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
  editor **Performance panel** — which already surfaces per-pass GPU-timer ms
  (`GpuTimer` → `GpuPassTiming{name,timeMs}`) and FPS from the live
  `PerformanceProfiler` — on the 3D_E-0027 meadow, comparing the terrain pass ms
  against the ≤3.0 ms ceiling and total FPS against ≥60. (The panel is registered
  under shortcut F12 in the panel registry; note `README.md` still lists F12 as
  "toggle fullscreen" — a pre-existing code/doc conflict to reconcile separately,
  so open it via the `Window → Performance` menu if the shortcut is ambiguous.)
  Once S6 lands, the same numbers come from the CSV.
- The **≤ 3.0 ms** terrain-pass figure is an **initial target** (roughly a fifth
  of the 16.6 ms frame, leaving room for grass + water + shadows + props + post),
  not a derived bound — it is refined against the first measured baseline on the
  meadow.
- **Quality tiers — a graphics setting, NOT `FormulaQualityManager`.** These tiers
  *toggle shader features* (drop the distance-tiling second sample, drop detail
  normals, disable triplanar textures), which is a different axis from
  `FormulaQualityManager`'s `FULL / APPROXIMATE / LUT` (that enum selects a
  *formula's evaluation accuracy* — the right home only for the §4.4 roughness→
  specular formula, not for feature gating). The feature tiers are a global
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
- **Roughness → specular parity (unit).** The Workbench-exported
  `roughness → (shininess, scale)` fit (§4.4 item 1) is checked against its
  GGX/Cook-Torrance reference dataset: the exported C++ form matches the reference
  within tolerance (**R² ≥ 0.99** and **max abs error ≤ 0.05** on the normalised
  specular response over the roughness × NdotH grid), and the C++ and GLSL
  codegen agree — the Rule-7 parity lock before it ships.
- **Material-set load (unit).** Mismatched layer dimensions → `isValid()==false`
  (fallback), not a crash; a valid 4-layer set → three non-zero array handles.
- **Fallback (unit/behavioural).** With no material set, the terrain still renders
  (flat-colour path) — `u_useGroundTextures=false`. Tabernacle/material-demo
  unaffected.
- **Visual (`--visual-test`) — maintainer inspection, not an automated assert.**
  There is no golden-image diff in-tree; `--visual-test` captures viewpoint
  screenshots (pond_overview / pond_shore / open_meadow) that the **maintainer
  eyeballs**: ground reads as textured grass (not flat mint), no diagonal banding
  (RGB8 arrays uploaded with `GL_UNPACK_ALIGNMENT=1` and PoT width — §4.2), no
  stretching on the pond-bowl banks.
- **Performance — manual read, not an automated gate.** As above (§6), the
  automated CSV gate does not exist yet. The maintainer reads the **F12
  Performance panel** on the 3D_E-0027 meadow and checks the terrain pass ms
  against the **≤ 3.0 ms** ceiling and total FPS against **≥ 60** at High on the
  RX 6600. (An automated CSV regression gate is future work: 3D_E-0030.)

---

## 8. Assets & licensing

- **Source:** ambientCG / Poly Haven, **CC0** (no attribution required; credited
  as courtesy in `THIRD_PARTY_NOTICES.md`, rows in `ASSET_LICENSES.md`).
- **Layers:** grass (lush lawn), rock/cliff, dirt/soil, sand — each albedo +
  normal + a repacked **R=AO G=Roughness B=Height** material map, generated at
  asset-prep from the source `_diff` / `_nor_gl` / `_ao` / `_rough` / `_disp`
  maps.
- **Resolution: 1K** committed (albedo as JPG, normal + material as PNG). Est.
  ~4 layers × ~1.2 MB ≈ **~5 MB** total. This sits above the `ASSET_LICENSES.md`
  soft >1 MB "consider the future assets repo" line, but the repo already tracks
  >1 MB CC0 assets under the same guidance — the 2K `red_brick` / `brick_wall` /
  `plank_flooring` sets (`ASSET_LICENSES.md:121-123`) and the 1K
  `syferfontein_0d_clear_1k.hdr` (~1.5 MB) — consistent precedent (meadow design
  §6 has the full rationale). Note the repo's direction is externalization: the
  **4K** originals were already moved to a `VestigeAssets` side-repo to keep
  engine clones small (`ASSET_LICENSES.md:126`), so higher-res terrain variants
  likewise stay out, dropping in via a git-ignored override (mirrors
  `nature_local/`).
- Committed under `assets/textures/terrain/`. `copy_assets` globs `assets/` so no
  CMake change (same as the meadow model/HDRI subdirs).

---

## 9. Implementation slices

1. **A1 — `TerrainMaterialSet`** + GL array loader + parity-tested
   `heightBlendWeights` pure header. *Verify:* unit tests green (load, fallback,
   blend parity).
2. **A2 — Shader top-down path + roughness→specular fit.** Add array samplers +
   uniforms; textured top-down albedo/roughness/AO with height-blend; keep
   flat-colour fallback branch. **Includes the Formula-Workbench roughness →
   Blinn-Phong specular fit** (§4.4 item 1): author/fit/validate/export in the
   Workbench, wire the GLSL into the shader. *Verify:* roughness→specular parity
   test green (§7); maintainer inspection of `--visual-test` — meadow ground reads
   as textured grass, Tabernacle unchanged.
3. **A3 — Detail normals + triplanar (Whiteout).** Detail normal → world via the
   shader-built TBN (§4.3 point 5); triplanar textures on slopes. *Verify:*
   **objective** — shader compiles + links and a frame renders with **zero GL
   errors** (`glGetError`), and a unit test on the extracted pure
   `whiteoutBlend(macroN, detailN)` helper asserts the blended normal differs from
   the macro normal by > ε for a non-flat detail sample and is unit-length; **plus**
   maintainer inspection of `--visual-test` (bumps light correctly; no stretching
   on the pond-bowl banks).
4. **A4 — Tiling break-up.** Distance-tiling (albedo, §4.3 point 1) + macro
   variation. *Verify:* **objective** — a unit test on the pure distance-blend
   helper asserts the near/far mix follows the `smoothstep` over the
   `u_distanceTiling` range (near→far endpoints + a midpoint); **plus** maintainer
   inspection of `open_meadow` (no obvious repetition across the 256 m field).
5. **A5 — Assets + wire-up + quality tiers + perf.** Commit the CC0 layer set;
   the meadow sets it; manual quality-tier plumbing (default High). *Verify:*
   maintainer reads the F12 Performance panel on the 3D_E-0027 meadow — terrain
   pass ≤ 3.0 ms and ≥ 60 FPS at High on RX 6600 (the automated CSV path waits on
   meadow S6 — §6); CHANGELOG + ASSET_LICENSES + THIRD_PARTY_NOTICES rows added.

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
- **Perf regression** → manual quality tiers (default High) + the F12-panel perf
  read (§6); no automatic clamp this phase, so no Rule-5 clamp-logging obligation
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
- **Surfaced to user (scope):** whether the Workbench GGX roughness→specular fit
  (§4.4 item 1) is worth a second dual-impl + parity test for a matte-ground phase
  (Rule 2/9), or a simpler inline curve suffices — pending decision.
- **Surfaced (code-side):** `terrain.h:129` "RGB8" + `engine.h:118-124` present-
  tense `ProfileLog` doc-comments are stale (fix at implementation).

**Loop 3** — pending (after the §4.4 scope decision; cold re-read to confirm
convergence).
