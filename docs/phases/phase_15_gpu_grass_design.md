# Phase 15 — GPU Procedural Grass (real Bézier-blade field)

User redirection after the billboard-grass work (3D_E-0038): *adding more billboard
tufts will never match the reference meadows — we need a way to paint vast
expanses of real grass, like UE5's PCG / landscape grass, but our own
(non-proprietary) implementation. Grass first, sprinkles of flowers after.*

This is a **new rendering subsystem**, not a tuning change: grass becomes **real
3-D blade geometry generated on the GPU** from procedurally-scattered seeds, not
flat textured cards. It replaces the billboard **grass** (keeps the billboard path
for the sparse flowers). Fixture: the 3D_E-0027 meadow.

Layman line: *grass made of thousands of real 3-D blades the graphics card grows
across the field — like the big engines do — instead of flat cards.*

**Sections:** 1 Goal · 2 Non-goals · 3 Research · 4 Current state · 5 Architecture
(5.1 blade geometry · 5.2 placement/PCG · 5.3 LOD+cull · 5.4 shading+wind · 5.5
data model · 5.6 integration) · 6 CPU/GPU placement · 7 Performance · 8 Testing ·
9 Assets · 10 Slices (G1–G5) · 11 Accessibility · 12 Risks · 13 Open questions ·
14 Cold-eyes log · 15 Sources.

---

## 1. Goal

A **continuous field of real 3-D grass blades** that reads like the photoreference
meadows — blades that catch light, glow when backlit, bend in the wind, and ground
into the terrain — covering the meadow densely and holding **≥ 60 FPS at High on
the RX 6600**.

- **Real per-blade geometry**, GPU-generated from a per-blade seed (Bézier ribbon,
  ~6–10 triangles), not billboards, not shell texturing.
- **Procedural placement ("PCG"):** blades scattered by rule — only where the
  terrain **grass** splat layer has weight, thinned on slopes, off water and the
  earthy dirt patches (C1) and the pond.
- **Scales** via distance LOD (fewer, simpler blades far) + frustum culling.
- **Grass first.** Flowers stay the existing billboard path as sparse "sprinkles"
  (3D_E-0038 C3 flowers survive; only billboard *grass* is replaced).

## 2. Non-goals (this phase)

- **No mesh/task shaders.** They are **never in GL core** — mesh/task shaders exist
  only as `GL_NV_mesh_shader` / `GL_EXT_mesh_shader` vendor extensions (not even in
  GL 4.6, the final GL version). The blade geometry is built in the **vertex shader**
  from `gl_VertexID` + the per-blade seed — the portable equivalent (§5.1).
- **No shell texturing.** The stacked-transparent-shell technique reads as "fuzz",
  not distinct blades, and is overdraw-heavy — wrong aesthetic for the references
  (research §3).
- **v1 defers the fully GPU-driven pipeline.** v1 places blades on the **CPU** per
  terrain chunk and frustum-culls **per chunk** on the CPU; the per-blade GPU
  compute cull + `glMultiDrawElementsIndirect` + GPU placement is **v2**, dropped
  in additively because v1 stores blade seeds in an **SSBO from day one** (§5.5).
  CPU per-chunk placement is a standard, well-precedented first step; open-source
  reproductions (e.g. GodotGrass) reach this quality without the full GPU-driven
  pipeline (research §6).
- **No Nanite / virtualised micro-geometry** — that is the separate, pre-existing
  **Phase 14** ("virtualised geometry") design; this GPU-grass work is **Phase 15**
  to avoid the collision.
- **No new flower work** — flowers are out of scope beyond keeping the C3 billboard
  sprinkles rendering.

## 3. Research summary (what current practice recommends)

- **Each blade is a procedural Bézier ribbon built on the GPU** from a small seed
  (root position, facing, height, width, lean/bend). Ghost of Tsushima uses a cubic
  Bézier; the AMD GPUOpen sample a quadratic (P0=root, P1=root+up·height,
  P2=P1+dir·height·0.3). Vertices are the curve evaluated at a few `t`, offset
  perpendicular for width, tapering to the tip. **~6–10 triangles / blade**
  (GoT near ≈15 verts, far ≈7; GPUOpen 8 verts→6 tris). The **vertex shader builds
  the mesh** — no authored blade mesh is stored. *(GoT GDC 2021; GPUOpen; GodotGrass.)*
