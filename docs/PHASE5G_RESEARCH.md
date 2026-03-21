# Phase 5G: Environment Painting — Research

This document summarizes research across game engines, graphics programming resources, and open-source projects to inform the design of Phase 5G.

---

## 1. Foliage / Grass Rendering

### Approaches Compared

| Technique | Draw calls | GPU cost | Visual quality | Complexity |
|-----------|-----------|----------|----------------|------------|
| Instanced billboards (`glDrawArraysInstanced`) | 1 per grass type | Low (simple quad) | Medium (flat, no depth) | Low |
| Instanced mesh (3-quad star) | 1 per grass type | Low-medium | Good (parallax from multiple planes) | Low |
| Geometry shader expansion | 1 total | Medium (GS bottleneck on AMD) | Medium | Medium |
| Compute shader + indirect draw | 1 total | Lowest (GPU culling) | Best | High |

**Recommendation: Instanced 3-quad star mesh.** Each grass blade is 3 intersecting quads (6 triangles) forming a star pattern when viewed from above. This gives parallax from all viewing angles, unlike a single billboard that looks flat from above. The particle renderer already demonstrates the instanced drawing pattern — we reuse the same approach with a different base mesh.

**Why not geometry shader?** AMD RDNA2 drivers (our target GPU) have known geometry shader performance issues. The GS stage is emulated on AMD hardware and can be 2-5x slower than equivalent instanced draws. Sources: AMD GPUOpen documentation, community benchmarks on r/opengl and gamedev forums.

**Why not compute + indirect?** Best performance at extreme scale (1M+ instances), but adds significant complexity (compute shaders, indirect draw buffers, GPU-side frustum culling). We keep this as a future upgrade path — start simple with CPU culling + instanced draw.

### Instance Data Layout

Per-instance data for grass (packed into a single VBO):

```
struct FoliageInstance {
    vec3 position;     // 12 bytes — world position
    float rotation;    //  4 bytes — Y-axis rotation (radians)
    float scale;       //  4 bytes — uniform scale multiplier
    vec3 color_tint;   // 12 bytes — RGB tint variation
};
// Total: 32 bytes per instance
```

At 100K instances: 3.2 MB of GPU memory — well within budget.

### Density Maps

**How other engines do it:**
- **Unity Terrain:** Stores a 2D density texture per foliage type (R8 format). The terrain brush paints into this texture. At runtime, instances are generated from non-zero texels within the camera frustum.
- **Unreal Landscape:** Uses a hierarchical instanced static mesh (HISM) component. Foliage is stored as explicit instance transforms in an octree structure. The brush adds/removes transforms directly.
- **Godot Terrain3D:** MultiMesh3D with per-32m-chunk instance lists. Brush writes instance transforms to chunk data files.

**Our approach:** Per-chunk instance list (like Unreal/Godot). The world is divided into a uniform grid of **FoliageChunks** (16m x 16m). Each chunk stores a `std::vector<FoliageInstance>` per foliage type. The brush tool adds/removes instances from chunk vectors. This is simpler than a density texture approach (no runtime generation step) and integrates naturally with frustum culling (cull entire chunks).

**Why not density textures?** Density textures require a generation pass each frame to convert texel values into instance positions. This adds latency and complexity. Explicit instance lists are immediate — what's stored is what's rendered. They also serialize trivially (array of structs → JSON array).

### Wind Animation

**GPU Gems Chapter 7 (NVIDIA):** Classic grass wind technique — vertex shader displaces grass blade tips using a combination of:
1. **Global wind direction** (uniform vec3)
2. **Per-vertex sway** using `sin(time * frequency + position.x * spatial_freq)`
3. **Amplitude modulated by vertex Y** (tips sway most, base stays fixed)

**Crysis vegetation (GPU Gems 3, Ch. 16):** Multi-layer wind:
- **Main bending:** Large-scale sway of entire plant (slow, wind-driven)
- **Detail bending:** Per-leaf flutter (fast, noise-driven)
- **Formula:** `displacement = windDir * sin(time + hash(position)) * amplitude * vertexHeight`

