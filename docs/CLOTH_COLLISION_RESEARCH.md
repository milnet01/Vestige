# Cloth Collision Detection Research

**Date:** 2026-03-31
**Purpose:** Research collision detection techniques for CPU-based XPBD/PBD cloth simulators, covering continuous collision detection, edge/triangle-based collision, collision thickness, self-collision, spatial acceleration, and friction response.
**Context:** Our current system uses per-particle discrete collision against primitive shapes (spheres, planes, cylinders, boxes) with ~50-70 particles per cloth panel and 4-6 collider shapes per panel.

---

## Table of Contents

1. [Continuous Collision Detection (CCD)](#1-continuous-collision-detection-ccd)
2. [Edge-Based and Triangle-Based Collision](#2-edge-based-and-triangle-based-collision)
3. [Collision Thickness and Margins](#3-collision-thickness-and-margins)
4. [Self-Collision](#4-self-collision)
5. [Spatial Acceleration and Broad-Phase Culling](#5-spatial-acceleration-and-broad-phase-culling)
6. [Friction and Collision Response](#6-friction-and-collision-response)
7. [Recommended Approach for Vestige](#7-recommended-approach-for-vestige)

---

## 1. Continuous Collision Detection (CCD)

### The Tunneling Problem

Discrete collision detection checks particle positions only at the end of each timestep. If a particle moves fast enough to pass entirely through a thin collider between frames, the collision is missed -- this is called "tunneling". A classic example: a small fast-moving particle is in front of a wall one frame and behind it the next, with no collision ever detected.

For cloth simulation, tunneling manifests when:
- Wind gusts push particles through collider geometry in a single step
- Cloth drops from a height and particles pass through a floor plane
- Fast character animation drags cloth through body colliders

### How CCD Works

CCD computes the **time of impact** -- the exact moment during a timestep when collision first occurs -- rather than only checking the final position.

For cloth, CCD operates on two primitive types:

**Vertex-Face (VF) test:** Does a moving vertex pass through a moving triangle during the timestep? Given four vertices (1 point + 3 triangle corners) interpolating linearly over the timestep, a collision requires:
1. **Coplanarity test:** Find times when all four points are coplanar. This reduces to finding roots of a **cubic polynomial** in time t (since the positions are linear in t, and the volume determinant is cubic).
2. **Inside test:** At each coplanar time, check if the point lies inside the triangle using barycentric coordinates.

**Edge-Edge (EE) test:** Do two moving edges pass through each other? Given four vertices (2 per edge), similarly:
1. **Coplanarity test:** Same cubic polynomial approach.
2. **Inside test:** Check if the closest points on each (infinite) line lie within the finite edge segments using barycentric coordinates clamped to [0,1].

For a pair of triangles, CCD requires **6 VF tests** (each vertex against the opposite triangle) and **9 EE tests** (each edge against each edge of the other triangle). Each elementary test involves solving a cubic equation, which takes roughly 155 additions, 217 multiplications, and 6 divisions on average (using interval Newton methods).

### Numerical Robustness Issues

The cubic polynomial solver is vulnerable to rounding error and requires ad-hoc tolerances. It is particularly fragile in near-planar configurations (which are common in cloth). Brochu et al. (SIGGRAPH 2012) developed a "geometrically exact" alternative that guarantees the correct Boolean result even in degenerate cases, based on the insight that only the **parity** of collisions matters for robust simulation, which can be computed with simpler non-constructive predicates.

### Practical Alternatives to Full CCD for Game Engines

Full CCD is expensive and complex to implement correctly. Professional game engines use several practical alternatives:

**1. Substeps (most common for PBD/XPBD):**
Divide the timestep into smaller sub-intervals: dt_sub = dt / n_substeps. Run prediction, collision detection, and constraint solving for each substep. With enough substeps, discrete collision detection catches most collisions because particles never move far enough in a single substep to tunnel through geometry.

This is the standard approach in PBD/XPBD cloth. Matthias Muller's Ten Minute Physics tutorials demonstrate 4-8 substeps for stable cloth. NVIDIA PhysX performs collision detection once per substep with shape positions interpolated between frame start and end.

**2. Velocity clamping:**
Cap particle velocity to a maximum threshold so particles cannot move farther than the collision thickness in a single substep:

```
v_max = collisionThickness * substepsPerFrame / dt
```

For example, with 1cm collision thickness, 20 substeps, at 60 FPS: v_max = (0.01 * 20) / (1/60) = 12 m/s. Any particle exceeding this is clamped. This is cheap and catches the vast majority of tunneling cases.

**3. Bridson's three-phase pipeline (used in film/AAA):**
1. **Repulsion forces:** Apply soft spring forces between geometry that is too close (within cloth thickness). Handles 95%+ of collisions cheaply.
2. **CCD with impulses:** For the few remaining high-velocity cases, run CCD and apply velocity impulses to separate geometry.
3. **Impact zones:** For still-unresolved cases, group penetrating vertices into rigid clusters and move them as a unit.

Each phase catches what the previous missed, with the expensive phases running rarely.

**4. NVIDIA PhysX approach:**
Sphere and capsule colliders support CCD natively (the math is simpler for these primitives than for triangle meshes). For triangle mesh colliders, PhysX recommends ensuring mesh tessellation is fine enough that particles cannot slip between triangles. An independent collision stage runs at each solver iteration with interpolated shape positions.

### What This Means for Vestige

Given our particle count (~50-70 per panel) and primitive colliders (spheres, planes, cylinders, boxes), full triangle-mesh CCD is overkill. The recommended approach is:

- **Increase substeps** from current count to 8-12 per frame (still cheap at our particle counts)
- **Clamp particle velocities** to prevent extreme tunneling
- **Use CCD for sphere/capsule primitives** (the math is simple: solve a quadratic for the time a point reaches distance r from a line segment)
- **Rely on collision thickness** (see Section 3) as a safety margin

**Sources:**
- [Brochu et al. -- Efficient Geometrically Exact CCD (SIGGRAPH 2012)](https://www.cs.ubc.ca/~rbridson/docs/brochu-siggraph2012-ccd.pdf)
- [Bridson et al. -- Robust Treatment of Collisions, Contact and Friction for Cloth Animation](https://graphics.stanford.edu/papers/cloth-sig02/cloth.pdf)
- [Bridson -- Cloth Collisions and Contact (slides)](https://slidetodoc.com/cloth-collisions-and-contact-robert-bridson-university-of/)
- [NVIDIA PhysX SDK -- Cloth Documentation](https://documentation.help/NVIDIA.PhysX-SDK/Cloth.html)
- [NVIDIA Flex Manual](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/flex/manual.html)
- [Bullet Physics Forum -- Cloth Collision State of the Art](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=7787)
- [Carmen Cincotti -- Cloth Self Collisions (XPBD)](https://carmencincotti.com/2022-11-21/cloth-self-collisions/)

---

## 2. Edge-Based and Triangle-Based Collision

### The Problem with Particle-Only Collision

When only particle centers are tested against collider geometry, mesh edges and faces can penetrate colliders even when all particles are outside. This happens because:

- The cloth mesh between two particles can drape over a thin edge or corner
- A triangle face can intersect a collider while all three vertices remain clear
- Edges can pass through narrow gaps between collider shapes

The severity depends on mesh resolution. At 50-70 particles per panel, our edge lengths are relatively long (several centimeters), making this a real concern for thin or sharp collider geometry.

### The Two Fundamental Collision Primitives

All cloth-vs-geometry collision reduces to two primitive tests:

**Point-Triangle (vertex-face):** Test whether a point is within distance h (cloth thickness) of a triangle. Steps:
1. Check if the point is close to the **plane** containing the triangle (dot product with triangle normal).
2. Project the point onto the plane.
3. Compute **barycentric coordinates** of the projected point.
4. If all barycentric coordinates are non-negative and sum to <= 1, the point is inside the triangle. If any are negative, the closest feature is an edge or vertex (handled by edge-edge tests).

**Edge-Edge:** Test whether two edges are within distance h. Steps:
1. Compute barycentric coordinates for closest points on the two infinite lines.
2. Clamp to the finite edge segments [0,1].
3. Compute the distance between the clamped closest points.

### Practical Approaches for Game Engines

**Approach 1: Thick Particles (marble model)**
Treat each particle as a sphere with radius r, where r is large enough that adjacent particle spheres overlap, covering the mesh surface. Stanford's cloth simulation page describes this as the "marble model": virtual spheres centered on each vertex, with radius larger than half the edge length, so adjacent marbles overlap. Collisions between connected particles are filtered out. This effectively creates a continuous collision surface.

For our system: with ~50-70 particles and rest lengths of several centimeters, setting particle radius to 0.6-0.8x the rest length would provide good coverage. This is the cheapest approach and works well with our existing per-particle collision system.

**Approach 2: Edge midpoint sampling**
Add virtual collision points at the midpoint (or third-points) of each edge. These "ghost" particles participate in collision detection but are not part of the constraint system -- their positions are interpolated from the endpoint particles each frame. This doubles or triples the collision sample count without adding solver complexity.

**Approach 3: Triangle-primitive intersection tests**
Test each cloth triangle against each collider primitive directly:
- **Triangle vs. sphere:** Standard geometric test
- **Triangle vs. plane:** Check if all three vertices are on the same side
- **Triangle vs. capsule/cylinder:** Closest point between triangle and line segment
- **Triangle vs. box:** Separating axis theorem (SAT) with 13 axes

This catches face penetrations that particle-only testing misses, but is more expensive and complex to implement collision response for (where exactly to push the triangle, and how to distribute corrections to vertices).

**Approach 4: Repulsion stencils (Bridson)**
For each close vertex-face and edge-edge pair, generate a "stencil" -- a quadruple of vertices that defines a collision event. The stencil implicitly defines a collision normal through which the four vertices can be relocated to eliminate the intersection. This is the approach used in production cloth systems.

### What This Means for Vestige

At our mesh resolution, the **thick particle (marble) approach** is the best fit:

- It requires no new collision test types -- just increase particle collision radius
- Coverage of the mesh surface is good when radius >= 0.6x rest length
- Filter out collisions between particles connected by constraints (1-ring neighbors)
- Cheap: same per-particle tests we already have, just with a larger radius
- Covers edges implicitly (overlapping spheres span the edge)

The edge midpoint sampling approach is a good secondary option if we see specific problem areas.

**Sources:**
- [Stanford -- Cloth Simulation (marble model)](https://graphics.stanford.edu/~mdfisher/cloth.html)
- [Bridson et al. -- Robust Treatment of Collisions](https://graphics.stanford.edu/papers/cloth-sig02/cloth.pdf)
- [Kikuchi -- A Unified Discrete Collision Framework for Triangle Primitives](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70029)
- [ResearchGate -- Fast Collision Detection for Deformable Models using Representative-Triangles](https://www.researchgate.net/publication/220791990_Fast_Collision_Detection_for_Deformable_Models_using_Representative-Triangles)

---

## 3. Collision Thickness and Margins

### The Concept

Real cloth has physical thickness (typically 0.1-3mm depending on fabric). In simulation, cloth is modeled as an infinitely thin surface, which causes problems:
- Particles can rest exactly on collider surfaces, causing jittering from floating-point precision errors
- Small perturbations push particles to the wrong side of a surface
- Cloth-on-cloth contact has zero separation, making stacking unstable

The solution is to give cloth a **virtual thickness** h -- a minimum separation distance maintained between cloth particles and collider surfaces (and between cloth layers for self-collision).

### How Thickness Works in Practice

**In collision detection:** A collision is detected when a particle is within distance h of a surface, not just when it touches. The constraint pushes the particle to exactly distance h from the surface.

**In XPBD constraint formulation:**
```
C(x) = (x - x_surface) . n - h
```
Where x is the particle position, x_surface is the closest surface point, n is the surface normal, and h is the thickness. The constraint is satisfied when C(x) >= 0 (inequality constraint).

**Position correction:**
```
delta_x = -C(x) / |grad_C|^2 * grad_C
        = -(d - h) * n     (when d < h)
```
Where d is the current distance to the surface.

### Typical Thickness Values

| Engine / System | Default Thickness | Notes |
|----------------|-------------------|-------|
| Marvelous Designer / CLO3D | 1.5mm per side (3mm total) | Adjustable per fabric |
| NVIDIA Flex | collisionDistance parameter | Must be non-zero for triangle mesh collision |
| NVIDIA PhysX | Per-particle radius | Affects GPU shared memory usage |
| Houdini Vellum | Thickness attribute per point | Can vary across the mesh |
| Bridson et al. | ~1mm | Used for repulsion force activation distance |
| Jolt Physics | mVertexRadius | Collision offset radius per vertex |

### Coordinating Thickness with Collider Offset

Many engines have **two** thickness parameters:
1. **Cloth thickness** (particle radius / collision distance): the cloth's own shell
2. **Collider skin offset** (avatar/shape offset): a margin around the collision shape

These are additive. Marvelous Designer defaults to 1.5mm cloth thickness + 3mm avatar offset = 4.5mm total separation. For game engines, typical combined values are 2-5mm.

### Thickness vs. Mesh Resolution Trade-Off

If thickness is too large relative to mesh resolution:
- Cloth appears to float above surfaces
- Neighboring particles constantly trigger collision with each other (if self-collision is enabled)
- Competing distance and collision constraints cause jittering/buckling

If thickness is too small:
- Particles tunnel through geometry more easily
- Floating-point precision errors cause particles to slip to the wrong side
- Jittering at rest (particle oscillates between sides of the surface)

NVIDIA Flex documentation explicitly notes: "Thickness values that are very high can result in jittering." The fix is either lowering thickness or increasing substeps. For self-collision, Flex recommends "author meshes with uniform edge lengths matching solid particle rest distance to prevent erroneous buckling or folding when distance and collision constraints compete."

### Best Practices

1. **Start with thickness = 1-2% of the smallest cloth dimension** (e.g., for a 1m curtain, use 1-2cm thickness). Tune visually.
2. **Keep thickness < 0.5x the rest length** between particles to avoid self-collision interference.
3. **Use the same thickness for all collider types** for consistent visual behavior.
4. **Increase substeps rather than thickness** when tunneling occurs.
5. **Consider per-fabric thickness** if simulating different materials (thin silk vs. thick canvas).

### What This Means for Vestige

Our cloth panels have ~50-70 particles. Assuming a 1m x 1m curtain, that gives roughly 7-8 particles per edge, with rest lengths of ~12-14cm. A collision thickness of 1-2cm (about 10% of rest length) provides good separation without visual floating. This is already larger than real cloth thickness, but for game purposes it prevents jitter and tunneling.

Implementation:
```cpp
// In cloth collision constraint
const float COLLISION_THICKNESS = 0.015f; // 1.5cm
float dist = dot(particlePos - surfacePoint, surfaceNormal);
if (dist < COLLISION_THICKNESS)
{
    // Push particle to thickness distance
    float correction = COLLISION_THICKNESS - dist;
    particlePos += correction * surfaceNormal;
}
```

**Sources:**
- [Marvelous Designer -- Adjust Collision Thickness](https://support.marvelousdesigner.com/hc/en-us/articles/47358430412441-FABRIC-PHYSICAL-PROPERTIES-Adjust-Collision-Thickness)
- [CLO3D -- Adjust Collision Thickness](https://support.clo3d.com/hc/en-us/articles/115002798068-Adjust-Collision-Thickness)
- [tyFlow -- CUDA Cloth Collision Solver](https://docs.tyflow.com/faq/CUDA_collisions/)
- [NVIDIA Flex Manual](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/flex/manual.html)
- [Jolt Physics -- Soft Body Physics](https://deepwiki.com/jrouwe/JoltPhysics/3.2-soft-body-physics)
- [Bridson et al. -- Robust Treatment of Collisions](https://graphics.stanford.edu/papers/cloth-sig02/cloth.pdf)

---

## 4. Self-Collision

### What It Is

Self-collision occurs when a piece of cloth folds onto itself -- for example, a curtain bunching up at the bottom, or a flag wrapping around a pole. Without self-collision handling, the cloth passes through itself, producing unrealistic interpenetration.

### Detection Approaches

**Particle-particle (simplest):**
Treat each particle as a sphere. Two non-adjacent particles collide when their distance is less than 2x the particle radius (or a configurable self-collision distance). "Non-adjacent" means they are not connected by a constraint (typically filtered by 1-ring or 2-ring neighbors in the mesh topology).

This is the approach used by Matthias Muller's Ten Minute Physics tutorials and by NVIDIA Flex (via the eFlexPhaseSelfCollide flag). It is the simplest and cheapest method.

Drawbacks: Particle size is hard to choose. Too small leads to gaps where cloth passes through itself. Too large causes neighboring particles to constantly collide, making the cloth stiff and bumpy.

**Vertex-face and edge-edge (production quality):**
The same VF and EE primitives from CCD (Section 1) are used, but testing cloth against itself rather than against external colliders. For each cloth triangle, test all non-adjacent triangles for proximity. This requires spatial acceleration (Section 5) because the naive O(n^2) test is too expensive.

**SDF-based (XRTailor / research):**
Build a signed distance field from the cloth mesh each frame. Test particles against the SDF. Effective but expensive to rebuild every frame.

### Resolution Approaches

**In PBD/XPBD:**
Self-collision is handled as a position-based constraint. When two non-adjacent particles are closer than the self-collision distance:

```
// Collision normal
n = (p1 - p2) / |p1 - p2|
// Penetration depth
d = selfCollisionDist - |p1 - p2|
// Correction (equal mass case)
p1 += 0.5 * d * n
p2 -= 0.5 * d * n
```

With unequal masses, corrections are weighted by inverse mass: w1/(w1+w2) and w2/(w1+w2).

**EA Frostbite predictive contacts (GDC 2018):**
Chris Lewin presented an approach where, instead of detecting collisions after they occur, the system predicts which particles will come close in the next timestep and preemptively generates contact constraints. This keeps the cloth on the correct side and removes tunneling entirely. Performance: 0.2ms per frame for character clothing, running on a single CPU thread.

Key insight: run collision detection once with predicted positions, generate contacts, then solve the contacts as part of the regular constraint solver. No need for iterative CCD.

**Global Intersection Analysis (GIA):**
Baraff et al. (2003) method that detects and resolves mesh intersections by computing the volume enclosed by self-penetrating cloth regions. The gradient of this volume forms a natural force that pushes vertices in the correct direction to resolve the intersection. Used in research and high-quality offline simulation.

### Spatial Hashing for Self-Collision (see Section 5)

Self-collision detection is dominated by the cost of finding nearby non-adjacent particles. Spatial hashing reduces this from O(n^2) to approximately O(n). For our ~50-70 particles, brute force O(n^2) is actually feasible (50^2 = 2500 pair checks, each is just a distance comparison), but spatial hashing is still preferred as it scales better if we add more panels.

### What This Means for Vestige

Self-collision is not our immediate problem (curtains and banners don't self-fold much), but when we need it:

1. **Use particle-particle collision** -- simplest, and at our mesh resolution the coverage is adequate
2. **Filter connected particles** -- skip collision for 1-ring neighbors (particles sharing a constraint)
3. **Self-collision distance = particle radius** -- typically 0.5-0.8x the rest length
4. **Integrate into XPBD solver loop** -- solve self-collision constraints after structural/bend constraints
5. **Consider adding later** -- for bunching curtains or draped tablecloths

At 50-70 particles, even brute-force pair testing is fast (~2500 pairs * simple distance check). Add spatial hashing only if we need multiple self-colliding panels simultaneously.

**Sources:**
- [Carmen Cincotti -- Cloth Self Collisions (XPBD)](https://carmencincotti.com/2022-11-21/cloth-self-collisions/)
- [EA Frostbite -- Cloth Self Collision with Predictive Contacts (GDC 2018)](https://www.ea.com/frostbite/news/cloth-self-collision-with-predictive-contacts)
- [Forefront Blog -- Summary of Frostbite Predictive Contacts](https://geimund.wordpress.com/2020/08/02/forefront-gdc-cloth-self-collision-with-predictive-contacts/)
- [Matthias Muller -- Ten Minute Physics: Cloth Self-Collision](https://matthias-research.github.io/pages/tenMinutePhysics/15-selfCollision.html)
- [NVIDIA Flex Manual -- Self-Collision](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/flex/manual.html)
- [ARXIV -- SENC: Handling Self-collision in Neural Cloth Simulation](https://arxiv.org/html/2407.12479v1)
- [Baraff et al. -- Untangling Cloth (GIA)](https://graphics.stanford.edu/courses/cs468-02-winter/Papers/Collisions_vetements.pdf)

---

## 5. Spatial Acceleration and Broad-Phase Culling

### Why Spatial Acceleration Matters

Without acceleration structures, collision detection is O(n * m) for n particles against m collider primitives, and O(n^2) for self-collision. For our current setup (50-70 particles, 4-6 colliders), this is manageable. But it becomes important when:
- Multiple cloth panels exist in the scene simultaneously
- Self-collision is enabled
- Cloth interacts with complex environments (many colliders)

### Spatial Hashing (Recommended for Cloth)

Spatial hashing is the preferred acceleration structure for deformable objects like cloth because it handles objects that change shape every frame (unlike BVH which needs expensive rebuilds).

**How it works:**
1. Divide 3D space into a uniform grid of cells with side length s
2. Hash each cell coordinate to a bucket in a hash table
3. Insert all particles into their corresponding buckets
4. For collision queries, check only particles in the same cell and the 26 neighboring cells (3x3x3 neighborhood)

**Hash function (Matthias Muller's "fantasy hash"):**
```
h = (xi * 92837111) ^ (yi * 689287499) ^ (zi * 283923481)
bucket = abs(h) % tableSize
```
Where xi, yi, zi are integer cell coordinates: xi = floor(x / cellSize).

**Dense implementation (Muller, Ten Minute Physics):**
Uses only two arrays for excellent cache performance:
1. **Count Array** (size = tableSize): number of particles in each bucket
2. **Particle Array** (size = numParticles): sorted particle indices, grouped by bucket

Construction is O(n) using counting sort:
1. Count particles per bucket (one pass)
2. Compute prefix sum to get bucket start indices (one pass)
3. Place particles into sorted array (one pass)

Query is O(1) amortized per particle (check local 3x3x3 neighborhood).

**Cell size selection:**
- For self-collision: cell size = 2x particle collision radius (Muller's recommendation)
- For external collision: cell size should be >= the maximum particle travel distance per substep
- General rule: cell size = 2x the maximum interaction distance

**Table size:** Typically 5x the number of particles for low collision rate.

### Bounding Volume Hierarchies (BVH)

Used by Bridson et al. for proximity detection. An axis-aligned bounding box (AABB) tree is built over the cloth triangles and collider geometry. Traversal starts from root pairs and recursively tests overlapping child nodes.

BVH is better for:
- Triangle-level collision (not just particles)
- Large separation between cloth and some colliders (BVH prunes entire subtrees)
- Static collider geometry (build once)

BVH is worse for:
- Deformable geometry (requires rebuild or refit every frame)
- Very uniform particle distributions (spatial hashing is simpler)

### Normal Cone Culling

For self-collision, if all normals in a region of cloth face roughly the same direction, that region cannot self-intersect. Normal cone culling computes the cone of normals for each BVH node and skips testing if the cone's half-angle is less than ~90 degrees. This can eliminate large portions of flat cloth from self-collision testing.

### What This Means for Vestige

At our current scale (50-70 particles, 4-6 colliders per panel):

**For external collision:** No spatial acceleration needed. 50 particles x 6 colliders = 300 tests per substep. Each test is a simple geometric calculation. This is negligible on modern CPUs.

**For self-collision (when added):** Brute force is feasible (50^2 / 2 = ~1250 pair tests), but spatial hashing is easy to implement and gives us headroom. Use Muller's dense hash with cell size = 2x particle radius.

**For multiple simultaneous panels (future):** Spatial hashing becomes important. With 10 panels of 70 particles each (700 total), brute force self-collision across panels would be 700^2/2 = 245,000 tests -- spatial hashing reduces this to ~7,000.

**Sources:**
- [Muller et al. -- Optimized Spatial Hashing for Collision Detection of Deformable Objects](https://matthias-research.github.io/pages/publications/tetraederCollision.pdf)
- [Carmen Cincotti -- Spatial Hash Maps Part One](https://carmencincotti.com/2022-10-31/spatial-hash-maps-part-one/)
- [Carmen Cincotti -- Spatial Hash Maps Part Two (Queries)](https://carmencincotti.com/2022-11-07/spatial-hash-maps-part-two/)
- [Matthias Muller -- Ten Minute Physics: Self-Collision](https://matthias-research.github.io/pages/tenMinutePhysics/15-selfCollision.html)
- [Tang et al. -- PSCC: Parallel Self-Collision Culling with Spatial Hashing](https://min-tang.github.io/home/PSCC/files/pscc.pdf)
- [Peerdh.com -- Implementing Spatial Hashing for 3D Collision Detection](https://peerdh.com/blogs/programming-insights/implementing-spatial-hashing-for-efficient-collision-detection-in-3d-environments)

---

## 6. Friction and Collision Response

### Basic Collision Response in PBD/XPBD

In PBD, collision response is a **position correction** (not a force). When a particle penetrates a surface:

```
// Compute penetration
d = dot(particle.pos - surfacePoint, surfaceNormal)
if (d < collisionThickness)
{
    correction = (collisionThickness - d) * surfaceNormal
    particle.pos += correction
}
```

The velocity is implicitly updated because PBD computes velocity from position change: v = (x_new - x_old) / dt.

### Normal and Tangential Velocity Decomposition

After position correction, the velocity change has both normal and tangential components. The normal component prevents continued penetration; the tangential component determines sliding behavior.

```
// Velocity from position change
v = (particle.pos - particle.prevPos) / dt

// Decompose relative to collision normal
v_n = dot(v, n) * n          // normal component
v_t = v - v_n                // tangential component (sliding)
```

### Friction Model

Cloth friction follows the Coulomb model with two regimes:

**Static friction (sticking):** If the tangential force magnitude is less than mu_s * |F_normal|, the contact point does not slide. In PBD terms: if |v_t| is very small, zero it out entirely. This creates stable resting contacts -- critical for curtain folds and cloth draped on surfaces.

**Kinetic friction (sliding):** If the tangential velocity exceeds the static threshold, apply a friction force opposing the sliding direction, proportional to the normal force:

```
// Kinetic friction in PBD (applied to velocity)
if (length(v_t) > staticThreshold)
{
    // Reduce tangential velocity by friction
    float frictionMag = min(mu_k * length(v_n), length(v_t))
    v_t -= normalize(v_t) * frictionMag
}

// Reconstruct velocity
v_new = v_n + v_t
particle.pos = particle.prevPos + v_new * dt
```

### Bridson's Friction Implementation

Bridson et al. decompose the relative velocity at contact into normal and tangential components. The normal component is corrected to ensure separation. Then:

1. If |v_t| < mu_s * |v_n_correction|: zero v_t (static friction -- the cloth sticks)
2. Otherwise: reduce |v_t| by mu_k * |v_n_correction| (kinetic friction -- the cloth slides with resistance)

Static friction is crucial for **stable folds and wrinkles** in curtains. Without it, cloth slides off surfaces unrealistically.

### Friction in the PBD Solver Loop

The recommended approach (from PBD research and the Barth Cave HXPBD implementation):

1. Run constraint solver (structural, bend, collision constraints)
2. Identify colliding particles (those that had collision corrections)
3. For each colliding particle:
   a. Compute the collision normal
   b. Extract tangential velocity: v_t = v - dot(v, n) * n
   c. Apply friction: dampen v_t by the friction coefficient
   d. Reconstruct the corrected velocity
4. Update positions from corrected velocities

This "velocity filter" approach is described as "a bit hacky" but works well in practice. The key is applying friction **after** position-based collision correction, operating on the resulting velocities.

### Practical Friction Coefficients

| Material Pair | Static mu_s | Kinetic mu_k |
|--------------|-------------|-------------|
| Cloth on wood | 0.4-0.6 | 0.3-0.5 |
| Cloth on metal | 0.3-0.5 | 0.2-0.4 |
| Cloth on cloth | 0.5-0.8 | 0.4-0.6 |
| Cloth on stone | 0.4-0.7 | 0.3-0.5 |

For game purposes, a single friction coefficient of 0.3-0.5 works for most surfaces. Per-material friction requires per-collider friction parameters.

### Restitution (Bounciness)

Cloth has very low restitution (~0.0-0.1). In practice, cloth collision response uses zero restitution: the normal velocity is fully absorbed on contact. This makes cloth "stick" to surfaces on impact rather than bouncing, which looks realistic.

In PBD terms: after collision correction, ensure the normal velocity component is non-negative (moving away from surface) but do not add bounce:

```
v_n = dot(v, n)
if (v_n < 0)
    v -= v_n * n  // Remove approaching velocity, zero restitution
```

### What This Means for Vestige

Our current per-particle collision should add:

1. **Friction with Coulomb model** (static + kinetic, applied as velocity filter after collision projection)
2. **Zero restitution** (cloth does not bounce)
3. **Start with mu = 0.4** as a default for all surfaces
4. **Add per-collider friction** later if needed (stored in the collider shape data)
5. **Static friction is critical** for curtain folds resting on surfaces

**Sources:**
- [Bridson et al. -- Robust Treatment of Collisions, Contact and Friction](https://graphics.stanford.edu/papers/cloth-sig02/cloth.pdf)
- [Bridson Cloth Collisions Slides](https://slidetodoc.com/cloth-collisions-and-contact-robert-bridson-university-of/)
- [SIGGRAPH 2022 -- Contact and Friction Simulation for Computer Graphics](https://siggraphcontact.github.io/assets/files/SIGGRAPH22_friction_contact_notes.pdf)
- [PBD Rigid Body Dynamics -- Friction Handling](https://animation.rwth-aachen.de/media/papers/2014-CAVW-PBRBD.pdf)
- [Barth Cave -- HXPBD Soft Body Simulation (friction implementation)](https://barthpaleologue.github.io/Blog/posts/hxpbd/)
- [PMC -- Robust High-Resolution Cloth Using Parallelism and Accurate Friction](https://pmc.ncbi.nlm.nih.gov/articles/PMC4629478/)

---

## 7. Recommended Approach for Vestige

### Current State
- Per-particle discrete collision against primitive shapes (spheres, planes, cylinders, boxes)
- ~50-70 particles per cloth panel
- 4-6 collider shapes per panel
- CPU-based XPBD solver

### Priority 1: Quick Wins (Low Effort, High Impact)

**A. Add collision thickness/margin**
- Set COLLISION_THICKNESS = 0.015f (1.5cm, adjustable per fabric)
- Modify collision constraints to maintain thickness distance from surfaces
- Prevents jitter and most tunneling at rest
- **Effort:** ~30 minutes, modify existing collision projection

**B. Add velocity clamping**
- Cap particle velocity to MAX_VEL = collisionThickness * substeps / dt
- Prevents extreme tunneling during wind gusts or fast motion
- **Effort:** ~15 minutes, add after velocity integration

**C. Add friction (Coulomb model)**
- After collision projection, decompose velocity into normal/tangential
- Apply static friction (zero tangential if below threshold) and kinetic friction
- Start with mu = 0.4 globally
- **Effort:** ~1-2 hours, add velocity filter after collision solver

### Priority 2: Medium-Term Improvements

**D. Increase particle collision radius (marble model)**
- Set particle radius to 0.6x rest length (instead of point collision)
- Covers mesh edges/faces implicitly through overlapping spheres
- Filter collisions between 1-ring neighbors
- **Effort:** ~2-3 hours, modify collision detection radius and add neighbor filtering

**E. Increase substeps**
- Move from current substep count to 8-12 per frame
- At 50-70 particles, each substep is very cheap (~50 microseconds)
- 12 substeps at 50 particles adds < 1ms per frame total
- **Effort:** ~30 minutes, change loop count and adjust dt

### Priority 3: Future Enhancements

**F. Spatial hashing (when self-collision or multi-panel needed)**
- Implement Muller's dense spatial hash (two arrays, counting sort)
- O(n) construction, O(1) amortized query
- **Effort:** ~4-6 hours for clean implementation

**G. Self-collision (when cloth folding is needed)**
- Particle-particle with spatial hashing
- Filter connected neighbors
- Integrate into XPBD solver loop after structural constraints
- **Effort:** ~6-8 hours

**H. CCD for sphere/capsule colliders (if tunneling persists)**
- Solve quadratic for time-of-impact against sphere/capsule
- Simpler than triangle-mesh CCD (no cubic polynomials)
- **Effort:** ~4-6 hours

### Cost/Benefit Summary

| Enhancement | Effort | Impact | Priority |
|------------|--------|--------|----------|
| Collision thickness | 30 min | High -- eliminates jitter, prevents most tunneling | P1 |
| Velocity clamping | 15 min | Medium -- prevents extreme tunneling | P1 |
| Friction | 1-2 hrs | High -- realistic sliding, stable folds | P1 |
| Thick particles (marble) | 2-3 hrs | Medium -- covers edges/faces | P2 |
| More substeps | 30 min | Medium -- reduces all collision issues | P2 |
| Spatial hashing | 4-6 hrs | Low (at current scale) -- future-proofing | P3 |
| Self-collision | 6-8 hrs | Low (curtains) / High (tablecloths) | P3 |
| Sphere/capsule CCD | 4-6 hrs | Low (substeps usually sufficient) | P3 |

### Key Takeaway

For a CPU-based XPBD cloth simulator with 50-70 particles and primitive colliders, the highest-impact improvements are the simplest: **collision thickness, velocity clamping, and friction**. These three changes (achievable in under 3 hours) would eliminate the vast majority of collision artifacts. Edge/face coverage via the marble model is the next best investment. Full CCD and spatial hashing are future optimizations that our current scale does not demand.