- **View-facing widening:** edge-on blades are stretched horizontally in view space
  so a thin blade never shrinks to a sub-pixel sliver (aliasing/shimmer fix).
- **Placement is rule-based scatter** (the "PCG" part): a jittered/uniform grid per
  terrain tile, modulated by a density mask / landscape-layer weight — grass grows
  where the "grass" layer is painted, thinned on slope/water. This is exactly UE5
  **Landscape Grass Types** (Epic recommends them over PCG for a ground-cover
  carpet). *(UE5 landscape-grass docs; StraySpark PCG guide.)*
- **Scaling to hundreds of thousands / millions of blades** = per-instance seeds in
  an **SSBO** + **distance LOD** (fewer blades *and* fewer segments far, survivors
  widened to hold apparent density, blended to avoid pop) + **frustum cull**
  (per-chunk CPU, or per-blade GPU compute writing an indirect-draw arg buffer) +
  **indirect instanced draw** (one draw for the field). *(GoT; GPUOpen; Cyanilux;
  Lingtorp GL indirect.)*
- **OpenGL 4.5 has all of it in core** — compute (4.3), SSBOs (4.3), indirect draw
  (`glDrawArraysIndirect` 4.0 / `glMultiDrawElementsIndirect` 4.3), DSA (4.5). No
  mesh shaders needed; the VS generates the blade.
- **Shading:** normal biased toward vertical/terrain-normal (so slopes/field aren't
  noisy); **view·light translucency glow** (the signature backlit-grass look);
  **height-based base ambient occlusion** (dark at the root → grounds the blade);
  boosted ambient so backsides read. **Wind:** scrolling 2-D noise → bend the upper
  control point, scaled by height along the blade, with a **per-blade phase** so
  neighbours don't move in lockstep. *(GPU Gems ch.7/16; GodotGrass.)*
- **Pragmatic v1** (research §6): CPU jittered-grid placement per chunk (sample
  terrain height/normal/grass-weight) → SSBO; VS Bézier blade; per-chunk CPU
  frustum cull; distance LOD. Additive v2 = GPU compute cull + indirect + GPU
  placement.

Sources: §15.

## 4. Current state (verified against source — engine infrastructure map)

The engine already ships every GPU primitive; the grass system assembles them.

- **Compute shaders — mature.** `Shader::loadComputeShader` (`shader.cpp:121-171`);
  dispatch idiom `use() → glBindBufferBase(GL_SHADER_STORAGE_BUFFER,…) → set uniforms
  → glDispatchCompute → glMemoryBarrier` (`gpu_particle_system.cpp:234-259`). 20
  `.comp.glsl` shaders exist incl. an **unused `frustum_cull.comp.glsl`**.
- **SSBOs — mature.** DSA create + `glBindBufferBase`; `std430`-matched CPU structs
  with `static_assert` on size (`gpu_particle_system.cpp:37-47,145-195`). Per-instance
  matrix SSBO in `indirect_buffer.{h,cpp}`.
- **Indirect draw — present.** `glMultiDrawElementsIndirect` (`IndirectBuffer`,
  `indirect_buffer.cpp:141`); `glDrawArraysIndirect` with a **compute-written**
  command buffer + `GL_COMMAND_BARRIER_BIT` (`gpu_particle_system.cpp:369-381,409`)
  — exactly the v2 pattern.
- **Terrain scatter data.** `Terrain::getHeight(wx,wz)` (`terrain.cpp:121`),
  `getNormal` (`:148`), `getSplatWeight(texelX,texelZ)→vec4 (R=grass,G=rock,B=dirt,
  A=sand)` (`terrain.cpp:546`, world→texel via `worldToTexel` `:633`), and GPU
  textures `getHeightmapTexture/getNormalMapTexture/getSplatmapTexture`
  (`terrain.h:126-132`) for a v2 compute path. **Grass weight = `.r`.** *(Grass
  placement does not currently consult the splat — new gating logic, accessor ready.)*
