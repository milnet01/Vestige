# Phase 10 — Meadow Realism A: PBR Ground Textures on Terrain

**Roadmap:** 3D_E-0031 (Meadow realism A). Fixture/benchmark: 3D_E-0027 meadow.
**Status:** DRAFT — pending cold-eyes convergence (§13) before implementation.
**Author:** in-session 2026-07-11 (user-requested realism overhaul).

---

## 1. Goal

Replace the terrain fragment shader's **flat-colour placeholder** — four constant
colours blended by splatmap weight, carrying the literal
`// will be replaced with texture arrays later` TODO
(`assets/shaders/terrain.frag.glsl:172`) — with **real tiled PBR ground
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

6. **Channel packing (ORM/ARM).** PBR greyscale maps pack into one RGB texture —
   one sample instead of three. glTF/Unreal use **ORM** (R=Occlusion,
   G=Roughness, B=Metallic); Poly Haven ships **ARM** (same channels, their
   naming). Ground is non-metallic, so we repurpose the metallic slot for the
   **height** channel the height-blend needs: our packed "material" texture is
   **R=AO, G=Roughness, B=Height**. Assets are **CC0** from ambientCG / Poly
   Haven (no attribution required; credited as courtesy).

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
  `stb_image` texture-load path (async upload not required — one-time init).
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

### 4.3 Shader changes (`terrain.frag.glsl`)

New uniforms: `sampler2DArray u_albedoArray/u_normalArray/u_materialArray`,
`float u_layerTiling[4]`, `bool u_useGroundTextures`, `vec2 u_distanceTiling`
(near/far scale + range). When `u_useGroundTextures` is false → **existing
flat-colour path unchanged** (fallback / Tabernacle).

Per-pixel algorithm when textures are on:

1. **UVs.** `uv_i = worldPos.xz * u_layerTiling[i]`. For the distance-tiling
   break-up, also `uv_far_i = uv_i * 0.25`; lerp albedo/material samples by
   `smoothstep` over view distance (`u_distanceTiling`).
2. **Sample** albedo, normal, material (AO/Rough/Height) for the 4 layers.
3. **Height-blend** by splat weight (Mishkinis, §2.2): compute per-layer
   `hw_i = height_i + splat_i`, `ma = max_i(hw_i) - DEPTH` (DEPTH≈0.2),
   `b_i = max(hw_i - ma, 0)`, `w_i = b_i / Σb_i`. Blend albedo, roughness, AO,
   and the detail normal by `w_i`.
4. **Slopes (triplanar).** Reuse the existing steepness → `triBlend` logic. Where
   `triBlend>0`, sample the layers triplanar (X/Y/Z world planes) and blend the
   three projections by geometric-normal weights; blend triplanar normals with
   **Whiteout** (Ben Golus, §2.3). `mix(topDown, triplanar, triBlend)`.
5. **Detail normal → world.** Combine the blended tangent-space detail normal
   with the macro terrain normal (`u_normalMap`) via Whiteout/RNM, so both the
   large terrain shape and the fine material bumps light correctly.
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
   `FormulaDefinition` in the **rendering** category (C++ + GLSL codegen), pinned
   by a parity/regression test (§7) against the GGX reference before it ships.

2. **Art-directed constants** — the height-blend band `DEPTH` (~0.2) and the
   distance-tiling near/far transition. These have **no reference dataset** (pure
   look tuning), so per Rule 6 they are hand-authored named constants carrying a
   `// TODO: revisit via Formula Workbench` comment at the definition site — the
   legitimate hand-code path when no data exists to fit against. If a look-target
   dataset is later captured, the transition curve becomes a Workbench curve.

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Texture-array assembly, mip/aniso setup, image decode | **CPU**, one-time init | I/O + allocation; not per-frame. |
| Splatmap weight generation (`generateAutoTexture`) | **CPU**, one-time | Already CPU; sparse/decision logic. |
| Per-pixel layer sample + height-blend + normal blend + triplanar | **GPU** (fragment) | Per-pixel, data-parallel, no branching decisions the CPU could hoist. |
| Height-blend math (parity reference) | **CPU** mirror for test only | Dual-impl parity (Rule 7); runtime is GPU. |

No "CPU now, move later" — the runtime path is GPU from the start; the CPU mirror
exists solely to lock the formula with a parity test.

---

## 6. Performance (60 FPS is a hard requirement)

Worst-case per-pixel cost (top-down, textures on): 4 layers × 3 maps = 12 array
samples, +4 for distance-tiling dual-scale on albedo/material, + height-blend ALU.
On steep triplanar pixels, ×3 for the extra projections — but triplanar only
kicks in above the steepness threshold (`triBlend>0`), which is a small fraction
of a gentle meadow.

**Budget & mitigations:**

