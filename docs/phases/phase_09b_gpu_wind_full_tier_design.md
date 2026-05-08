# Phase 9B GPU Cloth Wind — FULL Tier Design (Sh4)

## Status

Design doc, **awaiting blocking review** (project rule 1: research →
design → review → code). No code lands until this is reviewed.

Closes Phase 10.9 Slice 16 **Sh4** in the ROADMAP. Unblocks Slice 17
**Cl1** (CPU↔GPU cloth parity harness) once the FULL tier ships, since
parity coverage of the SIMPLE/APPROXIMATE/FULL trio depends on FULL
existing on the GPU.

## The bug being fixed

`engine/physics/cloth_simulator.cpp:1908-2052` (CPU path) implements
three quality tiers for cloth wind:

| Tier | Per-particle FBM (grid-coord hashNoise) | Per-triangle turbulence (centroid-coord FBM) | Per-triangle aerodynamic drag |
|---|---|---|---|
| `SIMPLE` | no | no | no |
| `APPROXIMATE` | no | no | yes (uniform wind) |
| `FULL` | yes | yes | yes (turbulence-modulated wind) |

`assets/shaders/cloth_wind.comp.glsl` (the GPU port) implements only an
**isotropic exponential drag** — `v += (windVel - v) * dragCoeff * dt`
applied per-particle. There is no surface-orientation dependence, no
turbulence, no FBM noise. `setWindQuality()` is plumbed through to
`u_dragCoeff` only, so all three tiers produce **the same output on
GPU** today — a known regression versus the CPU path.

The `setWindQuality` getter/setter is therefore visually meaningless on
the GPU backend. Cl1's parity harness can't bring up CPU↔GPU parity
loops for the wind step until the GPU shader matches the CPU's
per-triangle behaviour at FULL.

### CPU FULL tier — full inventory of state to port

The CPU `applyWind`/`precomputeWind` pair pulls from more state than
just the FBM and turbulence arrays. Every item in this list has a
binding/uniform/upload in the GPU port:

| CPU symbol (`cloth_simulator.cpp` line) | What it is | GPU location |
|---|---|---|
| `m_cachedParticleWind` (line 456, vec3 per particle) | Grid-coordinate `hashNoise` perturbation; FULL only. | New SSBO `ParticleWindFbm` |
| `m_cachedTriangleTurb` (line 457, float per triangle) | Centroid-coordinate FBM scalar; FULL only. | New SSBO `TriangleTurbulence` |
| `m_cachedFlutter` (line 458, scalar) | Time-only sin-based gust modulation. | New uniform `u_flutter` |
| `m_gustCurrent` (state machine, `updateGustState`) | Frame-level scalar combining gust ramp + flutter. CPU folds it into `baseWindVel` before the per-triangle loop. | Folded into `u_windVelocity` on CPU side before upload — NOT a separate uniform |
| `m_windDirOffset` (line 1995) | Per-frame perturbation of wind direction. CPU folds it into `effectiveDir` before the loop. | Same — folded into `u_windVelocity` on CPU side |
| `m_windPrecomputed` (line 1989) | Early-out flag if `precomputeWind` hasn't run. | CPU-side guard before dispatch — GPU shader assumes precompute ran |
| Pinned-particle guard (`m_inverseMasses[i] > 0.0f`, lines 2048-2050) | Skip the per-vertex velocity write when invMass = 0. | Multiplying by `invMass` (already in the SSBO) is mathematically equivalent (0 × delta = 0); no separate branch needed but the multiplication must NOT be skipped |
| **Step ordering**: per-particle FBM perturbation runs *before* the per-triangle drag loop (lines 1999-2006 vs 2008-2052), so the drag's `vAvg` reads the already-perturbed velocities. | Implementation contract | The Sh4b shader sequence must dispatch `cloth_wind_fbm` **before** `cloth_wind_drag` per substep |

Two things to call out from this table that the first draft missed:

- The two FBM functions are **different**: `m_cachedParticleWind`
  uses grid-coordinate `hashNoise` (line 1950); `m_cachedTriangleTurb`
  uses centroid-coordinate FBM (line 1978). Both ride along to GPU
  as cached SSBOs, but the CPU precompute that fills them runs two
  distinct noise calls. Sh4b's bridge layer must preserve the split.
- `m_gustCurrent` and `m_windDirOffset` aren't separate GPU state —
  the CPU folds them into the wind velocity before upload. The
  uniform `u_windVelocity` is `effectiveDir * windStrength * gustCurrent * flutter`,
  not just `windDirection * windStrength`. The first draft elided this.

## Push-back: is full per-triangle drag actually needed?

Before settling on the implementation, pushing back on the requirement
itself (CLAUDE.md rule 9):

**Argument for keeping the FULL tier on GPU.** Cloth animation looks
qualitatively different under per-triangle vs isotropic drag. A flag
or curtain edge-on to wind should slip through with much less force
than face-on. Without per-triangle, a vertical curtain in horizontal
wind billows the same as a horizontal banner — a visible authoring
problem the artist sees on first wind preview. Phase 10.9 shipped this
behaviour on CPU; backwards-incompatibility here is a real regression.

**Argument for collapsing to APPROXIMATE only on GPU.** APPROXIMATE
gets 70–80% of the visual win (uniform per-triangle drag without
turbulence) and lands in maybe 30 lines of new shader. FULL adds the
two cached arrays (per-particle wind perturbation, per-triangle
turbulence factor) and the more complex shader, for a smaller visual
delta. A user-facing tier that says "FULL is GPU-only on Vulkan"
would be acceptable for a shipping engine — many engines do exactly
this for compute-heavy effects.

**Recommendation: ship FULL on GPU, but stage the work.** Land
APPROXIMATE-on-GPU first as Sh4a (just the per-triangle drag, no
turbulence, no FBM); land FULL-on-GPU as Sh4b once the
per-triangle infrastructure is there and we've measured the
parity gap. Sh4a is much smaller than Sh4 as scoped, and Cl1 can
gate on Sh4a alone for the parity harness's first cut (testing
SIMPLE + APPROXIMATE only).

This staging is the surface-ambiguity question (CLAUDE.md rule 8) the
reviewer should answer before code lands. The rest of this doc covers
both stages so the reviewer can choose with full information.

## Sh4a: per-triangle aerodynamic drag on GPU

### CPU contract to mirror

`cloth_simulator.cpp:2008-2052`:

```cpp
for each triangle (i0, i1, i2):
    windVel  = baseWindVel * triTurb[ti]      // FULL only; APPROXIMATE = baseWindVel
    vAvg     = (v[i0] + v[i1] + v[i2]) / 3
    vRel     = windVel - vAvg
    edge1    = p[i1] - p[i0]
    edge2    = p[i2] - p[i0]
    crossVec = cross(edge1, edge2)
    area2    = length(crossVec)
    if area2 < 1e-7: continue
    normal   = crossVec / area2
    area     = area2 * 0.5
    vDotN    = dot(vRel, normal)
    force    = normal * (0.5 * dragCoeff * area * vDotN)
    pv       = force * (dt / 3)
    v[i0] += pv * invMass[i0]
    v[i1] += pv * invMass[i1]
    v[i2] += pv * invMass[i2]
```

The math is per-triangle but the **writes are per-vertex** — three
incident triangles all want to write to the same vertex's velocity.
That's the contention the GPU port has to resolve.

### Three implementation options

The reviewer picks one. All three preserve the same per-triangle
math; they differ only in how `v[i] +=` is sequenced.

#### Option A — `atomicAdd` over packed int32 floats

One compute thread per triangle. Each thread computes the per-vertex
delta and `atomicAdd`s it into the velocities SSBO.