- **Render loop.** Passes wrapped in `beginPass("<Name>")`: Scene → **Terrain** →
  **Foliage** (`engine.cpp:1615-1644`) → Water → Particles → PostProcess. The new
  grass pass slots at the **Foliage** step (after Terrain, before Water). Shadow
  pass runs in `beginFrame`.
- **Shadows.** CSM (`cascaded_shadow_map.h`); foliage is registered as a shadow
  caster via `Renderer::setFoliageShadowCaster` (`renderer.cpp:2793`) and cast into
  every cascade over all chunks (`renderer.cpp:3897-3915`), and receives by binding
  the cascade array (`foliage_renderer.cpp:184-206`). New grass reuses the receive
  path (it does not cast in v1 — §5.4).
- **Billboard `FoliageRenderer` (being replaced for grass).** 3-quad star card
  (0.26×0.4 m), `FoliageInstance` VBO (pos/rot/scale/tint), `glDrawArraysInstanced`,
  **CPU** per-chunk+per-instance distance cull, no GPU cull/indirect. Preserve its
  `FoliageQuality` tier + distance-fade + wind concepts. It stays for **flowers**
  (types 1/2/3) and as a possible far-field fallback.
- **GL 4.5 core confirmed** — GLFW 4.5 core forward-compat (`window.cpp:38-41`);
  all shaders `#version 450 core`; compute/SSBO/indirect/DSA all exercised at
  runtime (driver support proven; RX 6600 is GL 4.6).

## 5. Architecture

New subsystem **`GrassRenderer`** (`engine/renderer/grass_renderer.{h,cpp}`) +
`assets/shaders/grass.{vert,frag}.glsl` (+ `grass_shadow.*` and, in v2, a
`grass_cull.comp.glsl`). Owns the blade-seed SSBOs (per chunk), the LOD/cull logic,
and the draw. Coexists with `FoliageRenderer` (flowers).

### 5.1 Blade geometry — vertex-shader-generated Bézier ribbon

- **No stored blade mesh.** Each blade is **one triangle strip** drawn per instance;
  the vertex shader computes each vertex from `gl_VertexID` + the per-blade seed
  fetched by `gl_InstanceID` from the SSBO. An **N-segment** blade has rows `0..N`
  with 2 verts per row (left/right) collapsing to a single tip vertex → **`2N+1`
  strip vertices** (near LOD N=7 → 15 verts; far N=3 → 7 verts). Drawn with
  **`GL_TRIANGLE_STRIP`** — each instanced draw replays the strip independently, so
  strips do not connect across blades. *(This is the topology contract for G1 —
  `GL_TRIANGLE_STRIP`, `2N+1` verts, NOT a `GL_TRIANGLES` list.)*
- **Curve.** Quadratic Bézier (GPUOpen form): `P0 = root`, `P1 = root + up·height`,
  `P2 = P1 + facingDir·height·lean`. Evaluate at `t = row/N`; offset ±perpendicular
  (facing) by a width that **tapers to 0 at the tip**. The two verts of a row share
  `t`; the last row is a single centred tip vertex.
- **Per-vertex normal** from the curve tangent × width axis, then **biased toward
  the terrain normal / vertical** by a configurable amount so the lit field is not
  noisy (research §3).
- **View-facing widening** (§3): widen the blade in view space as it turns edge-on
  to the camera, clamped, to kill sub-pixel shimmer.
- **Opaque, not alpha-blended.** Real blades are solid geometry → render **opaque**
  (depth-tested, two-sided lighting with back-face cull off), which avoids the
  **alpha-blend sort + transparency overdraw** the billboard cards paid. **Trade to
  watch:** thin blades still incur **2×2-quad overshading** — sub-pixel / near-edge
  triangles shade whole fragment quads, wasting fragment work — mitigated by
  view-facing widening + LOD but not eliminated. So *"a denser real-blade field is
  cheaper per-pixel than the billboards it replaces"* is a **hypothesis to verify at
  G5**, with quad-overshading named as the primary vertex/fragment cost to watch —
  not an asserted fact (§7).