**Our approach:** Single-layer vertex shader wind. Grass is short — only needs tip displacement, not branch-level detail:

```glsl
// In vertex shader:
float windPhase = u_time * u_windFrequency + worldPos.x * 0.5 + worldPos.z * 0.3;
float windOffset = sin(windPhase) * u_windAmplitude * localPos.y; // localPos.y = 0 at base, 1 at tip
worldPos.xz += u_windDirection.xz * windOffset;
```

This costs nearly nothing — one sin() per vertex — and gives convincing grass sway.

### Sources
- GPU Gems Ch. 7: "Rendering Countless Blades of Waving Grass" (NVIDIA)
- GPU Gems 3 Ch. 16: "Vegetation Procedural Animation and Shading in Crysis" (Crytek)
- AMD GPUOpen: Geometry shader performance notes for RDNA2
- Unity Terrain documentation: Foliage painting system
- Unreal Engine documentation: Foliage tool and HISM
- Godot Terrain3D docs: Foliage instancing

---

## 2. Scatter Systems (Rocks, Debris)

### How Engines Handle Scatter Placement

**Unity Terrain:** The foliage tool handles both grass and "tree" (arbitrary mesh) placement. Meshes are instanced with random rotation/scale within configurable ranges. Minimum spacing is enforced by checking distance to existing nearby instances.

**Unreal Foliage Tool:** Painting spawns StaticMesh instances into a Hierarchical Instanced Static Mesh (HISM). Each instance stores a full 4x4 transform. The tool supports:
- Density (instances per square meter)
- Scale randomization (min/max per axis)
- Random yaw rotation
- Surface normal alignment (configurable blend between world-up and surface normal)
- Collision preset selection
- Minimum spacing radius

**Our approach:** Same FoliageChunk system as grass, but with additional per-instance data:

```
struct ScatterInstance {
    vec3 position;        // World position
    quat rotation;        // Full rotation (for normal alignment)
    float scale;          // Uniform scale
    uint8_t meshIndex;    // Which mesh in the scatter palette
};
```

Scatter objects are rendered using the existing instanced rendering pipeline (InstanceBuffer + buildInstanceBatches). They're regular meshes, not billboards — the existing scene shader handles them.

### Surface Normal Alignment

When placing rocks/debris on slopes, objects should sit flush with the surface:
1. Raycast from brush center downward to find the surface hit point and normal
2. Build a rotation quaternion that aligns object's up-vector to the surface normal
3. Add random rotation around the aligned up-axis for natural variation
4. Blend between world-up and surface normal based on a "surface alignment" slider (0 = always upright, 1 = fully aligned)

### Eraser Tool

Eraser removes instances within the brush radius. For each chunk overlapping the brush circle, iterate instances and remove those within radius. This is O(n) per chunk but chunks are small (16m) so n is bounded.

### Sources
- Unreal Engine 5 documentation: Foliage Tool reference
- Unity documentation: Terrain Tree/Detail system
- Godot documentation: MultiMesh and placement tools

---

## 3. Tree Rendering and LOD

### LOD Strategies for Trees

| LOD Level | Distance | Technique | Triangle count |
|-----------|----------|-----------|----------------|
| LOD0 | 0-30m | Full mesh | 2K-10K |
| LOD1 | 30-60m | Simplified mesh (50% reduction) | 1K-5K |
| LOD2 | 60-150m | Billboard cross (2 quads) | 8 |
| LOD3 | 150m+ | Single billboard | 4 |

**Billboard cross:** Two perpendicular quads with pre-rendered tree images from orthogonal angles. Cheap to render, looks acceptable at distance. At close range, the intersection line is visible — hence the transition distance.

**Impostor rendering (advanced):** Render the tree from 8-16 angles into an atlas. At runtime, select the two closest angles and blend. This is higher quality than billboard cross but requires pre-rendering and more texture memory. **Defer to later.**

