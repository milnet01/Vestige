/// @file terrain.h
/// @brief Heightmap-based terrain with CDLOD quadtree LOD system.
#pragma once

#include "editor/tools/brush_tool.h"
#include "renderer/camera.h"
#include "utils/frustum.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <vector>

namespace Vestige
{

/// @brief Configuration for a terrain instance.
struct TerrainConfig
{
    int width = 257;              ///< Heightmap width (power-of-two + 1).
    int depth = 257;              ///< Heightmap depth (power-of-two + 1).
    float spacingX = 1.0f;        ///< World units per texel (X axis).
    float spacingZ = 1.0f;        ///< World units per texel (Z axis).
    float heightScale = 50.0f;    ///< Maximum height in world units.
    glm::vec3 origin{0.0f};      ///< World position of heightmap corner (0,0).
    int gridResolution = 33;      ///< CDLOD grid mesh vertices per side (must be odd).
    int maxLodLevels = 6;         ///< Quadtree depth.
    float baseLodDistance = 20.0f; ///< Finest LOD range in meters.
};

/// @brief A CDLOD quadtree node for terrain LOD selection.
struct CDLODNode
{
    glm::vec2 center;             ///< World XZ center of this node.
    float size;                   ///< Half-extent in world units.
    float minHeight;              ///< Minimum height in this node's region.
    float maxHeight;              ///< Maximum height in this node's region.
    int lodLevel;                 ///< LOD level (0 = finest).
    int children[4] = {-1, -1, -1, -1}; ///< Child node indices (-1 = none).
};

/// @brief Data for a single terrain draw call (one CDLOD node).
struct TerrainDrawNode
{
    glm::vec2 worldOffset;        ///< Node world XZ origin (top-left corner).
    float scale;                  ///< Node world size (full extent).
    int lodLevel;                 ///< LOD level for morph factor calculation.
    float morphFactor;            ///< Vertex morphing blend (0 = fine, 1 = coarse).
};

/// @brief Heightmap-based terrain with CDLOD quadtree LOD.
///
/// Owns the CPU-side heightmap, splatmap, and normal data.
/// Creates GPU textures for rendering. Provides height/normal queries
/// for object placement and editor picking.
class Terrain
{
public:
    Terrain();
    ~Terrain();

    // Non-copyable
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    /// @brief Initializes the terrain with the given configuration.
    /// Creates a flat heightmap and all GPU textures.
    /// @param config Terrain configuration.
    /// @return True if initialization succeeded.
    bool initialize(const TerrainConfig& config);

    /// @brief Releases all GPU resources and data.
    void shutdown();

    /// @brief Returns true if the terrain has been initialized.
    bool isInitialized() const { return m_initialized; }

    // --- Height queries ---

    /// @brief Gets the interpolated height at a world position.
    /// Uses bilinear interpolation between heightmap texels.
    /// @param worldX World X coordinate.
    /// @param worldZ World Z coordinate.
    /// @return Interpolated height in world units.
    float getHeight(float worldX, float worldZ) const;

    /// @brief Gets the interpolated normal at a world position.
    /// @param worldX World X coordinate.
    /// @param worldZ World Z coordinate.
    /// @return Normalized surface normal.
    glm::vec3 getNormal(float worldX, float worldZ) const;

    /// @brief Gets the raw height value at a heightmap texel.
    /// @param x Texel X (0 to width-1).
    /// @param z Texel Z (0 to depth-1).
    /// @return Normalized height (0..1).
    float getRawHeight(int x, int z) const;

    /// @brief Sets the raw height value at a heightmap texel.
    /// @param x Texel X (0 to width-1).
    /// @param z Texel Z (0 to depth-1).
    /// @param height Normalized height (0..1).
    void setRawHeight(int x, int z, float height);

    // --- CDLOD quadtree ---

    /// @brief Rebuilds the CDLOD quadtree from current heightmap data.
    void buildQuadtree();

    /// @brief Selects visible CDLOD nodes for rendering.
    /// Performs frustum culling and distance-based LOD selection.
    /// @param camera Current camera for position and frustum.
    /// @param aspectRatio Viewport aspect ratio.
    /// @param outNodes Output draw node list.
    void selectNodes(const Camera& camera, float aspectRatio,
                     std::vector<TerrainDrawNode>& outNodes) const;

    // --- GPU textures ---

    /// @brief Gets the heightmap texture (R32F).
    GLuint getHeightmapTexture() const { return m_heightmapTex; }

    /// @brief Gets the normal map texture (RGB8).
    GLuint getNormalMapTexture() const { return m_normalMapTex; }

    /// @brief Gets the splatmap texture (RGBA8).
    GLuint getSplatmapTexture() const { return m_splatmapTex; }

    // --- Partial GPU updates (for sculpting) ---

    /// @brief Uploads a rectangular region of the heightmap to GPU.
    void updateHeightmapRegion(int x, int z, int w, int h);

    /// @brief Recomputes normals for a region and uploads to GPU.
    void updateNormalMapRegion(int x, int z, int w, int h);

    /// @brief Uploads a rectangular region of the splatmap to GPU.
    void updateSplatmapRegion(int x, int z, int w, int h);

    // --- Splatmap access ---

    /// @brief Sets the splatmap weight for a channel at a texel.
    void setSplatWeight(int x, int z, int channel, float weight);

    /// @brief Gets the splatmap weights at a texel (RGBA).
    glm::vec4 getSplatWeight(int x, int z) const;

    // --- Raycast ---

