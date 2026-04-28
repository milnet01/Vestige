# Phase 5G: Environment Painting — Design Document

## Goal

Add a brush-based environment painting system to the editor. Users paint grass, rocks, trees, paths, and water features onto surfaces in real-time, with undo/redo support, scene serialization, and 60 FPS performance at 100K visible foliage instances.

**Milestone:** The user can paint a courtyard scene with grass, scattered rocks, olive trees, a stone path, and a water stream — all through the editor GUI — save/load the scene, and maintain 60 FPS.

---

## Current State (End of Phase 5F)

The editor has:
- **Full scene authoring:** Entities, lights, materials, particles, water surfaces
- **Scene persistence:** Save/load JSON, undo/redo (200-command history), auto-save, crash recovery
- **Rendering pipeline:** Forward PBR, shadows, SSAO, bloom, tonemap, TAA, instanced rendering, particle renderer, water renderer
- **Performance tools:** GPU timer queries, CPU profiler, console panel, screenshot tool
- **Reusable widgets:** CurveEditor, GradientEditor (from Phase 5E)
- **InstanceBuffer class:** Dynamic VBO for per-instance mat4 upload

**What's missing:**
- No foliage rendering (no grass, ground cover)
- No scatter/placement system (no rocks, debris)
- No tree LOD (no distance-based mesh simplification)
- No path/road tool
- No brush-based painting interface
- No biome presets

---

## Research Summary

See `docs/phases/phase_05g_research.md` for full research findings. Key design decisions:

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Grass geometry | Instanced 3-quad star mesh (6 tri/blade) | Parallax from all angles; avoids AMD GS perf issues |
| Spatial partitioning | 16m × 16m FoliageChunks | Natural frustum cull unit; bounded memory; simple |
| Foliage storage | Explicit instance lists per chunk | Simpler than density maps; immediate; trivial serialization |
| Wind animation | Single-layer vertex shader sin() | Nearly free; sufficient for short grass/plants |
| Scatter rendering | Existing instanced PBR pipeline | Scatter objects are regular meshes; no new renderer needed |
| Surface alignment | Quaternion from surface normal with blend factor | Standard technique; configurable per scatter type |
| Tree LOD | 2-level: full mesh + billboard | Simplest effective approach; upgrade later |
| Spline type | Catmull-Rom | Passes through control points; intuitive editing |
| Path mesh | Thin strip with alpha-blended edges | Decal-like; works without terrain system |
| Brush UX | Scroll=radius, stamp spacing, preview circle | Industry standard pattern |
| Biome presets | JSON data structs (foliage + scatter + tree layers) | Composable, serializable, user-customizable |
| Undo | One command per paint stroke (stores added/removed instances) | Consistent with existing undo system |

---

## Architecture Overview

Phase 5G adds these subsystems:

```
+------------------------------------------------------------------+
|                            Editor                                 |
|                                                                   |
|  Environment System                  Brush UI                     |
|  +----------------------------+     +---------------------------+ |
|  | FoliageChunk               |     | BrushTool                 | |
|  | (16m cell, stores          |     | (radius, density, type,   | |
|  |  instance lists per type)  |     |  stamp spacing, falloff)  | |
|  +----------------------------+     +---------------------------+ |
|  | FoliageManager             |     | BrushPreviewRenderer      | |
|  | (chunk grid, add/remove,   |     | (circle decal on surface) | |
|  |  frustum cull, serialize)  |     +---------------------------+ |
|  +----------------------------+     | EnvironmentPanel (ImGui)  | |
|  | FoliageRenderer            |     | (brush settings, palette, | |
|  | (instanced star-mesh,      |     |  biome presets, layers)   | |
|  |  wind shader, LOD fade)    |     +---------------------------+ |
|  +----------------------------+                                   |
|  | ScatterRenderer            |     Path System                   |
|  | (uses existing instanced   |     +---------------------------+ |
|  |  PBR pipeline)             |     | SplinePath                | |
|  +----------------------------+     | (Catmull-Rom waypoints,   | |
|  | TreeRenderer               |     |  width, material)         | |
|  | (mesh LOD + billboard      |     +---------------------------+ |
|  |  crossfade)                |     | PathMeshGenerator         | |
|  +----------------------------+     | (spline → triangle strip) | |
|                                     +---------------------------+ |
|  Biome System                                                     |
|  +----------------------------+                                   |
|  | BiomePreset (JSON data)    |                                   |
|  | BiomeLibrary (built-in +   |                                   |
|  |  user presets)             |                                   |
|  +----------------------------+                                   |
+------------------------------------------------------------------+
```