- The blade math (curve eval + width taper + tip) is factored into a **pure CPU
  mirror** `grassBladeVertex(seed, row, side) → position` for a Rule-7 parity unit
  test against the GLSL (like `terrain_material_blend.h`).

### 5.2 Placement / PCG (v1: CPU per chunk)

- **Chunked scatter.** Partition the meadow into grass chunks (reuse the terrain
  grid / a fixed cell, e.g. 8–16 m). For each chunk, a **jittered grid** of candidate
  blade roots at the target near-density; per candidate:
  - sample `terrain.getHeight` (seat the root) and `terrain.getNormal` (slope +
    orientation);
  - **gate on the grass splat weight** `getSplatWeight(...).r` — spawn probability
    ∝ grass weight, so blades grow where grass is painted and thin out over the
    **C1 dirt patches** (the earthy bare ground shows through naturally) and rock;
  - **clamp each candidate to the meadow interior before sampling** — `getSplatWeight`
    returns full-grass `(1,0,0,0)` for out-of-bounds texels (`terrain.cpp:548-551`),
    so an unclamped border candidate would read grass=1 and over-spawn; candidates
    stay within the terrain bounds inset by a margin (as the grass block's
    `EDGE_MARGIN` does);
  - **reject on steep slope** (normal.y below a threshold) and inside the **pond /
    water exclusion**;
  - randomise facing, height, width, lean, tint, and a per-blade hash (wind phase),
    all seeded deterministically from the chunk id + index (reproducible; testable).
- **Output:** a per-chunk **SSBO of `GrassBlade` seeds** (§5.5), uploaded once at
  meadow build (rebuilt only if the terrain changes). Chunk AABB stored for cull.
- **PCG rule set is data-driven** via a `GrassConfig` (near-density, slope cutoff,
  height range, grass-weight response, exclusion) so the meadow tunes it without
  new code — the "paint vast expanses" control surface.

### 5.3 LOD + culling (v1)

- **Per-chunk frustum cull (CPU).** Reuse the `getVisibleChunks` AABB-vs-frustum
  pattern; skip non-visible chunks' draws entirely.
- **Distance LOD per chunk** (two axes, research §3):
  - **fewer segments** — pick the blade vertex count (15/11/7) by chunk distance;
  - **fewer blades** — draw only `instanceCount · lodFraction` of the chunk's
    blades far away (the seeds are ordered so a prefix is a valid thinned subset),
    and **widen** survivors slightly to hold apparent density.
  - LOD tiers are distance bands with a small blend region; the blended tip width /
    fade avoids pop (research §3). Pure `grassLodForDistance(dist)` helper →
    unit-tested.
- **Draw (v1):** per visible chunk, `glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0,
  vertsForLod, chunkBladeCount·lodFraction)` (`vertsForLod = 2N+1`, §5.1), seeds
  read from the chunk SSBO bound at a binding point. One draw per visible chunk
  (tens), not per blade.
- **v2 (future, additive):** a `grass_cull.comp.glsl` tests blades per-frame,
  compacts survivors + writes an indirect-args buffer, and the field draws in **one**
  `glMultiDrawElementsIndirect` — the particle indirect precedent
  (`gpu_particle_system.cpp:369-409`). The v1 SSBO layout is chosen so this is
  additive, not a rewrite.

### 5.4 Shading + wind

- **Fragment shading:** Lambert diffuse with the vertical-biased normal; **view·light
  translucency** term (backlit glow, `pow(max(dot(view,-lightDir),0), k)` gated by
  the blade facing away from the light); **height-based base AO** (dark at root →
  bright at tip, coupled with wind bend); boosted ambient (power curve) so backsides
  read; receives **CSM shadows** (bind cascade array, reuse the foliage receive
  path). Colour = blade tint (green with per-blade variation) × light.
- **Wind (vertex):** sample scrolling 2-D value noise by world XZ (reuse the engine
  wind direction/speed from `EnvironmentForces`, as billboard grass does at
  `engine.cpp:1625-1627`) → bend the Bézier `P1/P2` control points, scaled by height
  along the blade (root fixed, tip moves most), offset by the **per-blade phase**
  (from the seed hash) so neighbours desync; a small high-frequency jitter for gust
  turbulence.
