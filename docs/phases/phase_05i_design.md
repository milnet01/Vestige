# Phase 5I Design: Terrain System

**Date:** 2026-03-23
**Based on:** phase_05i_research.md
**Engine:** Vestige (C++17, OpenGL 4.5, ImGui docking)

---

## Architecture Overview

The terrain system consists of four core components:

1. **`Terrain`** — Data owner: heightmap, splatmap, normal map, CDLOD quadtree, height queries
2. **`TerrainRenderer`** — GPU rendering: shaders, grid mesh, instanced CDLOD draw calls
3. **`TerrainPanel`** — Editor UI: settings, brush controls, layer management
4. **Terrain shaders** — Vertex (heightmap displacement + morphing) and fragment (splatmap blending + PBR lighting)

The terrain is a scene-level singleton (one terrain per scene), not an entity component. This matches how FoliageManager works — owned by Engine, pointer passed where needed.

---

## Implementation Sub-Phases

### 5I-1: Core Terrain + CDLOD Rendering
- `Terrain` class with heightmap (R32F), CPU float array, CDLOD quadtree
- `TerrainRenderer` with shared grid mesh, vertex shader heightmap lookup + morphing
- Basic flat-shaded rendering (normals from central differences in vertex shader)
- Frustum culling of quadtree nodes
- Integration into Engine render pipeline (after opaque scene, before foliage)

### 5I-2: Normal Map + Splatmap Texturing
- Pre-computed normal map (RGB8 texture)
- Splatmap (RGBA8) with 4 texture layers via GL_TEXTURE_2D_ARRAY
- Height-based blending in fragment shader
- PBR terrain material support (albedo+height, normal, ORM arrays)
- Default terrain textures (procedural grass/rock/dirt/sand)

### 5I-3: Shadow Integration + Height Queries
- Terrain rendered into CSM shadow passes
- Shadow depth shader (position + heightmap only)
- `getHeight(x, z)` / `getNormal(x, z)` API with bilinear interpolation
- Foliage placement uses terrain height (wire FoliageManager → Terrain)
- Raycast for editor terrain picking

### 5I-4: Sculpting Tools
- Terrain brush modes: raise, lower, smooth, flatten
- CPU-side heightmap modification + `glTexSubImage2D` partial upload
- Normal map partial recomputation after sculpting
- Undo/redo via region snapshots (TerrainSculptCommand)
- Brush preview circle on terrain surface

### 5I-5: Texture Painting + Editor Panel
- Splatmap painting brush with weight normalization
- TerrainPanel: heightmap settings, sculpt brush, texture layers, import/export
- Terrain layer management (add/remove/reorder texture layers)
- Import heightmap from R16/R32F/PNG files

### 5I-6: Serialization + Polish
- Save/load terrain data with scene (heightmap .r32, splatmap .png, settings JSON)
- Export heightmap to R16 for external tool interop
- Water edge integration (shore depth)
- Triplanar mapping for steep slopes (optional toggle)
- LOD tuning and performance optimization

---

## Class Design

### Terrain (engine/environment/terrain.h)