### Data Flow

```
User paints with brush
  → BrushTool calculates affected area
  → FoliageManager adds/removes instances in affected FoliageChunks
  → Undo command records the delta (added/removed instances)
  → Each frame:
      FoliageManager frustum-culls chunks
      → FoliageRenderer uploads visible instances → instanced draw
      → ScatterRenderer batches visible scatter → existing instanced PBR draw
      → TreeRenderer draws LOD0 meshes + LOD1 billboards
```

---

## File Structure

```
engine/
├── environment/
│   ├── foliage_chunk.h / .cpp         — 16m cell storing instance lists
│   ├── foliage_manager.h / .cpp       — Chunk grid, spatial queries, serialize
│   ├── foliage_instance.h             — Per-instance data structs
│   ├── biome_preset.h / .cpp          — Biome data + built-in presets
│   └── spline_path.h / .cpp           — Catmull-Rom spline + mesh generation
├── renderer/
│   ├── foliage_renderer.h / .cpp      — Instanced star-mesh + wind shader
│   └── tree_renderer.h / .cpp         — LOD mesh + billboard rendering
├── editor/
│   ├── tools/
│   │   ├── brush_tool.h / .cpp        — Brush state, stamp logic, mode switching
│   │   └── brush_preview.h / .cpp     — Circle decal overlay renderer
│   ├── panels/
│   │   └── environment_panel.h / .cpp — ImGui panel for brush/palette/biome
│   └── commands/
│       ├── paint_foliage_command.h     — Undo command for foliage strokes
│       ├── paint_scatter_command.h     — Undo command for scatter strokes
│       ├── place_tree_command.h        — Undo command for tree placement
│       └── path_edit_command.h         — Undo command for path creation/edit
assets/
└── shaders/
    ├── foliage.vert.glsl              — Instanced star-mesh + wind animation
    ├── foliage.frag.glsl              — Simple textured with alpha test
    ├── tree_billboard.vert.glsl       — Billboard quad facing camera
    ├── tree_billboard.frag.glsl       — Textured billboard with alpha
    ├── brush_preview.vert.glsl        — Circle decal projection
    └── brush_preview.frag.glsl        — Semi-transparent circle overlay
tests/
    ├── test_foliage_chunk.cpp         — Chunk add/remove/query
    ├── test_spline_path.cpp           — Catmull-Rom evaluation, mesh generation
    └── test_biome_preset.cpp          — Serialize/deserialize biome presets
```

---

## Detailed Component Design

### 1. FoliageInstance (Data Structs)

```cpp
/// Per-instance data for grass/ground cover (GPU-uploadable)
struct FoliageInstance
{
    glm::vec3 position;     ///< World position
    float rotation;         ///< Y-axis rotation in radians
    float scale;            ///< Uniform scale
    glm::vec3 colorTint;    ///< RGB tint variation
};

/// Per-instance data for scatter objects (rocks, debris)
struct ScatterInstance
{
    glm::vec3 position;     ///< World position
    glm::quat rotation;     ///< Full rotation (surface alignment)
    float scale;            ///< Uniform scale
    uint32_t meshIndex;     ///< Index into scatter palette
};

/// Per-instance data for trees
struct TreeInstance
{
    glm::vec3 position;     ///< World position
    float rotation;         ///< Y-axis rotation
    float scale;            ///< Uniform scale
    uint32_t speciesIndex;  ///< Index into species palette
};
```

### 2. FoliageChunk

A 16m × 16m spatial cell that stores all environment instances within its bounds.

