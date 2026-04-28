# Phase 9B: GPU Compute Cloth Pipeline — Design

**Status:** Design — implementation gated on maintainer review.
**Roadmap:** Phase 9B → Cloth & Soft Body System → "GPU Compute Cloth Pipeline" sub-item.
**Prerequisite:** Phase 9B base shipped (ClothSystem wraps `ClothSimulator`); GPU particle pipeline shipped (precedent for SSBO + compute pattern).

---

## 1. Goal

Migrate the XPBD cloth solver from CPU (`engine/physics/cloth_simulator.cpp`, ~1,884 LOC) to a GPU compute pipeline so massively-resolution cloths (20×20 → 200×200 grid, ~40k particles) sustain the engine's hard 60 FPS budget without burning CPU time on inner constraint loops.

The CPU path remains the source of truth. GPU path is opt-in per cloth, with auto-selection above a particle-count threshold (mirrors `GpuParticleSystem`).

## 2. Why now

**CPU baseline cost.** The current XPBD solver does `substeps × constraints × particles` work per frame. At default 10 substeps with a 100×100 grid (10k particles, ~30k distance constraints), that is ~3 M constraint evaluations per frame. CPU profiling on the AMD 5600 shows this consumes ~6 ms at this resolution — a sixth of the 60 FPS budget for one cloth. Realistic scenes (multi-cloth banners, drapery, character clothing) need to support 4–10 cloths simultaneously without violating the budget.

**Hardware fit.** The AMD RX 6600 has 1,792 stream processors. XPBD's per-particle integration and red-black-partitioned constraint solving are embarrassingly parallel within each partition. The pattern is well-established in NVIDIA Flex, NvCloth, Bullet 3 GPU pipeline, and PhysX 5.

**Precedent in-tree.** `GpuParticleSystem` already proves the SSBO + compute + indirect-draw pattern works in this engine on Mesa AMD. We can reuse: SSBO buffer-management helpers, compute-shader compilation pipeline, draw-from-SSBO vertex shader pattern.

## 3. Research summary

### XPBD on GPU — algorithm

XPBD (Macklin, Müller, Chentanez 2016) is iteration-count-independent: stiffness is parameterised by *compliance* α, not by iteration count, so 10 substeps produces the same visual result whether a constraint is solved 1× or 1000× per substep. This lets the GPU solver use fewer Gauss-Seidel sweeps per substep without quality loss.

**Per-substep dispatch order:**
1. **Apply external forces** (gravity, wind, drag) → predicted positions
2. **Solve constraints** (distance: stretch / shear / bend) — multiple Gauss-Seidel sweeps
3. **Solve dihedral bending** (per-quad pair)
4. **Solve LRA** (long-range attachments to pins)
5. **Apply collisions** (spheres, planes, cylinders, boxes, mesh, ground)
6. **Update velocities** = (predicted − previous) / dt
7. **Apply velocity damping**

Per-frame (post-substeps):
8. **Self-collision** (spatial-hash rebuild + nearest-neighbour push, optional)
9. **Recompute normals** (per-vertex from triangle normals)

### The parallelism problem — and the red-black solution

Distance constraints share particles. Naïvely solving in parallel produces races: two threads writing different `Δp` to the same particle stomp each other. CPU code solves this by sequential Gauss-Seidel.

**Red-black graph colouring** assigns each constraint to a partition such that no two constraints in the same partition share a particle. For a regular grid cloth, structural and shear constraints fall into a natural 2-colour or 4-colour pattern:

```
Stretch X (horizontal): ⓡⓑⓡⓑⓡⓑ  (red = even col→col+1, black = odd col→col+1)
Stretch Y (vertical):   ⓡⓡⓡⓡⓡⓡ
                        ⓑⓑⓑⓑⓑⓑ
Shear:                  4-colour (NE/NW/SE/SW diagonals)
Bend:                   like stretch, 2-colour with stride-2
```

Each colour is dispatched as a separate compute pass — within a colour, all constraints can run truly in parallel (one workgroup per N constraints, one thread per constraint). This is the standard NvCloth / PhysX 5 approach and what NVIDIA Flex used.

