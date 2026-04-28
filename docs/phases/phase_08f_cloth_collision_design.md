# Phase 8F: Cloth Collision System Design

## Overview
Add BVH-accelerated triangle mesh collision and spatial-hash self-collision to the existing XPBD cloth simulator.

## Research Summary

### BVH (Bounding Volume Hierarchy)
- **Structure:** AABB tree with binned SAH (Surface Area Heuristic) splitting (8 bins)
- **Construction:** O(N log N) top-down recursive, leaf threshold of 4 triangles
- **Refit:** O(N) bottom-up AABB recomputation for animated meshes — no topology change
- **Proximity query:** Stack-based traversal, closest-child-first pruning, O(log N) average per particle
- **Reference:** Jolt Physics soft body uses same approach (BVH narrow-phase → collision planes → PBD constraints)

Sources:
- Ericson, "Real-Time Collision Detection" (2004) — closest-point-on-triangle (Voronoi regions)
- Jolt Physics (MIT) — soft body BVH collision pattern

### Spatial Hashing (Self-Collision)
- **Hash function:** Teschner 2003: `hash(x,y,z) = (x*73856093 ^ y*19349663 ^ z*83492791) % tableSize`
- **Cell size:** 2× cloth thickness (self-collision distance)
- **Data structure:** Counting-sort (Müller): count array → prefix sum → scatter. O(N), allocation-free after init
- **Adjacency filtering:** Grid-based check — particles within 1 grid step are adjacent, skip self-collision
- **Collision response:** Symmetric position correction along connecting line, velocity damping

Sources:
- Teschner et al. 2003, "Optimized Spatial Hashing for Collision Detection of Deformable Objects"
- Müller, "Ten Minute Physics" tutorials 11 (spatial hash) and 15 (cloth self-collision)
- Bridson et al. 2002, "Robust Treatment of Collisions, Contact and Friction for Cloth Animation"

## Architecture

```
ClothSimulator
├── existing: sphere/plane/cylinder/box colliders
├── NEW: ClothMeshCollider* pointers (non-owning)
│         └── ClothMeshCollider
│               ├── vertex/index data (owned copies)
│               └── BVH (SAH tree, refit, proximity query)
├── NEW: SpatialHash (member, rebuilt each substep when self-collision enabled)
└── NEW: ColliderGenerator (static utility)
```

## New Files

| File | Purpose |
|------|---------|
| `physics/bvh.h/cpp` | AABB BVH: build (SAH), refit, point-proximity query, closest-point-on-triangle |
| `physics/cloth_mesh_collider.h/cpp` | Triangle mesh collider: owns mesh data + BVH, provides queryClosest() |
| `physics/collider_generator.h/cpp` | Static utility: create ClothMeshCollider from raw mesh data |
| `physics/spatial_hash.h/cpp` | Spatial hash: build (counting-sort), query neighbors with self-exclusion |

## Modified Files

| File | Changes |
|------|---------|
| `physics/cloth_simulator.h` | Add mesh collider vector, self-collision members, SpatialHash member |
| `physics/cloth_simulator.cpp` | Mesh collision + self-collision in applyCollisions() |

## Key Algorithms

### BVH Build (Binned SAH)
1. Compute centroid + AABB per triangle
2. If count ≤ 4: create leaf node
3. For each axis (X/Y/Z): bin centroids into 8 bins, sweep left+right to compute SAH cost
4. Partition triangles at best split, recurse

### BVH Proximity Query
1. Initialize search radius = maxDist (collision margin)
2. Stack-based traversal from root
3. Prune nodes whose AABB distance > current best distance
4. At leaves: test closest-point-on-triangle (Ericson Voronoi region method)
5. Push closer child last (popped first) for better pruning

### Spatial Hash Self-Collision
1. Build hash: for each particle, compute cell → count → prefix sum → scatter IDs
2. For each particle: query 27 neighboring cells
3. Filter: skip self, skip adjacent grid neighbors (|dr|≤1 && |dc|≤1)
4. Narrow phase: if distance < selfCollisionDist, push apart symmetrically
5. Dampen relative velocity along collision normal

## Integration with ClothSimulator

Mesh collision and self-collision are added to `applyCollisions()`, which is already called twice per substep (after constraints, after pins). Order within applyCollisions():
1. Ground plane (existing)
2. Sphere colliders (existing)
3. Box colliders (existing)
4. Cylinder colliders (existing)
5. Plane colliders (existing)
6. **Mesh colliders (NEW)** — BVH-accelerated point-triangle proximity
7. **Self-collision (NEW)** — spatial hash broad phase + distance narrow phase

## API Additions to ClothSimulator

```cpp
void addMeshCollider(ClothMeshCollider* collider);   // non-owning pointer
void clearMeshColliders();
void enableSelfCollision(bool enable);
void setSelfCollisionDistance(float distance);        // default 0.02m (2cm)
bool isSelfCollisionEnabled() const;
float getSelfCollisionDistance() const;
```

## Performance Considerations
- BVH refit is O(N) — suitable for animated meshes at 60 FPS
- Spatial hash rebuild is O(N) per substep — fine for cloth meshes ≤ 1000 particles
- Self-collision is the most expensive addition; disabled by default
- Mesh collision uses same dual-pass strategy as existing primitives
- Stack-based BVH traversal (no recursion) — cache-friendly, bounded memory

## Test Plan
- BVH: build, query, refit, closest-point-on-triangle edge cases
- SpatialHash: build, query, self-exclusion, adjacency
- ClothMeshCollider: build, query, vertex update
- ColliderGenerator: create from mesh data
- ClothSimulator integration: mesh collision prevents penetration, self-collision prevents overlap
