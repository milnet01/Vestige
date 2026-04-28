# BVH Construction for Cloth-Body Collision Detection

**Date:** 2026-04-01
**Purpose:** Research BVH construction and traversal for real-time cloth particle vs. triangle mesh collision.

---

## 1. Splitting Strategy: SAH vs Midpoint vs Equal-Count

| Strategy | Build Cost | Tree Quality | Best For |
|----------|-----------|-------------|----------|
| SAH (Surface Area Heuristic) | O(N log^2 N) | Optimal traversal | Static/rarely-rebuilt meshes |
| Binned SAH (8-16 bins) | O(N log N) | Near-optimal | One-time build of body meshes |
| Midpoint | O(N log N) | Adequate | Per-frame rebuilds of deforming meshes |
| Equal-count (median) | O(N log N) | Good balance | General purpose, predictable depth |

**Decision:** Use **binned SAH** (8 bins) for the initial build of body meshes. Use **midpoint splitting** when a full rebuild is needed after topology change. For per-frame updates of animated meshes, **refit** rather than rebuild (see section 4).

## 2. AABB vs OBB

AABB is the standard for real-time cloth collision. OBBs give tighter fits (fewer false positives during traversal) but cost 15 separating-axis tests per overlap check vs. 3 interval checks for AABBs. Research by UPC Barcelona tested OBB BVH for cloth-avatar collision but concluded the tighter bounds did not justify the traversal cost at game-engine triangle counts. Jolt Physics, Bullet, FCL, and mclccd all use AABB trees.

**Decision:** Use **AABB-based BVH**. OBB is not worth the complexity.

## 3. Point-Triangle Proximity Traversal

For each cloth particle, find the closest triangle on the body mesh within distance `h` (collision thickness). Pseudocode:

```
function queryClosest(node, point, h, result):
    if distanceToAABB(point, node.bounds) > result.dist:
        return  // prune -- AABB is farther than current best
    if node.isLeaf:
        for each triangle in node:
            d = pointTriangleDistance(point, triangle)
            if d < result.dist:
                result.dist = d
                result.triangle = triangle
                result.normal = triangleNormal
        return
    // Visit closer child first (reduces pruning distance faster)
    if distanceToAABB(point, node.left.bounds) < distanceToAABB(point, node.right.bounds):
        queryClosest(node.left, point, h, result)
        queryClosest(node.right, point, h, result)
    else:
        queryClosest(node.right, point, h, result)
        queryClosest(node.left, point, h, result)
```

Initialize `result.dist = h` so only triangles within collision thickness are considered. Average traversal cost: O(log N) per particle for well-built trees.

## 4. Refit vs Rebuild for Animated Meshes

**Refitting** updates AABBs bottom-up without changing tree topology. Cost: O(N) per frame (one pass over all nodes). Tree quality degrades if mesh deforms significantly (limbs stretching, extreme poses).

**Rebuilding** reconstructs the tree. Cost: O(N log N) with midpoint splitting.

**Practical approach (hybrid):**
- **Refit every frame** (cheap, O(N)).
- **Track tree quality** via a metric: `quality = totalSurfaceArea(refitted) / totalSurfaceArea(freshBuild)`. When quality > 2.0, trigger a rebuild. For skeletal animation with stable topology, rebuilds are rarely needed.
- Full rebuild is also needed when mesh topology changes (LOD switch).

```
function refitBVH(node):
    if node.isLeaf:
        node.bounds = computeAABB(node.triangles)
        return
    refitBVH(node.left)
    refitBVH(node.right)
    node.bounds = merge(node.left.bounds, node.right.bounds)
```

## 5. CCD with BVH Acceleration

For cloth, CCD requires vertex-triangle (VF) and edge-edge (EE) elementary tests. A BVH accelerates this by wrapping swept volumes (the AABB of a triangle across both timestep endpoints) in the tree nodes.

At Vestige's current scale (50-70 particles, primitive colliders), full CCD is unnecessary. The existing substep approach (10 substeps) with velocity clamping handles tunneling. CCD becomes relevant when colliding against detailed triangle meshes (character bodies with 1000+ triangles). At that point:

1. Build BVH over body mesh triangles (binned SAH, one-time).
2. Refit each frame as skeleton animates.
3. For each cloth particle, traverse BVH to find triangles within `h`.
4. Apply position correction along the closest triangle's normal.

Full VF/EE CCD (cubic polynomial solving) should only be added if substeps prove insufficient.

## 6. Reference Implementations

| Library | Key Feature | API Pattern |
|---------|------------|-------------|
| [madmann91/bvh](https://github.com/madmann91/bvh) | Header-only C++20, SAH + binned builders, custom traversers | `Bvh<Node>`, `DefaultBuilder`, `SingleRayTraverser` |
| [mclccd](https://github.com/mattoverby/mclccd) | BVH-based CCD, VF/EE tests, callback API | `BVHTree<double,3>`, `update(V0,V1,F)`, `appendPair` callback |
| [JoltPhysics](https://github.com/jrouwe/JoltPhysics) | XPBD soft body, broad+narrow phase, collision planes per vertex | `SoftBodyMotionProperties`, `SoftBodyManifold` |
| [FCL](https://github.com/flexible-collision-library/fcl) | BVHModel with OBBRSS, proximity + collision queries | `BVHModel<OBBRSS>`, `collide()`, `distance()` |
| [Bullet](https://github.com/bulletphysics/bullet3) | btBvhTriangleMeshShape, refit support | `btBvhTriangleMeshShape`, `refitTree()` |

**Jolt's pattern is closest to Vestige's architecture**: XPBD solver, per-vertex collision planes generated from narrow-phase BVH queries, constraint-based response.

## Key Takeaways

1. **AABB BVH with binned SAH** for body mesh construction (one-time or on LOD switch).
2. **Bottom-up refit** every frame for animated meshes -- O(N), no rebuild needed for skeletal animation.
3. **Closest-point traversal** (not ray traversal) for cloth particles -- visit closer child first, prune by collision thickness `h`.
4. **Defer CCD** -- substeps + velocity clamping handle tunneling at current scale. Add BVH proximity queries when triangle mesh colliders are introduced.
5. **Follow Jolt's pattern**: generate collision planes per vertex from BVH narrow-phase, solve as XPBD constraints.

---

**Sources:**
- [BVH Wikipedia](https://en.wikipedia.org/wiki/Bounding_volume_hierarchy)
- [Efficient BVH-based Collision Detection Scheme (Tang et al.)](https://min-tang.github.io/home/BVH-OR/files/eg2018.pdf)
- [PBRT Book -- BVH Chapter](https://pbr-book.org/3ed-2018/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies)
- [Jacco's Blog -- How to Build a BVH Part 3: Quick Builds](https://jacco.ompf2.com/2022/04/21/how-to-build-a-bvh-part-3-quick-builds/)
- [Adaptive BVH for Deformable Surfaces (Chalmers)](https://publications.lib.chalmers.se/records/fulltext/155282.pdf)
- [Mochi: Fast & Exact Collision Detection](https://arxiv.org/html/2402.14801v4)
- [OBB-based Cloth-Avatar Collision (UPC Barcelona)](https://www.cs.upc.edu/~npelechano/CollisionDetection.pdf)
- [Brochu et al. -- Geometrically Exact CCD (SIGGRAPH 2012)](https://www.cs.ubc.ca/~rbridson/docs/brochu-siggraph2012-ccd.pdf)
- [madmann91/bvh Library](https://github.com/madmann91/bvh)
- [mclccd Library](https://github.com/mattoverby/mclccd)
- [JoltPhysics -- Soft Body Physics](https://deepwiki.com/jrouwe/JoltPhysics/3.2-soft-body-physics)
- [FCL Library](https://github.com/flexible-collision-library/fcl)
- [Bullet Physics btBvhTriangleMeshShape](https://www.staff.city.ac.uk/~andrey/INM377/bullet-2.82-html/html/classbtBvhTriangleMeshShape.html)
- [three-mesh-bvh Closest Point Queries](https://deepwiki.com/gkjohnson/three-mesh-bvh/3.3-closest-point-queries)