- **Shadow casting:** grass casting into the CSM is **expensive** (research §5 notes
  it is often skipped/limited). **Decided for v1: grass does NOT cast shadows** (it
  still *receives* them; a High/Ultra-only cast is a later candidate). This is a
  deliberate scope cap — logged per project Rule 5 in the **G4 commit + CHANGELOG**
  when the shading slice ships. The §13 open-questions entry is **confirm-only** (the
  decision stands unless the reviewer objects), not a re-open.

### 5.5 Data model (SSBO from day one)

`std430` `GrassBlade` seed — **committed layout** (CPU struct with
`static_assert(sizeof(GrassBlade)==32)`, like `GPUParticleGPU`):
```
struct GrassBlade {              // std430, 32 bytes
    glm::vec3 rootPos;   float height;                          // bytes 0..15
    float facingAngle;   float lean;  float width;  uint32_t hash;  // bytes 16..31
};                               // hash = wind phase + per-blade tint/height variation
```
`facingAngle` (radians) is cheaper than a `vec2` and keeps the struct at two
16-byte rows. This layout is fixed now (not "finalised in G1") so G1's Verify has a
concrete `static_assert` seam. Bound with
`glBindBufferBase(GL_SHADER_STORAGE_BUFFER, …)`, indexed by `gl_InstanceID`. Per-chunk SSBOs; a chunk-descriptor array (AABB, blade count, base
offset) drives cull/LOD. This layout is the v1↔v2 seam: v2's compute cull reads the
same seeds and writes a compacted visible list + indirect args.

### 5.6 Integration

- **Meadow wire-up:** replace the billboard-**grass** `paintFoliage(GRASS_TYPE_ID…)`
  block with `GrassRenderer::buildField(terrain, meadow bounds, pond exclusion,
  GrassConfig)`. **Keep** the C3 billboard **flower** clusters (types 1/2/3) and C1
  earthy ground unchanged. The billboard `FoliageRenderer` stays (flowers; possible
  far-field grass fallback later).
- **Render pass:** insert `m_grassRenderer->render(camera, viewProj, time, csm,
  dirLight)` in the **Foliage** pass (`engine.cpp:1615-1644`), after terrain, before
  water (grass is opaque so it could even precede water opaque; keep it in the
  foliage slot for clarity).
- **Quality tier:** a `GrassQuality {Low,Medium,High}` on the same `RendererQualitySink`
  path A5/B3 use — driving **draw distance + near-density + LOD aggressiveness**
  (Low: shorter distance, fewer blades). It is a **separate knob from
  `FoliageQuality`** (which still governs the flower billboards): the meadow carries
  two independent tiers on the sink — `GrassQuality` for the new blades,
  `FoliageQuality` for the flowers — both stepped by the same graphics
  `QualityPreset`, mirroring the existing wiring (`settings_apply.{h,cpp}`).
- **Wind/shadows:** reuse `EnvironmentForces` wind and the CSM receive path.

## 6. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Blade seed scatter + terrain/slope/splat gating | **CPU**, one-time at meadow build (v1) | I/O + sparse decision logic; moves to GPU compute in v2. |
| Per-chunk frustum cull + LOD selection | **CPU** per frame (v1) | Tens of chunks; branch/decision. GPU per-blade in v2. |
| Blade geometry generation (Bézier eval, width, normal) | **GPU** (vertex) | Per-vertex, data-parallel; the whole point of GPU grass. |
| Wind displacement | **GPU** (vertex) | Per-vertex. |
| Diffuse + translucency + AO + shadow receive | **GPU** (fragment) | Per-pixel BRDF. |
| Blade-vertex math parity mirror | **CPU** for the test only | Rule-7 parity (§8); runtime is GPU. |

Dual CPU-spec/GPU-runtime pinned by a parity test on the blade-vertex helper (Rule
7). No "CPU now, move later" hand-wave: v1's CPU placement is a *deliberate,
well-precedented* v1 (CPU per-chunk placement is a standard first step — research
§6), and the §5.5 SSBO seed layout is the v1↔v2 seam that makes v2's GPU placement
additive rather than a rewrite.

## 7. Performance (60 FPS is a hard requirement)