```cpp
namespace Vestige {

struct TerrainConfig
{
    int width = 257;              // Heightmap width (power-of-two + 1)
    int depth = 257;              // Heightmap depth
    float spacingX = 1.0f;        // World units per texel (X)
    float spacingZ = 1.0f;        // World units per texel (Z)
    float heightScale = 50.0f;    // Maximum height in world units
    glm::vec3 origin{0.0f};      // World position of heightmap corner (0,0)
    int gridResolution = 32;      // CDLOD grid mesh size (vertices per side)
    int maxLodLevels = 6;         // Quadtree depth
    float baseLodDistance = 20.0f; // Finest LOD range in meters
};

struct CDLODNode
{
    glm::vec2 center;             // World XZ center
    float size;                   // Half-extent in world units
    float minHeight, maxHeight;   // For frustum culling
    int lodLevel;
    int children[4] = {-1,-1,-1,-1}; // Indices into node array (-1 = leaf)
};

struct TerrainDrawNode
{
    glm::vec2 worldOffset;        // Node world XZ origin
    float scale;                  // Node world size
    int lodLevel;
    float morphFactor;            // For vertex morphing
};

class Terrain
{
public:
    Terrain();
    ~Terrain();

    bool initialize(const TerrainConfig& config);
    void shutdown();

    // Heightmap access
    float getHeight(float worldX, float worldZ) const;
    glm::vec3 getNormal(float worldX, float worldZ) const;
    float getRawHeight(int x, int z) const;
    void setRawHeight(int x, int z, float height);

    // CDLOD quadtree
    void buildQuadtree();
    void selectNodes(const Camera& camera, float aspectRatio,
                     std::vector<TerrainDrawNode>& outNodes) const;

    // GPU textures
    GLuint getHeightmapTexture() const;
    GLuint getNormalMapTexture() const;
    GLuint getSplatmapTexture() const;

    // Heightmap modification (sculpting)
    void updateHeightmapRegion(int x, int z, int w, int h);
    void updateNormalMapRegion(int x, int z, int w, int h);
    void updateSplatmapRegion(int x, int z, int w, int h);

    // Raycast for editor picking
    bool raycast(const Ray& ray, float maxDist, glm::vec3& outHit) const;

    // Config access
    const TerrainConfig& getConfig() const;
    int getWidth() const;
    int getDepth() const;

    // Splatmap access
    void setSplatWeight(int x, int z, int channel, float weight);
    glm::vec4 getSplatWeight(int x, int z) const;

    // Serialization
    nlohmann::json serializeSettings() const;
    void deserializeSettings(const nlohmann::json& j);
    bool saveHeightmap(const std::filesystem::path& path) const;
    bool loadHeightmap(const std::filesystem::path& path);
    bool saveSplatmap(const std::filesystem::path& path) const;
    bool loadSplatmap(const std::filesystem::path& path);

private:
    void createGpuTextures();
    void computeAllNormals();
    void computeNormalRegion(int x, int z, int w, int h);

    TerrainConfig m_config;
    std::vector<float> m_heightData;          // CPU heightmap (width * depth)
    std::vector<glm::vec3> m_normalData;      // CPU normal map
    std::vector<glm::vec4> m_splatData;       // CPU splatmap (RGBA weights)

    GLuint m_heightmapTex = 0;
    GLuint m_normalMapTex = 0;
    GLuint m_splatmapTex = 0;

    // CDLOD quadtree
    std::vector<CDLODNode> m_nodes;
    std::vector<float> m_lodRanges;
    int m_rootNode = -1;

    bool m_initialized = false;
};

} // namespace Vestige
```

### TerrainRenderer (engine/renderer/terrain_renderer.h)

```cpp
namespace Vestige {

struct TerrainTextureLayer
{
    std::string name = "Untitled";
    GLuint albedoTexture = 0;     // Individual texture (before array packing)
    GLuint normalTexture = 0;
    GLuint ormTexture = 0;
    float tiling = 10.0f;
};

class TerrainRenderer
{
public:
    bool init(const std::string& assetPath);
    void shutdown();

    void render(const Terrain& terrain,
                const Camera& camera,
                float aspectRatio,
                const SceneRenderData& sceneData);

    void renderShadow(const Terrain& terrain,
                      const glm::mat4& lightSpaceMatrix);

    // Texture layers
    void setTextureLayer(int index, const TerrainTextureLayer& layer);
    int getLayerCount() const;

private:
    void createGridMesh(int resolution);
    void generateDefaultTextures();

    Shader m_terrainShader;
    Shader m_shadowShader;

    // Shared grid mesh (reused for every CDLOD node)
    GLuint m_gridVao = 0;
    GLuint m_gridVbo = 0;
    GLuint m_gridEbo = 0;
    int m_gridIndexCount = 0;
    int m_gridResolution = 0;

    // Texture arrays (packed from layers)
    GLuint m_albedoArray = 0;
    GLuint m_normalArray = 0;
    GLuint m_ormArray = 0;
    int m_layerCount = 0;

    // Default textures
    GLuint m_defaultAlbedo = 0;
    GLuint m_defaultNormal = 0;
    GLuint m_defaultOrm = 0;

    // Per-frame draw node buffer
    std::vector<TerrainDrawNode> m_drawNodes;

    bool m_initialized = false;
};

} // namespace Vestige
```

---

## Shader Design

### terrain.vert.glsl
```glsl
#version 450 core

layout(location = 0) in vec2 a_gridPos;  // 0..1 grid coordinates

// Per-node uniforms
uniform vec2 u_nodeOffset;     // World XZ offset of this node
uniform float u_nodeScale;     // World size of this node
uniform int u_lodLevel;
uniform float u_morphFactor;

// Terrain uniforms
uniform sampler2D u_heightmap;
uniform float u_heightScale;
uniform vec2 u_terrainSize;    // Total terrain world size
uniform vec2 u_terrainOrigin;
uniform int u_gridResolution;

// Camera
uniform mat4 u_viewProjection;

out vec2 v_terrainUV;          // UV in terrain space (0..1)
out vec3 v_worldPos;
out float v_morphFactor;

void main()
{
    // Scale grid position to node world space
    vec2 worldXZ = u_nodeOffset + a_gridPos * u_nodeScale;

    // Vertex morphing: snap alternate vertices toward coarser grid
    vec2 gridFrac = fract(a_gridPos * float(u_gridResolution) * 0.5) * 2.0
                    / float(u_gridResolution);
    worldXZ -= gridFrac * u_nodeScale * u_morphFactor;

    // Compute terrain UV
    v_terrainUV = (worldXZ - u_terrainOrigin) / u_terrainSize;

    // Sample heightmap
    float height = texture(u_heightmap, v_terrainUV).r * u_heightScale;

    v_worldPos = vec3(worldXZ.x, height, worldXZ.y);
    v_morphFactor = u_morphFactor;

    gl_Position = u_viewProjection * vec4(v_worldPos, 1.0);
}
```

