# Phase 10 — Meadow Realism C: Realistic Trees & Plants (3D_E-0033)

**Status:** **v2 SIGNED OFF (2026-07-21) — cold-eyes converged, implementing T1 → T7.** The original v1
assumed a single heavy hero mesh needing decimation + a custom-baked octahedral impostor. During T1 the
asset reality changed the design materially (see the v2 note below); the doc was reconciled to v2 and
re-run through `/cold-eyes` (7 loops, 3 lanes) to convergence — the renderer, asset-pipeline, and
cross-cutting lanes all verified clean/polish, so spec sign-off is delegated to that convergence per the
project workflow.
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
it is real-game-looking vegetation, sourced **CC-BY** / free-for-commercial so it is Steam-safe
(attribution required — §6.3).

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
    (`createPlaceholderTree`, `tree_renderer.cpp:204-320`). "LOD1 impostor" is a hand-coded 64×128
    RGBA blob (`generateBillboardTexture`, `:378-433`) — **not** rendered from any mesh, **not**
    view-dependent. Impostor generation does **not** exist yet.
  - **Species ignored.** `TreeInstance` carries a `speciesIndex` but the renderer draws one hardcoded
    placeholder for every tree.
  - **Meadow feeds it nothing.** The only callers of the tree-add path are the editor
    (`brush_tool.cpp:260` → `placeTree`; undo/redo via `PlaceTreeCommand` → `addTreeDirect`). The
    meadow scene plants zero trees into `FoliageChunk::getTrees()`, so `render()` draws nothing there.
- **Meadow trees today** are plain glb props with **no LOD**: a `scatterGroup` lambda
  (`engine.cpp:2610`) loads Kenney glbs (`tree_oak.glb`, `tree_pineDefaultA/B.glb`, `tree_default.glb`,
  `tree_fat.glb`, `tree_detailed.glb`). The tree scatter *call site* is `engine.cpp:2669`; the
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
    `birch_lolipop_sketchfab_gltf` (5). Each pack bundles LOD0–2 meshes + a billboard (LOD3) — **except
    birch, which ships only LOD0–2** (no billboard; its far tier falls back to LOD2, §4.1) — split into
    **bark + foliage** materials, PBR at ~8–22 k LOD0 tris. **CC-BY 4.0** → the credit block is recorded
    in `ASSET_LICENSES.md` + `THIRD_PARTY_NOTICES.md` (§6.3).
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
name the billboard glTF. **The mid (LOD2) path is derived from `meshPath`** by the split tool's naming
convention — swap the `_lod0` stem for `_lod2` (`…/pine_pine_large_1_lod0.gltf` → `…_lod2.gltf`);
`lodMidMesh` is `nullptr` if that file is absent, and the mid bucket then collapses (LOD0 fades
straight to billboard). This keeps the reused two-path config (no third field) while T2 still knows
where to load all three tiers. The renderer then holds a *runtime* table of loaded models:

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
that machinery and points it at the three real `Model`s. **Bucketing is sub-keyed by `speciesIndex`**:
the existing renderer draws one shared placeholder mesh per bucket, but v2 has a distinct `Model`/VAO
(and, for the far tier, a distinct per-species billboard material) per species (§4.1), which cannot share
one `glDrawElementsInstanced`. So each bucket becomes **one instanced draw per (species × LOD)** — the
billboard draw binds that species' billboard material (§9). The three distance bands (the distances below
are **design targets, `TreeQuality`-tunable — D7** — superseding the current `lodDistance=50` default;
`billboardDistance` is a **new** second-switch knob the 2-bucket renderer does not yet have):

- **LOD0 mesh** for distance `< lodDistance` (≈ 40 m) — full per-material draw (§4.4).
- **Mid (LOD2) mesh** for `[lodDistance, billboardDistance)` (≈ 40–90 m) — same per-material draw path,
  lighter mesh.
