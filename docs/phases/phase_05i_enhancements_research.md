# Phase 5I Enhancements Research: Terrain Collision, Auto-Texturing, Triplanar Mapping

**Date:** 2026-03-30
**Scope:** Completing 3 of 4 outstanding Phase 5I terrain enhancement items. Terrain chunking deferred.

---

## 1. Terrain Collision (Character Controller Walks on Terrain Surface)

### Approach: Direct Height Query + Exponential Damping

Since `Terrain::getHeight()` and `Terrain::getNormal()` already exist with bilinear interpolation, the simplest approach is:

1. Query terrain height at the player's XZ position each frame
2. Use frame-rate-independent exponential damping (Rory Driscoll's formula) for smooth height following
3. Asymmetric damping rates: fast ascending (lambda=20), smooth descending (lambda=12)
4. Hard floor clamp: camera never goes below `terrainHeight + playerHeight`
5. Slope limiting: `normal.y < cos(maxAngle)` rejects uphill movement on steep terrain

### Walk/Fly Mode Toggle

- G key toggles between walk mode (terrain-grounded) and fly mode (existing behavior)
- When entering walk mode, smoothed height initializes from current camera Y to avoid snapping
- Walk mode suppresses vertical movement keys (Space/Shift)
- AABB collision still runs after terrain collision for building interaction

### Sources

- Rory Driscoll: Frame Rate Independent Damping using Lerp (rorydriscoll.com)
- LWJGL 3D Game Development Chapter 15: Terrain Collisions
- Unreal Engine WalkableFloorAngle: 44.76 degrees default
- Unity CharacterController: 45 degrees default slope limit
- Godot CharacterBody3D: `floor_max_angle` = 45 degrees

---

## 2. Automatic Slope/Altitude Texture Blending

### Algorithm

Per-texel CPU-side one-shot splatmap generation:

1. Compute slope from pre-computed normal: `slope = 1.0 - normal.y`
2. Perturb slope and height with FBM noise (3 octaves) to break uniform transition lines
3. Apply smoothstep thresholds for each layer:
   - Grass (R): default on flat terrain, reduced by rock on slopes
   - Rock (G): smoothstep from slopeGrassEnd (0.3) to slopeRockStart (0.6)
   - Dirt (B): smoothstep at higher altitude band
   - Sand (A): smoothstep at low altitude
4. Normalize weights to sum to 1.0
5. Upload full splatmap to GPU

### Editor Integration

- "Generate Auto-Texture" button in Terrain Panel > Paint tab
- Configurable parameters: slope thresholds, noise scale, noise amplitude
- Manual paint overrides after auto-generation

### Sources

- Rastertek Tutorial 14: Slope Based Texturing (slope = 1.0 - normal.y)
- Frostbite Terrain Rendering (Andersson, SIGGRAPH 2007): procedural shader splatting
- Terrain3D autoshader: `auto_slope` and `auto_height_reduction` parameters
- Alastair Aitchison: Procedural Terrain Splatmapping (Unity)

---

## 3. Triplanar Mapping for Steep Slopes

### Conditional Triplanar (Performance-Optimized)

Only apply triplanar on steep slopes to protect 60 FPS target:

1. Compute steepness: `1.0 - abs(normal.y)` (0 = flat, 1 = vertical)
2. Blend factor: `smoothstep(0.4, 0.7, steepness)`
3. Flat terrain (majority): 1 texture fetch — no extra cost
4. Steep terrain: 3 fetches from X, Y, Z axis projections
5. Blend weights: `pow(abs(normal), sharpness)`, normalized
6. Mirroring fix: negate UV coordinate on negative-facing sides

### Normal Map Support (Whiteout Blend)

When texture arrays with per-layer normal maps are added later, use Whiteout blending:
```
vec3 whiteoutBlend(vec3 n1, vec3 n2) {
    return normalize(vec3(n1.xy + n2.xy, n1.z * n2.z));
}
```

### Sources

- Catlike Coding: Triplanar Mapping tutorial
- Ben Golus: Normal Mapping for a Triplanar Shader (definitive reference)
- Inigo Quilez: Biplanar Mapping (2-fetch optimization, future option)
- The Demon Throne: Rendering Terrain Part 21 (measured 29% regression with full triplanar)

---

## 4. Terrain Chunking — DEFERRED

### Rationale

Research found that for 256m-2km terrain scope:
- A single 257x257 heightmap = ~257 KB GPU — trivially small
- Scaling to 2049x2049 = ~16 MB — still fits easily in 8 GB VRAM
- CDLOD quadtree already provides per-node frustum culling and LOD
- Chunking adds significant complexity (edge stitching, streaming, border normals, splatmap continuity) with no performance benefit at this scale
- Chunking becomes valuable at 4km+ terrain sizes

### Decision

Defer chunking until the engine needs terrains larger than 4km. If needed, the architecture change would be: replace `Terrain` with `TerrainWorld` owning a grid of `TerrainTile` objects, each with its own CDLOD quadtree. CDLOD's continuous morph factor naturally prevents inter-tile cracks.

### Sources

- CDLOD Paper (Strugar): StreamingCDLOD variant with min/max maps
- Terrain3D: 1024m regions + geometric clipmaps
- Unreal Engine: Landscape components (127x127 sections, World Partition streaming)