```glsl
layout(std430, binding = 2) buffer Velocities { vec4 velocities[]; };
// ... per-component as int32 + atomicCompSwap loop, since
// atomicAdd on float is GLSL 4.6 + ARB_shader_atomic_float — not
// guaranteed on RX 6600 GL 4.6. Spec-safe path = the "atomic CAS"
// dance over uint bit-cast of float.
void atomicAddFloat(inout uint slot, float delta) {
    uint expected, desired;
    do {
        expected = slot;
        desired  = floatBitsToUint(uintBitsToFloat(expected) + delta);
    } while (atomicCompSwap(slot, expected, desired) != expected);
}
```

**Pros.** Simplest dispatch (one shader, one thread per triangle, no
adjacency data). Mirrors the CPU loop nearly 1:1.

**Cons.**
- `ARB_shader_atomic_float` is **not in core**, only an extension.
  `RX 6600 (RDNA2, GL 4.6, Vulkan 1.3)` exposes it via Mesa per
  current driver, but a pre-2024 driver on the same chip might not.
  CAS-loop emulation works but is slow under contention (a vertex with
  6 incident triangles will retry up to 6× under heavy parallelism).
- **Non-deterministic** order of accumulation → bit-different
  velocities run-to-run. That kills any future replay-determinism
  claim (Phase 11A) and complicates Cl1's parity test (the CPU is
  deterministic in iteration order; GPU isn't).
- Contention scales poorly with grid size. A 256² cloth has 130k
  triangles all hammering ~65k vertex slots = average 2× contention
  per slot, peak 6×.

#### Option B — colour-grouped triangle dispatch *(recommended)*

CPU-side, one-shot at init: greedy-colour the triangles such that no
two triangles in the same colour share a vertex. Dispatch the wind
shader **once per colour**, with one thread per triangle in that
colour.

```cpp
// engine/physics/cloth_constraint_graph.{h,cpp} already has a
// triangle-colouring routine for the dihedral constraints
// (`colourDihedralConstraints`). This is the same problem at the
// triangle-incidence-on-vertex level rather than the
// dihedral-incidence-on-vertex level, so a sibling
// `colourTriangleConstraints` mirrors that code.
struct ColourRange { uint32_t first; uint32_t count; };
std::vector<ColourRange> colourTriangleConstraints(
    const std::vector<uint32_t>& indices,
    uint32_t particleCount,
    std::vector<GpuTriangle>& outTriangles);
```

Per-frame:

```cpp
for (const ColourRange& cr : m_triangleColourRanges) {
    glUniform1ui(uFirstTri, cr.first);
    glUniform1ui(uTriCount, cr.count);
    glDispatchCompute((cr.count + 63) / 64, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}
```

Inside the shader, no atomics — each thread writes directly to
`velocities[i0/i1/i2]`.

**Pros.**
- **Cross-run GPU determinism.** Colour groups are processed in fixed
  dispatch order; threads within a colour write to disjoint vertices.
  Two GPU runs of the same scene produce identical velocities — the
  property Phase 11A replay needs.
- **No driver-extension dependency.** Pure compute writes; works on
  any GL 4.5 compute-capable card.
- **Mirrors existing colouring infrastructure.** `cloth_constraint_graph.cpp`
  already greedy-colours edges (`colourConstraints`) and dihedral
  4-tuples (`colourDihedralConstraints`) using a 64-bit per-particle
  bitmask. The triangle-vs-vertex incidence graph is a different
  data shape (3 vertices instead of 2 or 4), so the new
  `colourTriangleConstraints` mirrors the **structure** of the existing
  routines but is genuinely new code — not a "reuse" claim.
- **Aligns with rule-7 GPU/CPU placement decision** for the existing
  constraint solvers — same pattern, same complexity, same cost.