- **Billboard** for `[billboardDistance, maxDistance)` (≈ 90–200 m) — the artist's flat card. **A static
  quad drawn by the per-instance mat4 alone would render at a fixed orientation, not face the camera**
  (`TreeInstance::rotation` is a fixed per-tree yaw, `foliage_instance.h`). So the billboard bucket is
  **special-cased**: it is drawn with a **yaw-billboard shader** that rebuilds the card's horizontal
  basis from a per-pass **camera-right uniform** (rotating about the vertical/trunk axis so the card
  never tilts) — reusing the mechanism the existing placeholder billboard already uses (the camera
  uniforms `u_cameraRight`/`u_cameraUp` set per-pass in `render()` at `tree_renderer.cpp:184-190`, over
  the quad geometry from `createBillboardQuad` at `:322-376`). Note the existing billboard shader expands
  a `vec2 a_offset` (billboard-local) via `u_cameraRight`/`u_cameraUp` — so the artist card is fed to it
  as those 2D offsets + its foliage UVs (its authored size → the offset extents), **not** dropped in as
  raw 3D vertices. It is a single flat quad, so on fast lateral motion past a dense treeline it can read
  slightly flat — the accepted v2 tradeoff (D2); the optional octahedral upgrade (§11) is the fix if it
  ever matters.
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

