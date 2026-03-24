/// @file terrain_brush.h
/// @brief Brush tool for terrain sculpting and texture painting.
#pragma once

#include "editor/tools/brush_tool.h"
#include "environment/terrain.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

class Camera;
class CommandHistory;

/// @brief Terrain brush operating mode.
enum class TerrainBrushMode
{
    RAISE,      ///< Raise terrain height.
    LOWER,      ///< Lower terrain height.
    SMOOTH,     ///< Average neighboring heights to smooth bumps.
    FLATTEN,    ///< Flatten to a reference height (sampled at stroke start).
    PAINT       ///< Paint splatmap weights for texture layers.
};

/// @brief Brush tool for sculpting and painting terrain.
///
/// Uses the terrain raycast for cursor placement and modifies
/// the CPU-side heightmap/splatmap with circular falloff.
/// Each stroke creates a single undo command capturing a before/after snapshot.
class TerrainBrush
{
public:
    TerrainBrush() = default;

    /// @brief Processes mouse input for terrain sculpting/painting.
    /// @param mouseRay Ray from camera through mouse cursor.
    /// @param mouseDown True if LMB is held.
    /// @param deltaTime Frame time for strength scaling.
    /// @param terrain Terrain to modify.
    /// @param history Command history for undo support.
    /// @return True if the brush consumed the input.
    bool processInput(const Ray& mouseRay, bool mouseDown, float deltaTime,
                      Terrain& terrain, CommandHistory& history);

    /// @brief Gets the current brush hit point on terrain.
    /// @param outPoint Output hit position.
    /// @param outNormal Output hit normal.
    /// @return True if the brush has a valid hit point this frame.
    bool getHitPoint(glm::vec3& outPoint, glm::vec3& outNormal) const;

    /// @brief Checks if the terrain brush is enabled.
    bool isActive() const { return m_enabled; }

    /// @brief Sets whether the terrain brush is enabled.
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // --- Configuration ---
    TerrainBrushMode mode = TerrainBrushMode::RAISE;
    float radius = 10.0f;          ///< Brush radius in world units.
    float strength = 0.5f;         ///< Sculpt strength per second (normalized).
    float falloff = 0.5f;          ///< Edge falloff (0=sharp circle, 1=full taper).
    int paintChannel = 0;          ///< Active splatmap channel for PAINT mode (0-3).

private:
    void beginStroke(Terrain& terrain);
    void endStroke(Terrain& terrain, CommandHistory& history);
    void applyBrush(const glm::vec3& center, float dt, Terrain& terrain);

    void applyRaise(int x, int z, float weight, float dt, Terrain& terrain);
    void applyLower(int x, int z, float weight, float dt, Terrain& terrain);
    void applySmooth(int x, int z, float weight, float dt, Terrain& terrain);
    void applyFlatten(int x, int z, float weight, float dt, Terrain& terrain);
    void applyPaint(int x, int z, float weight, float dt, Terrain& terrain);

    /// @brief Computes brush falloff weight for a given distance from center.
    float computeFalloff(float dist) const;

    /// @brief Expands the dirty region to include a texel.
    void expandDirtyRegion(int x, int z);

    bool m_enabled = false;
    bool m_painting = false;

    // Hit state
    glm::vec3 m_hitPoint{0.0f};
    glm::vec3 m_hitNormal{0.0f, 1.0f, 0.0f};
    bool m_hasHit = false;

    // Flatten reference height (sampled at stroke start)
    float m_flattenHeight = 0.0f;

    // Undo snapshot: heights before this stroke began
    std::vector<float> m_beforeHeights;
    std::vector<glm::vec4> m_beforeSplat;

    // Dirty region (texel bounds modified this stroke)
    int m_dirtyMinX = 0;
    int m_dirtyMinZ = 0;
    int m_dirtyMaxX = 0;
    int m_dirtyMaxZ = 0;
    bool m_hasDirtyRegion = false;
};

} // namespace Vestige