    /// @brief Raycasts against the terrain for editor picking.
    /// @param ray Camera ray.
    /// @param maxDist Maximum ray distance.
    /// @param outHit Output hit position.
    /// @return True if the ray hits the terrain.
    bool raycast(const Ray& ray, float maxDist, glm::vec3& outHit) const;

    // --- Config ---

    /// @brief Gets the terrain configuration.
    const TerrainConfig& getConfig() const { return m_config; }

    /// @brief Gets the heightmap width.
    int getWidth() const { return m_config.width; }

    /// @brief Gets the heightmap depth.
    int getDepth() const { return m_config.depth; }

    /// @brief Gets the total world-space terrain size (X extent).
    float getWorldWidth() const { return static_cast<float>(m_config.width - 1) * m_config.spacingX; }

    /// @brief Gets the total world-space terrain size (Z extent).
    float getWorldDepth() const { return static_cast<float>(m_config.depth - 1) * m_config.spacingZ; }

    /// @brief Gets the LOD distance ranges for per-vertex morphing.
    const std::vector<float>& getLodRanges() const { return m_lodRanges; }

    // --- Serialization ---

    /// @brief Serializes terrain settings to JSON (config, not data).
    nlohmann::json serializeSettings() const;

    /// @brief Loads terrain settings from JSON and reinitializes.
    /// @return True if deserialization and reinitialization succeeded.
    bool deserializeSettings(const nlohmann::json& j);

    /// @brief Saves the heightmap to a raw R32F binary file.
    /// @param path Output file path (typically .r32 extension).
    /// @return True if the save succeeded.
    bool saveHeightmap(const std::filesystem::path& path) const;

    /// @brief Loads a heightmap from a raw R32F binary file.
    /// @param path Input file path.
    /// @return True if the load succeeded (file size must match dimensions).
    bool loadHeightmap(const std::filesystem::path& path);

    /// @brief Saves the splatmap to a raw RGBA32F binary file.
    /// @param path Output file path (typically .splat extension).
    /// @return True if the save succeeded.
    bool saveSplatmap(const std::filesystem::path& path) const;

    /// @brief Loads a splatmap from a raw RGBA32F binary file.
    /// @param path Input file path.
    /// @return True if the load succeeded.
    bool loadSplatmap(const std::filesystem::path& path);

    /// @brief Configuration for bank blending near water edges.
    struct BankBlendConfig
    {
        float blendWidth = 3.0f;       ///< Distance from water edge to blend over (meters).
        int bankChannel = 3;           ///< Splatmap channel for the bank material (default: A=sand).
        float bankStrength = 0.8f;     ///< Maximum blend strength at the water edge (0..1).
    };

    /// @brief Applies bank blending to the splatmap near a water body.
    /// Blends the bank material (e.g. sand) into the terrain near the water edge.
    /// @param waterCenter World XZ center of the water body.
    /// @param waterHalfExtent World XZ half-extent of the water body.
    /// @param config Bank blending parameters.
    void applyBankBlend(const glm::vec2& waterCenter,
                        const glm::vec2& waterHalfExtent,
                        const BankBlendConfig& config);

    /// @brief Configuration for automatic slope/altitude-based splatmap generation.
    struct AutoTextureConfig
    {
        float slopeGrassEnd = 0.3f;    ///< Slope below this is fully grass (0=flat, 1=vertical).
        float slopeRockStart = 0.6f;   ///< Slope above this is fully rock.
        float altitudeSandEnd = 0.08f;  ///< Normalized height below this is sand.
        float altitudeSandStart = 0.02f; ///< Blend start for sand.
        float altitudeDirtStart = 0.25f; ///< Normalized height above this starts dirt.
        float altitudeDirtEnd = 0.45f;  ///< Fully dirt above this.
        float noiseScale = 0.05f;       ///< Noise frequency for breaking up transition lines.
        float noiseAmplitude = 0.12f;   ///< How much noise perturbs the thresholds.
    };

    /// @brief Generates splatmap weights automatically from slope and altitude.
    /// Layers: R=grass, G=rock, B=dirt, A=sand.
    /// @param config Auto-texture parameters.
    void generateAutoTexture(const AutoTextureConfig& config);

private:
    void createGpuTextures();
    void computeAllNormals();
    void computeNormalAt(int x, int z);

    /// @brief Recursive CDLOD quadtree builder.
    int buildNode(float cx, float cz, float halfSize, int lodLevel);

    /// @brief Recursive CDLOD node selection.
    void selectNode(int nodeIdx, const glm::vec3& cameraPos,
                    const FrustumPlanes& frustum,
                    std::vector<TerrainDrawNode>& outNodes) const;

    /// @brief Converts world XZ to heightmap texel coordinates (fractional).
    void worldToTexel(float worldX, float worldZ, float& tx, float& tz) const;

    TerrainConfig m_config;

    // CPU data
    std::vector<float> m_heightData;          ///< Normalized heights (0..1).
    std::vector<glm::vec3> m_normalData;      ///< Pre-computed normals.
    std::vector<glm::vec4> m_splatData;       ///< Splatmap weights (RGBA).

    // GPU textures
    GLuint m_heightmapTex = 0;
    GLuint m_normalMapTex = 0;
    GLuint m_splatmapTex = 0;

    // CDLOD quadtree
    std::vector<CDLODNode> m_nodes;
    std::vector<float> m_lodRanges;           ///< Distance ranges per LOD level.
    int m_rootNode = -1;

    bool m_initialized = false;
};

} // namespace Vestige
