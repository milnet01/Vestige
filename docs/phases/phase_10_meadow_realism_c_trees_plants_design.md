# Phase 10 — Meadow Realism C: Realistic Trees & Plants (3D_E-0033)

**Status:** Design (pre-implementation). Cold-eyes loop pending.
**ROADMAP:** [3D_E-0033] Meadow realism C — realistic trees & plants. (📋, `rendering-enhancements`)
**Depends on / reuses:** GPU grass (3D_E-0039, shipped), meadow benchmark scene (3D_E-0027), pond (3D_E-0037), cascaded shadow maps, `EnvironmentForces` wind.
**Author:** Claude (spec sign-off delegated to cold-eyes convergence per project workflow).

---

## 1. Goal (plain terms)

Swap the meadow's cartoon Kenney tree props for **realistic, game-ready trees** — including a
handful of **huge landmark ("hero") trees** — that stay at 60 FPS on the RX 6600, cast and receive
shadows, sway in the shared wind, and reflect in the pond. Then clean up the low-poly flowers
(kill the redundant double-draw) and replace the low-poly lily pads.

The realism bar matches the million-blade GPU grass already shipped: this is not stylized/cartoon —
it is real-game-looking vegetation, sourced CC0 / free-for-commercial so it is Steam-safe.

Scope order (locked with user): **trees first**, then flowers + lilies.

---

## 2. Current state (verified against source 2026-07-21)

- **`TreeRenderer` exists but is a skeleton.** `engine/renderer/tree_renderer.{h,cpp}`, shaders
  `assets/shaders/tree_mesh.*` + `tree_billboard.*`. Constructed by borrowing from the vegetation
  system at `engine/core/engine.cpp:265` (`m_treeRenderer = &vegSys->getTreeRenderer();`), rendered
  per-frame at `engine.cpp:1643`.
  - **LOD/crossfade machinery is real and working** — `render()` buckets each tree by camera
    distance into LOD0-mesh / LOD1-billboard with a complementary-alpha crossfade in
    `[lodDistance, lodDistance+fadeRange]`, then instanced-draws each bucket
    (`tree_renderer.cpp:100-201`). Public knobs: `lodDistance=50`, `fadeRange=10`, `maxDistance=200`.
  - **But the meshes are placeholders.** LOD0 is a procedural trunk-cylinder + cone-crown
    (`createPlaceholderTree`, `tree_renderer.cpp:204-320`). "LOD1 impostor" is a hand-coded 64×128
    RGBA blob (`generateBillboardTexture`, `:378-433`) — **not** rendered from any mesh, **not**
    view-dependent. Impostor generation does **not** exist yet.
  - **Species ignored.** `TreeInstance` carries a `speciesIndex` but the renderer draws one hardcoded
    placeholder for every tree.
  - **Meadow feeds it nothing.** The only callers of the tree-add path are the editor brush
    (`brush_tool.cpp` → `PlaceTreeCommand` → `FoliageManager::addTreeDirect`). The meadow scene
    plants zero trees into `FoliageChunk::getTrees()`, so `render()` draws nothing there.
- **Meadow trees today** are plain glb props with **no LOD**: a `scatterGroup` lambda
  (`engine.cpp:2610`) loads Kenney glbs (`tree_oak.glb`, `tree_pineDefaultA/B.glb`, `tree_default.glb`,
  `tree_fat.glb`, `tree_detailed.glb`) and `Model::instantiate`s one entity per scatter point
  (`engine.cpp:2668`).
- **Drop-in asset hook exists.** `propPath` lambda (`engine.cpp:2598-2605`) reads
  `assets/models/nature_local/<file>` first, then falls back to `assets/models/nature/<file>`.
  `nature_local/` is git-ignored and does not exist on disk yet — the intended home for large
  realistic assets kept out of the public repo.
- **Flowers double-drawn:** billboard "star" cross-cards via `FoliageRenderer::createStarMesh`
  (`foliage_renderer.cpp:355`, textures set `engine.cpp:2539-2541`) **and** tiny glb props
  `flower_purpleA/redA/yellowA.glb` (`engine.cpp:2702-2705`). The glb props are the low-poly offenders.