- **Hard gate: ≥ 60 FPS at High on the RX 6600**, Release, measured via the
  `--profile-log` CSV **Grass** GPU pass scope (per-pass GPU ms; 3D_E-0030 tooling)
  at a **fixed, reproducible ground-level pose**: G5 adds a named `grass_dense`
  `--visual-test` viewpoint (eye ~1.5 m above the terrain in the densest meadow
  interior; exact XZ + yaw/pitch recorded in the viewpoint list) so the baseline
  repeats run-to-run. The prior billboard meadow measured GPU-total ~11 ms worst /
  ~8 ms avg on the RX 6600 (this-session `--visual-test` read, to be re-recorded into
  the 3D_E-0030 CSV for reproducibility) — headroom, though from an elevated camera.
- **Opaque blades avoid the billboard cards' alpha-blend sort + transparency
  overdraw** — a genuine saving. But the **dominant** cost of thin blades is **2×2
  quad overshading** (sub-pixel / near-edge triangles shade whole fragment quads),
  alongside vertex/primitive throughput. So *"a denser real-blade field is cheaper
  per-pixel than the billboards"* is a **G5 hypothesis, not an assumption**;
  quad-overshading is the primary cost to watch, mitigated (not removed) by
  view-facing widening + LOD's fewer-verts / fewer-blades.
- **Blade budget (initial target, refined by G5 measurement):** dense near the
  camera, LOD-thinned with distance. Order-of-magnitude working figures: near-field
  density ~1–4 k blades/m² (UE-grass territory), LOD-thinned to a **~10⁵–10⁶ drawn**
  total; at 32 B/blade the stored per-chunk seed SSBOs sum to a few tens of MB
  (bounded, VRAM-cheap). These are *starting* numbers — the drawn count and density
  are recorded at first Release measurement (G5) as the baseline, not asserted up
  front — so the **Grass** pass stays within its share of the 16.6 ms frame.
- **v1 without GPU cull** is the risk (CPU per-chunk cull only draws visible chunks,
  but every blade in a visible chunk is submitted). If the G5 measurement misses 60
  FPS at High, the escalations, in order: (a) more aggressive distance LOD; (b) the
  **v2 GPU per-blade compute cull + indirect draw** (already scoped, additive). Any
  shipped cap (blade count, no-shadow-cast) is logged per Rule 5 (§13).
- **Quality tier** (Low/Med/High) is the weak-HW lever (distance + density + LOD),
  same path as A5/B3. Default High; no auto-detection this phase.

## 8. Testing

- **Blade-vertex parity (unit, Rule 7).** A **GL-free C++ hand-mirror**
  `grassBladeVertex(seed,row,side)` of the GLSL Bézier eval + width taper + tip —
  same mechanism as the A-phase `terrain_material_blend.h` parity tests; "parity
  against the GLSL" means the two hand-kept formulas agree, **not** a GL readback.
  Test endpoints (row 0 = root, last row = tip on the curve), width tapers
  monotonically to ~0 at the tip, and the
  curve passes through P0/P2. No GL.
- **LOD selection (unit).** `grassLodForDistance(dist, bands)` → correct tier at band
  edges + midpoints, monotonic non-increasing detail with distance.
- **Placement gating (unit).** Pure predicates: spawn-probability ∝ grass weight
  (0 grass weight → 0 blades), slope rejection above the cutoff, exclusion-disc
  rejection — deterministic for a fixed seed (reuse the `scatterProps` test pattern).
- **Objective render check.** Grass shaders link and a meadow frame renders
  **GL-error-free** (`glGetError`) with the field built.
- **Visual (`--visual-test`) — maintainer inspection vs references.** Ground-level
  meadow: a continuous field of **distinct 3-D blades** (not cards), backlit glow,
  grounded bases, wind motion, bare earth over the C1 dirt patches, no LOD pop at
  band edges.
- **Perf (G5).** Release `--profile-log` **Grass** GPU pass ms + FPS at the dense
  vantage on the RX 6600; record the baseline; ≥ 60 FPS at High is the gate.

## 9. Assets & licensing

