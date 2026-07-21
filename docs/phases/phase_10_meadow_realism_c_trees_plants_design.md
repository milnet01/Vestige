# Phase 10 — Meadow Realism C: Realistic Trees & Plants (3D_E-0033)

**Status:** **REVISED 2026-07-21 (v2) — re-review pending.** The original v1 (cold-eyes converged loop 6)
assumed a single heavy hero mesh needing decimation + a custom-baked octahedral impostor. During T1 the
asset reality changed the design materially (see the v2 note below), so this doc is **reconciled to v2 and
must re-run `/cold-eyes` to convergence before further implementation** (project Rule 9 / global Rule 14).
**ROADMAP:** [3D_E-0033] Meadow realism C — realistic trees & plants. (🚧, `rendering-enhancements`)
**Depends on / reuses:** GPU grass (3D_E-0039, shipped), meadow benchmark scene (3D_E-0027), pond (3D_E-0037), cascaded shadow maps, `EnvironmentForces` wind.
**Author:** Claude (spec sign-off delegated to cold-eyes convergence per project workflow).

> **v2 change summary (2026-07-21).** Sourcing pivoted from Poly Haven photoscans (7–17 M triangles —
> film-quality, unusable as a real-time LOD0: decimating 99.6 % turns solid-geometry needles into bald
> sticks) to **LOLIPOP's game-ready CC-BY tree packs** (pine/fir/maple/birch — each already shipping
> LOD0–2 real meshes + a billboard, PBR, ~8–22 k LOD0 tris). Because the packs **already contain the
> distance LODs + billboards**, the **custom octahedral impostor baker (old T2) is dropped** — the engine
> distance-switches the artist LODs instead (user-chosen). Octahedral impostors remain a documented
> *optional* future upgrade (§11). This removes the hardest, riskiest slice; the rest of the architecture
> (revive `TreeRenderer`, per-material LOD0 draw, placement via `placeTree`, wind/CSM, pond reflection,
> flowers/lilies cleanup) is unchanged.