**Caveat — bit-equality with CPU is NOT preserved.** Floating-point
addition is commutative but **not associative**: `(a + b) + c ≠ a + (b + c)`
in general FP32. CPU's `applyWind` walks triangles in `m_indices`
order; GPU's colour-grouped path walks them in colour-bucket order.
For a vertex with N incident triangles, CPU and GPU sum the same N
contributions but in different orders → last-1-or-2-ULP differences
expected. Cl1's parity harness must use an **epsilon tolerance**, not
exact equality. The earlier draft of this doc claimed bit-equality;
that was wrong — the right justification for Option B is **cross-run
GPU determinism** (every Vestige replay on the same GPU produces
identical results) plus the no-extension and infrastructure-fit
arguments above.

**Cons.**
- ≤ 6 colour dispatches per substep (chromatic number of the
  triangle-vs-vertex incidence graph for a regular grid; greedy
  may emit up to 8 in the worst case before subgroup re-bucketing).
  At 16 substeps × 6 colours = ~96 dispatches/frame just for the
  wind step. Each is a small kernel (T/6 threads), so dispatch
  overhead may dominate compute on small cloths. Verify-step 4
  measures the actual cost; if it dominates we either merge small
  colours or accept Option A's atomic path.
- One-shot CPU colouring at init adds setup-time cost (analogous to
  the existing dihedral colouring; budget tracked the same way under
  the editor's "apply preset" path that Cl6 just optimised).
- Need a new `m_triangleColourRanges` SSBO + a `GpuTriangle` struct
  storing the three vertex indices per triangle.

#### Option C — per-vertex gather over incident triangles

Build a topology-adjacency SSBO at init storing, for each particle,
the list of incident triangle indices. Dispatch one thread per
**particle**; each thread gathers force contributions from every
triangle it touches.

```glsl
// Per-particle adjacency (CSR-style):
//   adjOffset[p..p+1]  → range into adjList[]
//   adjList[i]         → triangle index
struct GpuTriangle { uint i0, i1, i2; };
layout(std430, binding = 9) buffer Triangles { GpuTriangle tris[]; };
layout(std430, binding = 10) buffer AdjOffsets { uint adjOffset[]; };
layout(std430, binding = 11) buffer AdjList   { uint adjList[]; };

void main() {
    uint p = gl_GlobalInvocationID.x;
    if (p >= u_particleCount) return;
    vec3 deltaV = vec3(0.0);
    uint begin = adjOffset[p];
    uint end   = adjOffset[p + 1];
    for (uint k = begin; k < end; ++k) {
        // Recompute the triangle's normal + area + force, find which
        // of the 3 vertices is `p`, accumulate this triangle's share
        // of the per-vertex force.
    }
    velocities[p].xyz += deltaV * invMass[p];
}
```

**Pros.**
- Single dispatch per substep. No atomic contention, no colour count.
- Deterministic at the per-particle level (each particle reads its
  triangles in fixed order).
- Adjacency SSBO can be reused for any future per-vertex-gather
  feature (e.g. SH probe construction, mesh-collider self-collision
  on GPU).

**Cons.**
- **3× redundant cross-product math.** The same triangle's normal +
  area is recomputed once per incident vertex (typically 3 vertices,
  so 3× the work of options A and B at the triangle-math level).
- **Irregular memory access.** CSR-indexed reads from `adjList[]` and
  `tris[]` are pointer-chasing patterns the GPU doesn't love.
- **Two new SSBOs** (`adjOffset`, `adjList`). Bandwidth overhead +
  binding management for both.
- Per-particle determinism doesn't compose into per-frame determinism
  if the colour ordering of constraint dispatches in the broader
  substep loop already shuffles state — but that's an issue with all
  three options.

### Recommendation: Option B (colour-grouped)

- **Cross-run GPU determinism** — every replay on the same GPU
  produces identical velocities, what Phase 11A replay needs. This
  does NOT mean CPU↔GPU bit-equality; see the §Caveat above.
- **No driver-extension dependency** wins for Mesa-compatibility.
- **Mirrors existing colouring code structure** — `colourConstraints`
  and `colourDihedralConstraints` already exist in
  `cloth_constraint_graph.cpp`; the new `colourTriangleConstraints`
  is a sibling routine sharing the bitmask-walk pattern (~80 LOC,
  not a one-liner).
- **Dispatch overhead is the only real cost**; if it shows up in
  profiling, the fix is well-understood (subgroup-sized colour bins
  or merging small colours). Verify-step 4 measures it.

This is the recommendation **the reviewer is asked to confirm or
overrule**. If the reviewer prefers C for single-dispatch simplicity
or A for raw lines-of-code minimum, the rest of the doc adapts —
the per-triangle math is identical across options.

## Sh4b: per-particle FBM noise + per-triangle turbulence

### Two cached arrays the CPU FULL tier consumes

```cpp
// cloth_simulator.h:455-458
std::vector<glm::vec3> m_cachedParticleWind;   // per-particle FBM noise
std::vector<float>    m_cachedTriangleTurb;    // per-triangle scalar
```

Both are recomputed once per frame in `precomputeWind()` and
consumed N times in `applyWind()`. The CPU spec for the FBM noise
function lives in `cloth_simulator.cpp:1920-1965`.

### CPU-compute-and-upload vs shader-recompute

**Upload-once-per-frame.**
- 256² particles × 16 bytes (vec3 padded to vec4) = 1.0 MB for
  `ParticleWindFbm`.
- ~130 050 triangles × 4 bytes = 0.52 MB for `TriangleTurbulence`.
- Total: ~1.5 MB upload per frame. Cached on GPU and consumed
  16× per substep — the upload runs once, not per substep.

**Shader-recompute.**
- FBM noise is 3-octave at FULL tier (`cloth_simulator.cpp:1932`).
  Each octave is one `noise(vec3)` call.
- If recomputed once per frame and cached on GPU (what the upload
  path also gives us): 3 octaves × 65 k particles + 3 octaves ×
  130 k triangles ≈ 0.6 M sample evaluations per frame.
- If recomputed per substep (no caching): × 16 substeps ≈ 9.4 M
  samples per frame — the version that actually competes with the
  upload path.

**Decision: upload per frame.** The honest comparison is
"upload + cache" vs "shader-recompute + cache" — both pay 0.6 M
samples per frame, the difference is *where*. Three reasons to
keep the compute on CPU and upload:

1. **Bit-identical FBM across vendors.** CPU `glm`-based simplex /
   hashNoise is reproducible across vendor drivers; GPU FP32
   (especially fast-math `precise` lapses) is not. Cl1 parity
   testing wants the FBM input to be the *same* on both backends
   so the only divergence is solver math.
2. **CPU FBM is already implemented and tested.** Re-implementing
   the two noise functions as GLSL would duplicate a contract.
3. **PCIe upload of 1.5 MB is ~0.1 ms at 16 GB/s** on the dev
   hardware — well below the per-substep frame budget. Even
   amortised across 16 substeps the cost is invisible.

Re-noising per substep is the only path that's clearly worse, and
nobody's proposing it.

### New SSBO bindings

Following the existing convention in `gpu_cloth_simulator.h`:

```cpp
enum class BufferBinding : GLuint {
    Positions      = 0,
    PrevPositions  = 1,
    Velocities     = 2,
    Constraints    = 3,
    Dihedrals      = 4,
    Colliders      = 5,
    LRA            = 6,
    Normals        = 7,
    // Sh4 additions:
    Triangles          = 8,   ///< GpuTriangle{i0,i1,i2}, one per triangle.
    TriangleColours    = 9,   ///< ColourRange{first,count}, one per colour.
    ParticleWindFbm    = 10,  ///< vec4 per particle (xyz = perturbation).
    TriangleTurbulence = 11,  ///< float per triangle (1.0 = baseline).
};
```

`Triangles` and `TriangleColours` are immutable after init (one-shot
upload). `ParticleWindFbm` and `TriangleTurbulence` re-upload every
frame in `precomputeWind()`.

## Verify-step plan (CLAUDE.md rule 12)

Each step is a separate commit; verify checks gate the next.
Tolerances are grounded in FP32 ULP and XPBD substep error rather
than round-number percentages.

```
Sh4a — per-triangle drag, no turbulence:

1. Add `colourTriangleConstraints` to cloth_constraint_graph.{h,cpp};
   one-shot greedy colour over {triangle, particle} incidence graph.
   → verify: new test in test_cloth_constraint_graph.cpp asserts
     (a) no two triangles in same colour share any vertex (per-vertex
     bitmask scan); (b) colour count for a regular triangulated NxM
     grid is ≤ 6 — the chromatic number of the triangle-vs-vertex
     incidence graph for a regular grid (each interior vertex has 6
     incident triangles in the standard NW-SE diagonal triangulation).
     Greedy may emit 7-8 in practice; assert ≤ 8 with a TODO to drop
     to 6 if dispatch overhead becomes an issue.

2. Add `GpuTriangle` SSBO + colour-range upload to gpu_cloth_simulator.
   → verify: add `getTriangleColourCount()` getter + a
     `BindTrianglesEnumPinned` test mirroring the existing
     dihedrals binding-enum test.

3. Author `assets/shaders/cloth_wind_drag.comp.glsl` — one thread per
   triangle, writes per-vertex velocity directly (no atomics, colour
   guarantees disjoint writes).
   → verify: new test in test_gpu_cloth_simulator.cpp asserts that
     after 1 substep with strong perpendicular wind on a vertical
     plane cloth, |velocity_GPU[i] - velocity_CPU[i]| ≤ 1e-5 m/s for
     every free particle (~10 ULPs of FP32 at velocities of order
     1 m/s; covers FP commutativity-only summation differences).
     Sign of velocity[centre].x must match sign(windDir.x).

4. Wire substep loop to dispatch the new shader once per colour
   (analogous to constraint colour-dispatch loop already present).
   → verify: existing 38-test cloth suite still green; the
     single-substep parity test from step 3 passes; the dispatch
     count per substep equals `1 + getTriangleColourCount()`
     (one wind-velocity init pass + one per-colour drag pass).

Sh4b — per-particle FBM noise + per-triangle turbulence:

5. Refactor cloth_wind.comp.glsl to read `ParticleWindFbm` and
   `TriangleTurbulence` SSBOs when uniform `u_windQuality == FULL`.
   Add the `u_flutter` uniform; fold `m_gustCurrent`, `m_windDirOffset`,
   and `m_cachedFlutter` into `u_windVelocity` on CPU before upload.
   → verify: with a fixed seed, FULL produces velocity[i] differing
     from APPROXIMATE by ≥ 1e-3 m/s on at least 50% of unpinned
     particles after 1 substep. (Without FBM, FULL and APPROXIMATE
     would match exactly; this thresholds the "FBM data is reaching
     the shader" check.)

6. Bridge precomputeWind() output to the SSBOs each frame.
   FBM-perturbation dispatch must run before the colour-grouped
   drag dispatches within a substep.
   → verify: per-particle position delta |p_GPU[i] - p_CPU[i]| ≤
     1e-3 m after 2 s of simulation on the parity-test cloth (matches
     Cl1's full-loop tolerance, accounts for substep accumulation
     of per-substep ULP-level FP differences).

7. Wire `setWindQuality` getter on the GPU backend to honour the
   tier (today it's stored but unused). Tier-out behaviour now
   matches CPU's three-tier ladder.
   → verify: SIMPLE = no drag (matches old GPU behaviour);
     APPROXIMATE = uniform-wind per-triangle drag (matches CPU
     APPROXIMATE within step-3's tolerance); FULL = turbulence +
     FBM (matches CPU FULL within step-6's tolerance).

Cl1 (separate ROADMAP item) gates on Sh4b's verify steps 5-7.
```

## Performance expectations

The earlier draft of this doc tried to predict a CPU↔GPU speedup
table; the numbers didn't survive review (mismatched thread counts,
unsourced per-dispatch latency). Replaced with a measurement plan
instead — the verify-step tests above produce the real numbers, and
the implementation gates on those, not a forecast.

