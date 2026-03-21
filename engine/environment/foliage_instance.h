/// @file foliage_instance.h
/// @brief Per-instance data structs for foliage, scatter objects, and trees.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

namespace Vestige
{

/// @brief Per-instance data for grass and ground cover (GPU-uploadable layout).
struct FoliageInstance
{
    glm::vec3 position{0.0f};   ///< World-space position.
    float rotation = 0.0f;      ///< Y-axis rotation in radians.
    float scale = 1.0f;         ///< Uniform scale factor.
    glm::vec3 colorTint{1.0f};  ///< RGB tint variation.
};

/// @brief Per-instance data for scatter objects (rocks, debris).
struct ScatterInstance
{
    glm::vec3 position{0.0f};          ///< World-space position.
    glm::quat rotation{1, 0, 0, 0};   ///< Full rotation (for surface alignment).
    float scale = 1.0f;                ///< Uniform scale factor.
    uint32_t meshIndex = 0;            ///< Index into the scatter type palette.
};

/// @brief Per-instance data for trees.
struct TreeInstance
{
    glm::vec3 position{0.0f};      ///< World-space position.
    float rotation = 0.0f;         ///< Y-axis rotation in radians.
    float scale = 1.0f;            ///< Uniform scale factor.
    uint32_t speciesIndex = 0;     ///< Index into the tree species palette.
};

/// @brief Configuration for a foliage type (grass, flowers, etc.).
struct FoliageTypeConfig
{
    std::string name;
    std::string texturePath;
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float windAmplitude = 0.1f;
    float windFrequency = 2.0f;
    glm::vec3 tintVariation{0.1f, 0.1f, 0.05f};  ///< RGB range for random tint.
};

/// @brief Configuration for a scatter object type (rocks, debris).
struct ScatterTypeConfig
{
    std::string name;
    std::string meshPath;
    float minScale = 0.5f;
    float maxScale = 1.5f;
    float surfaceAlignment = 0.8f;  ///< 0 = upright, 1 = fully aligned to surface normal.
};

/// @brief Configuration for a tree species.
struct TreeSpeciesConfig
{
    std::string name;
    std::string meshPath;
    std::string billboardTexturePath;
    float minScale = 0.8f;
    float maxScale = 1.2f;
    float minSpacing = 3.0f;  ///< Minimum distance between trees of this species.
};

} // namespace Vestige