```cpp
class FoliageChunk
{
public:
    FoliageChunk(int gridX, int gridZ);

    // Foliage (grass, flowers)
    void addFoliage(uint32_t typeId, const FoliageInstance& instance);
    int removeFoliageInRadius(uint32_t typeId, const glm::vec3& center, float radius);
    const std::vector<FoliageInstance>& getFoliage(uint32_t typeId) const;

    // Scatter (rocks, debris)
    void addScatter(const ScatterInstance& instance);
    int removeScatterInRadius(const glm::vec3& center, float radius);
    const std::vector<ScatterInstance>& getScatter() const;

    // Trees
    void addTree(const TreeInstance& instance);
    int removeTreesInRadius(const glm::vec3& center, float radius);
    const std::vector<TreeInstance>& getTrees() const;

    // Spatial
    AABB getBounds() const;       ///< World-space bounding box
    bool isEmpty() const;         ///< True if all instance lists are empty
    int getTotalInstanceCount() const;

    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);

private:
    int m_gridX, m_gridZ;
    static constexpr float CHUNK_SIZE = 16.0f;

    /// Maps foliage type ID → instance list
    std::unordered_map<uint32_t, std::vector<FoliageInstance>> m_foliage;
    std::vector<ScatterInstance> m_scatter;
    std::vector<TreeInstance> m_trees;
};
```

### 3. FoliageManager

Manages the chunk grid and provides the high-level API for the brush tool.

```cpp
class FoliageManager
{
public:
    FoliageManager();

    /// Adds foliage instances within a brush stamp.
    /// Returns the list of added instances (for undo).
    std::vector<std::pair<int, FoliageInstance>> paintFoliage(
        uint32_t typeId,
        const glm::vec3& center,
        float radius,
        float density,
        float falloff,
        const FoliageTypeConfig& config);

    /// Removes foliage within radius. Returns removed instances (for undo).
    std::vector<std::pair<int, FoliageInstance>> eraseFoliage(
        uint32_t typeId,
        const glm::vec3& center,
        float radius);

    /// Scatter paint/erase (similar API)
    std::vector<ScatterInstance> paintScatter(...);
    std::vector<ScatterInstance> eraseScatter(...);

    /// Tree placement
    void placeTree(const TreeInstance& instance);
    void removeTree(const glm::vec3& position, float radius);

    /// Frustum culling — returns chunks that intersect the view frustum
    std::vector<const FoliageChunk*> getVisibleChunks(
        const glm::mat4& viewProjection) const;

    /// Serialization (all chunks)
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);

    /// Clears all environment data
    void clear();

private:
    FoliageChunk& getOrCreateChunk(int gridX, int gridZ);
    std::pair<int, int> worldToGrid(const glm::vec3& pos) const;

    std::unordered_map<uint64_t, std::unique_ptr<FoliageChunk>> m_chunks;
};
```

### 4. FoliageRenderer

Renders grass/ground cover using instanced 3-quad star meshes with wind animation.

```cpp
class FoliageRenderer
{
public:
    bool init(const std::string& assetPath);
    void shutdown();

    /// Renders visible foliage instances.
    /// @param chunks Frustum-culled chunks from FoliageManager.
    /// @param camera Current camera.
    /// @param viewProjection VP matrix.
    /// @param time Elapsed time for wind animation.
    /// @param maxDistance Distance beyond which foliage is not rendered.
    void render(const std::vector<const FoliageChunk*>& chunks,
                const Camera& camera,
                const glm::mat4& viewProjection,
                float time,
                float maxDistance = 100.0f);

private:
    void createStarMesh();              ///< 3 intersecting quads
    void uploadInstances(const std::vector<FoliageInstance>& instances);

    Shader m_shader;
    GLuint m_starVao = 0;
    GLuint m_starVbo = 0;
    GLuint m_instanceVbo = 0;
    int m_instanceCapacity = 0;

    // CPU staging buffer
    std::vector<FoliageInstance> m_visibleInstances;
};
```

### 5. TreeRenderer

Handles LOD switching between full mesh and billboard.

```cpp
class TreeRenderer
{
public:
    bool init(const std::string& assetPath);
    void shutdown();

    /// Renders trees from visible chunks with LOD selection.
    void render(const std::vector<const FoliageChunk*>& chunks,
                const Camera& camera,
                const glm::mat4& viewProjection,
                float time);

    /// Configuration
    float m_lodDistance = 50.0f;          ///< Distance at which LOD0→LOD1 transition occurs
    float m_fadeRange = 10.0f;           ///< Crossfade range
    float m_maxDistance = 200.0f;        ///< Beyond this, trees are not rendered

private:
    Shader m_billboardShader;
    GLuint m_billboardVao = 0;
    GLuint m_billboardVbo = 0;
    GLuint m_billboardInstanceVbo = 0;
};
```

