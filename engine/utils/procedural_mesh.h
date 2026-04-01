/// @file procedural_mesh.h
/// @brief Static utility for generating architectural meshes (walls, roofs, stairs, floors).
#pragma once

#include "renderer/mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Describes a rectangular opening (door or window) in a wall.
struct WallOpening
{
    float xOffset = 1.0f;   ///< Distance from wall left edge to opening left edge (m).
    float yOffset = 0.0f;   ///< Distance from wall bottom to opening bottom (m).
    float width   = 1.0f;   ///< Opening width (m).
    float height  = 2.1f;   ///< Opening height (m).
};

/// @brief Roof type for procedural generation.
enum class RoofType
{
    FLAT,    ///< Simple flat slab.
    GABLE,   ///< Two slopes meeting at a ridge along the length.
    SHED     ///< Single slope from high edge to low edge.
};

/// @brief Stair type for procedural generation.
enum class StairType
{
    STRAIGHT,  ///< Linear flight of steps.
    SPIRAL     ///< Helical staircase around a central axis.
};

/// @brief Result of procedural mesh generation: vertex and index data.
struct ProceduralMeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

/// @brief Static utility for generating architectural geometry.
/// All meshes are generated centered at the origin with proper normals, UVs, and tangents.
class ProceduralMeshBuilder
{
public:
    /// @brief Generates vertex/index data for a solid wall.
    static ProceduralMeshData generateWall(float width, float height, float thickness);

    /// @brief Generates vertex/index data for a wall with rectangular openings.
    static ProceduralMeshData generateWallWithOpenings(
        float width, float height, float thickness,
        const std::vector<WallOpening>& openings);

    /// @brief Generates vertex/index data for a flat floor/ceiling slab.
    static ProceduralMeshData generateFloor(float width, float depth, float thickness);

    /// @brief Generates vertex/index data for a roof mesh.
    static ProceduralMeshData generateRoof(RoofType type, float width, float depth,
                                            float peakHeight, float overhang);

    /// @brief Generates vertex/index data for a straight flight of stairs.
    static ProceduralMeshData generateStraightStairs(float totalHeight, float riseHeight,
                                                      float treadDepth, float width);

    /// @brief Generates vertex/index data for a spiral staircase.
    static ProceduralMeshData generateSpiralStairs(float totalHeight, float riseHeight,
                                                    float innerRadius, float outerRadius,
                                                    float totalAngle);

    /// @brief Convenience: generates data and uploads to a Mesh. Requires GL context.
    static Mesh createWall(float width, float height, float thickness);
    static Mesh createWallWithOpenings(float width, float height, float thickness,
                                       const std::vector<WallOpening>& openings);
    static Mesh createFloor(float width, float depth, float thickness);
    static Mesh createRoof(RoofType type, float width, float depth,
                           float peakHeight, float overhang);
    static Mesh createStraightStairs(float totalHeight, float riseHeight,
                                      float treadDepth, float width);
    static Mesh createSpiralStairs(float totalHeight, float riseHeight,
                                    float innerRadius, float outerRadius,
                                    float totalAngle);

private:
    /// @brief Adds a quad (two triangles) to vertex/index buffers.
    static void addQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                        const glm::vec3& p0, const glm::vec3& p1,
                        const glm::vec3& p2, const glm::vec3& p3,
                        const glm::vec3& normal,
                        const glm::vec2& uv0, const glm::vec2& uv1,
                        const glm::vec2& uv2, const glm::vec2& uv3);

    /// @brief Convenience overload: auto-computes UVs from world positions.
    static void addQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                        const glm::vec3& p0, const glm::vec3& p1,
                        const glm::vec3& p2, const glm::vec3& p3,
                        const glm::vec3& normal);
};

} // namespace Vestige