What we *do* know going in:

- CPU FULL wind on a 256² cloth at 16 substeps is the dominant
  cost in the cloth budget on the dev hardware (Ryzen 5 5600 +
  RX 6600). Profile from the existing cloth benchmark shows
  per-frame cloth wind ~30-40 ms at FULL.
- GPU constraint solver (the existing colour-grouped distance +
  dihedral path) on the same cloth is ~1.5 ms / frame for an
  equivalent number of dispatches. Wind has fewer constraints
  per dispatch (one per triangle vs one per edge for distance),
  so ballpark a similar order of magnitude.

What we'll learn from verify-step 4: actual per-substep wind cost
on GPU. If it's > 1 ms, the dispatch-overhead concern in §B-cons
is real and we either merge small colours or move per-substep work
into a single per-triangle pass with atomics (Option A as a backup,
accepting the determinism trade).

## Backwards compatibility

The new shader uses the same `BufferBinding::Velocities = 2` and
positions binding as the existing wind shader. Old saved scenes
keep working — the wind quality preset is stored in the cloth
config (`m_windQuality`) and is already round-tripped by the
serializer. Default for new scenes stays SIMPLE.

## Cited sources

1. **Provot, X.** (1995). "Deformation Constraints in a Mass-Spring
   Model to Describe Rigid Cloth Behaviour." *Graphics Interface
   '95*, pp. 147–154. — Original per-triangle aerodynamic-drag
   formulation. Source of the `0.5 * Cd * A * (vRel·n) * n` term used
   in the CPU implementation and to be ported to GPU.