### 6. BrushTool

Handles brush state and paint/erase interaction in the viewport.

```cpp
class BrushTool
{
public:
    enum class Mode
    {
        FOLIAGE,
        SCATTER,
        TREE,
        PATH,
        ERASER
    };

    /// Processes mouse input for painting.
    /// @param mouseRay Ray from camera through mouse cursor.
    /// @param mouseDown True if LMB is held.
    /// @param deltaTime Frame time.
    /// @return True if the brush consumed the input (suppress other tools).
    bool processInput(const Ray& mouseRay, bool mouseDown, float deltaTime);

    /// Checks if the brush tool is active (user is in environment painting mode).
    bool isActive() const;

    /// Gets the current brush hit point (for preview rendering).
    bool getHitPoint(glm::vec3& outPoint, glm::vec3& outNormal) const;

    // Configuration
    Mode mode = Mode::FOLIAGE;
    float radius = 5.0f;                ///< Brush radius in meters
    float density = 2.0f;               ///< Instances per m² (foliage/scatter)
    float stampSpacing = 0.5f;          ///< Minimum distance between stamps
    float falloff = 0.5f;              ///< Edge falloff (0=sharp, 1=full taper)
    uint32_t selectedTypeId = 0;        ///< Selected foliage/scatter type
    uint32_t selectedSpeciesId = 0;     ///< Selected tree species

private:
    bool m_painting = false;
    glm::vec3 m_lastStampPos;
    glm::vec3 m_currentHitPoint;
    glm::vec3 m_currentHitNormal;
    bool m_hasHit = false;
};
```

### 7. SplinePath

Catmull-Rom spline for paths and streams.

```cpp
class SplinePath
{
public:
    /// Adds a waypoint at the given world position.
    void addWaypoint(const glm::vec3& position);

    /// Inserts a waypoint at the given index.
    void insertWaypoint(int index, const glm::vec3& position);

    /// Removes a waypoint.
    void removeWaypoint(int index);

    /// Moves a waypoint.
    void setWaypointPosition(int index, const glm::vec3& position);

    /// Evaluates the spline at parameter t (0 = start, 1 = end).
    glm::vec3 evaluate(float t) const;

    /// Evaluates the tangent at parameter t.
    glm::vec3 evaluateTangent(float t) const;

    /// Gets the total arc length of the spline.
    float getLength() const;

    /// Generates a triangle strip mesh for the path.
    /// @param width Path half-width in meters.
    /// @param sampleSpacing Distance between samples along the spline.
    /// @return Mesh data (positions, normals, UVs, indices).
    MeshData generatePathMesh(float width, float sampleSpacing = 0.5f) const;

    /// Generates a water surface mesh for a stream.
    MeshData generateStreamMesh(float width, float sampleSpacing = 0.5f) const;

    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& j);

    const std::vector<glm::vec3>& getWaypoints() const;

private:
    std::vector<glm::vec3> m_waypoints;
};
```

### 8. BiomePreset

```cpp
struct FoliageTypeConfig
{
    std::string name;
    std::string texturePath;          ///< Grass blade texture
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float windAmplitude = 0.1f;
    float windFrequency = 2.0f;
    glm::vec3 tintVariation = {0.1f, 0.1f, 0.05f};  ///< RGB range for random tint
};

struct ScatterTypeConfig
{
    std::string name;
    std::string meshPath;
    float minScale = 0.5f;
    float maxScale = 1.5f;
    float surfaceAlignment = 0.8f;    ///< 0=upright, 1=fully aligned to surface
};

struct TreeSpeciesConfig
{
    std::string name;
    std::string meshPath;
    std::string billboardTexturePath;
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float minSpacing = 3.0f;          ///< Minimum distance between trees of this species
};

struct BiomePreset
{
    std::string name;
    std::string groundMaterialPath;

    struct FoliageLayer
    {
        uint32_t typeId;
        float density;                ///< Instances per m²
    };
    std::vector<FoliageLayer> foliageLayers;

    struct ScatterLayer
    {
        uint32_t typeId;
        float density;
    };
    std::vector<ScatterLayer> scatterLayers;

    struct TreeLayer
    {
        uint32_t speciesId;
        float density;
    };
    std::vector<TreeLayer> treeLayers;

    nlohmann::json serialize() const;
    static BiomePreset deserialize(const nlohmann::json& j);
};
```