- **No textures required** — blades are geometry + procedural per-blade colour
  (green with hue/value variation). Optional: a 1-D blade colour-gradient ramp
  (root→tip), engine-authored (MIT) if used. No external assets, no licensing rows
  unless a ramp texture is added.

## 10. Implementation slices

1. **G1 — Blade geometry + shader.** `GrassRenderer` skeleton; `GrassBlade` SSBO;
   `grass.{vert,frag}.glsl` VS-generated Bézier blade (single LOD); render one hard-
   coded test patch of instanced blades (flat-lit). Pure `grassBladeVertex` mirror +
   parity test. *Verify:* a patch of real 3-D blades renders GL-error-free; parity
   test green.
2. **G2 — CPU placement + PCG gating.** Per-chunk jittered-grid scatter over the
   meadow, seated on terrain height, gated on grass-splat weight + slope + pond
   exclusion; per-chunk seed SSBOs + chunk descriptors. Placement-predicate unit
   tests. *Verify:* `--visual-test` — blades cover the grass areas, follow the hills,
   thin over dirt patches, avoid water/slopes; GL-error-free.
3. **G3 — LOD + per-chunk frustum cull.** Distance LOD (segments + blade fraction +
   survivor widening, blended) and CPU per-chunk frustum cull. `grassLodForDistance`
   unit test. *Verify:* field holds across distance with no obvious pop; only visible
   chunks drawn (instrument a drawn-chunk count).
4. **G4 — Shading + wind + shadow receive.** Vertical-biased normals, view·light
   translucency glow, height base AO, boosted ambient; wind vertex bend with
   per-blade phase; **receives** CSM shadows (no cast in v1 — Rule-5 logged).
   *Verify:* `--visual-test` — backlit glow, grounded bases, wind; blades sit in
   shadow under trees; GL-error-free.
5. **G5 — Tiers + perf + meadow wire-up.** `GrassQuality` tier on the
   `RendererQualitySink`; replace the billboard-grass meadow block with
   `GrassRenderer::buildField`; keep C1 ground + C3 flowers. Release `--profile-log`
   Grass-pass read on the RX 6600. *Verify:* ≥ 60 FPS at High (baseline ms recorded);
   preset→tier unit test; CHANGELOG + ROADMAP.

**v2 (separate future phase):** GPU compute cull + `glMultiDrawElementsIndirect` +
GPU-side placement (the data model is v2-ready). Not in this phase.

Each slice commits locally; the phase pushes when G5 lands green.

## 11. Accessibility

- Motion: wind stays gentle (amplitude clamped, reuse the environment-wind clamp);
  the Low tier (shorter distance, fewer blades, no wind option if needed) doubles as
  the reduced-motion / low-end-GPU path.
- Legibility: the field's value/normal structure (not hue) carries the read
  (colour-blind safe); bare-earth patches add value contrast.

## 12. Risks & mitigations

- **v1 perf without GPU cull** → per-chunk cull + aggressive distance LOD; escalate
  to the scoped v2 GPU cull only if G5 misses 60 FPS (Rule-5 logged).
- **LOD pop** → blended tip width + blade-fraction fade at band edges (research §3).
- **Blades noisy/dark on slopes** → normal biased toward terrain-normal/vertical +
  boosted ambient (research §5).
- **Big new subsystem risk** → sliced G1–G5, each independently verifiable; the
  billboard path stays intact for flowers + as a fallback, so a stall doesn't break
  the meadow.
- **Shadow-cast omission looks wrong** (grass not self/tree-shadowing) → grass still
  *receives* shadows; casting is a tracked High/Ultra follow-up, Rule-5 logged.

## 13. Open questions for review

- **Grass shadow casting in v1 — confirm-only.** Already **decided**: grass receives
  but does not cast in v1 (§5.4, Rule-5 logged in the G4 commit). Listed here only so
  the reviewer can object; not a re-open. (A High/Ultra-only cast is the later
  candidate.)
- **Chunk size / near-density / draw-distance** starting values — pin in G2/G5 by
  visual + perf read (art-directed; `TODO: revisit via Formula Workbench`).
- **Keep billboard grass as a far-field fallback**, or GPU grass all the way to the
  fade distance? Decide by the G5 perf read (far chunks are the cheapest to LOD, so
  likely GPU-grass all the way).
