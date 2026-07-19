# Meadow GPU Grass (real Bézier-blade field)

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

A **tall, wild, clumped field of real 3-D grass blades** that reads like the
photoreference meadows — long blades that flop and lean in natural tussocks, catch
light, glow when backlit, and ground into the terrain — covering the meadow at the
**v1 target density** (VRAM-bounded base density, dense read carried by grazing-angle
overlap; higher *uniform* density is a v2 goal — §7) and holding **≥ 60 FPS at High on
the RX 6600**. Target look: **untended wild meadow**, not a mowed lawn — the default
tuning is tall (~0.6–1.2 m), strongly leaning, and clumped.

- **Real per-blade geometry**, GPU-generated from a per-blade seed (Bézier ribbon,
  ~5–13 triangles by LOD, i.e. `2N−1`), not billboards, not shell texturing.
- **Clumping is what makes it a field, not tufts (§5.2a).** Blades snap toward
  invisible **clump points** that set their height, lean direction, colour, and bend —
  so the field reads as natural tussocks (tall here, short there, leaning together,
  varied green) instead of a uniform bristle-brush / lawn. This is the single biggest
  "reads as a real meadow" lever (Ghost of Tsushima's Voronoi clumping).
- **Procedural placement ("PCG"):** blades scattered by rule — only where the
  terrain **grass** splat layer has weight, thinned on slopes, off water and the
  earthy dirt patches (C1) and the pond.
- **Scales** via distance LOD (fewer, simpler blades far) + frustum culling.
- **Grass first.** Flowers stay the existing billboard path as sparse "sprinkles"
  (3D_E-0038 C3 flowers survive; only billboard *grass* is replaced).

## 2. Non-goals (this phase)

- **No mesh/task shaders.** They are **never in GL core** — mesh/task shaders exist
  only as the `GL_NV_mesh_shader` vendor extension on desktop GL (not even in GL 4.6,
  the final GL version; `EXT_mesh_shader` is Vulkan-side). The blade geometry is built in the **vertex shader**
  from `gl_VertexID` + the per-blade seed — the portable equivalent (§5.1).
- **No shell texturing.** The stacked-transparent-shell technique reads as "fuzz",
  not distinct blades, and is overdraw-heavy — wrong aesthetic for the references
  (research §3).
- **v1 defers the fully GPU-driven pipeline.** v1 places blades on the **CPU** per
  terrain chunk and frustum-culls **per chunk** on the CPU; the per-blade GPU
  compute cull + `glMultiDrawArraysIndirect` (the **non-indexed** multi-draw — the
  blade strip has no index buffer, so `…ArraysIndirect`, not `…ElementsIndirect`) +
  GPU placement is **v2**, dropped in additively because v1 stores blade seeds in a
  **single shared SSBO from day one** (§5.5).
  CPU per-chunk placement is a standard, well-precedented first step; open-source
  reproductions (e.g. GodotGrass) reach this quality without the full GPU-driven
  pipeline (research §3).
- **No Nanite / virtualised micro-geometry** — that is the roadmap's **Phase 14
  (Adaptive Geometry System)** plan (meshlets / virtual geometry; a ROADMAP-level scope,
  no design doc yet). This grass work is unrelated to it and is filed as a
  **meadow-realism** doc (`phase_10_meadow_gpu_grass_design.md`, sibling to the earlier
  meadow grass designs), **not** a numbered engine phase — every Phase number 12–26 is
  already reserved.
- **No new flower work** — flowers are out of scope beyond keeping the C3 billboard
  sprinkles rendering.

## 3. Research summary (what current practice recommends)

- **Each blade is a procedural Bézier ribbon built on the GPU** from a small seed
  (root position, facing, height, width, lean/bend). Ghost of Tsushima uses a cubic
  Bézier; the AMD GPUOpen sample a quadratic (P0=root, P1=root+up·height,
  P2=P1+dir·height·0.3). Vertices are the curve evaluated at a few `t`, offset
  perpendicular for width, tapering to the tip. **~5–13 triangles / blade** (`2N−1`:
  near N=7 ≈ 13, far N=3 ≈ 5; GPUOpen's 8-vert far blade = 6)
  (GoT near ≈15 verts, far ≈7; GPUOpen 8 verts→6 tris). The **vertex shader builds
  the mesh** — no authored blade mesh is stored. *(GoT GDC 2021; GPUOpen; GodotGrass.)*
- **View-facing widening:** edge-on blades are stretched horizontally in view space
  so a thin blade never shrinks to a sub-pixel sliver (aliasing/shimmer fix).
- **Placement is rule-based scatter** (the "PCG" part): a jittered/uniform grid per
  terrain tile, modulated by a density mask / landscape-layer weight — grass grows
  where the "grass" layer is painted, thinned on slope/water. This is exactly UE5
  **Landscape Grass Types** (Epic recommends them over PCG for a ground-cover
  carpet). *(UE5 landscape-grass docs; StraySpark PCG guide.)*
- **Clumping is the "field, not tufts" technique** (the finding from the 2026-07-19
  research sweep). A grid of identical blades reads as a bristle-brush / mowed lawn; a
  real meadow grows in **clumps**. Ghost of Tsushima seeds the ground with **Voronoi
  clump points** — each blade finds its nearest clump and inherits that clump's
  **height, lean direction, colour, and bend**, producing natural tussocks (tall/short
  patches, blades leaning together, colour drift) at almost no cost (a noise/Voronoi
  lookup). This is the biggest single realism lever and the piece a naïve per-blade-
  random field misses. Full-geometry blades (what we build) are the current
  recommended approach; **shell texturing is rejected** — it reads as "fuzz", not
  distinct blades. *(GoT GDC 2021 clumping; hexaquo grass theory; Acerola — rejected.)*
- **Scaling to hundreds of thousands / millions of blades** = per-instance seeds in
  an **SSBO** + **distance LOD** (fewer blades *and* fewer segments far, survivors
  widened to hold apparent density, blended to avoid pop) + **frustum cull**
  (per-chunk CPU, or per-blade GPU compute writing an indirect-draw arg buffer) +
  **indirect instanced draw** (one draw for the field). *(GoT; GPUOpen; Cyanilux;
  Lingtorp GL indirect.)*
- **OpenGL 4.5 has all of it in core** — compute (4.3), SSBOs (4.3), indirect draw
  (`glDrawArraysIndirect` 4.0 / `glMultiDrawArraysIndirect` 4.3 — the **non-indexed**
  variants the blade strip needs, since it carries no index buffer), DSA (4.5). No
  mesh shaders needed; the VS generates the blade.
- **Shading:** normal biased toward vertical/terrain-normal (so slopes/field aren't
  noisy); **view·light translucency glow** (the signature backlit-grass look);
  **height-based base ambient occlusion** (dark at the root → grounds the blade);
  boosted ambient so backsides read. **Wind:** scrolling 2-D noise → bend the upper
  control point, scaled by height along the blade, with a **per-blade phase** so
  neighbours don't move in lockstep. *(GPU Gems ch.7/16; GodotGrass.)*
- **Pragmatic v1** (research §3): CPU jittered-grid placement per chunk (sample
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
  (types 1/2/3); GPU grass covers the field to the fade distance, so no billboard
  grass fallback is planned.
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
  strip vertices** (LOD tiers: near N=7 → 15 verts; mid N=5 → 11; far N=3 → 7). Drawn with
  **`GL_TRIANGLE_STRIP`** — each instanced draw replays the strip independently, so
  strips do not connect across blades. *(This is the topology contract for G1 —
  `GL_TRIANGLE_STRIP`, `2N+1` verts, NOT a `GL_TRIANGLES` list.)* The draw pulls **no
  vertex attributes** (everything from `gl_VertexID`/`gl_InstanceID` + SSBO), but a
  **non-zero VAO must still be bound** — `glDrawArrays*` with VAO 0 is
  `GL_INVALID_OPERATION` in a core profile (the engine binds `m_gpuVao` before its
  SSBO-driven particle draw, `particle_renderer.cpp:370`); grass binds an empty VAO.
- **Curve.** Quadratic Bézier (GPUOpen form): `P0 = root`, `P1 = root + up·height`,
  `P2 = P1 + facingDir·height·lean`. Evaluate at `t = row/N`; offset ±perpendicular
  (facing) by a width that **tapers to 0 at the tip**. The two verts of a row share
  `t`; the last row is a single centred tip vertex.
- **Per-vertex normal** = curve tangent × width axis — the raw **geometric** normal
  (roughly horizontal, in the plane of the blade face). At the **tip** the width → 0
  makes the width axis (and the cross product) degenerate → the tip vertex takes the
  **row N−1 normal** rather than a NaN/zero normal. The **bias toward the terrain
  normal / vertical** (so the lit field isn't noisy, research §3) is applied in the
  **fragment shader after** the two-sided face-forward flip (see the opaque bullet) —
  the order is load-bearing.
- **View-facing widening** (§3): widen the blade in view space as it turns edge-on
  to the camera, clamped, to kill sub-pixel shimmer. **Pin a floor:** the projected
  blade width must not fall below **~1–1.5 px**; a blade thinner than a sample stride
  flickers as it crosses pixels. This complements the engine's edge AA rather than
  replacing it — the scene renders to a **4× MSAA** FBO by default (`engine.cpp:110`,
  `renderer.cpp:530-544`), which covers geometric blade edges; but under the **TAA/SMAA**
  AA modes the scene renders to a **non-MSAA** FBO (`renderer.cpp:713-732`), so there
  the widening floor + the §5.3 fade-to-zero are grass's main sub-pixel defense (TAA's
  temporal accumulation catches the residual crawl). Verified against the AA path, not
  assumed.
- **Opaque, not alpha-blended.** Real blades are solid geometry → render **opaque**
  (depth-tested, blend off; two-sided lighting with back-face cull off — the fragment
  shader **faces the geometric normal toward the camera first** (`faceforward` / flip
  on `!gl_FrontFacing`) and **then** applies the vertical bias, so **both** faces keep a
  positive up-component and a back-lit blade is not a black backside. The order is
  load-bearing: biasing to near-vertical *then* flipping would point the back face's
  normal **downward** → no diffuse under an overhead sun, the very artifact the flip is
  meant to prevent), which avoids the
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
  - randomise facing, height, width, lean, and a per-blade `hash` — which **packs**
    the wind phase + per-blade tint/height variation (§5.5); tint is hash-derived, **not**
    a stored `GrassBlade` field, so the struct stays 32 B — all seeded deterministically
    from the chunk id + index (reproducible; testable).
- **Output:** each chunk's `GrassBlade` seeds are appended into the **one shared
  SSBO** (§5.5) with the chunk's **base offset + blade count + AABB** recorded in its
  descriptor, uploaded once at meadow build (rebuilt only if the terrain changes).
  **Seed ordering (load-bearing for LOD):** within a chunk, seeds are stored **shuffled
  / interleaved**, NOT in raster/grid order, so that any prefix `[0, count·fraction)`
  is a **spatially-uniform** subset of the chunk — the exact property §5.3's
  prefix-thinning distance-LOD relies on (a raster prefix would draw a spatial slab,
  not a thinned field). A fixed deterministic shuffle keeps it testable.
- **PCG rule set is data-driven** via a `GrassConfig` (near-density, slope cutoff,
  height range, grass-weight response, **clump scale/strength** (§5.2a), exclusion) so
  the meadow tunes it without new code — the "paint vast expanses" control surface.

### 5.2a Clumping — the "field, not tufts" layer

The realism lever (research §3): blades group into **clumps** instead of standing as
identical uniform spikes. This is *the* difference between "real meadow" and "mowed
lawn / bristle-brush".

- **Clump field = a pure function of world XZ**, `grassClump(worldX, worldZ, scale)`,
  shared by CPU and GLSL (parity, §8). It scatters one **clump centre** per cell of a
  coarse grid (cell size ≈ `clumpScale`, ~0.5–1.5 m for a wild meadow), examines the
  **3×3 neighbourhood** of cells around the sample, and returns a **smooth-kernel
  weighted blend** (below) of the neighbourhood's per-clump factors derived from each
  cell's id: `clumpHeight` (a **multiplier ~0.6–1.6×**, tall vs short tussock),
  `clumpLeanDir` (a **unit `vec2`**, the shared XZ direction the whole clump leans
  toward), `clumpTint` (a small clamped green/colour drift), `clumpBend` (0–1, how far the
  tussock flops), and `clumpPhase` (a shared wind phase, §5.4). The cell index is
  `floor(worldXZ / cellSize)` — **`floor`, never `int()` truncation**, or the meadow's
  negative half (the field is centred on the origin, so negative cell coords are the
  *common* case) seams at x=0/z=0. The cell-centre jitter and all factors come from a
  **deterministic integer bit-hash** of the (two's-complement `uint32`) cell coordinates
  (PCG/xxhash-style int ops — **not** `fract(sin(dot(…)))`, which does not bit-match
  across GPU/CPU), so the CPU mirror and the GLSL agree exactly (parity + the AABB below
  both depend on identical clump membership).
- **Soft boundaries via a smooth falloff kernel.** Weight each of the 3×3 centres by a
  smooth kernel of its distance, `wᵢ = 1 − smoothstep(0, kernelR, dᵢ)` (**written
  `edge0 < edge1`; `smoothstep(kernelR, 0, …)` is GLSL-undefined and silently returns 1
  on a spec-literal driver → clumping disabled**), and take the **normalised weighted
  average** of the factors. A centre's weight rises from 0 as it nears and falls to 0 by
  `kernelR`, so — **within the envelope below** — the blend is C⁰/C¹ **everywhere**,
  including the second-nearest-swap loci and Voronoi triple points where a plain
  two-nearest `d₂/(d₁+d₂)` lerp still jumps (that lerp is only continuous across the
  nearest-pair edge — the earlier draft's "two-nearest" claim was wrong). This holds only
  for the **scalar** factors (height/tint/bend); the lean *direction* field has isolated
  singularities handled in the VS bullet.
- **Kernel envelope — a provably-correct window (committed constants).** The blend is
  C⁰ everywhere **and** never divides by zero iff the tunables sit in a concrete window,
  which a **3×3 search does admit**. With jitter bounded to a **radius `jr ≤ 0.25·cellSize`**
  about each cell centre (a radius, not a per-axis box):
  - **Coverage (no all-zero):** the worst sample is a cell *corner*; the nearest of the
    four centres meeting there is at most `cellSize·√2/2 + jr ≈ 0.707·cellSize + jr` away.
    So **≥ 1 of the 3×3 centres is within `kernelR` everywhere** iff
    `kernelR ≥ 0.707·cellSize + jr` — then `Σwᵢ > 0` always. (The right invariant is
    "**at least one** of the 3×3 centres within `kernelR`", *not* "the own-cell centre" —
    at a corner the coverage comes from neighbours.)
  - **C⁰ (no window-entry jump):** the nearest *excluded* (≥ 2 cells out) centre is at
    least `1.5·cellSize − jr` away, so it carries zero weight iff
    `kernelR ≤ 1.5·cellSize − jr`.
  - Both hold together for `jr = 0.25·cellSize` and **`kernelR = cellSize`** (window
    `[0.957, 1.25]·cellSize` is non-empty) — the committed default. Then `Σwᵢ > 0`
    **everywhere**, so the `Σwᵢ < ε` guard (`ε = 1e-6`) is an **unreachable float-safety
    net** (fall back to the nearest cell's factors), **not** a live path — no `0/0` NaN,
    and no fallback discontinuity in the field. Crisper tussocks (smaller `kernelR` than
    the coverage floor) require a **5×5 search** (which raises the excluded-centre floor),
    not a smaller-than-envelope `kernelR` on a 3×3.
- **How a blade uses it (VS).** Each blade evaluates `grassClump` at its `rootPos.xz` and
  blends the (kernel-smoothed) clump factors into its seed, scaled by
  `clumpStrength ∈ [0,1]` (the wild↔tidy dial, default **wild ≈ 0.7**): `height =
  baseHeight · mix(1, clumpHeight, clumpStrength)`; **facing/lean blended as a direction
  vector** (sum `wᵢ · clumpLeanDirᵢ`, renormalise) then back to `facingAngle` — **never**
  by lerping the scalar radian (0/2π wrap). **Antipodal guard:** if the summed direction's
  length is ~0 (opposing lean dirs at ~equal weight — an isolated index-±1 singularity the
  guard makes *defined*, not continuous), fall back to the **dominant-weight** direction,
  with a **deterministic tie-break** (lowest cell id) so CPU and GPU pick the same one on
  an exact-equal-weight tie. `bend` raised by `clumpBend`;
  tint shifted by the clamped `clumpTint`. **`clumpPhase` is taken nearest-only, not
  kernel-blended** — it is a *cyclic* phase (blending it reintroduces the 0/2π wrap), and
  clumps are meant to sway as separate bodies; if a hard sway-seam ever shows, blend the
  resulting wind *displacement vector*, never the phase angle.
- **CPU cull path also evaluates `grassClump` (no per-blade *storage* change, but not
  VS-only).** Clumping adds **no field** to `GrassBlade` (it is a function of `rootPos`),
  so the struct stays 32 B (§5.5). **But** because clump height/lean change the *drawn*
  geometry, the chunk **AABB** (which drives frustum cull + the LOD nearest-point,
  §5.3) must be built with the **clump-max height and lean/bend reach** — the CPU
  placement evaluates the same deterministic `grassClump` and pads the AABB accordingly,
  else tall/leaning tussocks at the frustum edge get falsely culled and clip. The
  segment-LOD "sub-pixel" distance (§5.3) likewise keys on the clump-max height. The CPU
  may also bias placement density toward clump centres with the same helper.
- **Tall & wild defaults** (user direction 2026-07-19): base height ~0.6–1.2 m, strong
  lean/bend so long blades flop, `clumpStrength` high. All in `GrassConfig` so the look
  dials from wild meadow to tidy field without code.

### 5.3 LOD + culling (v1)

- **Per-chunk frustum cull (CPU).** Reuse the `getVisibleChunks` AABB-vs-frustum
  pattern; skip non-visible chunks' draws entirely.
- **Distance LOD** (two axes, research §3). The draw is per chunk (one
  `glDrawArraysInstanced`), so the *segment* axis is per-chunk; the *blade-count* axis
  is faded **per blade** in the shader to keep the pop off the chunk boundary:
  - **fewer segments (per chunk, coarse).** The blade vertex count (15/11/7) is one
    value for the whole chunk (it is one draw). This coarse step is placed **far enough
    out that the affected blades are near-sub-pixel**, so a chunk stepping 15→11→7 is
    imperceptible without per-blade blending.
  - **fewer blades (faded per blade, not hard-culled at the boundary).** Far chunks
    draw a smaller blade fraction — seeds are ordered so a prefix is a spatially-uniform
    subset (§5.2). To avoid the whole chunk dropping a slab of its population the instant
    it crosses a band, the chunk keeps submitting the **finer** tier's blade fraction
    *through* the blend band while the **vertex shader shrinks each soon-to-be-dropped
    blade's height/width → 0** over that band — a *geometric* fade (blades are opaque;
    there is no alpha cross-fade). **Two inputs pick the fade:** the blade's **rank**
    (`gl_InstanceID / chunkBladeCount`, the count from the §5.5 descriptor) against the
    current and next band's fraction thresholds decides *whether* a blade is in the drop
    set (the ones in `[nextFraction, curFraction)` of the shuffled order); its
    **root-to-camera distance** across the blend band decides *how far* it has shrunk.
    Rank picks who fades, distance picks how much. Once a blade reaches zero it falls out
    of the next-band draw fraction with nothing visible to pop. Survivors **widen**
    slightly to hold apparent density.
  - The per-blade distance is what makes the blade-count fade smooth *despite* the
    per-chunk draw granularity — the fade tracks each blade, not the chunk. Pure
    `grassLodForDistance(dist)` helper picks the segment tier + the blend-band edges →
    unit-tested (§8's "no obvious LOD pop" criterion rests on this per-blade fade).
  - **Precondition (else the pop returns).** The *removal* — dropping instanceCount from
    `curFraction·count` to `nextFraction·count` — is unavoidably **per-chunk** (one
    `glDrawArraysInstanced`, one instanceCount). For no blade to be un-submitted while
    still visible, the chunk's fraction step (and the segment-tier step) must key on the
    chunk's **nearest** point, and the **blend band width must be ≥ the chunk's
    max distance-spread** (its diagonal, ~22 m for a 16 m chunk) — so the last-to-fade
    (nearest) drop-set blade has already reached zero before the chunk drops it. Keying
    on chunk-*centre* with a band narrower than the spread would pop the near-edge blades.
    The segment-tier "near-sub-pixel" imperceptibility (above) likewise holds only if the
    chunk's **nearest** blade is past the sub-pixel threshold, not its centre.
- **Draw (v1):** per visible chunk, `glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0,
  vertsForLod, chunkBladeCount·lodFraction)` (`vertsForLod = 2N+1`, §5.1), seeds
  read from the chunk SSBO bound at a binding point. One draw per visible chunk
  (tens), not per blade.
- **v2 (future, additive):** a `grass_cull.comp.glsl` tests blades per-frame,
  compacts survivors + writes an indirect-args buffer, and the field draws in **one**
  `glMultiDrawArraysIndirect` — the **non-indexed** multi-draw (the blade strip has no
  index buffer, so it is `…ArraysIndirect`, not `…ElementsIndirect`; one draw command
  per LOD segment tier). Follows the engine's `glDrawArraysIndirect` compute-written /
  barrier particle precedent (`gpu_particle_system.cpp:369-409`), scaled to multi-draw.
  The v1 SSBO layout is chosen so this is additive, not a rewrite.

### 5.4 Shading + wind

- **Fragment shading:** Lambert diffuse with the vertical-biased normal; **view·light
  translucency** term (backlit glow, `pow(max(dot(view,-lightDir),0), k)` gated by
  the blade facing away from the light); **height-based base AO** (dark at root →
  bright at tip, coupled with wind bend); boosted ambient (power curve) so backsides
  read; receives **CSM shadows** (bind cascade array, reuse the foliage receive
  path). Colour = blade tint (green with per-blade variation **plus the per-clump tint
  drift of §5.2a**, so tussocks read as subtly different greens) × light.
- **Wind (vertex):** sample scrolling 2-D value noise by world XZ (reuse the engine
  wind direction/speed from `EnvironmentForces`, as billboard grass does at
  `engine.cpp:1625-1627`) → bend the Bézier `P1/P2` control points, scaled by height
  along the blade (root fixed, tip moves most). Two phase terms compose: the **per-clump
  phase** (`clumpPhase`, §5.2a) makes a whole tussock sway **together as a body**, and a
  small **per-blade phase** (from the seed hash) adds fine desync on top so blades within
  the clump don't move in perfect lockstep; a small high-frequency jitter for gust
  turbulence. (Offsetting the control points slightly lengthens the Bézier arc — a
  faint stretch — negligible at the clamped gentle amplitude of §11; if it ever shows,
  apply the bend as a small rotation about the root instead of a control-point offset.)
- **Shadow casting:** grass casting into the CSM is **expensive** (research §3 notes
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
concrete `static_assert` seam.

**One shared blade SSBO** holds every chunk's seeds back-to-back — **not** per-chunk
buffers. This is load-bearing for the v1↔v2 seam: v2 draws the whole field in one
`glMultiDrawArraysIndirect` and culls it in one compute dispatch, and a single draw /
dispatch can bind only **one** SSBO — so per-chunk buffers would force a buffer
*consolidation* in v2 (a rewrite), exactly what the seam is meant to avoid. A
**chunk-descriptor array** (AABB, blade count, **base offset** into the shared buffer)
drives cull/LOD. Bound once with `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, …)`; the
VS indexes a seed by **`gl_InstanceID + chunkBaseOffset`** — a per-chunk uniform in v1.
(`gl_InstanceID` alone is `[0, instanceCount)` and does **not** include `baseInstance`;
`baseInstance` offsets only *vertex attributes*, which this attribute-less draw has
none of — so the base offset must be added explicitly.) v2's compute cull reads the
same seeds **in place** and writes a compacted visible list + indirect args — additive,
no buffer consolidation.

### 5.6 Integration

- **Meadow wire-up:** replace the billboard-**grass** `paintFoliage(GRASS_TYPE_ID…)`
  block with `GrassRenderer::buildField(terrain, meadow bounds, pond exclusion,
  GrassConfig)`. **Keep** the C3 billboard **flower** clusters (types 1/2/3) and C1
  earthy ground unchanged. The billboard `FoliageRenderer` stays for **flowers only**
  (GPU grass covers the field to the fade distance).
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
| Clump lookup (`grassClump`, §5.2a) → per-clump height/lean/tint/bend | **GPU** (vertex) at draw; **CPU** for placement bias, **chunk-AABB clump-max padding** (§5.2a — else tall/leaning tussocks false-cull), + parity test | Pure function of world XZ; per-vertex on the GPU, mirrored on CPU (Rule-7 parity). |
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
  `--profile-log` CSV **Grass** GPU pass scope (per-pass GPU ms; the `--profile-log`
  CSV logger is **3D_E-0027** tooling, consumed by the **3D_E-0030** perf-regression
  gate) at a **fixed, reproducible ground-level pose**: G5 adds a named `grass_dense`
  `--visual-test` viewpoint (eye ~1.5 m above the terrain in the densest meadow
  interior; exact XZ + yaw/pitch recorded in the viewpoint list) so the baseline
  repeats run-to-run. **Two poses, not one:** the `grass_dense` pose is a near-level
  *grazing* look (best case for the grazing-angle overlap §7 relies on) **plus** a
  **downward-pitched `grass_lookdown` pose** — the worst case for the base-density read,
  where ~130 blades/m² (~9 cm spacing) is sparsest and overlap does not help. The
  look-down pose is what actually stresses the flagship density risk (§7 memory bullet). The prior billboard meadow measured GPU-total ~11 ms worst /
  ~8 ms avg on the RX 6600 (this-session `--visual-test` read, to be re-recorded into
  the 3D_E-0030 CSV for reproducibility) — headroom, though from an elevated camera.
- **Opaque blades avoid the billboard cards' alpha-blend sort + transparency
  overdraw** — a genuine saving. But the **dominant** cost of thin blades is **2×2
  quad overshading** (sub-pixel / near-edge triangles shade whole fragment quads),
  alongside vertex/primitive throughput. So *"a denser real-blade field is cheaper
  per-pixel than the billboards"* is a **G5 hypothesis, not an assumption**;
  quad-overshading is the primary cost to watch, mitigated (not removed) by
  view-facing widening + LOD's fewer-verts / fewer-blades.
- **Blade budget — stored ≠ drawn (v1 places once, camera-independent):** the meadow is
  a **256×256 m** field (`test_meadow_terrain.cpp:199-203`: 257 verts × 1 m spacing;
  ~246×246 ≈ **60 k m²** of grass after the edge margin, pond, and C1 dirt patches).
  Because v1 CPU-places blades **once at build** (§5.2/§6), the **stored** blade count is
  `base-density × grass-footprint` — set at build, *independent* of the camera and so of
  the LOD-thinned **drawn** count. That is a hard VRAM constraint, not a drawn-cost one:
  at 32 B/blade, a **~256 MB** grass-SSBO budget on the 8 GB RX 6600 caps storage at
  **~8 M stored blades ≈ ~130 blades/m² base density** across the full field. A reference
  near-density of 1–4 k blades/m² therefore **cannot be stored uniformly over the whole
  256 m field in v1** — so **v1's near-camera density *is* the base density (~130/m²)**,
  not a higher figure. (Survivor-widening is a *far-field* mechanism — §5.3 applies it
  only to far chunks that drop a blade fraction — so it does **not** raise near-camera
  density; the earlier draft mis-stated this.) What carries the dense continuous *read*
  at a ground-level camera is **grazing-angle overlap** — the eye looks *across* the
  field, so even ~130 blades/m² of real 3-D geometry stack into a continuous mat. Whether
  that reads dense enough close-up is the flagship **G5 visual risk**; true uniform high
  density is deferred to **v2** (GPU/view-dependent placement stores only near the
  camera). These are *illustrative* budget figures — G2 pins the base density under the
  SSBO budget; G5 records the drawn count + per-pass ms baseline **and confirms the
  close-up density read**. The **Grass** pass must stay within its share of the 16.6 ms
  frame.
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
  curve passes through P0/P2. No GL. **Scope:** the mirror pins the **static** blade
  (curve + width taper + tip) only; the **wind bend** and **view-facing widening** also
  move the emitted vertex but are time-/view-dependent, so they are not in the parity
  seam — a GLSL divergence there is caught by the visual check (§5.4/§5.1), not this test.
- **Clump-function parity (unit, Rule 7).** `grassClump(worldX, worldZ, scale)` (§5.2a)
  is a second GL-free hand-mirror of its GLSL twin, built on the **deterministic integer
  bit-hash** (so CPU and GLSL bit-match — load-bearing for the AABB, §5.2a). Test:
  **determinism** (same XZ → same clump ids/factors), **blades near one centre share the
  clump's height/lean/tint**, factors stay in range (`clumpHeight` 0.6–1.6×, tint
  clamped). Two separate properties (not one — they cover different regimes):
  **(i) C⁰ over the covered field** — with the committed in-envelope constants (§5.2a:
  `jr = 0.25·cellSize`, `kernelR = cellSize`) `Σwᵢ > 0` **everywhere**, so a dense XZ
  **grid** crossing Voronoi *triple points*, second-nearest-swap loci, and **cell corners**
  (the cases a two-nearest lerp fails) shows the scalar factors change with no jump above a
  small epsilon; **(ii) finiteness always** — a **synthetic degenerate** call (`kernelR`
  shrunk below the coverage floor, or a direct all-zero-weights input) forces the
  `Σwᵢ < ε` safety net and asserts the result is **finite** (no `0/0` NaN) — *not* C⁰
  there, since the net is an intentional fallback. So the in-envelope field is C⁰ and the
  net is proven non-NaN, without the test contradicting itself. This pins the "field, not
  tufts" math the same way the blade-vertex mirror pins the static blade.
- **LOD selection (unit).** `grassLodForDistance(dist, bands)` → correct tier at band
  edges + midpoints, monotonic non-increasing detail with distance.
- **Placement gating (unit).** Pure predicates: spawn-probability ∝ grass weight
  (0 grass weight → 0 blades), slope rejection above the cutoff, exclusion-disc
  rejection — deterministic for a fixed seed (reuse the `scatterProps` test pattern).
- **Objective render check.** Grass shaders link and a meadow frame renders
  **GL-error-free** (`glGetError`) with the field built.
- **Visual (`--visual-test`) — maintainer inspection vs references.** Ground-level
  meadow: a continuous field of **distinct 3-D blades** (not cards), backlit glow,
  grounded bases, wind motion, bare earth over the C1 dirt patches, **no obvious LOD
  pop** at band edges (the per-blade distance fade of §5.3 holds the transition
  smooth despite per-chunk draws). Two checks a **static screenshot cannot show**, so
  both use a **moving** pose: (a) **no visible shimmer/crawl** on the blades under a slow
  camera pan (the widening floor + AA path of §5.1 must hold — a still frame hides
  temporal aliasing); (b) the **`grass_lookdown`** pose reads acceptably dense from above
  (the §7 base-density risk), not just the grazing `grass_dense` pose.
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
2. **G2 — CPU placement + PCG gating + clumping.** Per-chunk jittered-grid scatter over
   the meadow, seated on terrain height, gated on grass-splat weight + slope + pond
   exclusion; the **one shared seed SSBO** + per-chunk descriptors (base offset + count
   + AABB). Add the **`grassClump` function (§5.2a) — CPU + GLSL, with its parity test**
   — and apply clump height/lean/bend in the VS so the field reads as **tussocks, not
   uniform blades**; tall & wild default tuning. Placement + clump-parity unit tests.
   *Verify:* `--visual-test` — blades cover the grass areas, follow the hills, thin over
   dirt patches, avoid water/slopes, and **clearly clump** (tall/short patches, blades
   leaning together) rather than reading as a uniform lawn; GL-error-free.
3. **G3 — LOD + per-chunk frustum cull.** Distance LOD (per-chunk segment tier +
   per-blade rank/distance fade keyed on the chunk's nearest point, survivor widening)
   and CPU per-chunk frustum cull. `grassLodForDistance` unit test. *Verify:* under a
   slow camera **pan** (moving pose) the field holds across distance with **no obvious
   pop and no blade shimmer/crawl**; only visible chunks drawn (instrument a drawn-chunk
   count).
4. **G4 — Shading + wind + shadow receive.** Vertical-biased normals, view·light
   translucency glow, height base AO, boosted ambient, **per-clump tint drift (§5.2a)**;
   wind vertex bend with per-blade phase (**per-clump wind sway** so tussocks move
   together); **receives** CSM shadows (no cast in v1 — Rule-5 logged). *Verify:*
   `--visual-test` — backlit glow, grounded bases, wind, tussocks in subtly different
   greens; blades sit in shadow under trees; GL-error-free.
5. **G5 — Tiers + perf + meadow wire-up.** `GrassQuality` tier on the
   `RendererQualitySink`; replace the billboard-grass meadow block with
   `GrassRenderer::buildField`; keep C1 ground + C3 flowers. Release `--profile-log`
   Grass-pass read on the RX 6600 at **both** the `grass_dense` (grazing) and
   `grass_lookdown` poses. *Verify:* ≥ 60 FPS at High (baseline ms recorded); the
   look-down pose reads acceptably dense (the §7 base-density risk); preset→tier unit
   test; CHANGELOG + ROADMAP.

**v2 (separate future phase):** GPU compute cull + `glMultiDrawArraysIndirect`
(non-indexed) + GPU-side placement (the data model is v2-ready). Not in this phase.

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
  boosted ambient (research §3).
- **Big new subsystem risk** → sliced G1–G5, each independently verifiable; the
  billboard path stays intact for flowers, and a stall in any slice leaves the prior
  committed slice working, so it never breaks the meadow build.
- **Shadow-cast omission looks wrong** (grass not self/tree-shadowing) → grass still
  *receives* shadows; casting is a tracked High/Ultra follow-up, Rule-5 logged.

## 13. Open questions for review

- **Grass shadow casting in v1 — confirm-only.** Already **decided**: grass receives
  but does not cast in v1 (§5.4, Rule-5 logged in the G4 commit). Listed here only so
  the reviewer can object; not a re-open. (A High/Ultra-only cast is the later
  candidate.)
- **Chunk size / near-density / draw-distance** starting values — pin in G2/G5 by
  visual + perf read (art-directed; `TODO: revisit via Formula Workbench`).
- **Far-field grass — resolved:** GPU grass covers the field all the way to the fade
  distance (far chunks LOD cheaply); **no billboard-grass fallback**. The billboard
  `FoliageRenderer` is flowers-only. (Re-open only if the G5 read shows far chunks
  are unexpectedly expensive.)
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
  renamed the doc + title to **Phase 15** (which turned out to also be taken —
  Atmospheric Rendering; corrected in Loop 4 by dropping the Phase-NN framing entirely).
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

**Loop 2 (2026-07-18)** — 2 cold reviewers, identical briefs (pointed at the
renamed Phase-15 file). Tally: CRITICAL 0 · HIGH 0 · MEDIUM 1 · LOW 4 · INFO 3.
The accuracy lane verified every citation exact ("high-accuracy design doc"); the
technique lane confirmed the loop-1 topology/perf fixes held. Fixed:
- MEDIUM — prefix-thinning distance-LOD (§5.3) silently required blade seeds stored
  so a prefix is a *spatially-uniform* subset; §5.2 didn't state it → added the
  **shuffled/interleaved seed-ordering** property to §5.2 (a raster prefix would
  draw a spatial slab, not a thinned field).
- LOW ×4 — corrected the blade triangle count "~6–10" → `2N−1` (near ≈13, far ≈5,
  §1/§3); added the **mid LOD tier N=5→11** to the §5.1 vertex-count contract;
  dropped `GL_EXT_mesh_shader` (Vulkan-side, not desktop GL) leaving `GL_NV_mesh_shader`
  (§2); resolved the far-field-fallback open question (**GPU grass all the way**, no
  billboard-grass fallback — billboard path is flowers-only; §5.6/§12/§13); nudged
  two off-by-one terrain cites (`getHeight` 121→120, `getNormal` 148→147).
- INFO — `chunkBladeCount·lodFraction` needs a floor/cast at implementation (impl
  detail, not a design defect).

**Loop 3 (2026-07-19)** — 2 cold reviewers (accuracy/infra lane + graphics
technique/perf lane), identical cold briefs, no prior-loop context. Tally: CRITICAL 0
· HIGH 0 · MEDIUM 3 · LOW 5 · INFO 1 (all 8 actionable verified against source, 0
unverified). The accuracy lane re-verified every §4 citation exact; the technique lane
surfaced three real gaps. All fixed:
- MEDIUM — the v2 draw named `glMultiDrawElementsIndirect`, but the blade is a
  **non-indexed** `GL_TRIANGLE_STRIP` (`glDrawArraysInstanced`, no index buffer) and
  `…ElementsIndirect` *requires* an index buffer → corrected to
  **`glMultiDrawArraysIndirect`** (GL 4.3 core, non-indexed) in §2/§3/§5.3/§10; the
  engine has no `…ArraysIndirect` call yet — the precedent is `gpu_particle_system`'s
  `glDrawArraysIndirect`, scaled to multi-draw.
- MEDIUM — §7's "few tens of MB / 1–4 k blades/m² / 10⁵–10⁶ drawn" figures were
  mutually inconsistent: v1 places **once, camera-independent**, so **stored** count is
  `base-density × footprint`, not the drawn count. Grounded the footprint (256×256 m,
  `test_meadow_terrain.cpp:199-203`; ~60 k m² grass) and made storage explicit — a
  ~256 MB SSBO budget caps base density at ~130 blades/m²; reference near-density is
  reached via LOD survivor-widening, uniform high density deferred to v2 (§7).
- MEDIUM — LOD was selected **per chunk** (one draw = one segment count + one blade
  fraction), so a chunk crossing a band would pop its whole population — contradicting
  §8's "no LOD pop". Specified the actual mechanism: segment axis stays per-chunk
  (coarse, placed sub-pixel-far); blade-count axis **fades per blade** in the VS
  (geometric height/width → 0 over the blend band by each blade's own distance — opaque,
  no alpha cross-fade), decoupling the fade from the chunk boundary (§5.3/§8).
- LOW ×5 — nudged `getHeight` 120→**121** / `getNormal` 147→**148** (the loop-2 nudge
  had overshot the signature line); re-attributed the `--profile-log` CSV logger to
  **3D_E-0027** (3D_E-0030 is the consuming gate), §7; added the two-sided
  **`!gl_FrontFacing` normal-flip** so backlit backsides aren't black (§5.1); noted the
  attribute-less draw needs a **bound non-zero VAO** (VAO 0 = `GL_INVALID_OPERATION` in
  core; `particle_renderer.cpp:370` precedent, §5.1); tip-vertex normal falls back to the
  **row N−1 normal** (width→0 degenerates the width axis, §5.1).
- INFO — noted wind control-point offsets faintly stretch the Bézier arc (negligible at
  the §11 clamp; rotate-about-root if it ever shows) — §5.4.

Loop 3 surfaced substantive findings (3 MEDIUM) → **not converged**. Loop 4 (cold) is
the next step before sign-off + G1.

**Loop 4 (2026-07-19)** — 2 cold reviewers, identical cold briefs. Tally: CRITICAL 0
· HIGH 1 · MEDIUM 4 · LOW 2 · INFO 0 (all 7 verified against source, 0 unverified).
Both lanes again confirmed every §4 citation exact and the topology/struct/memory math
sound; the remaining findings are seam/ordering/attribution gaps. Fixed:
- MEDIUM — §5.5 said "**Per-chunk SSBOs**", which breaks the v1↔v2 "additive" seam it
  claims: a single `glMultiDrawArraysIndirect` / single compute dispatch can bind only
  **one** SSBO, so per-chunk buffers would force a consolidation (rewrite) in v2 →
  changed to **one shared blade SSBO** with per-chunk **base offset** in the descriptor;
  the VS indexes `gl_InstanceID + chunkBaseOffset` (noted `gl_InstanceID` excludes
  `baseInstance`) — aligned §5.2/§10 (§5.5/§5.2/§10).
- MEDIUM — §5.1/§5.4 normal handling flipped **after** the vertical bias (bias→flip),
  which drives a back-lit blade's normal downward → black backside, the exact artifact
  the flip prevents → specified **face-forward flip first, vertical bias second**, both
  faces keep a positive up-component (§5.1).
- MEDIUM — §7 attributed near-camera density to **survivor-widening**, but that is a
  far-field mechanism → corrected: v1 near-camera density **is** the base density
  (~130/m²); the dense continuous *read* comes from **grazing-angle overlap**, and
  confirming it close-up is the flagship **G5 visual risk** (§7).
- MEDIUM — §2 still named `glMultiDrawElementsIndirect` for the grass v2 path (missed in
  loop 3) → `glMultiDrawArraysIndirect` (non-indexed), §2.
- LOW — §5.3's per-blade fade said "by its own distance", but *membership* in the drop
  set is set by **rank** (`gl_InstanceID / chunkBladeCount` vs the fraction thresholds),
  distance only the *amount* → spelled out rank-picks-who / distance-picks-how-much (§5.3).
- **HIGH — phase-number collision (resolved).** The loop-1 rename 14→15 dodged Phase 14
  (Adaptive Geometry) but **15 is Atmospheric Rendering** (`ROADMAP.md:29`,
  `phases_12_to_26_stubs.md:48-56`, live cross-refs at ROADMAP :405/:424/:603); in fact
  every number 12–26 is a reserved mega-phase. **Resolution (user decision):** dropped
  the Phase-NN framing — renamed to **`phase_10_meadow_gpu_grass_design.md`** / title
  "Meadow GPU Grass", a sibling of the earlier `phase_10_meadow_*` grass docs. The §2
  Phase-14 label was corrected in the same pass ("virtualised geometry" → "Adaptive
  Geometry System"; it is a ROADMAP-level plan, not a design doc).

Loop 4 surfaced a HIGH (naming) + 4 MEDIUM → **not converged**. Renamed to the meadow
bucket; Loop 5 cold on the renamed doc is next before sign-off + G1.

**Loop 5 (2026-07-19)** — 2 cold reviewers on the renamed doc. Tally: CRITICAL 0 · HIGH
0 · MEDIUM 2 · LOW 2 · INFO 1 (verified 4 / unverified 1). The **accuracy lane came back
fully clean** — every §4 citation exact, all topology/struct/memory numbers consistent,
phase-naming resolved. Technique lane (fixed):
- MEDIUM — §5.3 fade had an **unstated precondition**: removal (the instanceCount step)
  is per-chunk, so if it keys on chunk-*centre* a near-edge drop-set blade is still
  mid-fade when the chunk drops it → the pop returns. Added: the fraction step + segment
  tier must key on the chunk's **nearest** point and the **blend band ≥ the chunk's
  max distance-spread** (§5.3).
- MEDIUM — grass anti-aliasing. The reviewer's premise ("engine has no MSAA/TAA, only
  FXAA+CAS") was **UNVERIFIED and dismissed** — the engine renders to a **4× MSAA** FBO
  by default (`engine.cpp:110`, `renderer.cpp:530-544`) with TAA/SMAA/FXAA modes. But the
  *valid* sub-point held: view-facing widening needed a concrete floor + a motion check.
  Added a **~1–1.5 px projected-width floor**, noted the TAA/SMAA path renders non-MSAA
  so widening + fade carry there, and added a **moving-pose shimmer/crawl** check to
  G3/§8 (a static screenshot hides temporal aliasing) (§5.1/§8/§10).
- LOW — the G5 `grass_dense` pose is grazing (flatters overlap); the flagged density risk
  is the **look-down** case → added a downward-pitched **`grass_lookdown`** pose to §7/§8/G5.
- LOW — the Rule-7 parity mirror covers only the **static** curve; wind + view-widening
  also move the vertex → scoped the seam explicitly, noted they're visual-checked (§8).
- INFO — softened §1's density claim to "v1 target density … higher uniform density in v2".

**Max-loops cap (5) reached.** Loop 5's one genuinely-substantive finding (the §5.3 fade
precondition) is fixed; the rest were testability adds + one dismissed reviewer error.
The accuracy lane is clean and findings have narrowed each loop (L3: 3 MED; L4: 1 HIGH +
4 MED; L5: 1 real MED + polish) with none resurfacing. Per the cap rule, the
continue/accept decision goes to the user before sign-off + G1.

**SIGNED OFF (2026-07-19).** At the loop-5 cap the user chose *accept & sign off*
(accuracy lane clean, findings narrowing and non-resurfacing, last substantive finding
fixed). Design is the contract for G1–G5; implementation begins at G1.

**Post-G1 refinement (2026-07-19) — clumping + tall/wild.** After G1 shipped the blade
pipeline, the user flagged the meadow still reads as tufts/lawn and asked for a research
sweep on real long-grass fields. The finding: **clumping** (Voronoi/noise clump points
controlling per-clump height/lean/colour/bend) is the "field, not tufts" lever a naïve
per-blade-random field misses. Folded in — new **§5.2a** clump layer (a pure
`grassClump(xz)` function, no `GrassBlade` struct change since it's a function of
`rootPos`), tall/wild defaults, per-clump tint/wind, updated §1/§3/§5.4/§6/§8/§10/§15.
Lands in **G2** (placement + clump function + parity) with colour/wind in **G4**. This
is a material design change → **re-run cold-eyes** on the clumping addition before G2.

**Clumping cold-eyes loop 1 (2026-07-19)** — 2 cold reviewers (technique + accuracy).
Tally: CRITICAL 0 · HIGH 1 · MEDIUM 3 · LOW 4 (all verified). The addition was sound but
under-specified its CPU interaction; all fixed:
- HIGH — clump height/lean change the *drawn* geometry, but the design framed clumping as
  "VS-only, no CPU knowledge" → the CPU chunk **AABB** (frustum cull + LOD nearest-point)
  would be built from base height and **falsely cull** tall/leaning tussocks at the
  frustum edge. Corrected §5.2a: no per-blade *storage* change, but the CPU cull path
  evaluates `grassClump` and pads the AABB by clump-max height + lean reach.
- MEDIUM — `1 − distToCentre` is **not** seam-free at a Voronoi boundary (there d₁≈d₂ →
  weight ~½ → factor jump) → blend the **two nearest** clumps by boundary distance
  (`w = d₂/(d₁+d₂)`); single-cell-hash is the seam-free fallback.
- MEDIUM — blending `facingAngle` (a scalar radian) toward `clumpLeanDir` wraps wrongly
  at 0/2π → blend as a **unit-vector direction** (shortest arc) then back to the angle;
  `clumpLeanDir` specified as a unit `vec2`.
- MEDIUM — `fract(sin(dot()))` hashing won't bit-match CPU↔GPU (breaks the AABB/parity) →
  specified a **deterministic integer bit-hash** (PCG/xxhash-style) for both.
- LOW ×4 — pinned the 3×3 Voronoi neighbourhood + `clumpHeight` 0.6–1.6× range + tint
  clamp; added a **C⁰ seam-continuity** assertion to the §8 parity test; noted the
  segment-LOD sub-pixel distance keys on clump-max height; added `clumpPhase` (per-clump
  wind sway) to §5.2a's factor list and reconciled it with §5.4's per-blade desync.

Loop 1 surfaced a HIGH → **not converged**. Loop 2 (cold) confirms before G2.

**Clumping cold-eyes loop 2 (2026-07-19)** — 2 cold reviewers. Tally: CRITICAL 0 · HIGH 0
· MEDIUM 2 · LOW 2 (all verified). The loop-1 fixes held; the reviewers caught a real math
error *in* the loop-1 seam fix:
- MEDIUM — the loop-1 "two-nearest blend `w=d₂/(d₁+d₂)`" is **not** globally C⁰: it is
  continuous only across the nearest-pair edge, and **jumps** where the second-nearest
  clump swaps (interior loci / Voronoi triple points) — a counter-example confirmed it →
  replaced with a **smooth-falloff kernel** over the 3×3 neighbourhood (`wᵢ =
  smoothstep(kernelR,0,dᵢ)`, normalised weighted average), C⁰/C¹ everywhere. The §8 test
  now samples a **grid** (crosses triple points), so it can actually catch a regression.
- MEDIUM — `normalize(mix(dirA,dirB))` for the lean blend **NaNs** at near-antipodal
  directions (≈0 vector) → added an **antipodal guard** (fall back to dominant-weight dir).
- LOW — `clumpPhase` is cyclic; kernel-blending it re-wraps at 0/2π → taken **nearest-only**
  (clumps sway as separate bodies; blend the displacement vector, not the phase, if needed).
- LOW — §5.2's "randomise … tint" enumeration implied a stored tint field (would break the
  32 B `static_assert`) → clarified tint is packed in `hash`, not a struct field.

Loop 2 surfaced substantive math fixes → **not converged**. Loop 3 (cold) confirms before G2.

**Clumping cold-eyes loop 3 (2026-07-19)** — 2 cold reviewers. Tally: CRITICAL 0 · HIGH 1
· MEDIUM 3 · LOW 3 (all verified). Loop-2's kernel blend was right in spirit but shipped
on unstated constraints:
- HIGH — the normalised kernel average **`0/0` NaNs** when all 3×3 weights are zero (a
  sample in an uncovered corner pocket); loop-2 dropped loop-1's nearest-cell fallback →
  restored it as a hard `Σwᵢ<ε` safety net (**no NaN regardless of tuning**), + §8 asserts
  finiteness at cell corners.
- MEDIUM — `smoothstep(kernelR, 0, d)` is **GLSL-undefined** (`edge0 > edge1`) and a
  spec-literal driver returns 1 → clumping silently off → rewrote as
  `1 − smoothstep(0, kernelR, d)`.
- MEDIUM — C⁰ holds only inside an envelope the doc didn't state → pinned the invariant
  (`kernelR ≤ cellSize` for a 3×3 search; bound jitter so the own cell always covers; 5×5
  if wider blending is wanted) and scoped the "C⁰ everywhere" claim to the **scalar**
  factors.
- MEDIUM — §6 CPU/GPU table omitted the CPU cull-AABB `grassClump` role (loop-1's HIGH) →
  added it so §6 matches §5.2a.
- LOW ×3 — negative cell coords need `floor` (not `int()`) + `uint32` two's-complement
  hash (meadow is origin-centred, so negatives are the common case); the lean-direction
  fallback needs a **deterministic tie-break** (lowest cell id) for CPU/GPU parity; and the
  pre-existing "research §5/§6" cites (which don't map to a section) corrected to §3.

Loop 3 surfaced a HIGH (NaN) → **not converged**. Loop 4 (cold) confirms before G2.

**Clumping cold-eyes loop 4 (2026-07-19)** — 2 cold reviewers. **Both lanes converged on
one coherent issue** (a strong convergence signal): the loop-3 kernel *envelope*
(`kernelR ≤ cellSize` + "±¼ cell" jitter) did **not** actually guarantee coverage across
the tunable `kernelR` range, so the `Σwᵢ<ε` fallback was a **live discontinuous path** —
and §8 then contradicted itself (demanded C⁰ *and* that the fallback "fires at corners").
Resolved by pinning a **provably-correct window**:
- Jitter is a **radius `jr ≤ 0.25·cellSize`**; coverage (worst case a cell corner) needs
  `kernelR ≥ 0.707·cellSize + jr`; C⁰ (excluded centre zero-weight) needs
  `kernelR ≤ 1.5·cellSize − jr`. Both hold for the committed **`jr = 0.25·cellSize`,
  `kernelR = cellSize`** (window `[0.957, 1.25]·cellSize` non-empty) → `Σwᵢ > 0`
  **everywhere**, so the `Σwᵢ<ε` guard (`ε=1e-6`) is an **unreachable float-safety net**,
  not a live seam. The coverage invariant is "**≥ 1 of the 3×3 centres within `kernelR`**"
  (not "own-cell centre"). Crisper tussocks → 5×5 search, not sub-envelope `kernelR`.
- §8 split into two non-contradictory properties: **(i) C⁰** over the (now fully) covered
  field, **(ii) finiteness** of the fallback via a **synthetic degenerate** input (not "at
  cell corners"). Q1 correctness (smoothstep, floor/uint hash, antipodal guard) and Q4
  (CPU-AABB-by-clump-max + §6 table) both verified clean.

Loop 4 converged: both lanes on a **single** now-fixed issue, closed with provably-correct
committed constants (`jr=0.25·cellSize`, `kernelR=cellSize`, `ε=1e-6`) and a self-consistent
§8 test. The clumping design is the contract for **G2**; the §8 parity test is the
build-time enforcement of the C⁰/finiteness properties.

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
- **Clumping sweep (2026-07-19):** GoT GDC talk (Voronoi clumping — clump controls
  height/direction/colour/bend) https://gdcvault.com/play/1027033/Advanced-Graphics-Summit-Procedural-Grass
  · breakdown https://tigerabrodi.blog/what-we-can-learn-from-grass-in-ghost-of-tsushima-renders
  · hexaquo "Grass Rendering — Theory" (four realism properties; full-geometry recommended)
  https://hexaquo.at/pages/grass-rendering-series-part-1-theory/
  · Acerola grass renderer (**shell texturing — rejected: reads as fuzz, not blades**)
  https://archive.org/details/grass-renderer
  · UE5 Grass Quick Start (density/clump tuning) https://dev.epicgames.com/documentation/unreal-engine/grass-quick-start-in-unreal-engine
