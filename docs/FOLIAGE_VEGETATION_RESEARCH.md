# Foliage & Vegetation Rendering Research

**Date:** 2026-03-21
**Scope:** Instanced grass/foliage rendering, brush painting, wind animation, LOD/culling, scatter placement, spline paths, and open-source references
**Engine:** Vestige (C++17, OpenGL 4.5, ImGui docking)
**Target Hardware:** AMD RX 6600 (RDNA2), ~100k visible instances at 60 FPS

---

## Table of Contents

1. [Instanced Billboard Grass Rendering in OpenGL](#1-instanced-billboard-grass-rendering-in-opengl)
2. [Foliage Density Maps and Brush Painting](#2-foliage-density-maps-and-brush-painting)
3. [Wind Animation for Vegetation](#3-wind-animation-for-vegetation)
4. [LOD and Culling for Vegetation](#4-lod-and-culling-for-vegetation)
5. [Scatter/Placement Systems](#5-scatterplacement-systems)
6. [Spline-Based Path/Road Tools](#6-spline-based-pathroad-tools)
7. [Open-Source References](#7-open-source-references)
8. [Recommended Approach for Vestige](#8-recommended-approach-for-vestige)

---

## 1. Instanced Billboard Grass Rendering in OpenGL

### 1.1 Rendering Approaches: Billboards vs Geometry vs Mesh

There are six primary techniques used in modern engines for grass rendering, each with distinct trade-offs:

#### A. Billboard Quads (Camera-Facing)
- A single textured quad always faces the camera.
- **Pros:** Extremely cheap -- 4 vertices per instance. A grass texture containing many blades condenses what would be hundreds of vertices into a single quad.
- **Cons:** Obvious at close range; billboards clip through each other during rotation. Only suitable for distant LOD.
- **Best use:** Far-distance LOD tier where individual blade fidelity is irrelevant.

#### B. Cross/Star Billboards (2-3 Intersecting Quads)
- Two or three quads intersect at angles (e.g., 60 degrees apart) forming a "star" or "cross" pattern.
- **Pros:** Convincing volume from any viewing angle with minimal geometry (8-12 vertices). No camera-facing rotation needed. The standard approach in most shipped games.
- **Cons:** Still reveals flat planes at very close range. Alpha blending/testing required for transparency edges.
- **Best use:** The workhorse approach for grass, flowers, and small ground cover. This is the recommended default.

#### C. Geometry Shader Generated Blades
- Start with terrain vertices; a geometry shader emits grass blade geometry at each vertex.
- **Pros:** No CPU-side instance buffer needed. Density tied to terrain tessellation.
- **Cons:** Geometry shaders have notoriously poor performance on AMD hardware (RDNA architecture). Not recommended for AMD RX 6600. Each invocation has high overhead compared to instancing.
- **Best use:** Prototyping only. Avoid for production on AMD GPUs.

#### D. Tessellation + Geometry Shader
- Tessellate terrain mesh, then use geometry shader to emit blades at tessellated vertices.
- **Pros:** Adaptive density based on camera distance.
- **Cons:** Same geometry shader performance problems as (C), compounded by tessellation overhead.
- **Best use:** Not recommended for Vestige given AMD target hardware.

#### E. GPU-Instanced Mesh Blades
- Individual blade meshes (3-7 triangles each) rendered via `glDrawArraysInstanced` or `glDrawElementsInstanced`. Per-instance data (position, rotation, scale, color tint) stored in an SSBO or vertex buffer with `glVertexAttribDivisor`.
- **Pros:** Full control over blade shape. Works extremely well with compute shader culling. Excellent performance on all hardware. LearnOpenGL demonstrates going from ~1,500 smoothly rendered objects to 100,000 with instancing.
- **Cons:** Requires CPU or compute shader to populate instance buffer. Slightly more complex pipeline.
- **Best use:** Close and mid-range grass. This is the recommended primary approach.

#### F. Compute Shader + Indirect Draw (Most Advanced)
- A compute shader generates instance data, performs culling, and fills an indirect draw command buffer. The CPU issues a single `glDrawArraysIndirect` or `glMultiDrawArraysIndirect` call.
- **Pros:** Zero CPU involvement in culling. GPU does all the work. Scales to millions of instances. The GL_DRAW_INDIRECT_BUFFER is GPU memory and can be manipulated directly by compute shaders.
- **Cons:** More complex to implement. Requires OpenGL 4.3+ (we have 4.5, so fine).
- **Best use:** The ultimate scalability solution. Can be added as an optimization pass on top of approach (E).

### 1.2 Per-Instance Data Layout

For GPU instancing, each grass instance typically stores:

```
struct GrassInstance {
    vec4 positionAndScale;   // xyz = world position, w = uniform scale
    vec4 rotationAndType;    // x = Y-axis rotation (radians), yzw = type/color/etc.
};
```

Or more compactly:
```
struct GrassInstance {
    vec3 position;           // 12 bytes
    float rotation;          // 4 bytes  (Y-axis only)
    float scale;             // 4 bytes
    uint  typeAndFlags;      // 4 bytes  (packed: foliage type index + flags)
};  // 24 bytes per instance
```

At 24 bytes per instance, 100k instances = 2.4 MB of GPU memory -- trivial.

Using an SSBO, the vertex shader indexes into the buffer with `gl_InstanceID`:
```glsl
layout(std430, binding = 0) buffer InstanceData {
    GrassInstance instances[];
};
```

### 1.3 Alpha Handling for Grass Textures

Grass textures use transparency to cut out blade shapes from quads. Three approaches:

- **Alpha Test (`discard`):** Simple, cheap, works with depth buffer. But causes hard aliasing on edges. Enable early depth test for performance.
- **Alpha-to-Coverage:** Requires MSAA enabled. Converts alpha values to coverage mask samples, producing smooth edges without sorting. Order-independent transparency. Minor MSAA overhead but superior edge quality. This is the standard for shipped games with foliage.
- **Alpha Blending:** Requires back-to-front sorting, which is impractical for 100k instances. Avoid for grass.

**Recommendation:** Use alpha test (`discard` in fragment shader with threshold ~0.5) as baseline, with alpha-to-coverage enabled when MSAA is active for better quality.

### 1.4 Performance Considerations for 100k Instances

- **Draw calls:** With instancing, 100k instances = 1 draw call per foliage type. With 5-10 foliage types, that's 5-10 draw calls total.
- **Vertex count:** Cross-billboard (2 quads) = 8 vertices x 100k = 800k vertices. Well within RX 6600 capability.
- **Fill rate:** The primary bottleneck for grass is overdraw from overlapping transparent quads. Alpha test with early-z helps. Keep grass blade textures small (64x64 or 128x128 atlas entries).
- **SSBO bandwidth:** 2.4 MB read per frame is negligible for GDDR6.
- **Batch by foliage type:** All instances of the same grass type share one draw call. Different types (grass, flowers, ferns) get separate draw calls.

**Sources:**
- [LearnOpenGL - Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing)
- [Cyanilux - GPU Instanced Grass Breakdown](https://www.cyanilux.com/tutorials/gpu-instanced-grass-breakdown/)
- [NVIDIA Instancing Sample](https://docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/instancingsample.htm)
- [Six Grass Rendering Techniques in Unity](https://danielilett.com/2022-12-05-tut6-2-six-grass-techniques/)
- [AMD GPUOpen - Procedural Grass Rendering with Mesh Shaders](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/)
- [OpenGL Instancing Demystified - GameDev.net](https://www.gamedev.net/tutorials/programming/graphics/opengl-instancing-demystified-r3226/)
- [Ben Golus - Anti-aliased Alpha Test: The Esoteric Alpha To Coverage](https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f)
- [Megabyte Softworks - Waving Grass OpenGL Tutorial](https://www.mbsoftworks.sk/tutorials/opengl3/29-terrain-pt2-waving-grass/)

---

## 2. Foliage Density Maps and Brush Painting

### 2.1 How Major Engines Implement Foliage Painting

#### Unreal Engine Foliage Tool
- **Brush:** A transparent sphere in 3D space. When painting, the engine performs line traces inside the sphere parallel to the camera's viewing direction. Any visible surface inside the sphere is a candidate for placement.
- **Density control:** "Paint Density" setting (0.0 to 1.0) controls how many instances are placed per brush stroke. Higher values = more instances, lower = fewer with more spacing.
- **Erase tool:** Selects candidate instances inside the sphere and randomly removes them. "Erase Density" controls removal aggressiveness.
- **Storage:** Instances are stored as Hierarchical Instanced Static Meshes (HISM). These are automatically batched into clusters for efficient culling and rendering with minimal draw calls.
- **Per-type settings:** Each foliage type has independent density, scale range, alignment options, and LOD settings.

#### Unity Terrain System
- **Density map approach:** Unity's terrain stores foliage as a detail layer -- essentially a 2D grid (density map/heightmap resolution) where each cell stores a density value per foliage type.
- **Spatial hash rendering:** A spatial hash partitions instances into chunks. Only visible/nearby chunks are rendered. Close instances are batched into single `DrawMeshInstanced` calls.
- **Third-party tools (GPU Instancer, Flora):** Use scene-wide spatial hashes that update every frame for fast culling, runtime modifications, and efficient spatial queries.

#### Godot / Terrain3D
- **Chunk-based MultiMesh:** Each terrain region is divided into 32x32m cells. A MultiMesh3D node is generated per mesh type per cell. All instances in a MultiMesh render in one draw call.
- **Paintable control map:** An Image (not stored in VRAM) is used as a density/control map. This map drives generation of MultiMesh chunks.
- **Visibility LOD:** 10 levels of detail. Visibility ranges in mesh settings allow culling by distance.

### 2.2 Data Structures for Painted Foliage

There are two fundamental approaches to storing painted foliage:

#### A. Density Map / Splatmap Approach
- A 2D texture (R8 or RGBA8) where each pixel represents a terrain cell.
- Each channel can represent a different foliage type's density (0-255 = density).
- **Pros:** Compact storage, GPU-friendly (texture sampling), easy to paint with brush tools (just modify pixel values), trivial serialization (save/load as image).
- **Cons:** Resolution-limited (a 512x512 map over a 256m terrain = 0.5m per pixel). Cannot store exact instance positions -- instances are generated procedurally from density values at runtime.
- **Brush painting:** Modify pixel values in the density texture using a circular brush kernel with falloff. Upload modified region to GPU via `glTexSubImage2D`.

#### B. Explicit Instance List Approach
- Store an array of `{position, rotation, scale, type}` per painted instance.
- **Pros:** Exact control over every instance. Supports manual placement and fine adjustments. No resolution limit.
- **Cons:** More memory for dense foliage. Spatial queries for brush operations need a spatial index (grid, quadtree, or spatial hash).
- **Brush painting:** Raycast from cursor to terrain surface. Generate N instances within brush radius using Poisson disk or jittered grid sampling. Add to instance list. For erasing, spatial query within brush radius and remove.

#### C. Hybrid Approach (Recommended)
- Use a density map for the painting interface and storage.
- At load time or when the density map changes, regenerate explicit instance positions from the density map using deterministic seeded random placement.
- Store the generated instances in an SSBO for rendering.
- **Pros:** Best of both worlds -- compact storage, exact rendering, brush-friendly editing.
- **Cons:** Regeneration cost when density map changes (mitigated by only regenerating affected chunks).

### 2.3 Brush Implementation Details

A foliage brush needs:
- **Radius:** Size of the brush sphere/circle on terrain.
- **Strength/Density:** How many instances per unit area are placed per stroke.
- **Falloff:** Typically a smooth falloff from center (1.0) to edge (0.0), e.g., `smoothstep(outerRadius, innerRadius, distance)`.
- **Raycast:** Each frame while painting, cast a ray from the mouse cursor through the camera to find the terrain hit point. The hit point is the brush center.
- **Surface normal:** Instances can optionally align to the surface normal at their placement point for natural appearance on slopes.
- **Type selection:** UI for selecting which foliage type(s) to paint.
- **Randomization:** Per-instance random rotation (Y-axis), scale variation (e.g., 0.8 to 1.2), and slight position jitter within each density cell.

**Sources:**
- [Unreal Engine 4.27 - Foliage Tool Documentation](https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/Foliage)
- [Unreal Engine 5.7 - Foliage Mode](https://dev.epicgames.com/documentation/en-us/unreal-engine/foliage-mode-in-unreal-engine)
- [Moving UE4's Foliage System to Unity - 80 Level](https://80.lv/articles/moving-ue4s-foliage-system-in-unity)
- [Foliage Optimization in Unity - Eastshade Studios](https://www.eastshade.com/foliage-optimization-in-unity/)
- [Terrain3D - Foliage Instancing Documentation](https://terrain3d.readthedocs.io/en/latest/docs/instancer.html)
- [Terrain3D Foliage Instancing Tracker - GitHub Issue #43](https://github.com/TokisanGames/Terrain3D/issues/43)
- [Godot In-game Splat Map Texture Painting](https://alfredbaudisch.com/blog/gamedev/godot-engine/godot-engine-in-game-splat-map-texture-painting-dirt-removal-effect/)
- [Foliage Landscape Optimization UE5 - Outscal](https://outscal.com/blog/landscape-and-foliage-optimization-unreal-engine-5)

---

## 3. Wind Animation for Vegetation

### 3.1 GPU Gems Chapter 7: Waving Grass Foundation

The foundational technique from NVIDIA GPU Gems (2004, still widely used) uses trigonometric functions in the vertex shader:

**Core formula:**
```
offset = windDirection * windStrength * sin(time * frequency + position.x * waveScale)
```

Key principles:
- **Only move top vertices:** Check the vertex's V texture coordinate or vertex color. Vertices at the base (ground level) have zero displacement; vertices at the tip get full displacement. Interpolate linearly with height.
- **Position-based phase offset:** Use the instance's world position (e.g., `position.x + position.z`) as a phase offset in the sine function. This creates a rippling wave effect across the field rather than all blades swaying in unison.
- **Three animation levels:**
  1. **Cluster animation:** Groups of nearby blades share the same offset (cheapest).
  2. **Per-object animation:** Each blade gets its own offset based on position.
  3. **Per-vertex animation:** Each vertex gets a unique offset (most expensive, most realistic).

### 3.2 Crysis Vegetation System (GPU Gems 3, Chapter 16)

The Crysis system introduced a layered wind animation model that is still the industry standard:

#### Layer 1: Main Bending
- Applies to the entire plant/blade as a whole.
- Wind direction and speed are global uniforms. The bend amount increases with vertex height.
- A simple directional force that bows the entire plant in the wind direction.

#### Layer 2: Detail Bending (Leaves/Tips)
- Uses per-vertex color channels painted by artists:
  - **Red channel:** Stiffness of leaf edges.
  - **Green channel:** Per-leaf phase variation (prevents synchronous movement).
  - **Blue channel:** Overall leaf stiffness.
  - **Alpha channel:** Precomputed ambient occlusion.
- For grass (no artist painting), these can be generated procedurally based on vertex height and position.

#### Layer 3: Edge Flutter
- High-frequency, low-amplitude oscillation on leaf/blade edges.
- Adds visual complexity cheaply.

**Key insight from Crysis:** For grass specifically, all shading was done per-vertex (not per-pixel) due to grass's heavy fill-rate requirements. This is an important performance consideration.

### 3.3 Wind Field Textures (Modern Approach)

A world-space 2D "wind texture" provides spatially varying wind:
- A scrolling 2D Perlin noise texture (e.g., 256x256) represents wind intensity across the terrain.
- The vertex shader samples this texture using the instance's XZ world position.
- The texture scrolls over time in the wind direction, creating natural gusting patterns.
- **Cost:** One texture sample per vertex in the vertex shader. Negligible.

**Implementation:**
```glsl
// In vertex shader
vec2 windUV = worldPos.xz * windTextureScale + windDirection.xz * time * windScrollSpeed;
float windStrength = texture(windTexture, windUV).r;
vec3 displacement = windDirection * windStrength * vertexHeight * windAmplitude;
```

### 3.4 Interactive Wind (Player/Object Influence)

For grass that reacts to the player walking through it:
- Maintain a small render target (e.g., 64x64) centered on the player representing a "bend map."
- Objects that should bend grass write their influence to this texture.
- The vertex shader samples the bend map and applies additional displacement.
- **Cost:** Minimal if the bend map is small and updated only when needed.

### 3.5 Performance Considerations

- **Sine/cosine in vertex shader:** Modern GPUs compute trig functions in ~4 cycles. At 100k instances x ~8 vertices = 800k vertex shader invocations, this is negligible.
- **Wind texture sampling:** One texture fetch per vertex. Negligible bandwidth.
- **Perlin noise alternative:** If not using a wind texture, procedural noise (e.g., simplex noise) can be computed in the vertex shader, but a pre-baked scrolling texture is cheaper.
- **All wind animation is purely visual** -- no physics simulation, no collision detection, no CPU involvement.

**Sources:**
- [GPU Gems Ch. 7 - Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)
- [GPU Gems 3 Ch. 16 - Vegetation Procedural Animation and Shading in Crysis](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis)
- [GPU Gems 3 Ch. 6 - GPU-Generated Procedural Wind Animations for Trees](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees)
- [Wind Animations for Vegetation - IceFall Games](https://mtnphil.wordpress.com/2011/10/18/wind-animations-for-vegetation/)
- [GameDev.net - Good Formula for Swaying Grass](https://www.gamedev.net/forums/topic/686315-good-formula-for-swaying-grass/)
- [Harry Alisavakis - Grass Shader (Part I)](https://halisavakis.com/my-take-on-shaders-grass-shader-part-i/)
- [damdoy/opengl_examples - Grass with Perlin Noise Wind](https://github.com/damdoy/opengl_examples)

---

## 4. LOD and Culling for Vegetation

### 4.1 Frustum Culling Strategy: Chunk-Based, Not Per-Instance

The universal recommendation across all engines and references is: **never frustum-cull individual grass instances.** Instead, use chunk-based spatial partitioning:

1. **Divide the world into a grid of chunks** (e.g., 16x16m or 32x32m cells).
2. **Each chunk has a bounding box** (AABB).
3. **Frustum test each chunk's AABB** against the camera frustum. If the chunk is outside, skip all its instances entirely.
4. **Only chunks that pass the frustum test** submit their instances for rendering.

This reduces frustum tests from 100,000 (per instance) to perhaps 50-200 (per chunk). With hierarchical spatial partitioning (quadtree), you can sometimes reject 50% of the scene with a single bounding volume check.

### 4.2 GPU-Based Frustum Culling (Compute Shader)

For maximum scalability, move frustum culling to the GPU:

1. **All instance data** lives in an SSBO (input buffer).
2. **A compute shader** runs one thread per instance (or per chunk). Each thread tests the instance/chunk position against the frustum planes.
3. **Visible instances** are appended to an output SSBO and an atomic counter tracks how many passed.
4. **An indirect draw command buffer** is filled with the visible count.
5. **`glDrawArraysIndirect`** renders only visible instances with zero CPU involvement.

This approach was previously done with geometry shaders and transform feedback (for 1M+ vegetation instances), but compute shaders are faster and more flexible.

**OpenGL 4.5 pipeline:**
```
SSBO (all instances) -> Compute Shader (cull) -> SSBO (visible instances) + Indirect Command Buffer -> glDrawArraysIndirect
```

### 4.3 Distance-Based LOD for Foliage

Multiple LOD tiers based on camera distance:

| Distance | Technique | Tri Count | Notes |
|----------|-----------|-----------|-------|
| 0-20m | Instanced mesh blades | 6-14 tris/blade | Full detail, per-vertex wind |
| 20-60m | Cross-billboard (2 quads) | 4 tris/instance | Texture-based, still has wind |
| 60-120m | Single billboard | 2 tris/instance | Camera-facing, simplified wind |
| 120m+ | Density fade to nothing | 0 | Gradually reduce instance count |

**Density fade:** Rather than a hard cutoff, gradually reduce the number of rendered instances with distance. For example, at 80m render 100% of instances, at 100m render 50%, at 120m render 10%, at 140m render 0%. Implemented by skipping instances based on a hash of their position vs. distance threshold.

**Smooth LOD transitions:** The AMD GPUOpen grass renderer uses fractional blade scaling -- the last blade in a patch has its width scaled by the fractional LOD factor to hide popping transitions.

### 4.4 Chunk Data Structure

```cpp
struct FoliageChunk
{
    AABB          boundingBox;          // For frustum culling
    uint32_t      instanceOffset;       // Offset into global instance SSBO
    uint32_t      instanceCount;        // Number of instances in this chunk
    uint32_t      visibleCount;         // Set by compute shader after culling
    float         distanceToCamera;     // For LOD selection
    bool          isDirty;              // Needs instance regeneration
};
```

A flat grid (e.g., `std::vector<FoliageChunk>`) indexed by `(chunkX, chunkZ)` is simple and cache-friendly. A quadtree is beneficial only if chunks are very sparse.

### 4.5 Performance Budget

For 100k instances on RX 6600:
- **CPU frustum culling (chunk-based):** ~200 AABB-frustum tests = microseconds. Negligible.
- **GPU compute culling:** A compute shader dispatched with 100k/256 = ~391 work groups. On RX 6600 with 1792 shader cores, this completes in well under 0.1ms.
- **Draw calls:** 1 per foliage type per LOD tier. With 5 types x 3 LOD tiers = 15 draw calls max. Trivial.
- **The real bottleneck is fill rate** (overdraw from transparent grass quads), not vertex processing or draw call overhead.

**Sources:**
- [GameDev.net - Instanced Drawing and Frustum Cull (multiple threads)](https://gamedev.net/forums/topic/686024-instanced-drawing-and-frustum-cull/5330666/)
- [Spatial Partitioning and Frustum Culling - Chetan Jags](https://chetanjags.wordpress.com/tag/frustum-culling/)
- [Rendering 1 Million Spheres: Frustum Culling & LOD](https://dk1242.github.io/2025/05/27/Rendering-1M-spheres-4-Frustum-Culling-and-LOD.html)
- [DiceWrench Designs - Foliage Renderer Documentation](https://dicewrenchdesigns.com/2024/02/foliage-renderer/)
- [Foliage Optimization in Unity - Eastshade Studios](https://www.eastshade.com/foliage-optimization-in-unity/)
- [GPU Instancer Features - GurBu Wiki](https://wiki.gurbu.com/index.php?title=GPU_Instancer:Features)
- [J Stephano - Multi-Draw Indirect (MDI)](https://ktstephano.github.io/rendering/opengl/mdi)
- [CPP Rendering - Indirect Rendering](https://cpp-rendering.io/indirect-rendering/)
- [Lingtorp - OpenGL SSBO Indirect Drawing](https://lingtorp.com/2018/12/05/OpenGL-SSBO-indirect-drawing.html)
- [Khronos Forums - Culling with a Compute Shader](https://community.khronos.org/t/culling-with-a-compute-shader/110684)
- [RasterGrid - Instance Culling Using Geometry Shaders](https://www.rastergrid.com/blog/2010/02/instance-culling-using-geometry-shaders/)
- [NVIDIA - gl_occlusion_culling Sample](https://github.com/nvpro-samples/gl_occlusion_culling)

---

## 5. Scatter/Placement Systems

### 5.1 Placement Algorithms

#### A. Jittered Grid
- Divide the surface into a regular grid. Place one instance per cell with random offset within the cell.
- **Pros:** Simple, O(1) per instance, guarantees minimum spacing (cell size), easy density control (skip cells based on density map).
- **Cons:** Can look slightly regular at low densities.
- **Implementation:** `position = cellCenter + random(-cellSize/2, cellSize/2)`

#### B. Poisson Disk Sampling
- Place points such that no two points are closer than a minimum distance `r`.
- Bridson's fast algorithm runs in O(n) time.
- **Pros:** Natural-looking distribution, no clustering or regularity. The gold standard for natural scatter.
- **Cons:** More complex than jittered grid. Harder to control exact density.
- **Implementation:** Start with a seed point, generate candidates around it at distance [r, 2r], reject if too close to existing points. Use a background grid for O(1) neighbor lookups.

#### C. Quasi-Monte Carlo (QMC) Sampling
- A recent research approach (2025) uses low-discrepancy sequences for GPU-based foliage placement on arbitrary surfaces.
- Compute shaders handle parallel placement and evaluation -- no spatial validation structures or CPU involvement needed.
- Can place 400k+ leaves in ~2 seconds on a mid-range GPU.
- **Pros:** Fully GPU-driven, deterministic, reproducible, scalable.
- **Cons:** Research-stage, more complex to implement.

#### D. Density Map Driven (Recommended for Brush Painting)
- Use the density map from Section 2 as the driver.
- For each grid cell in the density map, the density value determines how many instances to place in that cell.
- Within each cell, use jittered sub-grid or Poisson disk for natural distribution.
- **Deterministic seeding:** Use `hash(cellX, cellZ, instanceIndex)` as the random seed so the same density map always produces the same instance positions.

### 5.2 Surface Normal Alignment

Instances should align to the terrain surface normal for natural appearance on slopes:
- **Full alignment:** Instance Y-axis = surface normal. Grass stands perpendicular to the slope.
- **Partial alignment:** Blend between world up and surface normal. e.g., `alignedUp = mix(worldUp, surfaceNormal, 0.7)`. Prevents grass from looking too tilted on steep slopes.
- **Random yaw:** Apply random Y-axis rotation for variety. Combine with normal alignment.
- **Slope limits:** Optionally skip placement on surfaces steeper than a threshold (e.g., >60 degrees from horizontal). Grass shouldn't grow on cliff faces.

Implementation: When placing an instance, raycast downward to find the surface position and normal. Construct a rotation matrix that aligns the instance's up vector with the surface normal while applying random yaw rotation.

### 5.3 Spacing Rules and Collision Radius

Professional foliage tools use per-type radius settings:
- **Collision radius:** Minimum distance between instances of the same type. Prevents overlapping meshes.
- **Shade radius:** A larger radius that reduces density of other types nearby (e.g., a bush suppresses grass around it).
- **Cluster variation:** Add noise to spacing for organic feel. Perfectly spaced instances look artificial.

### 5.4 Eraser Tool

The eraser is a spatial query:
1. Determine brush center (raycast from cursor to terrain).
2. Query all instances within brush radius. Requires a spatial index.
3. Remove a fraction of found instances based on erase strength.
4. Update the density map (if using hybrid approach) to reflect removal.
5. Mark affected chunks as dirty for instance regeneration.

**Spatial index options:**
- **Flat grid (recommended):** O(1) lookup by cell. Same grid as the chunk system. Simple, cache-friendly.
- **Quadtree:** Better for very sparse or very uneven distributions. More complex.
- **Spatial hash:** Good for dynamic scenes where instances move. Overkill for static foliage.

### 5.5 Placement Modes from Professional Engines

Unreal Engine's Procedural Foliage Tool supports these spawning methods:
- Random Location
- Organic Clusters
- Clusters around actors/points
- Along spline actors
- Random patches
- Grid Distribution
- Spawn on specific zones
- Hierarchical Clusters

All methods respect slope limits, height restrictions, and obstacle avoidance.

For Vestige, starting with **Random Location** (basic jittered grid within brush radius) and **Organic Clusters** (Poisson disk with density variation) covers 90% of use cases.

**Sources:**
- [Poisson Disk Sampling - Dev.Mag](http://devmag.org.za/2009/05/03/poisson-disk-sampling/)
- [Real-Time GPU Foliage Instancing with QMC Sampling (2025)](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=5295246)
- [Unreal Engine - Procedural Foliage Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-foliage-tool-in-unreal-engine)
- [Building Worlds Faster with Procedural Scattering in UE5 - Jettelly](https://jettelly.com/blog/building-worlds-faster-with-procedural-scattering-in-unreal-engine-5)
- [Unity Landscaper Tool - Procedural Foliage Placement](https://forum.unity.com/threads/landscaper-a-tool-for-procedural-foliage-placement.322883/)
- [Unreal Engine - Foliage Tool Documentation](https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/Foliage)

---

## 6. Spline-Based Path/Road Tools

### 6.1 Catmull-Rom vs Bezier Splines

| Feature | Catmull-Rom | Cubic Bezier |
|---------|-------------|--------------|
| Control points on curve | Yes (all points lie on the curve) | No (only endpoints; control points are off-curve) |
| Intuitive editing | Click to place points; curve passes through them | Requires placing off-curve control handles |
| C1 continuity | Automatic between segments | Must manually match tangents |
| Local control | Changing one point affects 4 nearby segments | Changing one point affects 1 segment |
| Conversion | Can convert to Bezier for rendering | - |

**Recommendation: Centripetal Catmull-Rom** for the path editing tool. It is the most intuitive for users (click to place points, curve passes through all of them), has automatic C1 continuity, and avoids loops/cusps/self-intersections that plague uniform Catmull-Rom.

### 6.2 Centripetal Catmull-Rom Implementation

The centripetal variant parameterizes knot intervals using `alpha = 0.5`:

```
t_i+1 = t_i + |P_i+1 - P_i|^alpha
```

Where `alpha = 0.0` = uniform, `alpha = 0.5` = centripetal, `alpha = 1.0` = chordal.

Centripetal is preferred because:
- No cusps within curve segments.
- No self-intersections within curve segments.
- Tighter curves follow the control points more closely.

For a segment between points P1 and P2 (with neighboring points P0 and P3), the interpolation at parameter t is:

```cpp
glm::vec3 catmullRom(glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3, float t)
{
    float t01 = glm::pow(glm::distance(P0, P1), 0.5f);
    float t12 = glm::pow(glm::distance(P1, P2), 0.5f);
    float t23 = glm::pow(glm::distance(P2, P3), 0.5f);

    glm::vec3 m1 = (P2 - P0) / (t01 + t12) - (P2 - P1) / t12 + (P1 - P0) / t01;  // Adjusted
    glm::vec3 m2 = (P3 - P1) / (t12 + t23) - (P3 - P2) / t23 + (P2 - P1) / t12;

    // Hermite basis with tangents m1, m2 between P1 and P2
    // ... standard Hermite interpolation
}
```

### 6.3 Road/Path Mesh Generation

Given a spline, generate a road mesh:

1. **Sample the spline** at regular intervals along arc length (not parameter space) to get evenly spaced points.
2. **At each point**, compute:
   - **Tangent:** Direction along the spline (forward).
   - **Normal:** Terrain surface normal at this point (or world up if flat).
   - **Binormal:** Cross product of tangent and normal (left/right direction).
3. **Extrude** the cross-section: Place vertices at `point +/- binormal * halfWidth`.
4. **Connect** adjacent cross-sections into triangle strips.
5. **UV mapping:** U = 0 at left edge, 1 at right edge. V = accumulated arc length / texture tile size.

**Width variation:** Allow width to change along the spline via per-control-point width values, interpolated with the same Catmull-Rom math.

### 6.4 Texture Blending at Path Edges

Where the path meets terrain, blend textures for a natural transition:
- Use a **splatmap/blend mask** -- a texture that defines where the path texture fades into terrain texture.
- Render the path mesh with an alpha gradient at the edges (1.0 at center, 0.0 at edge).
- In the terrain fragment shader, sample the path blend texture and use it to mix between terrain and path materials.
- Alternative: Render a slightly wider "border mesh" with soft alpha that overlays the terrain.

### 6.5 Automatic Foliage Clearing Along Paths

When a path is placed, foliage should be automatically removed from the path area:

**Approach 1: Exclusion zone in density map**
- When the spline changes, rasterize the spline's footprint (with width + margin) into the density map as zero-density zones.
- Foliage regeneration for affected chunks will automatically exclude the path area.

**Approach 2: Runtime distance check**
- During foliage instance generation, check each candidate position against all spline segments.
- Reject instances within `pathWidth + margin` distance from the nearest spline point.
- More flexible but slower for many splines.

**Recommendation:** Approach 1 (density map rasterization) is cleaner and faster. It integrates naturally with the brush painting system.

### 6.6 Terrain Projection

Paths should conform to terrain height:
- When sampling spline points, project each point down to terrain height: `point.y = terrainHeight(point.x, point.z)`.
- Optionally add a small offset (e.g., 0.02m) above terrain to prevent z-fighting.
- For smooth paths on bumpy terrain, use more sample points and average heights.

**Sources:**
- [Catmull-Rom Splines in Game Development - Nudgie Dev Diary](https://andrewhungblog.wordpress.com/2017/03/03/catmull-rom-splines-in-plain-english/)
- [Centripetal Catmull-Rom Spline - Wikipedia](https://en.wikipedia.org/wiki/Centripetal_Catmull%E2%80%93Rom_spline)
- [Smooth Paths Using Catmull-Rom Splines - Mika's Coding Bits](https://qroph.github.io/2018/07/30/smooth-paths-using-catmull-rom-splines.html)
- [Everything About Interpolation in Unity - Habrador](https://www.habrador.com/tutorials/interpolation/1-catmull-rom-splines/)
- [Unity Road Generator (Catmull-Rom) - GitHub](https://github.com/Alan-Baylis/Unity-Road-Generator)
- [GameDev.net - Tips for Programming a Road Building Tool](https://www.gamedev.net/forums/topic/629976-tips-for-programming-a-road-building-tool/4971786/)
- [Advanced Terrain Texture Splatting - GameDeveloper.com](https://www.gamedeveloper.com/programming/advanced-terrain-texture-splatting)
- [Spline-Based Procedural Terrain Generation - Jarne Peire](https://jarnepeire.be/splinebasedprocterraingen/)

---

## 7. Open-Source References

### 7.1 OpenGL/C++ Grass Rendering Projects

| Project | Language | Technique | Instances | Key Features |
|---------|----------|-----------|-----------|--------------|
| [GLGrassRenderer](https://github.com/LesleyLai/GLGrassRenderer) | C++/OpenGL | Bezier curves + compute + tessellation | Thousands | Compute shader culling, indirect draw, force simulation (wind/gravity/restoration), frustum & distance culling. Based on "Responsive Real-Time Grass Rendering for General 3D Scenes" paper. |
| [RealTimeGrassRendering](https://github.com/DeveloperDenis/Real-Time-Grass) | C/C++/OpenGL | Geometry-based instancing | Configurable | Grass blades generated once, instancing draws patches multiple times. Simple but educational. |
| [damdoy/opengl_examples (grass)](https://github.com/damdoy/opengl_examples) | C++/OpenGL | Geometry shader + instancing | 100k | 100k grass elements, billboard approach (2 intersecting planes), Perlin noise wind. Closest to Vestige's target. |
| [OpenGL-Landscape](https://github.com/jackw-ai/OpenGL-Landscape) | C++/OpenGL | Mesh-based | - | Trees, leaves, grass under skybox. Extensible framework with texture/shader/mesh infrastructure. |
| [opengl_terrain](https://github.com/damdoy/opengl_terrain) | C++/OpenGL | Procedural | - | Procedural terrain with hills, grass, water, and trees. |

### 7.2 Unity/Vulkan References (Architecture Study)

| Project | Technique | Notable Features |
|---------|-----------|-----------------|
| [Unity-Grass-Instancer](https://github.com/MangoButtermilch/Unity-Grass-Instancer) | GPU instancing | Multiple approaches, frustum + occlusion culling comparison |
| [Indirect-Rendering-With-Compute-Shaders](https://github.com/ellioman/Indirect-Rendering-With-Compute-Shaders) | Compute + indirect draw | Frustum culling, occlusion culling, LOD, `DrawMeshInstancedIndirect` |
| [Vulkan Foliage Rendering](https://thegeeko.me/blog/foliage-rendering/) | Vulkan + GPU instancing | Spatial hash architecture, chunk-based culling |

### 7.3 Key Articles and Tutorials

| Resource | Focus |
|----------|-------|
| [LearnOpenGL - Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing) | OpenGL instancing fundamentals, 100k asteroid demo |
| [J Stephano - SSBOs](https://ktstephano.github.io/rendering/opengl/ssbos) | SSBO usage for per-instance data |
| [J Stephano - Multi-Draw Indirect](https://ktstephano.github.io/rendering/opengl/mdi) | MDI tutorial for OpenGL 4.x |
| [GPU Gems Ch. 7](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass) | Grass rendering and wind animation |
| [GPU Gems 3 Ch. 16](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis) | Crysis vegetation animation system |
| [AMD GPUOpen - Procedural Grass](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/) | Mesh shader grass (future Vulkan reference) |
| [CPP Rendering - Indirect Rendering](https://cpp-rendering.io/indirect-rendering/) | Compute culling + indirect draw workflow |
| [Geeks3D - SSBO Introduction](https://www.geeks3d.com/20140704/tutorial-introduction-to-opengl-4-3-shader-storage-buffers-objects-ssbo-demo/) | SSBO fundamentals for OpenGL 4.3+ |
| [GPU Gems Source Code Collection](https://github.com/QianMo/GPU-Gems-Book-Source-Code) | Complete source code for GPU Gems 1-3 |

### 7.4 Research Papers

| Paper | Relevance |
|-------|-----------|
| "Responsive Real-Time Grass Rendering for General 3D Scenes" | Bezier blade representation, compute shader simulation, tessellation rendering. Basis of GLGrassRenderer. |
| "Simulation and Rendering for Millions of Grass Blades" (i3D 2015) | Scalability techniques for massive grass counts. |
| "Real-Time GPU Foliage Instancing on Arbitrary Surfaces Using QMC Sampling" (2025) | Modern GPU-only placement with compute shaders. No CPU involvement. |

---

## 8. Recommended Approach for Vestige

### 8.1 Architecture Summary

Based on this research, the recommended architecture for Vestige's foliage system:

```
┌─────────────────────────────────────────────────────────────┐
│                    EDITOR (ImGui)                            │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐  │
│  │ Brush    │ │ Foliage  │ │ Eraser   │ │ Spline Path   │  │
│  │ Painter  │ │ Type Sel │ │ Tool     │ │ Tool          │  │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └──────┬────────┘  │
│       │            │            │               │           │
│       v            v            v               v           │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Density Map (R8 Texture per type)       │   │
│  │         + Exclusion Zones (from spline paths)        │   │
│  └────────────────────────┬─────────────────────────────┘   │
└───────────────────────────┼─────────────────────────────────┘
                            │ (on change: regenerate affected chunks)
                            v
┌──────────────────────────────────────────────────────────────┐
│                 FOLIAGE MANAGER (CPU)                         │
│  ┌─────────────┐  ┌─────────────────┐  ┌─────────────────┐  │
│  │ Chunk Grid  │  │ Instance Gen    │  │ Dirty Chunk     │  │
│  │ (16x16m)    │  │ (from density)  │  │ Tracker         │  │
│  └──────┬──────┘  └────────┬────────┘  └────────┬────────┘  │
│         │                  │                     │           │
│         v                  v                     v           │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            Instance SSBO (GPU buffer)                 │   │
│  │    {position, rotation, scale, type} x N              │   │
│  └────────────────────────┬─────────────────────────────┘   │
└───────────────────────────┼──────────────────────────────────┘
                            │
                            v
┌──────────────────────────────────────────────────────────────┐
│                   RENDERER (GPU)                             │
│                                                              │
│  Phase 1: Compute Shader Culling (optional, Phase 2 opt)    │
│    - Frustum cull per chunk (or per instance)                │
│    - Distance LOD selection                                  │
│    - Write visible instances + indirect draw command          │
│                                                              │
│  Phase 2: Instanced Draw                                     │
│    - glDrawArraysInstanced (or glDrawArraysIndirect)         │
│    - Vertex shader: apply instance transform + wind anim     │
│    - Fragment shader: texture atlas + alpha test              │
│                                                              │
│  Assets: Cross-billboard mesh (2 quads, 8 verts)             │
│          Foliage texture atlas (grass, flowers, ferns)        │
│          Wind noise texture (scrolling Perlin 256x256)        │
└──────────────────────────────────────────────────────────────┘
```

### 8.2 Phased Implementation Plan

#### Phase 1: Core Instanced Rendering (MVP)
- Cross-billboard mesh (2 intersecting quads) shared geometry.
- Per-instance data in SSBO: position, rotation, scale, type index.
- `glDrawArraysInstanced` with `gl_InstanceID` indexing into SSBO.
- Foliage texture atlas (grass, flowers).
- Alpha test (`discard`) in fragment shader.
- Basic CPU frustum culling per chunk (16x16m grid).
- Simple wind: `sin(time + position.x) * height` in vertex shader.
- Target: 100k instances at 60 FPS.

#### Phase 2: Brush Painting System
- Density map (R8 texture per foliage type, or RGBA for 4 types).
- ImGui brush tool: radius, density, falloff, foliage type selector.
- Raycast from cursor to terrain for brush center.
- Paint into density map, regenerate instances for affected chunks.
- Eraser tool: paint zero-density into density map.
- Serialization: save/load density maps alongside scene.

#### Phase 3: Wind and Visual Polish
- Scrolling Perlin noise wind texture.
- Multi-layered wind animation (main bend + detail flutter).
- Per-vertex color data for stiffness (generated procedurally).
- Alpha-to-coverage for smooth edges (when MSAA enabled).
- Color variation per instance (slight hue/brightness shifts).

#### Phase 4: LOD and Optimization
- Distance-based LOD tiers (mesh -> cross-billboard -> single billboard -> fade).
- Density fade at distance.
- GPU compute shader culling + indirect draw.
- Chunk-level occlusion awareness (optional, if needed for performance).

#### Phase 5: Spline Path Tool
- Centripetal Catmull-Rom spline editor in ImGui.
- Path mesh generation (extrusion along spline with width).
- Terrain-projected paths with texture blending.
- Automatic foliage exclusion zones (rasterize spline footprint into density map).

### 8.3 Performance Budget Estimate (RX 6600)

| Component | Cost Estimate |
|-----------|---------------|
| Instance SSBO upload (100k x 24 bytes) | 2.4 MB, <0.1ms |
| CPU chunk frustum culling (~200 chunks) | <0.01ms |
| Vertex shader (800k vertices with wind) | ~0.2ms |
| Fragment shader (alpha test, texture atlas) | ~0.5-1.0ms (fill-rate dependent) |
| Wind texture sampling | Negligible |
| **Total estimated GPU cost** | **~1-2ms** (well within 16.6ms budget) |

The RX 6600 has 8GB GDDR6 and 1792 shader cores. 100k instances of cross-billboards is a modest workload for this GPU. The main risk is overdraw from overlapping transparent quads, which alpha test + early-z mitigates effectively.

### 8.4 Key Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Grass geometry | Cross-billboard (2 quads) | Best quality-to-cost ratio; looks good from all angles |
| Instance storage | SSBO | Flexible, no size limits, compute shader compatible |
| Culling | CPU chunk-based (Phase 1), GPU compute (Phase 4) | Start simple, optimize later |
| Wind | Scrolling Perlin noise texture | Cheap, natural-looking, spatially varying |
| Density storage | R8 texture per type | Compact, GPU-friendly, brush-paintable |
| Instance generation | CPU from density map, seeded RNG | Deterministic, easy to serialize |
| Alpha | discard + alpha-to-coverage | Order-independent, no sorting needed |
| Spline type | Centripetal Catmull-Rom | Points on curve, no cusps, intuitive editing |
| Spatial partitioning | Flat chunk grid (16x16m) | Simple, cache-friendly, sufficient for Vestige's scale |

---

*This research document should be reviewed and discussed before implementation begins. It will inform the design document for the foliage/vegetation phase.*