**Our approach for Phase 5G:** Start with **full mesh + single billboard**. Two LOD levels only:
- LOD0: Full mesh (within 50m)
- LOD1: Billboard (beyond 50m, with alpha fade transition)

This is the simplest LOD system that still prevents distant trees from destroying frame rate. Billboard generation can be automatic (render tree to texture at load time) or use pre-authored textures.

### Tree Species for Biblical Environments

Relevant species for the Temple complex and surrounding areas:
- **Olive tree** (Olea europaea): Gnarled trunk, silver-green foliage, broad crown. Iconic to the Mount of Olives.
- **Cedar of Lebanon** (Cedrus libani): Tall conifer, horizontal spreading branches, dark green. Used extensively in Temple construction.
- **Date palm** (Phoenix dactylifera): Tall single trunk, feather-like frond crown. Common throughout ancient Israel.
- **Acacia** (Acacia tortilis): Umbrella-shaped crown, thorny branches, sparse foliage. Desert regions, wood used in Tabernacle construction.

For Phase 5G, we provide **placeholder geometry** (simple trunk + sphere/cone crown with appropriate proportions) and establish the preset system. Detailed models are an art asset concern, not an engine concern.

### Sources
- Eastshade Studios: "Foliage Optimization in Unity" (draw call reduction, LOD)
- GPU Instancer (GurBu Technologies): GPU-driven LOD for vegetation
- Terrain3D documentation: Instanced foliage with LOD support

---

## 4. Path and Road Tools

### Spline Types

| Spline type | Continuity | Ease of editing | Computation |
|-------------|------------|-----------------|-------------|
| Catmull-Rom | C1 (smooth through points) | Easy — path passes through control points | Simple |
| Cubic Bezier | C0-C2 (configurable) | Harder — control points + handles | Simple |
| B-Spline | C2 (very smooth) | Harder — doesn't pass through points | Moderate |

**Recommendation: Catmull-Rom.** Paths pass directly through the user's clicked waypoints (intuitive). C1 continuity gives smooth curves. The formula is well-known and cheap to evaluate:

```
P(t) = 0.5 * ((2*P1) + (-P0 + P2)*t + (2*P0 - 5*P1 + 4*P2 - P3)*t² + (-P0 + 3*P1 - 3*P2 + P3)*t³)
```

where P0-P3 are four consecutive control points and t ∈ [0,1].

### Mesh Generation from Splines

1. Sample the spline at uniform parameter intervals (e.g., every 0.5m)
2. At each sample point, compute the tangent (spline derivative) and normal (cross product with up)
3. Generate left/right vertices at ±halfWidth along the normal
4. Connect consecutive sample pairs into quads (2 triangles each)
5. Generate UV coordinates: U along width (0-1), V along length (accumulated distance / texture tile size)

### Terrain Texture Blending

Paths need soft edges where they meet surrounding ground. Two approaches:
- **Decal projection:** Project path texture downward onto existing ground geometry using a decal shader. Blends automatically.
- **Splatmap modification:** The path tool modifies a terrain splatmap texture, increasing the "path material" channel along the path footprint with falloff at edges.

**Our approach:** Since we don't have a terrain system yet (Phase 15), paths will be **mesh-based decals** — thin mesh strips rendered slightly above the ground plane with alpha-blended edges.

### Foliage Clearing