For irregular topology (post-fracture cloth, torn cloth) we fall back to atomic-add accumulation with deferred normalisation — slower but correct.

### Constraint solver per-thread cost

Each thread:
1. Loads two particle positions (16 B + 16 B, two SSBO reads)
2. Computes correction `Δp` from XPBD update rule
3. Writes both particles back (two SSBO writes — safe within colour)

This is ~6 memory ops per constraint, dominated by memory bandwidth, not ALU. The RX 6600 has 224 GB/s memory bandwidth; for a 30k-constraint cloth the per-substep pass is ~720 KB → ~3 µs theoretical. Real cost ~20–40 µs accounting for cache effects and dispatch overhead.

### Sources

- Macklin, Müller, Chentanez. "XPBD: Position-Based Simulation of Compliant Constrained Dynamics" (2016). https://matthias-research.github.io/pages/publications/XPBD.pdf
- NVIDIA Flex documentation: https://developer.nvidia.com/flex
- NVIDIA NvCloth (open source): https://github.com/NVIDIAGameWorks/NvCloth — particularly the SwSolver vs GpuSolver split
- Macklin et al. "Position Based Simulation of Compliant Constrained Dynamics" (the 2014 PBD paper)
- AMD GPUOpen: "Compute shader bandwidth notes" — re red-black pattern bandwidth
- Bullet Physics 3 GPU cloth — `src/btSoftBodyGPU/`

## 4. Architecture

### File layout

| File | Purpose |
|---|---|
| `engine/physics/gpu_cloth_simulator.h/.cpp` | New. GPU-resident cloth solver mirroring `ClothSimulator`'s public API. |
| `engine/physics/cloth_solver_backend.h` | New. Pure-virtual `IClothSolverBackend` so `ClothSimulator` and `GpuClothSimulator` are interchangeable behind a single interface. |
| `engine/physics/cloth_simulator.{h,cpp}` | Lightly modified — implements `IClothSolverBackend`. No behavioural change. |
| `engine/physics/cloth_component.{h,cpp}` | Owns a `std::unique_ptr<IClothSolverBackend>` chosen at `initialize()` time. |
| `assets/shaders/cloth_wind.comp.glsl` | New. Force accumulation: gravity + wind + per-particle noise. |
| `assets/shaders/cloth_integrate.comp.glsl` | New. Symplectic Euler: `prevPos = pos; pos += vel·dt; vel·= (1-damping)`. |
| `assets/shaders/cloth_constraints.comp.glsl` | New. Parameterised by colour ID; one dispatch per colour per sweep. Handles distance / shear / bend uniformly. |
| `assets/shaders/cloth_dihedral.comp.glsl` | New. Per-quad-pair bending. Uses 4-colour partitioning. |
| `assets/shaders/cloth_collision.comp.glsl` | New. Iterates collider arrays (sphere/plane/cylinder/box) from a UBO; ground plane is a fast-path branch. |
| `assets/shaders/cloth_normals.comp.glsl` | New. Per-vertex normal accumulation from triangle normals. |
| `tests/test_gpu_cloth_simulator.cpp` | New. Headless GL context tests via existing GoogleTest fixture. |

### `IClothSolverBackend` interface

A thin abstraction that `ClothComponent` holds. Both implementations satisfy it.

```cpp
class IClothSolverBackend {
public:
    virtual ~IClothSolverBackend() = default;
    virtual void initialize(const ClothConfig& cfg, uint32_t seed) = 0;
    virtual void simulate(float dt) = 0;
    virtual const glm::vec3* getPositions() const = 0;  // CPU-readable, may copy from GPU
    virtual const glm::vec3* getNormals() const = 0;
    virtual uint32_t getParticleCount() const = 0;
    virtual bool pinParticle(uint32_t i, const glm::vec3& worldPos) = 0;
    // ... mirrors the existing ClothSimulator surface
};
```

The interface is declared in a new header so we keep `cloth_simulator.h` independent of any GPU types.