- **Lilies** = `lily_large.glb` + `lily_small.glb` on the pond (`engine.cpp:2735-2736`).
- **Asset library** (`/mnt/Games/3D Engine Assets/Models/Nature/Trees/`) holds CC0 Poly Haven 4K
  trees as heavy `.blend.zip` originals: `pine_tree_01` (1.3 GB), `fir_tree_01` (913 MB),
  `jacaranda_tree` (558 MB), `island_tree_01–03`, `quiver_tree_01/02`, plus saplings/stumps/roots.
  One already-unpacked glTF: `Trees/low_poly_tree_scene_free/scene.gltf`.

---

## 3. Design decisions & rationale

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | **Revive `TreeRenderer`** (Approach A), keep its LOD/crossfade/instancing machinery, replace its two procedural generators (placeholder mesh + procedural billboard) with real glb LOD0 meshes and a **baked octahedral impostor** LOD1. | Reuse before rewrite. The distance-bucketing + crossfade + instanced draw is the hard part and already works; only the *content* is fake. |
| D2 | **Octahedral impostor** for LOD1 (user-chosen over simple billboard). Bake a hemisphere grid of views into a per-species atlas at load; runtime blends the 3 nearest views. | Distant trees stay properly 3D as you walk past a treeline (a single camera-facing card flattens on lateral motion). Standard modern technique; we have the 60 FPS headroom. |
| D3 | **Two-tier species model**: *hero* trees (few, sparse, big — decimated Poly Haven 4K) and *field* trees (many, the treeline — mid-weight CC0 glTF). Both ride the same renderer + impostor path; they differ only in LOD0 triangle budget and placement density. | User wants "huge trees" as landmarks plus a believable treeline. One code path, two content budgets — no special-casing. |
| D4 | **LOD0 casts + receives shadows; impostors receive only.** | Trees are big — ground shadow-cast matters. Casting *from* a flat impostor is ill-defined and the near mesh already casts; a distant impostor's cast contribution is negligible. Scope cap, logged per project Rule 5. |
| D5 | **Wind reuses `EnvironmentForces`** (shared source), gentle-capped, dead-calm at wind 0. | Same wind that drives grass/foliage — one coherent gust across the meadow. Accessibility reduced-motion floor at 0. |
| D6 | **Placement reuses `scatterProps`** (the existing deterministic jittered-grid point generator), routed through `FoliageManager::addTreeDirect` instead of raw `Model::instantiate`. | Trees land where the current glb tree-props land; no new placement algorithm; deterministic (seeded). |
| D7 | **Impostor atlas resolution / LOD distances are a quality tier** on `RendererQualitySink` (new `TreeQuality`, mirroring the shipped `GrassQuality`). | Weak-HW scalability (ties into perf program); Low bakes fewer/smaller frames. |
| D8 | **Trees render into the water reflection pass** (`TreeRenderer::render` already takes a `clipPlane`). | 3D_E-0033 requires the treeline to reflect in the pond. |
| D9 | **Asset pipeline is a scripted, repeatable step** (Blender-headless extract + decimate + texture-downscale → glTF in `nature_local/`), not hand-exported one-offs. | Six-month test: the heavy originals stay out of the repo; anyone can re-run the prep. |
| D10 | **Species must be biome-coherent with the meadow.** The demo meadow is a **temperate grassland**, so both hero and field species are drawn from temperate flora (oak, birch, maple, willow, beech; fir/pine/spruce at the edges). Desert / subtropical / tropical library assets (quiver tree, jacaranda, island tree) are **excluded** — they read as wrong in a green meadow. | User requirement: the trees must make sense to be there. A believable treeline is biome-consistent, not a random species grab-bag. |

---

## 4. Architecture

### 4.1 Species model

A small **species table** owned by `TreeRenderer`, one entry per distinct tree:

