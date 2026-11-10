// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file procedural_mesh.h
/// @brief Static utility for generating architectural meshes (walls, roofs, stairs, floors).
#pragma once

#include "renderer/mesh.h"
#include "utils/mesh_recipe.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

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

    /// @brief Number of steps a staircase of this height and rise has: the
    ///        nearest whole number, at least one. Both stair generators use it.
    static int stairStepCount(float totalHeight, float riseHeight);

    /// @brief Convenience: generates data and uploads to a Mesh. Requires GL context.
    ///        Each stamps the recipe of its own arguments onto the mesh it returns.
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

    /// @brief Generates a flat ribbon of the given width along a Catmull-Rom
    ///        spline through `points`, in the points' own (world) space.
    static ProceduralMeshData generatePath(const std::vector<glm::vec3>& points,
                                           float width, int samplesPerSegment);
    static Mesh createPath(const std::vector<glm::vec3>& points,
                           float width, int samplesPerSegment);

    /// @brief Rebuilds a mesh from its recipe, through the create* function
    ///        named by recipe.generator. Requires GL context.
    static Mesh build(const MeshRecipe& recipe);

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