> **Implementation note (T2, 2026-07-22):** the vertex-rewrite framing above is **not**
> implementable against a `ResourceManager`-loaded `Model` — `Mesh::upload()` discards its CPU
> vertex data, so there is nothing to flatten in place. The shipped code keeps the shared cached
> `Model`/`Mesh` VAOs untouched and instead applies each node's baked world matrix as a per-draw
> `u_nodeMatrix` uniform in `tree_mesh.vert` (`world = i_model * (u_nodeMatrix * a_position)`). Same
> rendered result, no per-instance vertex duplication, full ResourceManager reuse (Rule 3). The
> per-material grouping (below) is unchanged — it is built from the node walk's `{mesh, nodeMatrix,
> material}` draw list.
>
> **Revision (T2 fix, 2026-07-22):** first-light on-hardware review caught three issues the
> initial T2 got wrong. (1) **The far "billboard" is NOT a flat card.** The LOLIPOP LOD3 billboard
> glTF is a small 3-D *cloud of leaf cards* (~18 quads in a ~19×20×20 m box) whose UVs are baked
> into a 3×3 view-atlas. The initial code drew a synthetic camera-facing quad textured with the
> whole atlas → all 9 view-cells showed at once, mirrored (the "upside-down triplets" bug). Fixed
> by rendering the impostor **glTF as a third mesh tier** through the same instanced path
> (`billboardPrims`), which honours the baked UVs and is view-independent (so it serves the
> pond-reflection pass too). This **deletes** the separate `tree_billboard.*` shaders + synthetic
> quad. The impostor's atlas material is `BLEND`; drawn unsorted that haloes, so the far tier forces
> an order-independent **alpha-cutout at 0.4**. (2) **Birch dropped.** Every LOLIPOP birch variant
> maps its leaf cards across the *whole* bark+leaf atlas, so its canopy renders as bark; the
> maples/pines/firs map to their leaf region correctly. The birch field slot became a third maple.
> (3) **LOD distances retuned** (lod 40→45, billboard 90→180, max 200→350, fade 12→15) so the solid
> mid mesh holds far out and impostors only appear where fog already hides them — no visible pop-in.
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

> **Implementation note (T4, 2026-07-23):** shipped as `TreeRenderer::renderShadow()` + new
> `tree_shadow.{vert,frag}.glsl`, mirroring the foliage caster. The vert repeats
> `tree_mesh.vert`'s instanced transform + wind sway (so the shadow tracks the swaying canopy);
> the frag alpha-tests leaf cutouts and writes RSM flux to match the directional shadow map's
> MRT (`shadow_depth.frag` contract). The renderer's shadow pass gathers the shared
> `FoliageManager` chunks once and feeds both the foliage and tree casters per cascade
> (`Renderer::setTreeShadowCaster`, wired at `engine.cpp` init). Casters are bucketed into the
> **same tier the viewer sees** (LOD0 near, mid beyond) and culled at `billboardDistance`, so
> the far impostor tier does **not** cast (D4 scope cap). Verified on RX 6600 / Mesa: 0 GL
> errors, all ctest green, Tree GPU pass 0.12–0.22 ms. At the meadow's near-midday sun the cast
> shadows pool tightly under canopies (physically correct — short shadows), so they read subtly
> in the fixed midday viewpoints; they will strengthen at low sun angles once time-of-day lands.

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
  `ScatterPoint` → `TreeInstance{ …, fieldSpecies + i % heroSpecies }`, routed through `placeTree` like
  the field set. **The offset matters:** hero entries are appended *after* the `fieldSpecies` field
  entries in the single `m_species` table (§4.1), so a bare `i % heroSpecies` would index the first
  field entries and draw heroes as medium/small field trees (contradicting D3). `fieldSpecies + i %
  heroSpecies` lands on the hero entries (`m_species.size() == fieldSpecies + heroSpecies`).
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
| Birch (broadleaf) | `birch_lolipop_sketchfab_gltf` | 5 | ~8–20 k | field |

Each pack ships LOD0/1/2 meshes + a billboard, **except birch** (LOD0–2 only, no billboard — its far
tier falls back to the LOD2 mesh held to `maxDistance`, §4.1).

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
2. **Split** — `tools/asset_prep/split_tree_packs.py` (Blender-headless, `<blender> --background
   --python`, where `<blender>` is the installed Blender LTS binary — `blender-5.2` on this dev machine;
   use the **current stable LTS** and confirm against blender.org before running, so no version pin goes
   stale). Per tree it exports **LOD0** (near), **LOD2** (mid) and — **where the pack provides one** (all
   but **birch**, which ships only LOD0–2; its far tier falls back to LOD2-to-`maxDistance` per §4.1) —
   the **billboard**, as separate **glTF-separate**
   files (`.gltf` + `.bin` + shared `textures/`), **recentred so the trunk base sits at the origin**
   (y = 0, centred x/z), into `Models/Nature/Trees/gameready/<species>/`, symlinked to the engine's
   git-ignored `assets/models/nature_local/gameready/`.

   **Textures shared per species (VRAM-critical).** The **mesh** maps (bark + foliage) are shared by all
   variants of a species; the **billboard** maps are per-variant (each tree's card has its own artist
   pre-rendered texture — §9). To preserve the mesh-map sharing, the finalised tool exports each tree's
   **geometry + material bindings** (with image URIs rewritten to a shared folder — no per-variant image
   copies) and points every variant at **one shared per-species `textures/` folder** by relative URI. **Concretely:** a naïve
   `export_scene.gltf(export_format="GLTF_SEPARATE")` **copies** textures next to each output (the exact
   duplication to avoid). The required mechanism: **downscale each species' maps once (§below), write them
   to `gameready/<species>/textures/`, and post-process the emitted glTFs to rewrite their image URIs to
   that shared folder** — a one-time copy + rewrite yielding a self-contained per-species dir where each
   shared map exists on disk once. (Blender's `export_keep_originals=True` — which references the *source
   pack's* `textures/` in place with no copy — is **not** usable here: it would re-use the **un-downscaled
   4K source** maps and blow the §9 budget outright, e.g. birch 4 × 4096² ≈ 340 MiB. The downscale must
   happen, so the copy is not optional.) `ResourceManager`'s path cache then
   loads each shared mesh map **once** for the whole species (`resource_manager.cpp:40-72` caches `Texture`
   by path; embedded-in-`.glb` images bypass that cache — the T1 prototype hit exactly this, embedding
   ~20–64 MB per file). **Downscale** (the engine uploads textures **uncompressed**, so pixel
   resolution — not JPG/PNG disk size — sets VRAM; §9): shared **mesh** maps to **≤ 1024 on the longest
   edge** (source is **1 K–4 K**: pine `Bark` 1024×2048 / `Clusters` 2048²; fir/birch mesh maps **up to
   4096²** — fir bark base/normal are 2048×4096, birch's four maps 4096² — a real 4K→1K re-encode),
   per-variant **billboard** maps to **512²** (they are the distant
   card). Re-encode JPG/PNG (no upscaling). The classifier matches size words case-insensitively
   (`[A-Za-z]+`, so `Acer_Sapling_*` is not dropped).

`nature_local/` and the downloaded packs stay out of the repo (git-ignored / in the external library);
the **two scripts are committed** so the set is reproducible from the token. The `SOURCES_sketchfab.md`
manifest is written beside the downloads **in the library** (not the repo) — the repo-tracked home of the
CC-BY obligation is the `ASSET_LICENSES.md` + `THIRD_PARTY_NOTICES.md` rows (§6.3) that T7 verifies.
Bundled fallback: if a
`nature_local/` asset is absent, the `propPath` hook already falls back to the lightweight `nature/`
Kenney glb, so a fresh clone still runs (just with the cartoon trees).

### 6.3 Attribution (CC-BY 4.0 — required)

The packs are **CC-BY 4.0**, not CC0: attribution is legally required (Steam-safe). The credit block —

> 3D tree models by **LOLIPOP** (sketchfab.com/lolipop_1707), licensed under CC BY 4.0.

— goes in the project's **established, repo-tracked attribution files**: a row in `ASSET_LICENSES.md`
and the full credit line in `THIRD_PARTY_NOTICES.md` (the same mechanism the sibling meadow doc uses,
and where a future in-game credits screen would source its text — the engine has no credits UI today).
The `SOURCES_sketchfab.md` manifest beside the downloads (§6.2) is a convenience record, **not** the
tracked obligation. T7's checklist verifies the `ASSET_LICENSES.md` + `THIRD_PARTY_NOTICES.md` rows
exist. (`ASSET_LICENSES.md`'s charter is assets that *ship* in the public repo; the LOLIPOP packs are
git-ignored today, so the row is a **forward record** — it takes effect when the trees ship via a
release / the assets repo — with a one-line note saying so, so the file's scope stays honest.)

---

## 7. Testing

- **No octahedral parity test in v2** — dropping the custom impostor removes the only CPU↔GPU contract
  (§5). The artist LODs are plain meshes; there is no bake/runtime map to pin. This is the single
  biggest test-surface reduction from the v2 simplification.
- **Existing tree/foliage tests stay green** — the `TreeInstance` round-trip
  (`FoliageChunk` / `FoliageManager` `SerializeDeserialize`, `test_foliage_chunk.cpp`) is untouched;
  species-index semantics unchanged.
- **Split-tool asset check** (**dev-machine local gate**, not CI — the `gameready/` assets live in
  git-ignored `nature_local/` and are absent from a fresh CI clone, so a CI test could only vacuously
  skip): a small script asserts each `gameready/<species>/` glTF loads as a valid `Model` with **≥2
  materials** (scoped to the **mesh** `_lod0`/`_lod2` glTFs — a `_billboard.gltf` legitimately has **1**
  material, so the ≥2 check excludes it) and its **trunk-base bound `y ≈ 0`** (the recentre held); that a
  species' variants share their **mesh** texture files — the shared **mesh**-map count per species is
  bounded (≲6), while billboard maps are legitimately per-variant (§9); **and that every uploaded map is
  downscaled — mesh maps ≤ 1024 on the longest edge, billboard maps ≤ 512²** (the §9 budget rests on the
  downscale as much as the sharing; the 4K-source fir/birch maps would otherwise upload ~16× the pixels
  and pass a count-only check) — guarding the §6.2 VRAM contract mechanically.
- **Visual-test viewpoints** (run `./vestige --visual-test` **from `build/bin/`**,
  `ASAN_OPTIONS=detect_leaks=0`): add `treeline_far` (billboard band), `tree_near` (LOD0 up close, at
  correct real-world scale), `tree_crossfade` (walk through the two LOD transitions), `hero_tree` (a
  large landmark), `pond_reflection` (treeline mirrored). Assert 0 GL errors + visual inspection of
  captures.
- **Perf gate:** `--profile-log` Tree-pass read on the RX 6600 at the tree viewpoints; **≤ 2.0 ms GPU
  Tree-pass at High** and **≥ 60 FPS at High** (§9). GPU-pass ms is the true metric (frame-total is
  polluted by screenshot readback). This gate + the visual-test captures are **manual / real-GPU-gated**
  (RX 6600 dev target, not the llvmpipe CI runner). The **serialization unit test** is the CI-enforced
  part; the split-tool asset check, perf gate, and visual captures are **dev-machine local gates** (the
  `gameready/` assets are git-ignored, so CI has nothing to load).

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
  (`GL_RGBA8`/`GL_SRGB8_ALPHA8` — `selectInternalFormat`, `texture.cpp:89`; storage + mips at `:162/172`;
  no BC/KTX2 path), so a map's VRAM cost is ~1.33 × w·h·4 bytes **independent of its JPG/PNG disk size**.
  A 1 K RGBA map ≈ 4 MiB + ~33 % mips ≈ **5.3 MiB**; a 512² map ≈ **1.3 MiB**. Two texture tiers with
  **different sharing** (verified against the pine pack's glTF `images`):
  - **Mesh maps are SHARED per species** — up to ~6 maps (bark + foliage × baseColor/normal/metallic-
    roughness — glTF metal-rough workflow, not a packed ARM texture; some species carry fewer — e.g.
    birch has 4, no metallic-roughness), one set for all variants of a species. At **1 K** (§6.2):
    ≲32 MiB/species × **4 species** ≈ **~110–120 MiB**.
  - **Billboard maps are PER-VARIANT** (each tree's card has its own artist pre-rendered baseColor +
    normal, and fir adds a near-flat metallic-roughness — 2–3 maps — they do **not** share). At **512²**
    (§6.2, distant card): ~2–3 maps × 1.3 MiB × the billboard-bearing curated variants (**≈ 8–10**; birch
    has no billboard) ≈ **~25 MiB** (a safe over-estimate).
  - **Total ≈ ~145 MiB** texture VRAM + a few MiB of mesh buffers. Fits the 8 GB RX 6600 with wide
    headroom, below v1's 340 MiB atlas. (Three axes: mesh textures scale with **4 species**; billboard
    textures + mesh buffers with the **8–12 distinct variants**.) **Shared mesh textures are
    load-bearing** — embedding per file (T1's prototype) multiplies the mesh-map cost by the variant
    count. Levers if it grows: drop the metallic-roughness map to constants, shrink billboards further, or add a BC7
    upload path (~4× cut). `TreeQuality` scales LOD switch distances, not texture size.
- **Shadow cast** adds LOD0 trees to the cascade depth pass — bounded (near trees only).
- Budget check happens at the T7 perf gate before ship; if the Tree pass exceeds 2.0 ms (or the frame
  threatens 16.6 ms), the first lever is `lodDistance` / `billboardDistance` (push cheaper LODs nearer),
  then the distinct-mesh count.

---

## 10. Implementation slices

| Slice | Work | Verify |
|-------|------|--------|
| **T1** | Asset prep — `fetch_sketchfab_trees.py` (done) + `split_tree_packs.py` finalised: per-tree LOD0 + LOD2 + billboard glTFs (glTF-separate, shared textures), recentred base-at-origin, **shared per-species external textures** (§6.2), curated ≈ 8–12 distinct variants, sapling-name fix. Scripts committed; `SOURCES_sketchfab.md` written in the library (§6.2). | Each `gameready/<species>/` glTF loads as a `Model` (mesh `_lod0`/`_lod2` ≥2 materials; billboard 1; base `y ≈ 0`); a species' variants share their **mesh** texture files; assets resolve via the `propPath`/`nature_local` hook. |
| **T2** | Revive `TreeRenderer`: species table (from `TreeSpeciesConfig` → LOD0/mid/billboard `Model*`), flatten node transforms into vertices at load, per-material **LOD0 + mid mesh draw** + **billboard draw** (replace both placeholder generators; instance layout: mat4 @6–9 binding 1 + crossfade-alpha/phase `vec2` @12 binding 3), 3-bucket LOD distance-switch + two crossfade bands, wind + CSM receive + directional light. **Widen `TreeRenderer::render` signature (+ `csm`, `dirLight`, wind), update the `engine.cpp:1643` call site, wrap it in its own `beginPass("Tree")` GPU-timer.** | Near mesh + mid mesh + far billboard render; crossfade alpha monotonic across both fade bands; shadows + light visible on trees; a **"Tree" GPU-pass appears in `--profile-log`**; `tree_near`/`tree_crossfade`/`treeline_far` captures + 0 GL errors. |
| **T3** | Meadow placement: route field-tree (medium/small variants) + hero-tree (large variants) scatter points through `placeTree`; remove the raw glb tree-prop `scatterGroup`; **placement scale tuned to the packs' real-world metres** (≈ 0.8–1.5, not Kenney's 1.3–2.4). | Trees appear at former prop spots at believable size; LOD switches walking toward/away. |
| **T4** | LOD0 (+ mid) shadow-caster pass (reuse foliage caster). | Trees cast ground shadows in the `tree_near` capture; shadow bias reuses the foliage caster's value (no new constant); 0 GL errors. |
| **T5** | Pond reflection: render trees in the water reflection pass (clipPlane). | Treeline mirrors in the pond (`pond_reflection` capture). |
| **T6** | Flowers cleanup: delete **only** the three `flower_*A.glb` entries from the shared `scatterGroup` call (keep `mushroom_red.glb` + `plant_bush.glb`; keep billboard star-mesh flowers). Replace lily pads with better CC0/CC-BY model. | Flower draw-call count drops; mushrooms/bushes still present; no double-draw; **new lily model visible in `pond_reflection` capture and old `lily_{large,small}.glb` no longer instantiated**; 0 GL errors. |
| **T7** | `TreeQuality` tier on `RendererQualitySink` (+ preset→tier test); perf gate on RX 6600 (≥60 FPS High); **add the LOLIPOP CC-BY rows to `ASSET_LICENSES.md` + `THIRD_PARTY_NOTICES.md`** (§6.3). ROADMAP 3D_E-0033 ✅ + CHANGELOG. | ctest green; **Tree pass ≤ 2.0 ms + ≥ 60 FPS at High** (RX 6600); CC-BY credit rows present; docs updated. |

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
  https://sketchfab.com/lolipop_1707 . Each ships LOD0–2 meshes + a billboard (birch: LOD0–2 only). Licence:
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

**v2 (2026-07-21) — CONVERGED (7 loops, 3 cold lanes: asset-pipeline · renderer/placement ·
cross-cutting/rules).** The T1 asset reality forced a material redesign (LOLIPOP artist-LOD packs; custom
octahedral baker dropped; sourcing + prep + slices reworked). Briefed cold each loop (no prior-fix list);
v1 history did not carry over. Trajectory — every finding verified against source before fixing:
- **Loop 1** — HIGH 2 · MED 4 · LOW 3: §9 VRAM measured *disk* size not *uncompressed GPU* VRAM (recomputed);
  billboard not auto camera-facing (specified the yaw-billboard shader path); renderer is 2-bucket not
  "3-bucket" (reworded to an extension); glTF-separate outputs are `.gltf` not self-contained `.glb`;
  stale "parity"/"Testing & parity"; sibling benchmark-doc note still said "baked octahedral impostors".
- **Loop 2** — HIGH 1 · MED 2: billboards are per-variant textures (not shared) → VRAM undercount fixed;
  mid-LOD (LOD2) path had no source (derive `meshPath` `_lod0`→`_lod2`); §7 asset check can't run in CI
  (assets git-ignored) → dev-machine local gate.
- **Loop 3** — MED 1: named the concrete shared-texture export mechanism; per-`(species×LOD)` draw; misc.
- **Loop 4** — MED 5: §1 still said "CC0" (assets are CC-BY); attribution pinned to a phantom credits
  screen → retargeted to `ASSET_LICENSES.md`+`THIRD_PARTY_NOTICES.md` (project convention); birch ships
  **no** billboard; sources are 1K–4K not 1K–2K; asset check must also guard the downscale. Renderer CLEAN.
- **Loop 5** — HIGH 1 (cross-doc): ROADMAP 3D_E-0033 still described v1 → synced to v2. Plus billboard-≥2-
  material false-fail (scoped to mesh glTFs) + precision. Renderer CLEAN.
- **Loop 6** — HIGH 1 · MED 1: a **loop-5 fix-introduced regression** — `export_keep_originals` re-used the
  un-downscaled 4K source maps (budget blow-out) → removed, copy-downscale-rewrite is the only path; hero
  index `i % heroSpecies` would draw heroes as field trees → offset `fieldSpecies + i % heroSpecies`.
- **Loop 7 (confirm, asset lane) — CLEAN.** Export mechanism + VRAM budget verified sound against the real
  packs; only conservative-estimate/phrasing INFO. Convergence.

**v2 signed off (2026-07-21):** spec sign-off delegated to cold-eyes convergence (project workflow) —
converged at loop 7 with only polish remaining. Implementation proceeds T1 (finalise split tool) → T7.