```
struct TreeSpecies
{
    Model*                 lod0Mesh;      // real glb, loaded via ResourceManager (LOD0)
    ImpostorAtlas          impostor;      // baked octahedral atlas (LOD1)
    AABB                   bounds;        // for impostor framing + billboard size
    float                  trunkPivotY;   // base-at-origin correction if needed
};
std::vector<TreeSpecies>   m_species;     // indexed by TreeInstance::speciesIndex
```

`TreeInstance::speciesIndex` (already stored, currently ignored) becomes the index into this table.
Hero and field trees are just entries with different LOD0 triangle budgets — no type flag needed.

### 4.2 Octahedral impostor baker (new: `engine/renderer/impostor_baker.{h,cpp}`)

Bakes one atlas per species at load. Reuses `Framebuffer` (MRT) as the render target and the
`CascadedShadowMap::beginCascade/endCascade` pattern (bind FBO, set per-cell viewport, draw mesh)
as the render-loop template.

- **Grid:** `N×N` **hemisphere** octahedral views (foliage is rarely seen from below → hemisphere
  gives more side-view resolution than a full sphere). Default `N=16` (256 frames).
- **Atlas:** one texture, `N` cells per side. Default `2048²` → `128²` per frame (ample for
  ≥`lodDistance` viewing). Two color attachments baked in one pass:
  - `albedoAlpha` (RGBA8): albedo from `Material::getDiffuseTexture()`; **alpha = coverage** (honors
    the glb's MASK alpha-cutoff / double-sided, `Material::getAlphaMode/getAlphaCutoff/isDoubleSided`).
  - `normalDepth` (RGBA8): world-space (impostor-local) normal in RGB, view-depth in A (for parallax
    + lit-from-sky shading at runtime).
- **Per-frame camera:** orthographic, framed to `bounds`, oriented along the octahedral-decoded
  direction for cell `(i,j)`. Ortho (not perspective) keeps the impostor parallax-free per frame so
  the runtime parallax offset is the only depth cue — matches the reference technique.
- **Bake cost:** `N²` cheap mesh draws per species, once at load. 256 × ~8 species ≈ 2k draws into
  128² cells — sub-second; not per-frame.

### 4.3 Runtime impostor render (rewrite `tree_billboard.*`)

Per distant tree (LOD1 bucket), draw a **camera-facing quad** sized to `bounds`:

1. Compute impostor→camera direction in impostor-local space; clamp to hemisphere (`y ≥ 0`).
2. **Octahedral-encode** that direction to `[0,1]²` grid coords (no trig — the standard
   octahedral map). The enclosing grid triangle gives the **3 nearest frames** and their
   barycentric blend weights.
3. Sample `albedoAlpha` + `normalDepth` for each of the 3 frames at the frame's cell UVs, applying a
   **depth-parallax offset** (shift the sample by the view-tangent scaled by sampled depth) so the
   billboard reads as a solid volume, not a flat card.
4. **Blend** the 3 frames by the barycentric weights; **alpha-clamp** (discard below a threshold) to
   keep silhouettes crisp and hide inter-frame blend fuzz.
5. Light with the **blended sampled normal**: half-Lambert directional + ambient + **CSM shadow
   receive** (same uniforms as grass/foliage). No cast.

### 4.4 LOD0 mesh render (rewrite `tree_mesh.*`)

Real glb mesh, **instanced** (the existing `TreeDrawInstance` VBO: pos/rot/scale/alpha at instance
attrib locations 3–6). Vertex shader applies **wind sway** (per-tree phase from instance data,
gentle-capped, calm at wind 0; trunk rigid, canopy sways — sway scaled by height above pivot).
Fragment: PBR-ish lit (albedo from material, half-Lambert + ambient), **CSM shadow receive**.
A separate **shadow-caster** pass (reusing the foliage caster pattern, single `u_lightSpaceMatrix`)
renders LOD0 trees into the cascade depth so they cast onto the ground.

### 4.5 Meadow placement (edit `engine.cpp` meadow builder)

Replace the raw-prop tree `scatterGroup` call with foliage-tree placement:

- **Field trees:** reuse the *existing* tree `ScatterParams` (`seed=0xA11CE01u`, `cellSize=28`,
  `jitter=0.8`, `minDist=12`, exclusion = pond disc, `minScale=1.3..2.4`). For each `ScatterPoint p`:
  `TreeInstance{ {p.x, terrain.getHeight(p.x,p.z), p.z}, radians(p.yawDeg), p.scale, i % fieldSpecies }`
  → `m_foliageManager->addTreeDirect(...)`. (Yaw: ScatterPoint is degrees, TreeInstance is radians.)
- **Hero trees:** a second sparse scatter group (large `cellSize` ≈ 60, `minDist` ≈ 40, bigger scale,
  clustered off the main sightline) → hero species indices.
- Trees then render automatically via the existing `engine.cpp:1643` call (chunks already carry them).

### 4.6 Shadows / wind / lighting uniforms (copy verbatim from grass/foliage)

`u_cascadeShadowMap` (sampler2DArray, **unit 3**), `u_cascadeCount`, `u_cascadeSplits[4]`,
`u_cascadeLightSpaceMatrices[4]`, `u_hasDirectionalLight` + dir/color/intensity, `u_windDirection`
(vec3) from `EnvironmentForces::getBaseWindDirection()` + `getWindSpeed(pos)`. **Mesa fallback:** both
the mesh and impostor shaders must bind a 2DArray to unit 3 every draw — real CSM or
`sharedSamplerFallback().getSampler2DArray()` — else `GL_INVALID_OPERATION` (the grass no-shadow
branch is the copy source).

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Where | Why |
|------|-------|-----|
| Scatter point generation (`scatterProps`) | **CPU**, once at load | Sparse, deterministic, branchy (reject/exclusion tests). |
| Species table build, glb load, chunk insertion | **CPU**, once at load | I/O + scene-graph bookkeeping. |
| Impostor atlas bake (render mesh → atlas) | **GPU**, orchestrated CPU-side, once at load | Per-pixel rasterization into texture. |
| Per-frame LOD bucketing + crossfade alpha | **CPU**, per frame | Per-tree distance test; sparse (thousands, not millions) — cheaper than a GPU round-trip. Already how `TreeRenderer` works. |
| Mesh draw, wind sway, lighting, shadow sampling | **GPU** (vertex/fragment) | Per-vertex / per-pixel. |
| Octahedral frame select + 3-frame blend + parallax | **GPU** (fragment) | Per-pixel. |
| Octahedral encode/decode **contract** | **Dual** (GLSL runtime + CPU mirror) | Bake-camera placement (CPU) and runtime frame lookup (GLSL) must agree — pinned by a parity test (§7). |

No per-frame CPU↔GPU parity concern beyond the octahedral mapping, which §7 pins.

---

## 6. Asset pipeline & sourcing

**Species-coherence gate (D10):** every candidate is filtered for temperate-meadow fit *before*
prep. A CC0 licence is necessary but not sufficient — a desert or tropical species is rejected even
though it is free.

- **Hero trees:** decimated from the existing **Poly Haven 4K** library already on disk — but only
  the **temperate-coherent** ones: `fir_tree_01`, `pine_tree_01` (and their saplings). The library's
  `jacaranda_tree` (subtropical), `quiver_tree_01/02` (African desert succulent) and `island_tree_01–03`
  (tropical) are **excluded** by D10. Widen the hero set with a few more downloaded CC0 temperate
  giants (Poly Haven catalog: large oak / beech / broadleaf). Cedar/cypress are borderline-temperate
  and acceptable as heroes; **olive / acacia and other arid biblical species are deferred** to a
  future arid-biome scene (they belong with a matching ground/sky, not this green meadow).
- **Field trees:** mid-weight realistic CC0 glTF **temperate** species — the already-unpacked
  `low_poly_tree_scene_free` as a baseline, expanded with CC0 game-ready packs (Poly Haven /
  Quaternius / Kenney where realistic *and* temperate). Attribution not required for CC0; any
  non-CC0 pick is flagged before use.
- **Lily pads:** a better CC0 lily/water-plant model to replace `lily_{large,small}.glb`.

### 6.2 Prep (scripted, repeatable — `tools/asset_prep/prepare_trees.py`)

Blender-headless (`blender --background --python`) per hero asset:
1. Unzip `.blend.zip` → open `.blend`.
2. **Decimate** LOD0 to a triangle budget (hero ≈ 20–40k tris; field ≈ 3–10k).
3. **Downscale** 4K PBR textures to 2K (VRAM; distant trees never resolve 4K).
4. Export **glTF** (`.glb`) into `assets/models/nature_local/`.

`nature_local/` stays git-ignored (heavy binaries out of the public repo); the prep script + a
`SOURCES.md` (asset name → Poly Haven URL → CC0) are committed so the set is reproducible.
Bundled fallback: if a `nature_local/` asset is absent, the `propPath` hook already falls back to the
lightweight `nature/` Kenney glb, so a fresh clone still runs (just with the cartoon trees).

---

## 7. Testing & parity

- **Octahedral parity test** (`tests/test_impostor_octahedral.cpp`, new): a CPU mirror of the GLSL
  octahedral encode/decode. Asserts (a) `decode(encode(dir)) ≈ dir` roundtrip within tolerance over a
  hemisphere sweep, and (b) the frame the **baker** places for cell `(i,j)` is the frame the
  **runtime** selects when viewed from that cell's direction — the CPU/GPU contract pin (Rule 7).
- **Existing tree/foliage tests stay green** — `TreeInstance` serialization
  (`treeLayers` fields, `SerializeDeserialize`) is untouched; species index semantics unchanged.
- **Visual-test viewpoints** (run `./vestige --visual-test` **from `build/bin/`**,
  `ASAN_OPTIONS=detect_leaks=0`): add `treeline_far` (impostor band), `tree_near` (LOD0 up close),
  `tree_crossfade` (walk through the LOD transition), `hero_tree` (a landmark), `pond_reflection`
  (treeline mirrored). Assert 0 GL errors + visual inspection of captures.
- **Perf gate:** `--profile-log` Tree-pass read on the RX 6600 at the tree viewpoints; **≥ 60 FPS at
  High**. GPU-total pass is the true metric (frame-total is polluted by screenshot readback).

---

## 8. Accessibility

- **Wind gentle-capped**, dead-calm at wind 0 → reduced-motion floor (reuse the grass/foliage clamp).
- **Legibility via value + normal, not hue** — trees read by silhouette/shading, colour-blind safe.
- **Impostor alpha-clamp** kills shimmer/crawl on distant foliage edges (motion-comfort).
- Tree density is bounded by the meadow scatter params (no seizure-risk flicker from over-dense
  alpha layers at distance — the impostor is one blended quad, not stacked cards).

---

## 9. Performance plan (60 FPS hard floor)

- **LOD0 near only** (< `lodDistance` ≈ 50 m): a handful of full meshes, instanced. Hero-tree LOD0 is
  the heaviest single mesh but there are very few and only when close.
- **Impostor far** (the treeline): one blended camera-facing quad per tree, instanced — cheap.
- **Bake is one-time at load** (< ~1 s), not per-frame.
- **VRAM budget:** ~32 MB/species at 2048² × 2 maps; ~8 species ≈ 256 MB (fits the 8 GB RX 6600).
  `TreeQuality` tier scales atlas res/frames down for weaker HW.
- **Shadow cast** adds LOD0 trees to the cascade depth pass — bounded (near trees only).
- Budget check happens at the T8 perf gate before ship; if the Tree pass threatens the 16.6 ms frame,
  the first lever is `lodDistance` (push impostors nearer), then atlas res.

---

## 10. Implementation slices

| Slice | Work | Verify |
|-------|------|--------|
| **T1** | Asset sourcing + prep script (`prepare_trees.py`), populate `nature_local/`, `SOURCES.md`. Download extra CC0 hero trees. | Assets load via `propPath` hook; a smoke scene shows real meshes. |
| **T2** | `ImpostorBaker` (Framebuffer MRT, hemisphere octahedral views) + octahedral map GLSL + **CPU-mirror parity test**. | Parity test green; dumped atlas PNG looks correct (albedo + normal). |
| **T3** | Revive `TreeRenderer`: species table, real LOD0 mesh draw (replace placeholder), impostor LOD1 draw + runtime octahedral blend shader (replace procedural billboard), wind + CSM receive + directional light. | Near mesh + far impostor render; crossfade smooth; 0 GL errors. |
| **T4** | Meadow placement: route field-tree + hero-tree scatter points through `addTreeDirect`; remove the raw glb tree-prop `scatterGroup`. | Trees appear at former prop spots; LOD switches walking toward/away. |
| **T5** | LOD0 shadow-caster pass (reuse foliage caster). | Trees cast ground shadows; no shadow acne/peter-panning regressions. |
| **T6** | Pond reflection: render trees in the water reflection pass (clipPlane). | Treeline mirrors in the pond. |
| **T7** | Flowers cleanup: remove redundant `flower_*A.glb` props (keep billboard star-mesh flowers). Replace lily pads with better CC0 model. | No double-draw; flowers + lilies look clean. |
| **T8** | `TreeQuality` tier on `RendererQualitySink` (+ preset→tier test); perf gate on RX 6600 (≥60 FPS High). ROADMAP 3D_E-0033 ✅ + CHANGELOG. | ctest green; 60 FPS High; docs updated. |

Trees = T1–T6, T8 (hero + field). Flowers/lilies = T7. Matches the locked "trees first" order.

---

## 11. Risks & scope caps

- **Impostors receive but do not cast shadows** (D4) — logged in the T5 commit + CHANGELOG per Rule 5.
- **Blender dependency for asset prep** — the prep script needs Blender on the prep machine; the
  committed glTF outputs (in `nature_local/`, git-ignored) mean the *engine* never needs Blender.
  If Blender is unavailable, T1 falls back to hand-picked already-glTF CC0 trees (fewer hero options).
- **Hero-tree LOD0 weight** — a poorly-decimated 4K tree could blow the near budget; the T1 triangle
  budget + T8 perf gate guard this. Lever: raise `lodDistance` so heroes hit impostor sooner.
- **Atlas VRAM** — 8 species cap for the default tier; more species → lower per-atlas res or share
  atlases. Bounded by `TreeQuality`.

---

## 12. Cited sources

- **Octahedral impostors — technique:** Ryan Brucks (Epic), *Octahedral Impostors* —
  https://shaderbits.com/blog/octahedral-impostors (hemisphere-for-foliage, per-frame grid, atlas,
  runtime octahedral map, frame blend, depth/parallax).
- **Octahedral impostors — open-source reference impl (readable shader + baker):**
  wojtekpil, *Godot-Octahedral-Impostors* — https://github.com/wojtekpil/Godot-Octahedral-Impostors
  (grid size 16 default = 16×16 frames; hemisphere default for foliage; bakes albedo + normal+depth
  (+ORM); grid blending + alpha-clamp; depth-scale + normalmap-depth parallax/lighting params).
- **Impostors + LOD overview:** Amplify Impostors (Unity) manual —
  https://wiki.amplify.pt/index.php?title=Unity_Products%3AAmplify_Impostors%2FManual
  (2048 atlas guidance; full-sphere only when visible from below).
- **Realistic CC0 tree assets:** Poly Haven Models (CC0, glTF, no attribution) —
  https://polyhaven.com/models ; license — https://polyhaven.com/license . (Kenney / Quaternius also
  CC0, leaning stylized — field-tree filler.)

---

## 13. Cold-eyes loop log

_To be filled as `/cold-eyes` loops run (project Rule 1 + global Rule 14). Loops run cold — no
prior-loop briefing. Convergence = zero substantive verified findings._

- Loop 1: _pending_