**Contents:** [1 Goal](#1-goal-plain-terms) · [2 Current state](#2-current-state-verified-against-source-2026-07-21) ·
[3 Decisions](#3-design-decisions--rationale) · [4 Architecture](#4-architecture) · [5 CPU/GPU placement](#5-cpu--gpu-placement-project-rule-7) ·
[6 Asset pipeline](#6-asset-pipeline--sourcing) · [7 Testing](#7-testing) · [8 Accessibility](#8-accessibility) ·
[9 Performance](#9-performance-plan-60-fps-hard-floor) · [10 Slices](#10-implementation-slices) · [11 Risks](#11-risks--scope-caps) ·
[12 Sources](#12-cited-sources) · [13 Cold-eyes log](#13-cold-eyes-loop-log)

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
    (`tree_renderer.cpp:85-201`). Public knobs: `lodDistance=50`, `fadeRange=10`, `maxDistance=200`.
  - **But the meshes are placeholders.** LOD0 is a procedural trunk-cylinder + cone-crown
    (`createPlaceholderTree`, `tree_renderer.cpp:204-321`). "LOD1 impostor" is a hand-coded 64×128
    RGBA blob (`generateBillboardTexture`, `:378-434`) — **not** rendered from any mesh, **not**
    view-dependent. Impostor generation does **not** exist yet.
  - **Species ignored.** `TreeInstance` carries a `speciesIndex` but the renderer draws one hardcoded
    placeholder for every tree.
  - **Meadow feeds it nothing.** The only callers of the tree-add path are the editor
    (`brush_tool.cpp:260` → `placeTree`; undo/redo via `PlaceTreeCommand` → `addTreeDirect`). The
    meadow scene plants zero trees into `FoliageChunk::getTrees()`, so `render()` draws nothing there.
- **Meadow trees today** are plain glb props with **no LOD**: a `scatterGroup` lambda
  (`engine.cpp:2610`) loads Kenney glbs (`tree_oak.glb`, `tree_pineDefaultA/B.glb`, `tree_default.glb`,
  `tree_fat.glb`, `tree_detailed.glb`). The tree scatter *call site* is `engine.cpp:2668`; the
  per-point `Model::instantiate` runs inside the lambda at `engine.cpp:2635`.
- **Drop-in asset hook exists.** `propPath` lambda (`engine.cpp:2598-2605`) reads
  `assets/models/nature_local/<file>` first, then falls back to `assets/models/nature/<file>`.
  `nature_local/` is git-ignored and does not exist on disk yet — the intended home for large
  realistic assets kept out of the public repo.
- **Flowers double-drawn:** billboard "star" cross-cards via `FoliageRenderer::createStarMesh`
  (`foliage_renderer.cpp:355`, textures set `engine.cpp:2539-2541`) **and** tiny glb props
  `flower_purpleA/redA/yellowA.glb` (`engine.cpp:2702-2705`). The glb flower props are the low-poly
  offenders. **Caution:** that same `scatterGroup` call also carries `mushroom_red.glb` +
  `plant_bush.glb` — T6 must drop only the three `flower_*A.glb` entries, not the whole call.
- **Lilies** = `lily_large.glb` + `lily_small.glb` on the pond (`engine.cpp:2735-2736`).
- **Supersession note (cross-doc):** the sibling design doc
  `phase_10_meadow_benchmark_scene_design.md` previously said not to route trees through
  `FoliageManager::addTreeDirect` / `TreeRenderer` (that path drew *procedural placeholder* geometry).
  This design lifts that prohibition by replacing the placeholder generators with the artist's real LOD
  meshes + billboard (D1, T2); **that doc has already been reconciled** — see its "Historical note
  (superseded by 3D_E-0033)".
- **Asset sources (v2, verified 2026-07-21):**
  - **Game-ready packs (the actual source)** — LOLIPOP CC-BY packs fetched to the library by
    `tools/asset_prep/fetch_sketchfab_trees.py`: `pine_lolipop_sketchfab_gltf` (15 trees),
    `fir_lolipop_sketchfab_gltf` (3, "Christmas tree"), `maple_lolipop_sketchfab_gltf` (12 Acer),
    `birch_lolipop_sketchfab_gltf` (5). Each pack bundles LOD0–2 meshes + a billboard (LOD3), split
    into **bark + foliage** materials, PBR at ~8–22 k LOD0 tris. **CC-BY 4.0** → the credit block in
    `Trees/SOURCES_sketchfab.md` must appear in the game credits screen (§6.3).
  - **Heavy Poly Haven photoscans** (`fir_tree_01`, `pine_tree_01`, …) remain on disk but are **not
    used** as LOD0 (7–17 M tris). Retained only as a *possible* future impostor-bake source; nothing in
    this design depends on them. Desert/tropical library assets (jacaranda, quiver, island) stay
    excluded by D10.

---

## 3. Design decisions & rationale

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | **Revive `TreeRenderer`** (Approach A), keep its distance-bucketing / crossfade / instancing machinery (extending it from 2 buckets to 3 — §4.1), and replace its two procedural generators (placeholder mesh + procedural billboard) with the **artist's real LOD meshes** (LOD0 near, LOD2 mid) and the **artist billboard** far. | Reuse before rewrite. The distance-bucketing + crossfade + instanced draw is the hard part and already works; only the *content* is fake (and the mid bucket is a modest extension of it). |
| D2 | **Use the artist-supplied LODs + billboard** (v2, user-chosen). Each LOLIPOP tree already ships LOD0/1/2 real meshes + a flat billboard (LOD3); the renderer distance-switches between them. **No custom impostor is baked.** The v1 octahedral impostor is **deferred as an optional future upgrade** (§11) — its benefit (never-flat distant treeline on lateral motion) is much smaller now that real LOD1/LOD2 meshes cover most distances, and it was the single hardest/riskiest slice. | Shortest correct path (Rule 2): the artist already solved the distance-representation problem and tuned it. A flat billboard only kicks in far enough out that its flatness is rarely read in a scattered meadow. |
| D3 | **Two-tier species model**: *hero* trees (few, sparse, big) and *field* trees (many, the treeline) are just the **large vs medium/small size variants** already in each pack (e.g. `Pine_large_*` vs `Pine_medium/small_*`). Both ride the same renderer path; they differ only in which pack variant + placement density. | User wants "huge trees" as landmarks plus a believable treeline. One code path, two content picks — no special-casing, **no decimation** (the packs are already game-budget). |
| D4 | **Meshes (LOD0 + mid) cast + receive shadows; the far billboard receives only.** | Trees are big — ground shadow-cast matters. Casting *from* a flat far card is ill-defined and the near/mid meshes already cast; a distant billboard's cast contribution is negligible. Scope cap, logged per project Rule 5. |
| D5 | **Wind reuses `EnvironmentForces`** (shared source), gentle-capped, dead-calm at wind 0. | Same wind that drives grass/foliage — one coherent gust across the meadow. Accessibility reduced-motion floor (§8). |
| D6 | **Placement reuses `scatterProps`** (the existing deterministic jittered-grid point generator), routed through the foliage tree-placement API (`FoliageManager::placeTree`, which maps world→chunk-grid) instead of raw `Model::instantiate`. | Trees land where the current glb tree-props land; no new placement algorithm; deterministic (seeded). |
| D7 | **LOD switch distances are a quality tier** on `RendererQualitySink` (new `TreeQuality`, mirroring the shipped `GrassQuality`). Low pulls the LOD0→mid→billboard distances nearer (fewer heavy meshes on screen); High pushes them out. | Weak-HW scalability (ties into perf program). No atlas to size now — the lever is the crossfade distances. |
| D8 | **Trees render into the water reflection pass** (`TreeRenderer::render` already takes a `clipPlane`). | 3D_E-0033 requires the treeline to reflect in the pond. |
| D9 | **Asset pipeline is a scripted, repeatable two-step** (`fetch_sketchfab_trees.py` downloads the packs; `split_tree_packs.py` runs Blender-headless to split each pack into per-tree glTF-separate files with **shared per-species textures**), not hand-exported one-offs. | Six-month test: the heavy binaries stay out of the repo (git-ignored `nature_local/`); anyone with the API token can re-run the prep. Shared textures avoid duplicating a species' bark/foliage maps across its variant files (§6.2). |
| D10 | **Species must be biome-coherent with the meadow.** The demo meadow is a **temperate grassland**, so the species are temperate flora — the sourced set is **fir, pine (conifers) + maple, birch (broadleaves)**, all temperate. Desert / subtropical / tropical assets (quiver tree, jacaranda, island tree) are **excluded** — they read as wrong in a green meadow. | User requirement: the trees must make sense to be there. A believable treeline is biome-consistent, not a random species grab-bag. |

---

## 4. Architecture

### 4.1 Species model

**Reuse the existing config struct.** `struct TreeSpeciesConfig` already exists
(`engine/environment/foliage_instance.h:67`: `name`, `meshPath`, `billboardTexturePath`, `minScale`,
`maxScale`, `minSpacing`) — the *authored* description of a species. We reuse it (decision D1; project
Rule 3, reuse-before-rewrite): `meshPath` names the LOD0 glTF, `billboardTexturePath` is repurposed to
name the billboard glTF. The renderer then holds a *runtime* table of loaded models:

```
struct TreeSpecies                        // runtime form; built from a TreeSpeciesConfig at load
{
    Model*                 lod0Mesh;      // artist LOD0 glTF (near) — via ResourceManager (engine/resource/model.h)
    Model*                 lodMidMesh;    // artist LOD2 glTF (mid distance); nullptr → fall straight to billboard
    Model*                 billboard;     // artist billboard glTF (far, flat card); nullptr → mid holds to maxDistance
    AABB                   bounds;        // for LOD sizing + cull
    float                  trunkPivotY;   // Y of trunk base from model origin (= bounds.min.y); 0 when origin is at the base (split tool recentres to 0)
};
std::vector<TreeSpecies>   m_species;     // indexed by TreeInstance::speciesIndex
```

All three are plain `Model*` loaded from the split per-tree glTFs (§6). `TreeSpecies` is a bare-member
runtime holder (matching `TreeInstance`'s convention, not the `m_` style — it owns no invariants).
`TreeInstance::speciesIndex` (already stored, currently ignored) becomes the index into this table.
Hero and field trees are just entries pointing at large vs medium/small pack variants — no type flag
needed. **Which mid LOD:** the packs ship LOD1 and LOD2; we take **LOD2** as the single mid tier (LOD1
is barely lighter than LOD0 — the useful saving is LOD2). This **extends the renderer's existing
*2-bucket* scheme (LOD0 mesh + billboard, §2) to 3 buckets** by adding a mid (LOD2) bucket and a second
crossfade band — new work on top of the existing LOD0/billboard bucketing + first crossfade, but a
smaller lift than a 4-bucket scheme.

### 4.2 Artist LOD assets (what the split tool produces, how they load)

No baking. The **finalised** §6 split tool will emit, per tree, up to three **glTF-separate** files —
`*_lod0.gltf` (near), `*_lod2.gltf` (mid), `*_billboard.gltf` (far), each `.gltf` + `.bin` referencing
the **shared per-species `textures/`** — recentred so the trunk base sits at the origin. (glTF-separate,
not a single-container `.glb`: external shared textures require loose files, §6.2.) Each is a normal
`Model` loaded through `ResourceManager` (which **caches by file path**, so every instance of a given
file shares one loaded `Model`, and every variant referencing the same texture path shares one GPU
upload — the reason §6.2 keeps textures **external and shared per species**, not embedded per file). A
tree glTF has **≥2 materials** (bark opaque + foliage alpha-cutout, double-sided); the loader preserves
them for the per-material draw (§4.4). The billboard glTF is the artist's flat card (a quad with an
alpha-cutout foliage texture); its geometry is an ordinary quad, but it is **drawn with a camera-facing
(yaw-billboard) shader** (§4.3), not a plain static-mesh draw — far simpler than the v1 octahedral
impostor, but still oriented per render pass.

### 4.3 Runtime LOD selection (`TreeRenderer::render`, existing bucketing)

The existing renderer already buckets each tree by camera distance and instanced-draws each bucket with
a complementary-alpha crossfade in `[switch, switch+fadeRange]` (`tree_renderer.cpp:85-201`). v2 keeps
that machinery and points it at the three real `Model`s:

- **LOD0 mesh** for distance `< lodDistance` (≈ 40 m) — full per-material draw (§4.4).
- **Mid (LOD2) mesh** for `[lodDistance, billboardDistance)` (≈ 40–90 m) — same per-material draw path,
  lighter mesh.
- **Billboard** for `[billboardDistance, maxDistance)` (≈ 90–200 m) — the artist's flat card. **A static
  quad drawn by the per-instance mat4 alone would render at a fixed orientation, not face the camera**
  (`TreeInstance::rotation` is a fixed per-tree yaw, `foliage_instance.h`). So the billboard bucket is
  **special-cased**: it is drawn with a **yaw-billboard shader** that rebuilds the card's horizontal
  basis from a per-pass **camera-right uniform** (rotating about the vertical/trunk axis so the card
  never tilts) — reusing the mechanism the existing placeholder billboard already uses
  (`tree_renderer.cpp:322-376` `createBillboardQuad` + `u_cameraRight`/`u_cameraUp`), fed the artist
  card geometry instead of a procedural quad. It is a single flat quad, so on fast lateral motion past a
  dense treeline it can read slightly flat — the accepted v2 tradeoff (D2); the optional octahedral
  upgrade (§11) is the fix if it ever matters.
- Two crossfade bands (LOD0↔mid, mid↔billboard), each the renderer's existing complementary-alpha
  dither/blend. The crossfade alpha rides the per-instance `vec2` at attrib **location 12** (§4.4).

The switch distances are the `TreeQuality` tier knob (D7). Billboards receive light + CSM shadow the
same way the meshes do (a flat lit card, §4.6) — they **do not cast** (D4).

**Pond reflection (D8/T5):** because the billboard shader orients the card to **whatever camera renders
the pass** (from that pass's `u_cameraRight`), feeding the **mirrored reflection camera's** right vector
makes the far billboards face the reflection camera correctly — no atlas-view-coverage problem like v1's
hemisphere impostor had. This is a property of the billboard bucket's per-pass orientation (above), not
automatic static-mesh behaviour. Near-shore reflected trees are LOD0/mid mesh, far ones the billboard,
same as the main pass.

### 4.4 LOD0 mesh render (rewrite `tree_mesh.*`)

**Flatten node transforms at load, group by material.** A tree glb is a `Model` of several
`ModelPrimitive`s, each referenced by a `ModelNode` (via `ModelNode::primitiveIndices`) carrying its
own TRS transform (`model.h`). A single per-instance mat4 (below) can't express those per-node
transforms, so at load the loader **accumulates each node's world matrix** (from `m_rootNodes` down
`childIndices`, composing parent `computeLocalMatrix()` — not just the node-local TRS) and
**pre-multiplies that world transform into the node's referenced primitives' vertices** (one flattened
copy per node-reference, so a multiply-referenced primitive is not dropped). The flattened geometry is then **grouped by material**:
a real tree has ≥2 materials (opaque bark, alpha-cutout double-sided leaves), so LOD0 emits **one
instanced draw per material group**, each honouring its own
`Material::getAlphaMode/getAlphaCutoff/isDoubleSided`. (The near LOD0 view is the highest-fidelity one;
it must not collapse leaf-cutout into bark-opaque.) The mid (LOD2) draw uses this **same per-material
path** on the lighter mesh. The per-instance mat4 is the only *transform* applied at draw; the
per-material split is a fixed, small draw count per species.

Real glb mesh, **instanced**. **Instance-attrib layout must change** — the placeholder's
`TreeDrawInstance` uses locations 3–6, but a loaded `Mesh` VAO already occupies **0–5** (vertex
attribs: pos/normal/color/texCoord/tangent/bitangent) **and 10–11** (bone IDs/weights, enabled
unconditionally in `Mesh::upload`, `mesh.cpp:210`). The instancing helpers take **6–9** (current
model mat4, binding 1 — `Mesh::setupInstanceAttributes`, `mesh.cpp:251`) and **12–15** (prev-frame
model mat4, binding 2 — `setupPrevInstanceAttributes`, `mesh.cpp:273`). So slots **0–15 are fully
consumed** — and the design does not rely on more than the `GL_MAX_VERTEX_ATTRIBS` guaranteed minimum
of 16 (0–15), so there is **no 17th slot** to use (see `phase_10_rendering_design.md:104`). Reuse
`setupInstanceAttributes`
for the **model matrix at 6–9 (binding 1)** — but its binding-1 stride is hardcoded to
`sizeof(glm::mat4)`, so it **cannot** carry extra per-instance data. The LOD **alpha** + per-tree
**wind phase** therefore **reclaim the prev-model slots (12–15)**: those are motion-vector attributes
that **static tree meshes never set up** (`setupPrevInstanceAttributes` is called only on the main
scene draws in `renderer.cpp`, never for trees). Expose them as a `vec2` at **location 12** on a
separate per-instance VBO (binding point 3). Trees therefore emit **no motion vectors** (like
grass/foliage) — logged as a scope cap in §11. (`setupInstanceAttributes` is the *pattern* for the
mat4, not a call that also carries the crossfade data.) The vertex shader applies **wind sway**
(per-tree phase from that attribute,
gentle-capped, calm at wind 0; trunk rigid, canopy sways — sway scaled by height above pivot).
Fragment: **half-Lambert diffuse + ambient** (albedo from material), **CSM shadow receive**.
A separate **shadow-caster** pass (reusing the foliage caster pattern, single `u_lightSpaceMatrix`)
renders LOD0 trees into the cascade depth so they cast onto the ground.

### 4.5 Meadow placement (edit `engine.cpp` meadow builder)

Replace the raw-prop tree `scatterGroup` call with foliage-tree placement:

- **Field trees:** `scatterProps(0xA11CE01u, params)` with the *existing* tree `ScatterParams`
  (`cellSize=28`, `jitter=0.8`, `minDist=12`, exclusion = pond disc, `minScale=1.3..2.4`) — the seed
  `0xA11CE01u` is `scatterProps`'s first argument, **not** a `ScatterParams` field. For each
  `ScatterPoint p`: `TreeInstance{ {p.x, terrain.getHeight(p.x,p.z), p.z}, radians(p.yawDeg), p.scale,
  i % fieldSpecies }` (`fieldSpecies` = the count of field species) →
  `m_foliageManager->placeTree(inst, 0.0f)` (it maps world→chunk-grid
  internally; `scatterProps` already enforced spacing, so `minSpacing=0`). The lower-level
  `addTreeDirect(gridX, gridZ, inst)` is the alternative but requires the caller to derive
  `gridX/gridZ = floor(worldXZ / FoliageChunk::CHUNK_SIZE)` (16 m). (Yaw: ScatterPoint is degrees,
  TreeInstance is radians.)
- **Hero trees:** a second sparse scatter group — `scatterProps(0x4E1D0A11u, heroParams)` with
  `cellSize` ≈ 60, `minDist` ≈ 40, bigger `minScale/maxScale`, clustered off the main sightline; each
  `ScatterPoint` → `TreeInstance{ …, i % heroSpecies }` (mirroring the field index scheme), routed
  through `placeTree` like the field set.
- Placement alone makes the chunks carry trees, so the existing `engine.cpp:1643` call starts drawing
  them — **but that call passes neither shadow map nor light** (`m_treeRenderer->render(visibleChunks,
  *m_camera, viewProj, elapsed)`), unlike the foliage call just above (`:1641–1642`, which passes
  `csm, dirLight`) and the grass call below. To deliver §4.6's shadow-receive + directional light,
  **T2 must widen `TreeRenderer::render`'s signature** (add `csm` + `dirLight`, plus wind — mirroring
  `FoliageRenderer::render` / `GrassRenderer::render`) **and update the `:1643` call site** to pass
  them. Without that signature change the trees ship unlit and unshadowed.
- **Give trees their own GPU-timer pass.** The `:1643` tree render currently sits *inside* the shared
  `beginPass("Foliage")` bracket (opened `engine.cpp:1633`), so `--profile-log` has no separate "Tree"
  number to gate §9's ≤2 ms budget against. T2 wraps the tree render in its own
  `beginPass("Tree")/endPass()` (mirroring the grass `beginPass("Grass")` at `:1652`) so the §7/§9/T7
  Tree-pass metric is measurable.

### 4.6 Shadows / wind / lighting uniforms (copy verbatim from grass/foliage)

The CSM/light uniforms are shared by grass **and** foliage: `u_cascadeShadowMap` (sampler2DArray,
**unit 3**), `u_cascadeCount`, `u_cascadeSplits[4]`, `u_cascadeLightSpaceMatrices[4]`,
`u_hasDirectionalLight` + dir/color/intensity. For **wind**, mirror the **foliage** mesh pass —
`u_windDirection` (vec3), fed from `EnvironmentForces::getBaseWindDirection()` + `getWindSpeed(pos)`
(the *grass* renderer uses a different pair, `u_windDir` vec2 + `u_windStrength`, because it is a
separate shader — trees follow foliage, not grass, for wind). **Mesa fallback:** the tree mesh **and**
billboard shaders must bind a 2DArray to unit 3 every draw — real CSM or
`sharedSamplerFallback().getSampler2DArray()` — else `GL_INVALID_OPERATION` (the grass no-shadow
branch is the copy source).

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Where | Why |
|------|-------|-----|
| Scatter point generation (`scatterProps`) | **CPU**, once at load | Sparse, deterministic, branchy (reject/exclusion tests). |
| Species table build, glb load (LOD0/mid/billboard), node-flatten, chunk insertion | **CPU**, once at load | I/O + scene-graph bookkeeping. |
| Per-frame LOD bucketing + crossfade alpha | **CPU**, per frame | Per-tree distance test; sparse (thousands, not millions) — cheaper than a GPU round-trip. Already how `TreeRenderer` works. |
| Mesh / billboard draw, wind sway, lighting, shadow sampling | **GPU** (vertex/fragment) | Per-vertex / per-pixel. |

**No CPU↔GPU parity contract in v2** — dropping the custom octahedral impostor removes the only
dual-implementation (the hemi-octahedral encode/decode the v1 baker and runtime had to agree on). The
artist LODs are plain meshes; there is no bake-vs-runtime map to pin with a parity test.

---

## 6. Asset pipeline & sourcing

### 6.1 Sourcing (v2 — LOLIPOP CC-BY packs)

**Source of record:** LOLIPOP's game-ready packs on Sketchfab (CC-BY 4.0), one creator / consistent
realistic style. Four temperate species families, all passing the D10 biome gate:

| Species | Pack (library folder) | Trees | LOD0 tris | Role |
|---------|-----------------------|-------|-----------|------|
| Pine (conifer) | `pine_lolipop_sketchfab_gltf` | 15 (big/large/medium/small/sapling × 3) | ~8–12 k | hero (large) + field (medium/small) |
| Fir (conifer) | `fir_lolipop_sketchfab_gltf` | 3 ("Christmas tree") | ~13 k | hero + field |
| Maple (broadleaf) | `maple_lolipop_sketchfab_gltf` | 12 Acer (sapling/small/medium/large × 3) | ~9–22 k | hero (large) + field |
| Birch (broadleaf) | `birch_lolipop_sketchfab_gltf` | 5 | ~9 k | field |

**Distinct-mesh budget (VRAM gate, §9):** the meadow uses a **curated subset** of these variants as the
*distinct* tree files — target **≈ 8–12 distinct meshes** (e.g. 3 pine sizes, 2 fir, 3 maple sizes,
2 birch). Variety beyond that comes from **instancing + per-tree scale/rotation**, not more files.
`ResourceManager` caches each `Model` by path, so N placements of one file share one GPU upload — the
VRAM cost scales with *distinct files*, not tree count. Hero vs field is just large-variant vs
medium/small-variant of the same species (D3); **no decimation** (packs are already game-budget).

- **Hero trees:** the `*_large_*` / `*_big_*` variants (tallest, most detailed LOD0).
- **Field trees:** the `*_medium_*` / `*_small_*` variants (the treeline body).
- **Lily pads (T6):** a better CC0/CC-BY lily or water-plant model to replace `lily_{large,small}.glb`
  (LOLIPOP and other CC-BY nature packs carry water/broadleaf plants; sourced at T6, licence recorded).

### 6.2 Prep (two scripted steps, repeatable)

1. **Fetch** — `tools/asset_prep/fetch_sketchfab_trees.py` (stdlib): with a Sketchfab API token in
   `SKETCHFAB_API_TOKEN`, hits the Data API (`/v3/models/{uid}/download`), downloads each pack's glTF
   zip into the categorised library (`Models/Nature/Trees/<species>_lolipop_sketchfab_gltf/`), and
   writes the `SOURCES_sketchfab.md` credit manifest (§6.3).
2. **Split** — `tools/asset_prep/split_tree_packs.py` (Blender-headless, `blender-5.2 --background
   --python`; use the **current stable Blender LTS** — confirm against blender.org before running).
   Per tree it exports **LOD0** (near), **LOD2** (mid) and the **billboard** as separate **glTF-separate**
   files (`.gltf` + `.bin` + shared `textures/`), **recentred so the trunk base sits at the origin**
   (y = 0, centred x/z), into `Models/Nature/Trees/gameready/<species>/`, symlinked to the engine's
   git-ignored `assets/models/nature_local/gameready/`.

   **Textures shared per species (VRAM-critical).** All variants of a species share one bark + one
   foliage texture set. The split exports **glTF-separate** (not embedded `.glb`) into a per-species
   directory with a **single shared `textures/` folder**, so every variant's glTF references the *same*
   texture files by relative path and `ResourceManager`'s path cache loads each map **once** for the
   whole species. (Embedding textures per file — the naïve `.glb` path — duplicates a species' maps
   across its variant files and blows the §9 budget; the T1 prototype hit exactly this, embedding
   ~20–64 MB per file.) **Downscale the shared maps to 1 K** (the engine uploads textures **uncompressed**,
   so pixel resolution — not JPG/PNG disk size — sets VRAM; §9), re-encoded as JPG/PNG (no upscaling).
   The classifier matches size words case-insensitively (`[A-Za-z]+`, so `Acer_Sapling_*` is not dropped).

`nature_local/` stays git-ignored (heavy binaries out of the public repo); both scripts + the
`SOURCES_sketchfab.md` manifest are committed so the set is reproducible from the token. Bundled
fallback: if a `nature_local/` asset is absent, the `propPath` hook already falls back to the
lightweight `nature/` Kenney glb, so a fresh clone still runs (just with the cartoon trees).

### 6.3 Attribution (CC-BY 4.0 — required)

The packs are **CC-BY 4.0**, not CC0: attribution is legally required (Steam-safe with a credits
line). The credit block —

> 3D tree models by **LOLIPOP** (sketchfab.com/lolipop_1707), licensed under CC BY 4.0.

— is recorded in `Trees/SOURCES_sketchfab.md` and **must appear in the game's credits screen**. T7's
checklist includes verifying it is present (the credits screen / an `ATTRIBUTIONS`/`CREDITS` doc).

---

## 7. Testing

- **No octahedral parity test in v2** — dropping the custom impostor removes the only CPU↔GPU contract
  (§5). The artist LODs are plain meshes; there is no bake/runtime map to pin. This is the single
  biggest test-surface reduction from the v2 simplification.
- **Existing tree/foliage tests stay green** — the `TreeInstance` round-trip
  (`FoliageChunk` / `FoliageManager` `SerializeDeserialize`, `test_foliage_chunk.cpp`) is untouched;
  species-index semantics unchanged.
- **Split-tool asset check** (CI-cheap, no GPU): a small test/script asserts each `gameready/<species>/`
  glTF loads as a valid `Model` with **≥2 materials** and its **trunk-base bound `y ≈ 0`** (the recentre
  held), and that a species' variants **share** their texture files (distinct-texture count per species
  is bounded) — guarding the §6.2 VRAM contract mechanically.
- **Visual-test viewpoints** (run `./vestige --visual-test` **from `build/bin/`**,
  `ASAN_OPTIONS=detect_leaks=0`): add `treeline_far` (billboard band), `tree_near` (LOD0 up close, at
  correct real-world scale), `tree_crossfade` (walk through the two LOD transitions), `hero_tree` (a
  large landmark), `pond_reflection` (treeline mirrored). Assert 0 GL errors + visual inspection of
  captures.
- **Perf gate:** `--profile-log` Tree-pass read on the RX 6600 at the tree viewpoints; **≤ 2.0 ms GPU
  Tree-pass at High** and **≥ 60 FPS at High** (§9). GPU-pass ms is the true metric (frame-total is
  polluted by screenshot readback). This gate + the visual-test captures are **manual / real-GPU-gated**
  (RX 6600 dev target, not the llvmpipe CI runner); the split-tool asset check + serialization unit
  tests are the CI-enforced part.

---

## 8. Accessibility

- **Wind gentle-capped**, dead-calm at wind 0 → reduced-motion floor (reuse the grass/foliage clamp).
- **Legibility via value + normal, not hue** — trees read by silhouette/shading, colour-blind safe.
- **Billboard alpha-cutout** (the artist's far card) reads as a crisp silhouette, not a shimmering
  translucent layer — no edge crawl at distance (motion-comfort).
- Tree density is bounded by the meadow scatter params (no seizure-risk flicker from over-dense
  alpha layers at distance — the far LOD is one flat card per tree, not stacked cards). Checkable via
  the `treeline_far` motion capture against the WCAG 2.3.1 ≤3-flashes/s threshold.

---

## 9. Performance plan (60 FPS hard floor)

- **LOD0 near only** (< `lodDistance` ≈ 40 m): a handful of full meshes, instanced. Hero-tree LOD0 is
  the heaviest single mesh but there are very few and only when close.
- **Mid (LOD2) mesh** for the mid band, **artist billboard** far (the treeline): one flat card per
  tree, instanced — cheap.
- **No bake step** (v2) — assets load straight off disk.
- **Tree-pass GPU budget:** **≤ 2.0 ms at High on the RX 6600** (context: the shipped Grass pass is
  ~1.2 ms). This is the numeric target the T7 gate checks — not just the whole-frame 60 FPS floor — so
  a Tree-pass-specific regression is caught even when the frame still clears 16.6 ms.
- **VRAM budget (v2):** no impostor atlas. The engine uploads textures **uncompressed** with full mips
  (`GL_RGBA8`/`GL_SRGB8_ALPHA8`, `texture.cpp:162/172`; no BC/KTX2 path), so a map's VRAM cost is
  ~1.33 × w·h·4 bytes **independent of its JPG/PNG disk size**. At the **1 K** prep resolution (§6.2) a
  1 K RGBA map ≈ 4 MiB + ~33 % mips ≈ **5.3 MiB**. Each **species** shares ~6 maps (bark + foliage ×
  diffuse/normal/ARM) ≈ **~30 MiB/species**; the **4 species** ≈ **~120 MiB** of texture VRAM, plus a
  few MiB of mesh buffers for the ≈ 8–12 distinct meshes. That fits the 8 GB RX 6600 with wide headroom
  and is below v1's 340 MiB atlas budget. (Note the two axes differ: textures scale with the **4
  species**; meshes with the **8–12 distinct files**.) **Shared textures are load-bearing** — embedding
  per file (T1's prototype) multiplies the texture cost by the variant count and breaks this. Levers if
  it grows: drop the ARM map to constants, or add a BC7 upload path (would cut texture VRAM ~4×).
  `TreeQuality` scales LOD switch distances, not texture size.
- **Shadow cast** adds LOD0 trees to the cascade depth pass — bounded (near trees only).
- Budget check happens at the T7 perf gate before ship; if the Tree pass exceeds 2.0 ms (or the frame
  threatens 16.6 ms), the first lever is `lodDistance` / `billboardDistance` (push cheaper LODs nearer),
  then the distinct-mesh count.

---

## 10. Implementation slices

| Slice | Work | Verify |
|-------|------|--------|
| **T1** | Asset prep — `fetch_sketchfab_trees.py` (done) + `split_tree_packs.py` finalised: per-tree LOD0 + LOD2 + billboard glTFs (glTF-separate, shared textures), recentred base-at-origin, **shared per-species external textures** (§6.2), curated ≈ 8–12 distinct variants, sapling-name fix. `SOURCES_sketchfab.md` committed. | Each `gameready/<species>/` glTF loads as a `Model` (≥2 materials, base `y ≈ 0`); a species' variants share their texture files; assets resolve via the `propPath`/`nature_local` hook. |
| **T2** | Revive `TreeRenderer`: species table (from `TreeSpeciesConfig` → LOD0/mid/billboard `Model*`), flatten node transforms into vertices at load, per-material **LOD0 + mid mesh draw** + **billboard draw** (replace both placeholder generators; instance layout: mat4 @6–9 binding 1 + crossfade-alpha/phase `vec2` @12 binding 3), 3-bucket LOD distance-switch + two crossfade bands, wind + CSM receive + directional light. **Widen `TreeRenderer::render` signature (+ `csm`, `dirLight`, wind), update the `engine.cpp:1643` call site, wrap it in its own `beginPass("Tree")` GPU-timer.** | Near mesh + mid mesh + far billboard render; crossfade alpha monotonic across both fade bands; shadows + light visible on trees; a **"Tree" GPU-pass appears in `--profile-log`**; `tree_near`/`tree_crossfade`/`treeline_far` captures + 0 GL errors. |
| **T3** | Meadow placement: route field-tree (medium/small variants) + hero-tree (large variants) scatter points through `placeTree`; remove the raw glb tree-prop `scatterGroup`; **placement scale tuned to the packs' real-world metres** (≈ 0.8–1.5, not Kenney's 1.3–2.4). | Trees appear at former prop spots at believable size; LOD switches walking toward/away. |
| **T4** | LOD0 (+ mid) shadow-caster pass (reuse foliage caster). | Trees cast ground shadows in the `tree_near` capture; shadow bias reuses the foliage caster's value (no new constant); 0 GL errors. |
| **T5** | Pond reflection: render trees in the water reflection pass (clipPlane). | Treeline mirrors in the pond (`pond_reflection` capture). |
| **T6** | Flowers cleanup: delete **only** the three `flower_*A.glb` entries from the shared `scatterGroup` call (keep `mushroom_red.glb` + `plant_bush.glb`; keep billboard star-mesh flowers). Replace lily pads with better CC0/CC-BY model. | Flower draw-call count drops; mushrooms/bushes still present; no double-draw; **new lily model visible in `pond_reflection` capture and old `lily_{large,small}.glb` no longer instantiated**; 0 GL errors. |
| **T7** | `TreeQuality` tier on `RendererQualitySink` (+ preset→tier test); perf gate on RX 6600 (≥60 FPS High); **verify the LOLIPOP CC-BY credit is in the credits screen** (§6.3). ROADMAP 3D_E-0033 ✅ + CHANGELOG. | ctest green; **Tree pass ≤ 2.0 ms + ≥ 60 FPS at High** (RX 6600); credit present; docs updated. |

Trees = T1–T5, T7. Flowers/lilies = T6. Matches the locked "trees first" order. (v2 dropped the old
octahedral-baker slice; slices renumbered T1–T7.)

---

## 11. Risks & scope caps

- **Billboards receive but do not cast shadows** (D4) — logged in the T4 commit + CHANGELOG per Rule 5.
- **Flat far billboard** (D2 tradeoff) — the artist's single card can read slightly flat on fast lateral
  motion past a dense treeline. Accepted for v2; the fix, if it ever matters, is the **deferred optional
  octahedral impostor upgrade** (below), not a v2 requirement.
- **Octahedral impostor deferred (optional future upgrade)** — the v1 view-adaptive baked impostor is
  documented history (git) and remains a clean drop-in *later*: it would replace only the billboard
  bucket (§4.3), needs its own baker + hemi-octahedral GLSL + CPU-mirror parity test, and buys a
  never-flat distant treeline. Not built in v2 by user choice; noted so the six-month reader knows it
  was considered, not missed.
- **Trees emit no motion vectors** (§4.4) — the VAO's 16-attribute budget is full, so the LOD-crossfade
  `vec2` takes **location 12** from the prev-model region (13–15 left un-enabled) that TAA/SMAA velocity
  would use. Trees are treated as static for motion-vector purposes (same as grass/foliage), so fast
  camera passes near a tree may show minor TAA ghosting. Logged per Rule 5; if tree motion vectors are
  ever needed, the crossfade data must pack into spare components elsewhere.
- **Blender dependency for asset prep** — the split script needs Blender on the prep machine; the
  committed glTF outputs (in `nature_local/`, git-ignored) mean the *engine* never needs Blender. It
  runs headless (no GUI). If Blender is unavailable, the fetched packs can be hand-split, but the
  scripted path is the reproducible one.
- **Shared-texture prep is load-bearing** (§6.2/§9) — if the split ever regresses to embedded-per-file
  textures, a species' maps duplicate across its variant files and the VRAM budget breaks. The T1
  asset check (§7) bounds the distinct-texture count per species to catch this mechanically.
- **Placement scale** — the packs are real-world metres (a large maple ≈ 18 m); the T3 scatter scale
  must be tuned down from the Kenney-era values or trees will be giant. Guarded by the `tree_near` /
  `hero_tree` visual captures.
- **No Formula-Workbench-fit constants** (project Rule 6) — the LOD switch distances, crossfade width,
  and placement scale are single artist-tuned knobs, not fitted curves. Nothing here needs the Workbench;
  noted so the six-month reader doesn't hunt for a coefficient table.

---

## 12. Cited sources

- **Tree assets (source of record):** LOLIPOP, game-ready tree packs on Sketchfab, **CC-BY 4.0** —
  Pine `e1e9c07b8e2e445c943fec660beefba2`, Realistic Fir `f58e8b6d733e4b0586e5b7db847b89e7`, Maple
  `b5d2833c258f4054a01ee2b4ef85adf0`, Birch `08fe5117138e4fdaa7ca440ef1201e07` —
  https://sketchfab.com/lolipop_1707 . Each ships LOD0–2 meshes + a billboard. Licence:
  https://creativecommons.org/licenses/by/4.0/ (attribution required — §6.3).
- **Sketchfab Data API (download):** `GET /v3/models/{uid}/download` with `Authorization: Token …` →
  temporary glTF zip URL — https://docs.sketchfab.com/data-api/v3/index.html .
- **Deferred-upgrade references (octahedral impostor, §11 — not built in v2):** Ryan Brucks (Epic),
  *Octahedral Impostors* — https://shaderbits.com/blog/octahedral-impostors ; wojtekpil,
  *Godot-Octahedral-Impostors* — https://github.com/wojtekpil/Godot-Octahedral-Impostors . Kept for
  whoever picks up the optional upgrade.

---

## 13. Cold-eyes loop log

_Filled as `/cold-eyes` loops run (project Rule 1 + global Rule 14). Loops run cold — no
prior-loop briefing. Convergence = zero substantive verified findings._

- **Loop 1 (2026-07-21)** — 3 lanes (impostor/shaders · renderer/placement · cross-cutting/rules).
  **CRITICAL 0 · HIGH 3 · MEDIUM 7 · LOW 8 · INFO 3**, all verified against source, all fixed.
  Substantive fixes: instance-attrib collision (real mesh uses locs 0–5 → mat4 @6–9 + alpha @10);
  hemi-octahedral fold named (was "standard octahedral map"); `TreeRenderer::render` signature must
  gain `csm`/`dirLight` (the `:1643` call passed neither); FBO-wide float-format constraint + 8-bit
  depth cap acknowledged; atlas mips + tile-padding added; reuse existing `TreeSpeciesConfig`;
  cross-doc conflict with the benchmark-scene doc reconciled (both docs amended); species VRAM cap
  (≤8) and numeric Tree-pass budget (≤2 ms) pinned; T7 flower-only deletion (keep mushroom/bush).
- **Loop 2 (2026-07-21)** — same 3 lanes, cold re-read. **CRITICAL 1 · HIGH 1 · MEDIUM 2 · LOW 8 ·
  INFO 2**, all verified, all fixed. The cold read caught a **regression Loop 1's own fix introduced**:
  the crossfade attribute was moved to location 10, which is the mesh's **bone-ID** slot (10–11 bones,
  6–9 instance mat4, 12–15 prev-model all reserved) → moved to a separate binding-3 VBO at location 16,
  and noted `setupInstanceAttributes`' binding-1 stride can't carry extra data. Also: added the
  impostor-local→world normal rotation for lighting (per-tree yaw); reconciled the 256↔340 MB VRAM
  figure; past-tensed the benchmark-doc supersession note (already amended); `placeTree` (grid-mapping)
  over raw `addTreeDirect`; Blender 4.5 LTS; Tree-pass ≤2 ms echoed into §7/T8; misc citation nits.
- **Loop 3 (2026-07-21)** — same 3 lanes, cold re-read. **CRITICAL 2 · HIGH 1 · MEDIUM 2 · LOW 5 ·
  INFO 2**, all verified, all fixed. Biggest catch: Loop 2's "location 16" is **out of range** —
  `GL_MAX_VERTEX_ATTRIBS`' guaranteed minimum is 16 (indices 0–15), and the mesh VAO consumes all of
  0–15 (verts 0–5, instance mat4 6–9, bones 10–11, prev-model 12–15; cross-checked
  `phase_10_rendering_design.md:104`). Moved the crossfade attribute to **location 12** by reclaiming
  the prev-model/motion-vector slots that static trees never set up → trees emit no motion vectors
  (scope cap §11). Also synced the T3/T4 **slice rows** (still said `@10` / `addTreeDirect`) to the
  fixed body; corrected the wind uniform (foliage's `u_windDirection`, not grass's `u_windDir`); added
  a parallax reference-pixel check to the §7 parity test; fixed the `TreeInstance` serialize-test
  citation; RGBA16F-out-of-budget + Blender-currency + bake-cost nits.
- **Loop 4 (2026-07-21)** — same 3 lanes, cold re-read. **CRITICAL 0 · HIGH 0 · MEDIUM 5 · LOW 6 ·
  INFO 4**, all verified, all fixed. No critical/high — the Loop-3 attribute-budget + slice-sync fixes
  held (confirmed by all three lanes). Refinements: parallax offset specified **per-frame in each
  frame's captured basis** (not one billboard basis → no cross-frame swim); multi-primitive glb **node
  transforms flattened into vertices at load**; trees get their **own `beginPass("Tree")` GPU-timer**
  (the ≤2 ms budget was unmeasurable inside the shared Foliage pass); T7 gains a **lily-replaced
  acceptance check**; conifer hero-vs-edge role reconciled; parallax basis pinned in the §7 test;
  materialIndex −1 fallback, 128²-gutter, Blender-currency, and §11 slot-precision nits.
- **Loop 5 (2026-07-21)** — same 3 lanes, cold re-read. **CRITICAL 0 · HIGH 1 · MEDIUM 1 · LOW 7 ·
  INFO 3**, all verified, all fixed. Renderer + cross-cutting lanes came back **clean** (0 crit/high/med;
  no body-vs-slice contradiction; all numbers consistent) — only label/line-cite nits. Impostor lane
  found one fix-introduced HIGH: Loop 4's "flatten to one instanced draw" would render a multi-material
  tree (bark + leaf-cutout) with a single bound material → LOD0 now emits **one instanced draw per
  material group** (mirroring §4.2's per-primitive bake); plus a MEDIUM — the fixed atlas gutter doesn't
  survive the full mip chain, so the shader now **clamps the sampled mip level**. Nits: §5 "hemi-"
  prefix, spruce-not-broadleaf, Rule-3-not-D1 label, `beginPass` line cites (1633/1652), WCAG-2.3.1
  falsification anchor.
- **Loop 6 (2026-07-21) — CONVERGED.** Same 3 lanes, cold re-read (user-requested confirming loop).
  **CRITICAL 0 · HIGH 0 · MEDIUM 2 · LOW 3 · INFO 4** — **all polish** (no structural/mechanical/
  architectural finding). Renderer lane verdict "sound"; cross-cutting "implementation-ready…
  refinements, not corrections"; impostor 0 crit/high. The Loop-5 fixes (per-material LOD0 draw, mip
  clamp) held. Polish applied: named the node→world matrix accumulation (`m_rootNodes`/`childIndices`),
  pinned the 8 default-High species as a candidate-pool subset, gave heroes a concrete seed + index
  scheme, sketched `ImpostorAtlas`, glossed `fieldSpecies`, corrected the editor tree-add call chain,
  bake-count arithmetic.

**v1 signed off (2026-07-21):** converged at loop 6 with only polish remaining (delegated cold-eyes
convergence). Loops 1–6 above are the **v1 (octahedral-impostor) audit trail** — retained as history.

**v2 (2026-07-21) — re-review REQUIRED, not yet run.** The T1 asset reality forced a material redesign
(LOLIPOP artist-LOD packs; custom octahedral baker dropped; sourcing + prep + slices reworked — see the
v2 change summary at the top). Per project Rule 9 / global Rule 14 this revision **must run `/cold-eyes`
to convergence before further implementation**. The v1 loop history does **not** carry over — v2 is
briefed cold. This section will gain the v2 loop log as those loops run.
