// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mesh_recipe.h
/// @brief The numbers a ProceduralMeshBuilder mesh was built from, so a saved
///        scene can store them and rebuild the same mesh (3D_E-0967).
#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <optional>
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

/// @brief Which ProceduralMeshBuilder generator made a mesh, and its arguments.
///
/// Each field is named after the builder parameter it feeds; a generator reads
/// only its own fields (the saved form lists them per generator).
struct MeshRecipe
{
    enum class Generator { WALL, FLOOR, ROOF, STAIRS_STRAIGHT, STAIRS_SPIRAL, PATH };

    Generator generator = Generator::WALL;
    float width = 0, height = 0, thickness = 0, depth = 0;       // wall, floor, roof
    std::vector<WallOpening> openings;                            // wall
    RoofType roofType = RoofType::FLAT;                           // roof
    float peakHeight = 0, overhang = 0;                           // roof
    float totalHeight = 0, riseHeight = 0, treadDepth = 0;        // stairs
    float innerRadius = 0, outerRadius = 0, totalAngle = 0;       // spiral (angle in degrees)
    std::vector<glm::vec3> points;                                // path waypoints
    int samplesPerSegment = 0;                                    // path

    /// @brief The saved form: the generator's name and its own fields only.
    nlohmann::json toJson() const;

    /// @brief Reads a saved recipe. A scene file is untrusted, so this returns
    ///        nullopt for a missing or mistyped field, or any value outside the
    ///        limits the builder can be trusted with.
    static std::optional<MeshRecipe> fromJson(const nlohmann::json& j);

    /// @brief Most openings a wall recipe may hold.
    static constexpr size_t MAX_OPENINGS = 64;
};

} // namespace Vestige