2. **Müller, M., et al.** (2007). "Position Based Dynamics."
   *Journal of Visual Communication and Image Representation*. —
   The XPBD framework Vestige's cloth solver builds on; Section
   4.7 covers wind force coupling against the velocity solve.

3. **Choi, K-J. & Ko, H-S.** (2002). "Stable but responsive cloth."
   *ACM SIGGRAPH 2002*, pp. 604–611. — Per-triangle aerodynamic
   coupling discussion; informs the per-vertex distribution
   (`force * dt/3`) the design preserves.

4. **Le Grand, S.** (2007). "Broad-Phase Collision Detection with
   CUDA." *NVIDIA GPU Gems 3, Chapter 32.* — The graph-colouring
   parallel-write pattern Option B follows. (The earlier draft of
   this doc cited "GPU Gems 2 Ch. 32" — wrong; GPU Gems 2 (2005)
   predates CUDA, and the colouring chapter is in GPU Gems 3.)

5. **Bender, J., Müller, M., & Macklin, M.** (2014). "Position-Based
   Simulation Methods in Computer Graphics." *Eurographics Tutorial.*
   — Section on parallel constraint projection covers the same
   colour-grouped pattern in the PBD context Vestige's solver uses.
   Complements citation 4 with the cloth-specific application.

6. **Khronos OpenGL 4.6 core spec** + `GL_ARB_shader_atomic_float`
   extension spec (registered 2020). Confirms that float atomics are
   *not* in core 4.6 — only integer atomics and bitwise ops on
   image-store float views — so Option A's CAS-on-uint path is the
   spec-safe fallback. Mesa 24.x exposes `GL_ARB_shader_atomic_float`
   on RadeonSI/RADV for RDNA2 per the driver release notes; older
   Mesa or non-RDNA hardware would have to use the CAS fallback or
   default to Option B.