---

## Undo/Redo Integration

Each brush stroke produces a single undo command. The command stores the delta (added or removed instances) so it can be reversed:

```cpp
class PaintFoliageCommand : public UndoableCommand
{
public:
    PaintFoliageCommand(FoliageManager& manager,
                        std::vector<AddedInstance> added);

    void execute() override;     // Already applied during painting
    void undo() override;        // Remove added instances
    void redo() override;        // Re-add instances

private:
    FoliageManager& m_manager;
    std::vector<AddedInstance> m_added;  // (chunkKey, instance) pairs
};

class EraseFoliageCommand : public UndoableCommand
{
public:
    void undo() override;        // Re-add removed instances
    void redo() override;        // Remove again
};
```

Paint commands use the "already-executed" pattern — the brush applies changes in real-time during the stroke, and when the stroke ends, the accumulated delta is wrapped in a command and pushed onto the undo stack.

---

## Scene Serialization

The FoliageManager serializes as a top-level section in the `.scene` JSON file:

```json
{
    "scene": { ... },
    "entities": [ ... ],
    "environment": {
        "foliageTypes": [
            { "id": 0, "name": "Short Grass", "texture": "textures/grass_blade.png", ... }
        ],
        "scatterTypes": [
            { "id": 0, "name": "Small Rock", "mesh": "models/rock_small.glb", ... }
        ],
        "treeSpecies": [
            { "id": 0, "name": "Olive", "mesh": "models/olive_tree.glb", ... }
        ],
        "chunks": [
            {
                "gridX": 0, "gridZ": 0,
                "foliage": {
                    "0": [
                        { "pos": [1.5, 0.0, 2.3], "rot": 1.2, "scale": 0.9, "tint": [0.9, 1.0, 0.85] },
                        ...
                    ]
                },
                "scatter": [ ... ],
                "trees": [ ... ]
            }
        ],
        "paths": [
            {
                "name": "Garden Path",
                "waypoints": [[0,0,0], [5,0,3], [10,0,5]],
                "width": 1.5,
                "materialPath": "materials/stone_path.json"
            }
        ],
        "biomePresets": [ ... ]
    }
}
```

---

## Shader Design

### foliage.vert.glsl

```glsl
#version 450 core

// Star mesh vertex attributes
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texCoord;

// Per-instance attributes
layout(location = 3) in vec3 i_position;
layout(location = 4) in float i_rotation;
layout(location = 5) in float i_scale;
layout(location = 6) in vec3 i_colorTint;

uniform mat4 u_viewProjection;
uniform float u_time;
uniform vec3 u_windDirection;
uniform float u_windAmplitude;
uniform float u_windFrequency;
uniform float u_maxDistance;
uniform vec3 u_cameraPos;

out vec2 v_texCoord;
out vec3 v_colorTint;
out float v_alpha;

void main()
{
    // Apply per-instance rotation (Y-axis only)
    float s = sin(i_rotation);
    float c = cos(i_rotation);
    vec3 rotated = vec3(
        a_position.x * c - a_position.z * s,
        a_position.y,
        a_position.x * s + a_position.z * c
    );

    // Apply scale and position
    vec3 worldPos = rotated * i_scale + i_position;

    // Wind animation (only affects tip — modulated by vertex Y)
    float windPhase = u_time * u_windFrequency + i_position.x * 0.5 + i_position.z * 0.3;
    float windOffset = sin(windPhase) * u_windAmplitude * a_position.y;
    worldPos.xz += u_windDirection.xz * windOffset;

    // Distance fade
    float dist = distance(u_cameraPos, i_position);
    v_alpha = 1.0 - smoothstep(u_maxDistance * 0.8, u_maxDistance, dist);

    gl_Position = u_viewProjection * vec4(worldPos, 1.0);

    v_texCoord = a_texCoord;
    v_colorTint = i_colorTint;
}
```