- Target: the full meadow (this + grass + props + water + CSM) holds **≥60 FPS at
  1080p on the RX 6600** dev GPU. Verified against the 3D_E-0027 benchmark with
  `--profile-log` (compare terrain-pass GPU ms before/after).
- **Quality tiers** (wire into the existing `FormulaQualityManager` / settings):
  - **High** — all layers, distance-tiling + macro variation, detail normals,
    height-blend.
  - **Medium** — drop the distance-tiling second sample; keep detail normals +
    height-blend.
  - **Low** — albedo + AO only (no detail normal), linear weight blend, no
    triplanar textures (flat-colour on slopes). Roughly the current cost.
- Mip + anisotropy are mandatory (minification without mips is both ugly *and*
  slow via cache thrash).
- If the terrain pass regresses the frame budget, the profiler CSV localises it
  and the quality tier is dropped a notch by default on lower GPUs. Any shipped
  clamp/tier-forcing is logged per project Rule 5.

---

## 7. Testing

- **Height-blend parity (unit).** `terrain_material_blend.h` pure
  `heightBlendWeights(std::array<float,4> h, std::array<float,4> w, float depth)`;
  test that it matches the GLSL formula on hand-picked cases (single dominant
  layer → weight 1; equal heights+weights → equal split; a high-height low-weight
  layer still peeks through), and that weights sum to 1 and are non-negative.
- **Material-set load (unit).** Mismatched layer dimensions → `isValid()==false`
  (fallback), not a crash; a valid 4-layer set → three non-zero array handles.
- **Fallback (unit/behavioural).** With no material set, the terrain still renders
  (flat-colour path) — `u_useGroundTextures=false`. Tabernacle/material-demo
  unaffected.
- **Visual (`--visual-test`).** The existing meadow viewpoints
  (pond_overview / pond_shore / open_meadow) show textured ground (grass detail,
  not flat mint); no diagonal banding (watch the GL_UNPACK_ALIGNMENT lesson — all
  new arrays are RGB8/RGBA8 with correct alignment or padded to RGBA).
- **Performance (`--profile-log`).** Terrain-pass GPU ms and total FPS recorded
  against the benchmark; assert ≥60 FPS at High on the dev GPU.

---

## 8. Assets & licensing

- **Source:** ambientCG / Poly Haven, **CC0** (no attribution required; credited
  as courtesy in `THIRD_PARTY_NOTICES.md`, rows in `ASSET_LICENSES.md`).
- **Layers:** grass (lush lawn), rock/cliff, dirt/soil, sand — each albedo +
  normal + a repacked **R=AO G=Roughness B=Height** material map, generated at
  asset-prep from the source `_diff` / `_nor_gl` / `_ao` / `_rough` / `_disp`
  maps.
- **Resolution: 1K** committed (albedo as JPG, normal + material as PNG). Est.
  ~4 layers × ~1.2 MB ≈ **~5 MB** total. Above the `ASSET_LICENSES.md` soft
  >1 MB "consider the future assets repo" line, but the repo already tracks
  several >1 MB CC0 assets (2K brick/plank/HDRI) under the same guidance —
  consistent precedent, documented = compliant. 2K variants drop in via a
  git-ignored override later (mirrors the `nature_local/` pattern).
- Committed under `assets/textures/terrain/`. `copy_assets` globs `assets/` so no
  CMake change (same as the meadow model/HDRI subdirs).

---

## 9. Implementation slices

1. **A1 — `TerrainMaterialSet`** + GL array loader + parity-tested
   `heightBlendWeights` pure header. *Verify:* unit tests green (load, fallback,
   blend parity).
2. **A2 — Shader top-down path.** Add array samplers + uniforms; textured
   top-down albedo/roughness/AO with height-blend; keep flat-colour fallback
   branch. *Verify:* `--visual-test` meadow ground is textured grass; Tabernacle
   unchanged.
3. **A3 — Detail normals + triplanar (Whiteout).** Detail normal → world via
   macro-normal combine; triplanar textures on slopes. *Verify:* bumps light
   correctly; no stretching on the pond-bowl banks.
4. **A4 — Tiling break-up.** Distance-tiling dual-scale + macro variation.
   *Verify:* no obvious repetition across the 256 m field in `open_meadow`.
5. **A5 — Assets + wire-up + quality tiers + perf.** Commit the CC0 layer set;
   the meadow sets it; quality-tier plumbing; `--profile-log` shows ≥60 FPS.
   *Verify:* benchmark ≥60 FPS at High on RX 6600; CHANGELOG + ASSET_LICENSES +
   THIRD_PARTY_NOTICES rows.

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
  (deferred, noted §2.4); design keeps the sample sites isolated so it can slot in.
- **Perf regression** → quality tiers + profiler-gated default; Rule 5 logging for
  any clamp.
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

_(Filled as the /cold-eyes loops run — findings + resolutions per loop. Design is
not implemented until a pass converges with no substantive findings.)_

- Loop 1 — pending.