When a path is placed, any foliage instances within the path footprint should be removed. Implementation:
1. For each FoliageChunk that the path AABB overlaps
2. For each instance in those chunks
3. Test if the instance position is within the path polygon (point-in-polygon test against the path's left/right edge polyline)
4. Remove instances that are inside

This is a one-time operation when the path is created or edited, not per-frame.

### Sources
- "A Survey of Spline Curves" — standard reference for Catmull-Rom, Bezier
- Unreal Engine: Spline Mesh Component documentation
- Unity: SplineContainer and road generation tutorials

---

## 5. Water Painting (Streams and Ponds)

### Stream/River Splines

Same Catmull-Rom spline as paths, but generating a WaterSurface mesh instead of a ground decal:
- Per-waypoint width and depth controls
- Flow direction derived from spline tangent
- Flow speed as a uniform passed to the water shader's UV scroll
- The existing `WaterSurfaceComponent` from Phase 5E provides the shader and rendering — streams just need a differently-shaped mesh

### Pool/Pond Tool

User clicks points to define a closed polygon. The polygon is triangulated (ear-clipping or Delaunay) to create a flat water surface mesh. The existing water renderer handles the rest.

### Bank Blending

Terrain textures should transition to wet sand/mud near water edges. Since we don't have terrain splatmaps yet, this is **deferred** to Phase 15 integration. For now, the water mesh sits on top of whatever surface is below it.

### Sources
- Unreal Water system documentation
- Unity Water system (HDRP)
- Phase 5E water surface implementation (existing in codebase)

---

## 6. Brush Tool UX

### How Professional Editors Handle Brushes

Common UX patterns across Unity, Unreal, and Blender:

**Brush parameters:**
- **Radius:** Mouse scroll wheel or bracket keys [ ] to resize
- **Strength/Density:** Ctrl+scroll or separate slider
- **Falloff:** Linear, smooth (cosine), or sharp — controls density tapering from center to edge
- **Preview:** A circle projected onto the surface under the cursor showing brush extent

**Brush preview overlay:**
- Project a circle decal onto the scene geometry at the mouse cursor position
- The circle should follow surface contours (raycast from camera through mouse, place circle at hit point)
- Color: semi-transparent green for paint, red for erase
- Shader: simple unlit circle with alpha falloff

**Paint interaction:**
1. Mouse button down → begin stroke
2. Each frame (or at spacing intervals), "stamp" the brush at the current cursor position
3. Stamp spacing prevents over-densification (e.g., only stamp if cursor moved > 0.5m since last stamp)
4. Mouse button up → end stroke, commit as a single undo operation

**ImGui controls for brush panel:**
- Brush type selector (foliage, scatter, tree, path, eraser)
- Radius slider (0.5m - 50m)
- Density slider (instances per m²)
- Selected foliage/scatter palette
- Falloff curve (reuse CurveEditor widget from Phase 5E)

### Sources
- Unity Terrain painting UI
- Unreal Landscape/Foliage tool UI
- Blender sculpt/paint mode
- ImGui demo applications (slider patterns)

---

## 7. Biome Presets

### How Engines Package Biomes

**Unreal Landscape Layers:** Each "landscape layer" defines a ground material + associated foliage types. Painting a layer paints the ground texture and optionally spawns foliage.

**Unity Terrain Layers:** Similar — a TerrainLayer asset defines diffuse/normal/mask textures and tiling. Detail (grass) and tree prototypes are separate but can be grouped into presets via custom editor scripts.

### Our Approach

A **BiomePreset** is a data-only struct (serializable to JSON):

```cpp
struct BiomePreset {
    std::string name;
    std::string groundMaterialPath;    // Material for ground surface
    struct FoliageLayer {
        std::string meshPath;          // Grass/plant mesh
        std::string texturePath;       // Billboard texture
        float density;                 // Instances per m²
        float minScale, maxScale;
        float windAmplitude;
    };
    std::vector<FoliageLayer> foliageLayers;
    struct ScatterLayer {
        std::string meshPath;
        float density;
        float minScale, maxScale;
        bool alignToSurface;
    };
    std::vector<ScatterLayer> scatterLayers;
    struct TreeLayer {
        std::string meshPath;
        std::string billboardPath;
        float spacing;                 // Minimum distance between trees
        float minScale, maxScale;
    };
    std::vector<TreeLayer> treeLayers;
};
```

Built-in presets:
- **Garden:** Green grass, wildflowers, scattered stones, olive trees
- **Desert:** Sand ground, scrub brush, rocks, acacia trees
- **Temple Courtyard:** Stone pavers, sparse grass at edges, potted plants
- **Cedar Forest:** Forest floor, ferns, fallen branches, cedar trees

The biome brush paints all layers simultaneously. Individual layers can be adjusted afterward.

---

## 8. Performance Budget

### Target: 60 FPS with 100K Visible Foliage Instances

**RX 6600 capabilities (RDNA2):**
- ~1.7 TFLOPS (compute)
- ~28 Gpixels/s (fill rate)
- 8 GB VRAM
- ~58 million triangles/sec at 60 FPS (theoretical max)

**Budget allocation for foliage rendering:**
- 100K grass instances × 6 triangles = 600K triangles (trivial — ~1% of triangle budget)
- 100K instances × 32 bytes = 3.2 MB instance data (trivial)
- 1 draw call per foliage type per visible chunk (estimated 20-40 draw calls for grass)
- Grass shader is simple (no PBR, no shadows received) — very cheap per-fragment

**The bottleneck will be instance buffer upload, not GPU rendering.** At 100K instances, uploading 3.2 MB per frame via `glBufferSubData` takes ~0.1ms on PCIe 4.0. This is acceptable.

### Culling Strategy

1. **Chunk-based frustum culling:** Each 16×16m FoliageChunk has a bounding box. Chunks outside the view frustum are skipped entirely. For a 90° FOV at 100m range, roughly 25% of chunks are visible.
2. **Distance fade:** Beyond a configurable distance (e.g., 80m), foliage density is reduced by skipping every Nth instance. Beyond 120m, foliage is not rendered.
3. **No per-instance culling:** At 6 triangles per grass blade, the GPU renders and discards off-screen instances faster than the CPU can test them. Per-instance culling only pays off for high-poly meshes.

### Draw Call Estimate

| Category | Draw calls | Triangles |
|----------|-----------|-----------|
| Grass (1-2 types × 20 chunks) | 20-40 | 200K-600K |
| Scatter (2-3 types × 10 chunks) | 20-30 | 100K-300K |
| Trees (LOD0) | 10-50 (instanced batches) | 50K-500K |
| Trees (LOD1 billboards) | 5-10 | 5K-50K |
| Paths | 5-10 | 10K-50K |
| **Total foliage budget** | **60-140** | **365K-1.5M** |

This is well within budget. The existing scene already uses 50-100 draw calls; adding 100 more is fine for an RX 6600.

### Sources
- AMD RDNA2 architecture whitepaper (fill rate, triangle throughput)
- GPU Instancer benchmarks (100K+ instances at 60 FPS on mid-range hardware)
- Eastshade Studios foliage optimization article

---

## 9. Open-Source References

- **Terrain3D** (Godot plugin): Open-source terrain system with foliage instancing. Uses MultiMesh + chunk-based spatial partitioning. MIT license. [GitHub: TokisanGames/Terrain3D]
- **GPU Gems sample code** (NVIDIA): Classic grass rendering with vertex shader wind. Public domain.
- **Vegetation Studio** (Unity): Instanced indirect rendering for vegetation. Uses compute shaders for GPU culling. [GitHub: AwesomeTechnologies/Vegetation-Studio-Instanced-Indirect]
- **ImGuizmo** (ocornut): 3D gizmo library for ImGui — could provide reference for brush preview rendering. MIT license.

---

## 10. Key Takeaways for Design

1. **Start with instanced 3-quad star mesh for grass** — simple, proven, good enough visually
2. **Chunk-based spatial partitioning (16m cells)** — natural culling boundary, bounded memory per chunk
3. **Explicit instance lists, not density maps** — simpler, direct, serializes trivially
4. **Single-layer vertex shader wind** — one sin() per vertex, convincing for short grass
5. **Two-level tree LOD (mesh + billboard)** — simplest effective approach
6. **Catmull-Rom splines for paths/streams** — intuitive, passes through control points
7. **Brush UX: radius via scroll, preview circle, stamp spacing** — standard pattern
8. **Biome presets as JSON data** — composable, serializable, user-customizable
9. **Performance is not a concern at 100K instances** — well within RX 6600 budget
10. **Compute shader + indirect draw is the upgrade path** — defer until needed