### foliage.frag.glsl

```glsl
#version 450 core

in vec2 v_texCoord;
in vec3 v_colorTint;
in float v_alpha;

uniform sampler2D u_texture;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(u_texture, v_texCoord);

    // Alpha test (discard transparent pixels)
    if (texel.a < 0.5)
        discard;

    // Apply tint and distance fade
    fragColor = vec4(texel.rgb * v_colorTint, texel.a * v_alpha);

    // Discard fully faded
    if (fragColor.a < 0.01)
        discard;
}
```

---

## Implementation Sub-Phases

### Phase 5G-1: Foliage Core (Foundation)
**Files:** `foliage_chunk.h/.cpp`, `foliage_instance.h`, `foliage_manager.h/.cpp`, `foliage_renderer.h/.cpp`, `foliage.vert.glsl`, `foliage.frag.glsl`
**Tests:** `test_foliage_chunk.cpp`

1. Implement `FoliageInstance` data struct
2. Implement `FoliageChunk` — add/remove instances, AABB, serialize/deserialize
3. Implement `FoliageManager` — chunk grid, `paintFoliage()`, `eraseFoliage()`, `getVisibleChunks()`
4. Create 3-quad star mesh geometry
5. Write foliage vertex shader with wind animation
6. Write foliage fragment shader with alpha test + distance fade
7. Implement `FoliageRenderer` — upload visible instances, instanced draw
8. Integrate into `Engine` — create FoliageManager, call FoliageRenderer in render loop
9. Unit tests for FoliageChunk (add/remove/radius queries)
10. **Visual test:** Programmatically scatter 10K grass instances, verify rendering and wind

### Phase 5G-2: Brush Tool & Editor UI
**Files:** `brush_tool.h/.cpp`, `brush_preview.h/.cpp`, `environment_panel.h/.cpp`, `brush_preview.vert/frag.glsl`, `paint_foliage_command.h`

1. Implement `BrushTool` — mouse ray intersection with scene, stamp logic, spacing
2. Implement `BrushPreviewRenderer` — circle decal projected at hit point
3. Implement `EnvironmentPanel` (ImGui) — brush mode selector, radius/density sliders, foliage palette
4. Integrate brush tool with editor input handling (activate on panel open, deactivate on close)
5. Implement `PaintFoliageCommand` and `EraseFoliageCommand` for undo/redo
6. Wire brush strokes through FoliageManager → undo stack
7. Add bracket keys [ ] for radius adjustment
8. **Visual test:** Paint and erase grass in the editor viewport in real-time

### Phase 5G-3: Scatter System
**Files:** `paint_scatter_command.h`, updates to `foliage_chunk.h/.cpp`, `foliage_manager.h/.cpp`, `environment_panel.cpp`

1. Add `ScatterInstance` storage to `FoliageChunk`
2. Add scatter paint/erase to `FoliageManager`
3. Implement surface normal alignment (raycast + quaternion alignment)
4. Render scatter objects via existing instanced PBR pipeline (build InstanceBatches from scatter data)
5. Add scatter palette to `EnvironmentPanel` (mesh thumbnail selection)
6. Implement `PaintScatterCommand` for undo/redo
7. Implement eraser mode for scatter
8. **Visual test:** Paint rocks onto a sloped surface, verify alignment

### Phase 5G-4: Tree Placement & LOD
**Files:** `tree_renderer.h/.cpp`, `tree_billboard.vert/frag.glsl`, `place_tree_command.h`, updates to `foliage_chunk.h/.cpp`

1. Add `TreeInstance` storage to `FoliageChunk`
2. Implement tree placement in `FoliageManager` with minimum spacing enforcement
3. Implement `TreeRenderer` — LOD0 full mesh rendering (instanced)
4. Implement billboard generation (render tree to texture → billboard quad)
5. Implement LOD1 billboard rendering with crossfade
6. Add tree species palette to `EnvironmentPanel`
7. Add placeholder tree meshes (simple trunk + crown geometry) for 4 species
8. Implement `PlaceTreeCommand` for undo/redo
9. **Visual test:** Place olive and cedar trees, verify LOD transitions at distance