### Buffer layout (GPU-side SSBOs)

| Binding | Buffer | Layout | Size for 100×100 grid |
|---|---|---|---|
| 0 | `Positions` | `vec4` per particle (xyz + w=invMass) | 160 KB |
| 1 | `PreviousPositions` | `vec4` per particle | 160 KB |
| 2 | `Velocities` | `vec4` per particle (xyz + w=padding) | 160 KB |
| 3 | `Constraints[colour]` | one buffer per colour: `uvec4` (i0, i1, restLen-as-uint, complianceLog-as-uint) | ~120 KB / colour |
| 4 | `DihedralConstraints` | `uvec4` (p0,p1,p2,p3) + `vec4` (restAngle, compliance) | proportional to quads |
| 5 | `LraConstraints` | `uvec2` (free, pin) + `float maxDist` | proportional to free particles |
| 6 | `Normals` | `vec4` per particle | 160 KB |
| 7 | `Indices` | `uint32_t` triangles | 240 KB |
| 8 | `Colliders` (UBO, not SSBO) | tagged union of sphere/plane/cylinder/box | ≤ 16 KB (capped) |

Total VRAM per 100×100 cloth: ≈1.0 MB. Negligible against the 8 GB on an RX 6600.

`vec4` instead of `vec3` for positions is non-negotiable — GLSL std430 still pads `vec3` arrays unsafely on some drivers (Mesa included; bitten in the GPU particle pipeline already). The `w` channel doubles as inverse mass storage, removing a parallel `invMass` SSBO.

### Workgroup sizing

