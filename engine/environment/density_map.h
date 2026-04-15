// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file density_map.h
/// @brief Paintable density map for spatial foliage distribution control.
#pragma once

#include "environment/spline_path.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief A world-space grayscale texture that modulates foliage spawn probability.
///
/// Values range from 0.0 (no foliage) to 1.0 (full density). The map covers
/// a rectangular region of the world at a configurable resolution. Foliage
/// painting multiplies its density parameter by the sampled map value.
class DensityMap
{
public:
    DensityMap() = default;

    /// @brief Initializes the density map with the given dimensions.
    /// @param originX World X of the map's (0,0) corner.
    /// @param originZ World Z of the map's (0,0) corner.
    /// @param worldWidth Width in meters.
    /// @param worldDepth Depth in meters.
    /// @param texelsPerMeter Resolution (default 1 texel/m).
    void initialize(float originX, float originZ,
                    float worldWidth, float worldDepth,
                    float texelsPerMeter = 1.0f);

    /// @brief Returns true if the map has been initialized.
    bool isInitialized() const { return m_initialized; }

    /// @brief Samples the density at a world position (bilinear interpolation).
    /// Returns 1.0 for positions outside the map bounds.
    /// @param worldX World X coordinate.
    /// @param worldZ World Z coordinate.
    /// @return Density value in [0, 1].
    float sample(float worldX, float worldZ) const;

    /// @brief Paints density within a circular brush stamp.
    /// @param center World-space center of the brush.
    /// @param radius Brush radius in meters.
    /// @param value Target density value (0 = erase, 1 = full).
    /// @param strength Brush strength per application (0..1).
    /// @param falloff Edge falloff (0 = sharp, 1 = full taper).
    void paint(const glm::vec3& center, float radius,
               float value, float strength, float falloff);

    /// @brief Clears density along a spline path (writes zero).
    /// @param path The spline path to clear along.
    /// @param margin Extra clearance beyond the path's width (meters).
    void clearAlongPath(const SplinePath& path, float margin = 0.5f);

    /// @brief Fills the entire map with a uniform value.
    /// @param value Fill value (default 1.0 = full density).
    void fill(float value = 1.0f);

    /// @brief Gets the raw texel value at map coordinates.
    /// @param x Texel X (0 to width-1).
    /// @param z Texel Z (0 to height-1).
    /// @return Density value in [0, 1].
    float getTexel(int x, int z) const;

    /// @brief Sets the raw texel value at map coordinates.
    void setTexel(int x, int z, float value);

    /// @brief Gets the map width in texels.
    int getWidth() const { return m_width; }

    /// @brief Gets the map height in texels.
    int getHeight() const { return m_height; }

    /// @brief Gets the world origin (XZ).
    glm::vec2 getOrigin() const { return m_origin; }

    /// @brief Gets the world extent (width, depth) in meters.
    glm::vec2 getWorldExtent() const { return m_worldExtent; }

    /// @brief Gets the texels-per-meter resolution.
    float getTexelsPerMeter() const { return m_texelsPerMeter; }

    /// @brief Serializes the density map to JSON.
    nlohmann::json serialize() const;

    /// @brief Deserializes the density map from JSON.
    void deserialize(const nlohmann::json& j);

private:
    /// @brief Converts world coordinates to fractional texel coordinates.
    void worldToTexel(float worldX, float worldZ, float& tx, float& tz) const;

    std::vector<float> m_data;       ///< Texel values (row-major, 0..1).
    int m_width = 0;                 ///< Map width in texels.
    int m_height = 0;                ///< Map height in texels.
    glm::vec2 m_origin{0.0f};       ///< World XZ of the (0,0) texel.
    glm::vec2 m_worldExtent{0.0f};  ///< World width and depth in meters.
    float m_texelsPerMeter = 1.0f;   ///< Resolution.
    bool m_initialized = false;
};

} // namespace Vestige