### terrain.frag.glsl
```glsl
#version 450 core

in vec2 v_terrainUV;
in vec3 v_worldPos;

uniform sampler2D u_normalMap;
uniform sampler2D u_splatmap;
uniform sampler2DArray u_albedoArray;
uniform sampler2DArray u_normalArray;
uniform sampler2DArray u_ormArray;

uniform vec4 u_layerTiling;    // Tiling scale per layer (4 layers)
uniform int u_layerCount;

// Lighting uniforms (match existing scene shader)
uniform vec3 u_viewPos;
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
// ... shadow map uniforms ...

out vec4 fragColor;

void main()
{
    // Read terrain normal from pre-computed normal map
    vec3 normal = texture(u_normalMap, v_terrainUV).rgb * 2.0 - 1.0;
    normal = normalize(normal);

    // Read splatmap weights
    vec4 splat = texture(u_splatmap, v_terrainUV);

    // Blend terrain textures
    vec3 albedo = vec3(0.0);
    float roughness = 0.0;
    float metallic = 0.0;
    float ao = 1.0;

    for (int i = 0; i < min(u_layerCount, 4); i++)
    {
        float weight = splat[i];
        if (weight < 0.001) continue;

        vec2 tiledUV = v_worldPos.xz * u_layerTiling[i];
        vec3 layerAlbedo = texture(u_albedoArray, vec3(tiledUV, float(i))).rgb;
        vec3 layerOrm = texture(u_ormArray, vec3(tiledUV, float(i))).rgb;

        albedo += layerAlbedo * weight;
        ao += layerOrm.r * weight;
        roughness += layerOrm.g * weight;
        metallic += layerOrm.b * weight;
    }

    // Basic PBR lighting (simplified — will use existing lighting code)
    vec3 lightDir = normalize(-u_lightDirection);
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = albedo * NdotL * u_lightColor;
    vec3 ambient = albedo * 0.15 * ao;

    fragColor = vec4(ambient + diffuse, 1.0);
}
```

---

## Engine Integration

### Render Pipeline Position

```
Renderer::beginFrame()
  ↓
Renderer::renderScene(SceneRenderData)    // Opaque entities + shadows + sky
  ↓
★ TerrainRenderer::render()               // Terrain (after opaque, before foliage)
  ↓
FoliageRenderer::render()                 // Grass (placed on terrain)
  ↓
TreeRenderer::render()                    // Trees (placed on terrain)
  ↓
WaterRenderer::render()                   // Water surfaces
  ↓
ParticleRenderer::render()                // Particles
  ↓
Renderer::endFrame()                      // Post-processing
```

Terrain also renders into shadow passes via `renderShadow()`, called from the shadow pass loop in Renderer.

### Engine Class Additions

```cpp
// engine.h — new members
Terrain m_terrain;
TerrainRenderer m_terrainRenderer;

// engine.cpp — initialization
m_terrain.initialize(terrainConfig);
m_terrainRenderer.init(config.assetPath);
m_editor->setTerrain(&m_terrain);

// engine.cpp — render loop
m_terrainRenderer.render(m_terrain, *m_camera, aspectRatio, m_renderData);
```

### Editor Integration

```cpp
// editor.h — new members
Terrain* m_terrain = nullptr;
TerrainPanel m_terrainPanel;

// editor.h — new methods
void setTerrain(Terrain* terrain);
TerrainPanel& getTerrainPanel();
```

---

## File Layout

```
engine/
  environment/
    terrain.h
    terrain.cpp
  renderer/
    terrain_renderer.h
    terrain_renderer.cpp
  editor/
    panels/
      terrain_panel.h
      terrain_panel.cpp
    commands/
      terrain_sculpt_command.h  (5I-4)

assets/
  shaders/
    terrain.vert.glsl
    terrain.frag.glsl
    terrain_shadow.vert.glsl
    terrain_shadow.frag.glsl
```

---

## Performance Budget

| Metric | Budget | Expected |
|--------|--------|----------|
| Draw calls (terrain) | < 80 | 30-60 CDLOD nodes |
| Triangles (terrain) | < 200K | ~90K (50 nodes × 1800 tri) |
| GPU memory | < 50 MB | ~35 MB total |
| CPU time (node selection) | < 0.5 ms | ~0.1 ms |
| Sculpt upload (brush) | < 1 ms | ~0.1 ms (64×64 region) |

All well within 60 FPS budget on RX 6600.