- **Blade colour source** — pure procedural per-blade tint, or a root→tip gradient
  ramp? Procedural first; ramp only if the flat tint reads too uniform.

## 14. Cold-eyes loop log

Project Rule 9 / global Rule 14: fresh subagents per loop, no authoring context,
loop until no substantive verified finding remains.

**Loop 1 (2026-07-18)** — 2 cold reviewers (accuracy/infra lane + graphics
technique/perf lane). Tally: CRITICAL 0 · HIGH 2 · MEDIUM 5 · LOW 8 · INFO 1. The
accuracy lane verified **every §4 engine-infrastructure citation exact**; the
technique lane caught two real graphics bugs. All fixed:
- HIGH — the blade draw was `GL_TRIANGLES` but the "2 verts/row + tip" topology is a
  **triangle strip** (15 verts isn't a triangle-list) → committed to
  `GL_TRIANGLE_STRIP`, `2N+1` verts, as the G1 topology contract (§5.1/§5.3).
- HIGH — "opaque blades → far less overdraw, denser is cheaper" ignored **2×2 quad
  overshading** (the real dominant cost of thin blades) → reframed as a **G5
  hypothesis** with quad-overshading named the cost to watch (§5.1/§7).
- MEDIUM — "Phase 14" collided with the pre-existing virtualised-geometry Phase 14 →
  renamed the doc + title to **Phase 15**.
- MEDIUM — the perf gate's "dense vantage" wasn't a reproducible pose → G5 adds a
  named `grass_dense` viewpoint at a fixed recorded pose (§7).
- MEDIUM — shadow-cast was both "decided" (§5.4) and "open question" (§13) → stated
  decided (Rule-5 logged in G4), §13 entry made confirm-only.
- MEDIUM — the `GrassBlade` struct hedged "finalised in G1 / or split" → committed
  the exact 32-byte packing now (§5.5).
- LOW ×8 — split the `setFoliageShadowCaster` setter (`:2793`) from the cast site
  (`:3897-3915`); corrected "mesh shaders are 4.6-era" → never in GL core; added a
  splat-OOB **clamp candidates to meadow interior** (getSplatWeight returns
  full-grass out of bounds); softened the unverified "GodotGrass proves CPU
  placement" claim (§2/§15); stated GrassQuality vs FoliageQuality are two
  independent tiers (§5.6); clarified the parity test is a GL-free hand-mirror
  (§8); added an order-of-magnitude blade-count + SSBO-memory target (§7); trimmed
  the "additive not a rewrite" repetition.
- INFO — the ~11 ms/~8 ms billboard baseline is an in-session `--visual-test` read
  (to be re-recorded into the 3D_E-0030 CSV for reproducibility).

## 15. Sources

- Sucker Punch, GDC 2021 "Procedural Grass in Ghost of Tsushima" —
  https://archive.thedatadungeon.com/ghost_of_tsushima_2020/documents/gdc_2021/gdc_2021_procedural_grass_in_got.pdf
  · writeup https://tigerabrodi.blog/grass-in-ghost-of-tsushima
- 2Retr0 / GodotGrass (open-source GoT-style grass reproduction) — https://github.com/2Retr0/GodotGrass
- Haoran Liang, "Grass Rendering in a Game Engine" (compute placement, SSBO seeds, 3-way cull) — https://haoranliang.com/grass-rendering
- AMD GPUOpen, "Procedural grass with mesh shaders" (Bézier blade + LOD math) — https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/
- Lingtorp, "Generating draw commands on the GPU in OpenGL" (4.3+ compute→indirect) — https://lingtorp.com/2018/12/05/OpenGL-SSBO-indirect-drawing.html
- Cyanilux, "GPU Instanced Grass Breakdown" — https://www.cyanilux.com/tutorials/gpu-instanced-grass-breakdown/
- NVIDIA GPU Gems ch.7 (waving grass) — https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass
  · ch.16 (subsurface scattering) — https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-16-real-time-approximations-subsurface-scattering
- UE5 scatter: Landscape Grass Types vs PCG — https://www.strayspark.studio/blog/ue5-pcg-production-ready-guide