- **Particle passes** (force, integrate, normals): `local_size_x = 64`. One thread per particle. 64 is the AMD wavefront size — keeps occupancy maximal on RDNA2.
- **Constraint passes**: `local_size_x = 64`. One thread per constraint within a colour. Indirect-dispatched if the constraint count ever varies (it doesn't post-init, but indirect sets us up for runtime tear / re-mesh later).
- **Dihedral pass**: `local_size_x = 32`. Smaller per-thread register footprint. 4-colour partitioning: 4 dispatches.

### Auto CPU↔GPU selection

Mirror `GpuParticleSystem`'s pattern:

```cpp
// In ClothComponent::initialize:
const uint32_t particleCount = cfg.width * cfg.height;
const bool gpuEligible = particleCount >= GPU_CLOTH_THRESHOLD       // 1024 particles
                       && GpuClothSimulator::isSupported()           // GL 4.5 + compute
                       && !cfg.forceCpu;                             // user override
m_solver = gpuEligible
    ? std::unique_ptr<IClothSolverBackend>(new GpuClothSimulator())
    : std::unique_ptr<IClothSolverBackend>(new ClothSimulator());
```

Threshold of 1024 particles ≈ 32×32 grid. Below this the dispatch overhead and CPU↔GPU sync of `getPositions()` cost more than the CPU solver saves.

`GpuClothSimulator::isSupported()` checks for `GL_ARB_compute_shader`, `GL_ARB_shader_storage_buffer_object`, and runs a one-time micro-bench (1 warmup + 5 measured dispatches of a noop compute shader; if median > 1ms, we assume the driver is unhealthy and fall back to CPU).

### `getPositions()` — the awkward CPU readback

The CPU caller (renderer, debug viz, save/serialize) needs particle positions as `glm::vec3*`. On GPU these live in VRAM. We have three options; the design picks **(c)**:

(a) Read back every frame into a CPU mirror buffer — simple, but `glGetBufferSubData` is a sync point that stalls the pipeline (~0.5 ms per cloth on Mesa AMD).

(b) Persistent-mapped buffer with a triple-buffering ring — fastest, but requires GL 4.4 `glBufferStorage` + `GL_MAP_PERSISTENT_BIT` + manual fencing. Complex.

(c) **Lazy readback with dirty flag** (chosen). Render path doesn't need `getPositions()` at all — it draws directly from the SSBO via a `cloth.vert.glsl` that pulls vertex 0..N-1 out of the position buffer. CPU readback only happens when something explicitly asks (`cloth.getPositions()` from gameplay code, save, or debug viz). Each readback inserts a fence and stalls — but that cost is only paid when needed. **Tradeoff:** debug visualisations that hit the simulator every frame (e.g. drawing constraint edges) become expensive. Mitigation: `gpu_cloth_simulator.cpp` exposes a `bool m_positionsDirty` and skips the `glGetBufferSubData` if positions haven't changed since last call (i.e. if the simulator wasn't ticked).

### Rendering — direct SSBO read in vertex shader

New `cloth_gpu.vert.glsl`:

```glsl
#version 450 core
layout(std430, binding = 0) readonly buffer Positions { vec4 positions[]; };
layout(std430, binding = 6) readonly buffer Normals   { vec4 normals[]; };
// indices come via a regular IBO (drawElements)

void main() {
    uint vid = gl_VertexID;
    vec3 p = positions[vid].xyz;
    vec3 n = normals[vid].xyz;
    // ... transform + lighting as before
}
```

This avoids any per-frame CPU↔GPU vertex upload. Total CPU work per frame becomes: dispatch + `glDrawElements`.

### Self-collision on GPU — explicitly out of scope for this phase

Spatial-hash construction on GPU (see "Position-based fluids" Macklin & Müller 2013) is achievable but doubles the implementation surface. The CPU path keeps self-collision; the GPU path disables it (`cfg.forceCpu = true` if `cfg.selfCollision == true`, with a one-time warning logged). Phase 9B follow-up captures GPU self-collision separately.

## 5. Migration sequence

Each step is a separate PR-sized commit. After each, the engine runs (CPU path is untouched) and tests pass.

| # | Step | Verifiable outcome |
|---|---|---|
| 1 | Introduce `IClothSolverBackend`. Make `ClothSimulator` implement it. `ClothComponent` owns `unique_ptr<IClothSolverBackend>`. **No GPU code yet.** | All existing cloth tests pass unchanged. |
| 2 | Add `GpuClothSimulator` skeleton — initialize/teardown of all SSBOs, no compute shaders yet. `simulate()` is a no-op (positions don't move). | New tests: SSBO sizes, GL error checks, isSupported() probe. |
| 3 | Land `cloth_integrate.comp.glsl` + `cloth_wind.comp.glsl`. Particles fall under gravity. No constraints yet. | Visual smoke test: free-fall cloth lands on ground plane. |
| 4 | Land `cloth_constraints.comp.glsl` + 4-colour partitioning of structural/shear constraints. CPU-side colour assignment via greedy graph colouring (one-shot at `initialize`, not per frame). | Cloth holds shape, doesn't stretch infinitely. |
| 5 | Land bend constraints (extends step 4 with another colour set). | Cloth resists folding, behaviour matches CPU within ~5% Hausdorff distance. |
| 6 | Land `cloth_dihedral.comp.glsl`. | Dihedral bending tests parity with CPU. |
| 7 | Land `cloth_collision.comp.glsl`. Sphere + plane + ground first; cylinder + box + mesh in a follow-up commit. | Sphere collision visual smoke test matches CPU baseline. |
| 8 | Land `cloth_normals.comp.glsl`. Switch render path to read positions/normals directly from SSBOs. | Cloth renders without per-frame CPU→GPU vertex upload. |
| 9 | Land LRA constraints on GPU. | Pinned cloths don't drift under heavy wind. |
| 10 | Auto CPU↔GPU selection in `ClothComponent::initialize`. Threshold tuned via micro-bench on the dev hardware. | Default-config small cloths still use CPU; large cloths transparently use GPU. |
| 11 | Final sweep: integrate Tier-1 audit (GPU SSBO leak detection rule), update CHANGELOG, ROADMAP tick, write `tests/test_gpu_cloth_simulator.cpp` covering the public surface. | Clean audit, documented switch, regression tests. |

Each step is independently revertible.

## 6. Testing strategy

### Determinism

CPU XPBD with a fixed seed produces bit-identical output across runs. GPU XPBD with the same seed will *not* be bit-identical to CPU because (a) constraint solve order differs (red-black vs depth-sorted Gauss-Seidel) and (b) GPU floating-point semantics differ (FMA, vendor-specific). Tests use a Hausdorff-distance threshold (≤5% of cloth diagonal) for positional parity, not exact equality.

### Test fixtures

- **Headless GL context.** Reuse the existing test pattern — tests run under `xvfb-run` in CI, GLFW + invisible window already used by `test_gpu_particle_system.cpp`.
- **Snapshot-based tests.** Run CPU sim 60 frames → snapshot positions. Run GPU sim 60 frames → compare. Pass if Hausdorff ≤ threshold.
- **Force-CPU override.** Tests that don't require GPU run with `cfg.forceCpu = true` so they can run on CI runners that lack GL contexts (the Tier-1 audit job in particular).

### CI considerations

The existing `linux-build-test` job runs under `xvfb-run`, which gives a software-rasteriser GL context. Compute-shader tests work but are slow (Mesa llvmpipe takes ~50× the wall-clock of real hardware). New GPU cloth tests will be tagged `[gpu_required]` and skipped in CI unless a real driver is detected. Local dev runs them; CI does not. This matches the policy used by `test_gpu_particle_system.cpp`.

## 7. Performance acceptance criteria

The implementation lands when:

- 100×100 cloth (10k particles) sustains ≥120 FPS on the dev hardware (RX 6600) with default config (10 substeps).
- 200×200 cloth (40k particles) sustains ≥60 FPS.
- 32×32 cloth performs no worse than the CPU baseline (the auto-select threshold may shift up to 64×64 = 4k particles if 32×32 turns out to be GPU-disadvantaged in practice).
- Multi-cloth scene: 10 × 50×50 cloths sustains ≥60 FPS.

Numbers below these thresholds block the merge; they are not "nice to have."

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Mesa AMD driver bugs in compute (we have a documented sampler-binding issue already) | Probe in `isSupported()`. CPU fallback is always available. New driver issues found during dev get added to the probe. |
| Red-black colouring fails for irregular topology (post-tear cloth) | Phase 9B explicitly excludes torn cloth. Tearing remains CPU-only until a follow-up phase. |
| `getPositions()` readback stalls breaking debug overlays | Debug overlays move to using a dedicated debug shader that reads SSBOs directly (no readback). |
| Compute-shader compile failures on user hardware (older AMD/Intel) | `isSupported()` returns false; cloth quietly uses CPU path. Logged to console once per app launch. |
| API drift if `ClothSimulator` evolves while the GPU path is being built | The `IClothSolverBackend` interface is the contract. Any new public method on `ClothSimulator` requires the GPU path to also implement it (compile-time enforced). |

## 9. What this phase explicitly does NOT do

- GPU self-collision (deferred — needs spatial-hash on GPU)
- GPU mesh-collider (deferred — needs BVH on GPU; simple primitives only in this phase)
- Tearing on GPU (deferred — irregular topology)
- Multi-GPU dispatch (out of scope, single-GPU for the foreseeable future)
- Vulkan compute (this phase ships GL 4.5 compute; Vulkan migration is a Phase 11+ initiative covering the whole renderer)

Each of these gets its own roadmap entry once Phase 9B GPU cloth lands.

## 10. CHANGELOG / ROADMAP wiring

When this lands:

- `CHANGELOG.md` under the next minor version: a "Cloth GPU pipeline" section listing the new files, the auto-select threshold, and the perf numbers measured on the dev hardware.
- `ROADMAP.md` Phase 9B: tick the GPU Compute Cloth Pipeline checkbox; add deferred items (GPU self-collision, GPU mesh-collider, GPU tear) under a new "GPU cloth follow-ups" sub-list so they're not forgotten.
- `tools/audit/audit_config.yaml`: add a new tier-2 pattern that flags `glm::vec3` (not `vec4`) in cloth SSBO struct definitions, so the std430-padding pitfall stays caught at audit time.