### Phase 5G-5: Path Tool
**Files:** `spline_path.h/.cpp`, `path_edit_command.h`, updates to `environment_panel.cpp`
**Tests:** `test_spline_path.cpp`

1. Implement `SplinePath` — Catmull-Rom evaluation, tangent, arc length
2. Implement `PathMeshGenerator` — spline → triangle strip with UVs
3. Render paths as thin mesh strips with alpha-blended edges (existing PBR pipeline or a simple unlit shader)
4. Add path editing to the brush tool — click to place waypoints, drag to move
5. Implement automatic foliage clearing within path footprint
6. Implement `PathEditCommand` for undo/redo
7. Add path controls to `EnvironmentPanel` (width, material, presets)
8. Unit tests for spline evaluation and mesh generation
9. **Visual test:** Draw a curved path through a grassy area, verify foliage is cleared

### Phase 5G-6: Biome Presets & Serialization
**Files:** `biome_preset.h/.cpp`, updates to `foliage_manager.h/.cpp`, `entity_serializer.cpp`
**Tests:** `test_biome_preset.cpp`

1. Implement `BiomePreset` struct with serialize/deserialize
2. Create built-in presets: Garden, Desert, Temple Courtyard, Cedar Forest
3. Implement biome brush — paints all layers simultaneously
4. Add biome selector to `EnvironmentPanel` (dropdown + preview)
5. Integrate FoliageManager serialization into EntitySerializer (scene save/load)
6. Implement "Save Custom Biome" / "Load Custom Biome" in panel
7. Unit tests for biome serialization
8. **Visual test:** Paint a Garden biome, save scene, reload, verify all data preserved

### Phase 5G-7: Water Painting & Polish
**Files:** Updates to `spline_path.h/.cpp`, `environment_panel.cpp`

1. Extend `SplinePath` to generate stream water meshes
2. Implement stream tool — spline-based water surface with per-waypoint width
3. Implement pond tool — closed polygon → triangulated water surface
4. Integrate stream/pond meshes with existing `WaterRenderer` from Phase 5E
5. Add water painting controls to `EnvironmentPanel`
6. Performance profiling — verify 60 FPS with full courtyard scene
7. Polish: adjust wind parameters, add more foliage textures, tune LOD distances
8. **Final visual test:** Complete courtyard scene with all environment features

---

## Performance Considerations

| Concern | Mitigation |
|---------|-----------|
| Instance buffer upload (100K × 32B = 3.2 MB/frame) | `glBufferSubData` with orphan pattern; <0.1ms on PCIe 4.0 |
| Chunk frustum culling | AABB-frustum test per chunk; ~100 chunks max = trivial |
| Per-instance culling | Not needed — 6 tri/blade, GPU discards faster than CPU tests |
| Draw calls | 1 per foliage type per visible chunk batch; target <50 total |
| Grass alpha test | Discard in fragment shader, no sorting needed |
| Tree LOD transitions | Crossfade via alpha dithering over fade range |
| Scatter objects in PBR pipeline | Batched with existing instanced rendering; no extra cost |
| Wind shader | One sin() per vertex — negligible |
| Memory | 100K × 32B = 3.2 MB instances + chunk overhead; well within 8 GB VRAM |

---

## Accessibility Notes

- Brush preview circle uses both color (green=paint, red=erase) and a dashed pattern for colorblind users
- All brush parameters have numeric input fields alongside sliders (precision without fine motor control)
- Keyboard shortcuts for common operations (bracket keys for radius, Shift+bracket for density)
- Environment panel text is readable at the user's configured UI scale

---

## Dependencies

- **Existing systems used:**
  - `InstanceBuffer` (instanced rendering)
  - `UndoableCommand` / `CommandHistory` (undo/redo)
  - `EntitySerializer` (scene save/load via JSON)
  - `WaterRenderer` / `WaterSurfaceComponent` (stream/pond rendering)
  - `CurveEditor` widget (falloff curve editing)
  - `ResourceManager` (mesh/texture loading)
  - `Renderer::renderScene()` (scatter objects via existing PBR pipeline)

- **New external dependencies:** None. All new code uses existing libraries (GLM, OpenGL, ImGui, nlohmann/json).
