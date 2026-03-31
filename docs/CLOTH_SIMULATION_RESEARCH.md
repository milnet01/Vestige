# Cloth Simulation Research

**Date:** 2026-03-31
**Purpose:** Research cloth simulation techniques for real-time game engines targeting 60 FPS, with focus on curtains, drapes, banners, and flags for architectural walkthroughs.

---

## Table of Contents

1. [Mass-Spring Systems](#1-mass-spring-systems)
2. [Position-Based Dynamics (PBD/XPBD)](#2-position-based-dynamics-pbdxpbd)
3. [GPU vs CPU Cloth Simulation](#3-gpu-vs-cpu-cloth-simulation)
4. [Wind Interaction](#4-wind-interaction)
5. [Collision with Rigid Bodies](#5-collision-with-rigid-bodies)
6. [Pin Constraints](#6-pin-constraints)
7. [Self-Collision](#7-self-collision)
8. [Integration with Physics Engines](#8-integration-with-physics-engines)
9. [Real-World AAA Examples](#9-real-world-aaa-examples)
10. [Recommended Approach for Vestige](#10-recommended-approach-for-vestige)

---

## 1. Mass-Spring Systems

### How They Work

A mass-spring system models cloth as a grid of **point masses** (particles) connected by **springs**. Each particle has position, velocity, and mass. Springs exert forces based on Hooke's law, pulling particles back toward their rest-length separation.

Three types of springs are used:

| Spring Type  | Connects                          | Purpose                    |
|-------------|-----------------------------------|----------------------------|
| **Structural** | Horizontally/vertically adjacent particles | Maintains grid shape (cross-grain) |
| **Shear**      | Diagonally adjacent particles             | Resists shearing deformation (bias grain) |
| **Bend**       | Particles two units apart (skip one)      | Resists bending/folding (drape stiffness) |

**Simulation loop:**
1. Accumulate forces: gravity + wind + spring forces (Hooke's law + damping)
2. Integrate: update velocity and position using a numerical integrator
3. Apply constraints: enforce pin constraints, collision responses

### Pros

- **Conceptually simple** -- easy to understand and implement from scratch
- **Intuitive parameters** -- spring stiffness (k) and damping (d) map to physical intuition
- **Well-documented** -- decades of tutorials, papers, and open-source implementations
- **Flexible** -- easy to add/remove springs, tear cloth, vary material properties per-spring

### Cons

- **Stability problems with explicit integration** -- stiff springs require very small timesteps with explicit Euler, or the simulation explodes. The timestep must satisfy dt < sqrt(2m/k), which for realistic stiffness values forces impractically small steps.
- **Implicit integration is complex** -- solving the stiff system stably requires implicit Euler (Baraff & Witkin 1998), which involves assembling and solving a sparse linear system each frame. This is significantly more complex to implement.
- **Stiffness-timestep coupling** -- to create high-resolution cloth with realistic stiffness, simple explicit solvers cannot maintain interactive frame rates unless the timestep is made too small.
- **Superelasticity** -- springs can over-stretch before correction kicks in, causing visible "rubber band" artifacts.
- **Not popular in modern research** -- mass-spring systems have largely been superseded by PBD/XPBD and FEM in both research and production.

### Integration Methods (Ordered by Complexity)

1. **Explicit Euler** -- simplest, least stable, requires tiny timesteps
2. **Symplectic (Semi-Implicit) Euler** -- updates velocity first, then position. Much more stable than explicit Euler with minimal extra cost
3. **Verlet Integration** -- stores current and previous position (no explicit velocity). Very popular for cloth: `x_new = x + (x - x_prev) * (1 - drag) + a * dt^2`. Naturally stable, easy to implement constraints
4. **RK4 (Runge-Kutta 4th order)** -- much more stable than Euler, but 4x the force evaluations per step
5. **Implicit Euler (Baraff & Witkin)** -- unconditionally stable, allows large timesteps, but requires solving a linear system each frame

**Sources:**
- [Trinity College Dublin -- Mass-Spring Systems Lecture](https://www.scss.tcd.ie/michael.manzke/CS7057/cs7057-1516-14-MassSpringSystems-mm.pdf)
- [Stanford -- Cloth Simulation Overview](https://graphics.stanford.edu/~mdfisher/cloth.html)
- [UC Santa Cruz -- Cloth Simulation with Mass-Spring](https://creativecoding.soe.ucsc.edu/courses/cs488/finalprojects/cloth/cloth.pdf)
- [UCI CS114 -- Cloth Simulation Project](https://ics.uci.edu/~shz/courses/cs114/docs/proj3/index.html)
- [ResearchGate -- Theories for Mass-Spring Simulation: Stability, Costs and Improvements](https://www.researchgate.net/publication/31466567_Theories_for_Mass-Spring_Simulation_in_Computer_Graphics_Stability_Costs_and_Improvements)

---

## 2. Position-Based Dynamics (PBD/XPBD)

### How PBD Works

Instead of computing forces and integrating accelerations (like mass-spring), PBD works directly on **positions**. The algorithm:

1. **Predict positions** using symplectic Euler: `x_pred = x + v * dt + (F_ext / m) * dt^2`
2. **Project constraints** iteratively: for each constraint (distance, bending, collision), directly move particles to satisfy the constraint
3. **Update velocity** from position change: `v = (x_new - x_old) / dt`

Key constraint types for cloth:
- **Distance constraints** -- maintain edge lengths (replaces structural/shear springs)
- **Bending constraints** -- limit the dihedral angle between adjacent triangles (replaces bend springs)
- **Collision constraints** -- push particles out of colliding geometry

### How XPBD Differs from PBD

**PBD's problem:** Stiffness depends on iteration count and timestep. More iterations = stiffer material. Fewer iterations = rubber-like behavior. This makes tuning extremely difficult -- artists cannot set a "stiffness" value independently.

**XPBD (Extended PBD)** by Macklin, Muller, and Chentanez (2016) solves this by introducing a **compliance parameter** (alpha) with physical units (inverse stiffness). The key addition is tracking a **Lagrange multiplier** per constraint across iterations:

```
alpha_tilde = alpha / dt^2
delta_lambda = (-C(x) - alpha_tilde * lambda) / (sum(w_j * |grad_C_j|^2) + alpha_tilde)
delta_x = w * grad_C * delta_lambda
```

Where:
- `C(x)` is the constraint function (e.g., current distance - rest distance)
- `alpha` is compliance (0 = infinitely stiff, higher = softer)
- `lambda` accumulates constraint impulses across iterations
- `w = 1/m` is the inverse mass

**XPBD advantages over PBD:**
- Stiffness is **independent** of iteration count and timestep
- Material parameters are **physically meaningful** (compliance in m/N)
- Greatly simplifies asset creation -- artists set stiffness once
- Same unconditional stability as PBD

### Matthias Muller's Ten Minute Physics

Muller (co-author of both the original PBD and XPBD papers) created the [Ten Minute Physics](https://matthias-research.github.io/pages/tenMinutePhysics/index.html) tutorial series. Key episodes:

- **Episode 09 -- XPBD:** Explains the XPBD algorithm with interactive JavaScript demos
- **Episode 14 -- Cloth:** Demonstrates cloth simulation using zero-compliance distance constraints on mesh edges with XPBD. Achieves 6400 triangles at 30+ FPS on a smartphone

The tutorials include PDF slides, source code, and browser-based demos -- an excellent learning resource.

### Bending Constraints

Two main approaches:

1. **Dihedral angle constraint** -- targets the angle between normals of adjacent triangles. Physically correct but expensive: gradient derivation is complex, and `acos()` calls hurt performance and stability.

2. **Isometric bending constraint** (Bergou et al.) -- uses a quadratic energy based on the discrete Laplacian. More numerically stable and cheaper to compute. **Recommended for real-time cloth.**

Muller proposes an even simpler alternative: using distance constraints between vertices opposite a shared edge, which approximates bending resistance with minimal implementation complexity.

### Vertex Block Descent (VBD) -- 2024 State of the Art

A new method published at SIGGRAPH 2024. Uses block coordinate descent with vertex-level Gauss-Seidel iterations on the variational form of implicit Euler. Achieves:
- **Unconditional stability** like PBD
- **Better convergence** than PBD at high vertex counts
- **Fits computation budgets** by limiting iteration count while maintaining stability

Performance comparison: negligible difference from PBD at low vertex counts, but consistently outperforms PBD as node count increases (tested up to 128x128 grids).

**Sources:**
- [Muller et al. -- Position Based Dynamics (2007)](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf)
- [Macklin, Muller, Chentanez -- XPBD (2016)](https://matthias-research.github.io/pages/publications/XPBD.pdf)
- [Ten Minute Physics -- Episode 14: Cloth](https://matthias-research.github.io/pages/tenMinutePhysics/14-cloth.pdf)
- [Ten Minute Physics -- Episode 09: XPBD](https://matthias-research.github.io/pages/tenMinutePhysics/09-xpbd.pdf)
- [Carmen's Graphics Blog -- Distance Constraint of XPBD](https://carmencincotti.com/2022-08-22/the-distance-constraint-of-xpbd/)
- [Carmen's Graphics Blog -- Isometric Bending Constraint](https://carmencincotti.com/2022-08-29/the-isometric-bending-constraint-of-xpbd/)
- [Carmen's Graphics Blog -- Most Performant Bending Constraint](https://carmencincotti.com/2022-09-05/the-most-performant-bending-constraint-of-xpbd/)
- [InteractiveComputerGraphics/PositionBasedDynamics (GitHub)](https://github.com/InteractiveComputerGraphics/PositionBasedDynamics)
- [Vertex Block Descent -- SIGGRAPH 2024](https://graphics.cs.utah.edu/research/projects/vbd/vbd-siggraph2024.pdf)
- [VBD vs PBD Performance Comparison (2024)](https://www.mdpi.com/2076-3417/14/23/11072)
- [Mass-spring vs PBD Discussion](https://github.com/InteractiveComputerGraphics/PositionBasedDynamics/issues/98)

---

## 3. GPU vs CPU Cloth Simulation

### When to Use CPU

- **Low particle counts** (< ~1000 particles) -- GPU dispatch overhead negates parallelism benefits
- **Complex branching logic** -- CPU handles irregular control flow better
- **Tight integration with CPU-side game logic** -- avoids GPU readback latency
- **Prototyping** -- simpler to debug and iterate

### When to Use GPU (Compute Shaders)

- **High particle counts** (> ~1000 particles) -- massive parallelism shines
- **Multiple cloth objects** -- batch many cloths in a single dispatch
- **Already rendering on GPU** -- avoids CPU-to-GPU data transfer for rendering

### Performance Numbers

Research consistently shows dramatic GPU speedups:
- **61x faster** than single-core CPU for mass-spring particle updates
- **124x faster** for constraint enforcement specifically
- **WebGPU maintains 60 FPS with 640K nodes**, while WebGL (CPU-fallback equivalent) struggles beyond 10K nodes
- Ubisoft (Assassin's Creed Unity) achieved **>10x speedup** porting cloth from CPU to compute shaders

### OpenGL 4.5 Compute Shader Cloth Architecture

OpenGL 4.5 provides everything needed for GPU cloth:

**Key OpenGL features used:**
- `GL_COMPUTE_SHADER` -- general-purpose GPU computation
- `GL_SHADER_STORAGE_BUFFER` (SSBO) -- read/write structured data (positions, velocities, constraints)
- `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` -- synchronize between dispatch calls
- `glDispatchCompute(groups_x, 1, 1)` -- launch compute work

**Ping-pong buffer pattern:**
Two sets of SSBOs alternate as source/destination each substep:
```
Frame N:  Read from SSBO_A, write to SSBO_B
Frame N+1: Read from SSBO_B, write to SSBO_A
```
A simple integer flag (0 or 1) toggles which buffer set is source vs. destination.

**Typical dispatch sequence per frame:**
1. `ApplyExternalForces.glsl` -- gravity, wind (1 dispatch)
2. `PredictPositions.glsl` -- symplectic Euler prediction (1 dispatch)
3. `SolveConstraints.glsl` -- XPBD distance + bending constraints (N dispatches for N iterations)
4. `UpdateVelocity.glsl` -- derive velocity from position change (1 dispatch)
5. `HandleCollisions.glsl` -- push particles out of colliders (1 dispatch)

**Memory barrier between each dispatch** to ensure writes are visible.

**Jacobi vs. Gauss-Seidel on GPU:**
CPU implementations often use Gauss-Seidel iteration (sequential, fast convergence). GPUs must use **Jacobi iteration** (parallel, all particles update simultaneously) because particles are processed in parallel. Jacobi converges slower but compensates with more iterations per frame (GPU makes this cheap). Position corrections are accumulated into a `delta` buffer and averaged before application to avoid oscillation.

**Graph coloring for parallel constraint solving:**
Constraints that share particles cannot be solved in parallel without conflicts. Solution: pre-compute a **graph coloring** of constraints. Each color group can be dispatched as a single parallel batch with no data races. Typically 4-8 colors suffice for cloth meshes.

**Sources:**
- [GPU cloth with OpenGL Compute Shaders (GitHub)](https://github.com/likangning93/GPU_cloth)
- [OpenGL Build High Performance Graphics -- Compute Shader Cloth](https://www.oreilly.com/library/view/opengl-build/9781788296724/ch18s03.html)
- [Mass-Spring Cloth with OpenGL Compute Shader (GitHub)](https://github.com/MircoWerner/Mass-Spring-Model_Cloth-Simulation)
- [Parallel Cloth Simulation Using OpenGL Shading Language](https://www.techscience.com/csse/v41n2/45194/html)
- [GDC 2015 -- Ubisoft Cloth Simulation: C++ to Compute Shaders](https://www.gdcvault.com/play/1022421/Ubisoft-Cloth-Simulation-Performance-Postmortem)
- [SSBO Tutorial](https://ktstephano.github.io/rendering/opengl/ssbos)
- [Velvet -- CUDA XPBD Cloth Engine (GitHub)](https://github.com/vitalight/Velvet)

---

## 4. Wind Interaction

### Per-Triangle Aerodynamic Model

The standard real-time approach computes wind force **per triangle** of the cloth mesh:

```
For each triangle (p0, p1, p2):
    normal = normalize(cross(p1 - p0, p2 - p0))
    area = 0.5 * length(cross(p1 - p0, p2 - p0))
    v_rel = wind_velocity - triangle_avg_velocity
    v_normal = dot(v_rel, normal) * normal      // normal component

    // Drag force (opposes motion through air)
    F_drag = -C_drag * area * v_normal

    // Distribute 1/3 of force to each triangle vertex
    p0.force += F_drag / 3
    p1.force += F_drag / 3
    p2.force += F_drag / 3
```

**Key insight:** the force is proportional to:
1. The **projected area** of the triangle facing the wind (dot product of normal with wind direction)
2. The **relative velocity** between wind and cloth surface
3. A **drag coefficient** (tunable parameter)

When the cloth faces the wind head-on, it receives maximum force. When parallel to the wind, it receives almost none. This naturally produces realistic billowing.

### Lift Component

Research shows that the **lift component** (perpendicular to wind direction, like an airplane wing) plays an important role in realistic cloth animation and should not be neglected. A simplified model:

```
v_tangent = v_rel - v_normal    // tangential component
F_lift = C_lift * area * cross(v_rel, cross(normal, v_rel))
```

However, for curtains and drapes, the simpler drag-only model is often sufficient.

### Wind Variation for Realism

Static wind looks artificial. Add variation with:

1. **Perlin noise** -- spatial and temporal variation across the cloth surface
2. **Sinusoidal gusts** -- `wind * (1.0 + gust_strength * sin(time * gust_freq))`
3. **Turbulence** -- random perturbation added to the base wind vector
4. **Directional shifts** -- slowly rotate wind direction over time

A practical formula: `wind_at_point = base_wind * (1.0 + noise3D(position * scale, time)) + gust_vector * pulse(time)`

### Application to Curtains

For indoor curtains:
- Base wind should be **very low** (gentle drift from open windows/doors)
- Use **slow Perlin noise** for subtle movement even without wind
- Add **event-driven gusts** (player opens a door/window -> brief wind pulse)
- Consider **gravity sag** as the dominant force -- curtains mostly hang

**Sources:**
- [GameDev.net -- Adding Wind to Cloth Simulation](https://www.gamedev.net/forums/topic/316776-adding-wind-to-cloth-simulation/3030425/)
- [steven.codes -- Cloth Simulation Blog Post](https://steven.codes/blog/cloth-simulation/)
- [ResearchGate -- Modelling Effects of Wind Fields in Cloth Animation](https://www.researchgate.net/publication/221546464_Modelling_Effects_of_Wind_Fields_in_Cloth_Animation)
- [Flax Engine -- Cloth Documentation](https://docs.flaxengine.com/manual/physics/cloth.html)

---

## 5. Collision with Rigid Bodies

### Sphere and Capsule Proxies

The standard approach for real-time cloth-rigid collision uses **simplified collision shapes** rather than full mesh-mesh intersection:

**Sphere collision:**
```
For each particle:
    d = particle.position - sphere.center
    dist = length(d)
    if dist < sphere.radius:
        // Push particle to sphere surface
        particle.position = sphere.center + normalize(d) * sphere.radius
        // Optional: remove velocity component toward sphere
        v_n = dot(particle.velocity, normalize(d))
        if v_n < 0:
            particle.velocity -= v_n * normalize(d)
```

**Capsule collision (line segment + radius):**
A capsule is defined by two endpoints and a radius. Find the closest point on the line segment to the particle, then treat it like a sphere collision from that point.

**Why proxies?**
- O(n) per particle instead of O(n*m) for mesh-mesh
- Trivially parallel on GPU
- Good enough for most scenarios (human bodies approximated by ~10-15 capsules)
- NvCloth supports up to 32 spheres and 32 planes per cloth, with each capsule using 2 spheres

### Signed Distance Fields (SDFs)

For complex static geometry (furniture, architecture), **precomputed SDFs** provide efficient collision:

1. **Precompute:** Voxelize the rigid body into a 3D texture storing signed distance to the nearest surface (negative = inside, positive = outside)
2. **Runtime:** For each cloth particle, sample the SDF texture. If distance < 0, push the particle along the gradient direction by |distance|

**Advantages:**
- Constant-time collision query per particle (single texture lookup)
- Handles arbitrarily complex geometry
- Gradient gives collision normal for free
- GPU-friendly (texture sampling is fast)

**Disadvantages:**
- Memory cost for the 3D texture (64^3 = ~260KB per object at float32)
- Only works for static or slowly-deforming rigid bodies (SDF must be regenerated for deforming objects)
- Resolution limits accuracy near thin features

**For Vestige:** SDFs are ideal for architectural elements (walls, pillars, furniture) that curtains/drapes might contact. These are static and can have SDFs baked at load time.

**Sources:**
- [NVIDIA NvCloth -- Sphere Capsule Collision](https://gameworksdocs.nvidia.com/NvCloth/1.1/CollisionDetection/SphereCapsuleCollision.html)
- [Wicked Engine -- Capsule Collision Detection](https://wickedengine.net/2020/04/capsule-collision-detection/)
- [Cocos -- Building Collision Detection Using SDF](https://www.cocos.com/en/post/building-collision-detection-using-signed-distance-field)
- [ACM -- Local Optimization for Robust SDF Collision](https://dl.acm.org/doi/10.1145/3384538)
- [NVIDIA GPU Gems 3 -- SDF Using Single-Pass GPU Scan](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-34-signed-distance-fields-using-single-pass-gpu)
- [Muller et al. -- Optimized Spatial Hashing for Collision Detection](https://matthias-research.github.io/pages/publications/tetraederCollision.pdf)
- [Flax Engine -- Cloth Colliders](https://docs.flaxengine.com/manual/physics/cloth.html)

---

## 6. Pin Constraints

### Concept

Pin constraints fix specific cloth particles to world-space positions (or attach them to moving objects). This is how you attach a curtain to a rod, a banner to a pole, or a flag to a flagpole.

### Implementation (Trivially Simple in PBD/XPBD)

In PBD/XPBD, pin constraints are the simplest constraint type:

```cpp
// During constraint projection, for each pinned particle:
if (particle.isPinned)
{
    particle.position = particle.pinTarget;  // Fixed point
    // Or for moving attachment:
    particle.position = attachedObject.transform * particle.localOffset;
}
```

Set the particle's **inverse mass to 0** (infinite mass) so that constraint solving never moves it. Other constraints that reference this particle will move the unpinned particles instead.

### Curtain Rod Implementation

For a curtain on a rod:
1. Create cloth mesh as a rectangular grid
2. Pin the **top row** of particles to evenly-spaced points along the rod
3. Optionally space pins to create gathering/pleating:
   - **Every vertex pinned** = flat, taut curtain
   - **Every Nth vertex pinned** = natural pleats/folds between pins (curtain rings)
   - **Pins with slight slack** (pin positions closer together than rest-length spacing) = gathered fabric

### Dynamic Pins

Pins can be:
- **Static** -- fixed world position (rod mounted to wall)
- **Kinematic** -- follow an animated object (curtain being drawn open)
- **Breakable** -- release when force exceeds threshold (torn banner)

For breakable pins, track the constraint force (the position correction magnitude) and release when it exceeds a threshold.

### Practical Tips

- Pin at least 2 particles per attachment point for stability
- Use slightly softened pin constraints (small compliance in XPBD) to avoid jerky motion near attachment points
- For curtain rings, model each ring as a small capsule collider and let gravity drape the cloth over them (more realistic than hard pins)

**Sources:**
- [Autodesk -- Pinning Cloth Vertices to an Object](https://download.autodesk.com/global/docs/softimage2013/en_us/userguide/files/GUID-0AEFEC0E-74CB-4CAB-9808-F417FEFE1AE7.htm)
- [Blender Manual -- Cloth Pin Settings](http://builder.openhmd.net/blender-hmd-viewport-temp/physics/cloth/settings/cloth_settings.html)
- [RenderGuide -- Blender Cloth Simulation Tutorial](https://renderguide.com/blender-cloth-simulation-tutorial/)

---

## 7. Self-Collision

### Is It Needed for Curtains/Drapes?

**Short answer: usually not for simple curtains, but needed for heavy drapes that fold back on themselves.**

| Scenario | Self-Collision Needed? |
|----------|----------------------|
| Flat curtain hanging from rod | No -- gravity keeps it flat |
| Curtain being pulled/gathered | Maybe -- folds may overlap |
| Heavy velvet drapes with deep pleats | Yes -- fabric folds onto itself |
| Flag/banner in wind | Rarely -- tension prevents self-contact |
| Tabernacle curtain partitions | No -- mostly flat hanging panels |

### Performance Cost

Self-collision is **by far the most expensive part** of cloth simulation:

- **Naive approach:** O(n^2) particle-particle checks -- completely impractical
- **Spatial hashing:** O(n) average case. Partition space into grid cells, only check particles in same/adjacent cells. GPU-friendly.
- **Bounding volume hierarchies (BVH):** O(n log n) build, efficient pruning, but harder to parallelize on GPU
- **Research benchmarks:** GPU spatial hashing achieves 6-8x speedup over prior GPU methods (PSCC). Even so, complex self-collision scenes run at 2-8 FPS with hundreds of thousands of triangles -- far below 60 FPS targets.

### Practical Recommendations

1. **Skip self-collision for most curtain/drape scenarios** -- the performance cost is extreme and the visual benefit is minimal for mostly-flat hanging cloth
2. **If needed, use particle-particle collision with spatial hashing** -- simpler than triangle-triangle, sufficient for preventing major penetration
3. **Set a minimum particle separation distance** rather than exact collision -- this is what PBD engines do (treat each particle as a small sphere)
4. **Use thickness** -- give the cloth a small thickness parameter. If two particles from non-adjacent regions are closer than the thickness, push them apart
5. **Limit the collision check region** -- only check particles that are topologically distant but spatially close (skip adjacent particles that are supposed to be close)

### GPU Spatial Hashing for Self-Collision

```
1. Build hash grid: assign each particle to a cell based on position
2. For each particle, check same cell + 26 neighbors
3. For each nearby particle (not topologically adjacent):
   if distance < thickness:
       push apart along separation vector
```

The Velvet XPBD engine uses this approach with atomic operations for thread-safe updates.

**Sources:**
- [PSCC -- Parallel Self-Collision Culling with Spatial Hashing on GPUs](https://min-tang.github.io/home/PSCC/)
- [I-Cloth -- Incremental Collision Handling for GPU Cloth](https://min-tang.github.io/home/ICloth/)
- [ResearchGate -- Dynamic Cloth Simulation with Fast Self-Collision Detection](https://www.researchgate.net/publication/274512460_Dynamic_Cloth_Simulation_with_Fast_Self-Collision_Detection)
- [Stanford -- Robust Treatment of Collisions, Contact and Friction for Cloth Animation](https://graphics.stanford.edu/papers/cloth-sig02/cloth.pdf)
- [Carmen's Graphics Blog -- Cloth Self Collisions](https://carmencincotti.com/2022-11-21/cloth-self-collisions/)
- [Purdue -- Self-Collision Handling in Cloth Simulation Using GPU](https://hammer.purdue.edu/articles/thesis/An_Experimental_Fast_Approach_of_Self-collision_Handling_in_Cloth_Simulation_Using_GPU/14504817)

---

## 8. Integration with Physics Engines

### Bullet Physics -- btSoftBody

Bullet has had cloth simulation since Bullet 2.69 via `btSoftBody`:

**Capabilities:**
- Mass-spring model with structural, shear, and bending springs
- `btSoftBodyHelpers::CreateFromTriMesh()` creates cloth from triangle mesh
- Two-way interaction between soft bodies and rigid bodies
- Tear, pick, and manipulate cloth interactively
- Configurable solver iterations, collision flags, materials

**Drawbacks:**
- Uses older mass-spring approach (not PBD/XPBD)
- CPU-only -- no GPU acceleration
- Performance is adequate for simple cloth but not ideal for multiple high-res cloths
- The Bullet soft body API is less well-maintained than the rigid body side
- Bullet3 development has slowed significantly

**Example code path:** `bullet3/examples/ExtendedTutorials/SimpleCloth.cpp`

### Jolt Physics -- Soft Body

Jolt (used by Horizon Forbidden West, Death Stranding 2) includes soft body support:

**Capabilities:**
- **Uses XPBD** for constraint solving -- modern approach
- Edge constraints (distance), dihedral bend constraints
- Vertices connected as triangles (faces) for cloth
- Multi-core friendly design
- Actively maintained and developed (2024-2025)
- Godot Jolt integration supports soft bodies as of Godot 4.3

**Advantages over Bullet:**
- Modern XPBD solver (not mass-spring)
- Better multi-threaded performance
- Active development with AAA game usage
- Clean, well-documented C++ API

**Drawbacks:**
- No built-in GPU acceleration (CPU-side XPBD)
- Cloth is a secondary feature, not the primary focus
- Less flexible than a custom solution for engine-specific needs

### Standalone vs. Integrated -- Recommendation

| Factor | Physics Engine Integration | Standalone Implementation |
|--------|---------------------------|--------------------------|
| **Development time** | Lower (API exists) | Higher (build from scratch) |
| **Control** | Limited to engine API | Full control over everything |
| **GPU acceleration** | Not available (Bullet/Jolt are CPU) | You build it with compute shaders |
| **Performance ceiling** | Limited by CPU threading | Much higher with GPU compute |
| **Rigid body interaction** | Built-in | Must implement manually |
| **Learning value** | Lower | Very high |

**For Vestige:** A standalone XPBD implementation on the GPU is recommended. Reasons:
1. Vestige does not currently use Bullet or Jolt, so integrating either just for cloth adds significant dependency
2. GPU compute shader cloth will easily hit 60 FPS for the curtain/drape counts needed
3. The XPBD algorithm is well-documented and not excessively complex to implement
4. Full control allows tailoring to architectural walkthrough needs (static pin points, architectural collision)
5. Educational value aligns with the project's learning goals

**Sources:**
- [Bullet3 -- SimpleCloth.cpp Example](https://github.com/bulletphysics/bullet3/blob/master/examples/ExtendedTutorials/SimpleCloth.cpp)
- [Bullet -- btSoftBody Class Reference](https://pybullet.org/Bullet/BulletFull/classbtSoftBody.html)
- [Jolt Physics -- Soft Body Physics (DeepWiki)](https://deepwiki.com/jrouwe/JoltPhysics/3.2-soft-body-physics)
- [Jolt Physics -- Soft Body System (DeepWiki)](https://deepwiki.com/jrouwe/JoltPhysics/3.3-soft-body-system)
- [Jolt Physics -- Soft Body Support Issue #390](https://github.com/jrouwe/JoltPhysics/issues/390)
- [Godot Jolt -- Soft Body Support](https://80.lv/articles/godot-jolt-now-supports-soft-bodies-in-godot-4-3)
- [NvCloth -- NVIDIA Standalone Cloth Library](https://github.com/NVIDIAGameWorks/NvCloth)
- [NvCloth Documentation](https://docs.nvidia.com/gameworks/content/gameworkslibrary/physx/nvCloth/)

---

## 9. Real-World AAA Examples

### Unreal Engine -- Chaos Cloth

- Replaced NVIDIA NvCloth/APEX in UE5
- Uses a **low-level clothing solver** within the Chaos physics framework
- **Panel Cloth Editor** (UE 5.3+, mature in 5.4+) allows authoring cloth from 2D patterns
- Built-in **low-res sim mesh to high-res render mesh** pipeline -- simulate on coarse mesh, render detailed mesh
- Supports cloth-to-cloth constraints (UE 5.6 beta)
- Scales from mobile to high-end cinematics
- Modular: add a ChaosClothComponent, parent it to SkeletalMesh, select clothing asset

### Unity -- Built-in Cloth Component

- Uses **NVIDIA NvCloth** under the hood (CPU + optional GPU via CUDA/DX11)
- `Cloth` component attaches to `SkinnedMeshRenderer`
- Per-vertex constraint painting (max distance, surface penetration)
- Sphere/capsule colliders for character collision
- Limited to 1 cloth per renderer, no self-collision
- Research shows custom GPU PBD implementations can significantly outperform Unity's built-in cloth

### NVIDIA NvCloth (Standalone Library)

- Open-source library extracted from PhysX
- **Low-level API** -- you manage the simulation stepping
- CPU, CUDA, and DX11 GPU backends
- Fast collision with spheres, capsules, planes (up to 32 of each)
- Used as the cloth backend in Unity, O3DE, and other engines
- Available on GitHub: [NVIDIAGameWorks/NvCloth](https://github.com/NVIDIAGameWorks/NvCloth)

### Ubisoft -- Assassin's Creed Unity / Far Cry 4

The GDC 2015 talk by Alexis Vaisse is one of the most informative real-world cloth postmortems:
- Initially CPU-based cloth was a major performance bottleneck
- **Ported entirely to compute shaders** on Xbox One/PS4
- Achieved **>10x performance improvement**
- Key lesson: cloth simulation is embarrassingly parallel and belongs on the GPU
- Challenges: synchronizing with game loop, handling variable cloth counts per frame

### Naughty Dog, CD Projekt Red, Others

- Many AAA studios use custom cloth solvers tailored to their specific needs
- Common pattern: PBD/XPBD on GPU compute, low-res sim mesh, artist-paintable constraints
- Character cloth typically uses 500-2000 simulation vertices
- Environmental cloth (flags, banners) uses 200-1000 vertices

**Sources:**
- [Epic Games -- Chaos Cloth Tool Overview](https://dev.epicgames.com/community/learning/tutorials/OPM3/unreal-engine-chaos-cloth-tool-overview)
- [Epic Forums -- Chaos Cloth Updates 5.6](https://forums.unrealengine.com/t/tutorial-chaos-cloth-updates-5-6/2555686)
- [Epic -- Panel Cloth Editor Walkthrough (5.4)](https://forums.unrealengine.com/t/tutorial-panel-cloth-editor-walkthrough-updates-5-4/1817155)
- [GDC Vault -- Ubisoft Cloth Simulation: C++ to Compute Shaders](https://www.gdcvault.com/play/1022421/Ubisoft-Cloth-Simulation-Performance-Postmortem)
- [NvCloth GitHub](https://github.com/NVIDIAGameWorks/NvCloth)
- [NVIDIA NvCloth Documentation](https://docs.nvidia.com/gameworks/content/gameworkslibrary/physx/nvcloth.htm)
- [O3DE -- NVIDIA Cloth Gem](https://docs.o3de.org/docs/user-guide/gems/reference/physics/nvidia/nvidia-cloth/)

---

## 10. Recommended Approach for Vestige

### Summary of Requirements

- **Target objects:** Curtains, drapes, banners, tent fabric (Tabernacle/Temple scenes)
- **Performance:** 60 FPS hard minimum
- **Mesh complexity:** ~500-2000 vertices per cloth object, ~5-10 cloth objects per scene
- **Interactions needed:** Wind, gravity, pin constraints, basic collision with walls/pillars
- **Self-collision:** Not needed for initial implementation
- **Graphics API:** OpenGL 4.5 compute shaders
- **Hardware:** AMD RX 6600 (RDNA2) -- excellent compute shader performance

### Recommended Architecture: CPU XPBD (Phase 1) then GPU XPBD (Phase 2)

#### Phase 1 -- CPU XPBD (Ship First)

Start with a CPU-side XPBD implementation. This is the fastest path to working cloth:

1. **ClothMesh class** -- stores particles (position, prev_position, velocity, inv_mass, pinned flag), edges (distance constraints), and triangles (for rendering and wind)
2. **XPBD solver** -- iterative constraint projection with Lagrange multipliers
3. **Constraint types:**
   - Distance constraints on all mesh edges (structural integrity)
   - Distance constraints between opposite vertices of shared edges (bending resistance -- Muller's simple approach)
   - Pin constraints (zero inverse mass particles at attachment points)
4. **External forces:** Gravity + per-triangle wind drag
5. **Collision:** Sphere/capsule/plane proxies (sufficient for walls, pillars, rods)
6. **Rendering:** Update a VBO each frame from particle positions, render as standard mesh with cloth material

**Expected performance:** At 1000 particles with 5 solver iterations per frame, CPU XPBD will comfortably sustain 60 FPS for 5-10 cloth objects. Muller's Ten Minute Physics demo runs 6400 triangles at 30+ FPS on a smartphone -- a desktop CPU will handle this trivially.

**Estimated implementation time:** 2-3 weeks for a functional cloth system with wind and pins.

#### Phase 2 -- GPU Compute Shader XPBD (Optimization)

If more cloth objects or higher resolution is needed:

1. **Migrate particle data to SSBOs** -- positions, velocities, constraints stored in GPU buffers
2. **Compute shader pipeline:**
   - `cloth_predict.comp` -- apply external forces, predict positions
   - `cloth_solve.comp` -- XPBD constraint projection (dispatched N times for N iterations)
   - `cloth_update.comp` -- derive velocity, finalize positions
   - `cloth_collision.comp` -- resolve collisions
3. **Graph coloring** -- precompute constraint colors for parallel-safe solving
4. **Render directly from SSBO** -- vertex shader reads positions from the same SSBO, zero CPU-GPU transfer

**Expected performance:** 60 FPS with tens of thousands of cloth particles, or many cloth objects simultaneously.

#### Implementation Order

```
Step 1: Cloth data structures (ClothMesh, Particle, Constraint)
Step 2: XPBD solver on CPU (distance constraints only)
Step 3: Pin constraints (top row pinned for curtain)
Step 4: Gravity integration -- verify cloth hangs correctly
Step 5: Wind force (per-triangle drag)
Step 6: Bending constraints (prevent paper-thin folding)
Step 7: Sphere/plane collision (push off walls)
Step 8: Editor integration (inspector properties for stiffness, damping, wind)
Step 9: GPU migration (compute shaders) if needed
```

### Key Parameters for Artist Tuning

| Parameter | Description | Typical Range |
|-----------|-------------|---------------|
| `compliance` | Inverse stiffness (0 = rigid, higher = stretchy) | 0.0 - 0.001 |
| `bendCompliance` | Bending softness | 0.0 - 0.1 |
| `damping` | Velocity damping per frame | 0.98 - 0.999 |
| `gravity` | Gravity acceleration | (0, -9.81, 0) |
| `windDirection` | Wind vector | any normalized vec3 |
| `windStrength` | Wind force multiplier | 0.0 - 50.0 |
| `windTurbulence` | Noise-based wind variation | 0.0 - 1.0 |
| `dragCoefficient` | Aerodynamic drag | 0.01 - 0.1 |
| `solverIterations` | XPBD iterations per frame | 3 - 15 |
| `substeps` | Simulation steps per frame | 1 - 5 |

### Learning Resources (Priority Order)

1. **[Ten Minute Physics -- Episode 14: Cloth](https://matthias-research.github.io/pages/tenMinutePhysics/14-cloth.pdf)** -- Start here. Muller's own tutorial with working code
2. **[Ten Minute Physics -- Episode 09: XPBD](https://matthias-research.github.io/pages/tenMinutePhysics/09-xpbd.pdf)** -- Understand the XPBD algorithm
3. **[Carmen's Graphics Blog -- XPBD Series](https://carmencincotti.com/2022-08-22/the-distance-constraint-of-xpbd/)** -- Detailed walkthrough of each constraint type
4. **[Pikuma -- Verlet Integration and Cloth Physics](https://pikuma.com/blog/verlet-integration-2d-cloth-physics-simulation)** -- Excellent visual explanation of fundamentals
5. **[OpenCloth (GitHub)](https://github.com/mmmovania/opencloth)** -- Reference implementations of all major cloth algorithms in OpenGL
6. **[XPBD Paper (Macklin, Muller 2016)](https://matthias-research.github.io/pages/publications/XPBD.pdf)** -- The original paper, readable and well-explained
7. **[GDC 2015 -- Ubisoft Cloth to Compute Shaders](https://www.gdcvault.com/play/1022421/Ubisoft-Cloth-Simulation-Performance-Postmortem)** -- Real-world GPU migration lessons
8. **[Mosegaard's Cloth Tutorial](https://viscomp.alexandra.dk/index2fa7.html?p=147)** -- Classic beginner-friendly tutorial

### File Structure (Proposed)

```
engine/
  physics/
    cloth_mesh.h          // ClothMesh class: particles, constraints, triangles
    cloth_mesh.cpp
    cloth_solver.h        // XPBD solver: constraint projection loop
    cloth_solver.cpp
    cloth_collider.h      // Sphere, capsule, plane collision shapes
    cloth_collider.cpp
  renderer/
    cloth_renderer.h      // VBO management, material, rendering
    cloth_renderer.cpp
  editor/panels/
    cloth_inspector.h     // Inspector UI for cloth parameters
    cloth_inspector.cpp
assets/
  shaders/
    cloth_predict.comp    // (Phase 2) GPU compute shader
    cloth_solve.comp
    cloth_update.comp
    cloth.vert.glsl       // Cloth vertex shader
    cloth.frag.glsl       // Cloth fragment shader (fabric material)
tests/
  test_cloth_mesh.cpp     // Unit tests for cloth data structures
  test_cloth_solver.cpp   // Unit tests for XPBD solver correctness
```

---

## Conclusion

XPBD is the clear winner for real-time cloth simulation in 2026. It provides unconditionally stable simulation with physically meaningful parameters, is simple enough for a solo developer to implement, and has a clear path to GPU acceleration. Mass-spring systems are outdated for this purpose. Physics engine integration (Bullet/Jolt) adds dependency weight without matching the performance of a custom GPU solution.

For Vestige's architectural walkthroughs, a CPU-side XPBD implementation will be more than sufficient for the initial curtain/drape/banner use cases, with GPU compute shaders available as an optimization path if scene complexity grows.
