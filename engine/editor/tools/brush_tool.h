/// @file brush_tool.h
/// @brief Brush tool for painting environment instances in the editor viewport.
#pragma once

#include "editor/commands/paint_scatter_command.h"
#include "editor/commands/place_tree_command.h"
#include "environment/foliage_instance.h"
#include "environment/foliage_manager.h"

#include <glm/glm.hpp>

namespace Vestige
{

class Camera;
class CommandHistory;
class Scene;

/// @brief A ray from camera through a screen point.
struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;  ///< Normalized direction.
};

/// @brief Brush tool for painting/erasing environment instances.
class BrushTool
{
public:
    /// @brief Brush operating mode.
    enum class Mode
    {
        FOLIAGE,
        SCATTER,
        TREE,
        PATH,
        ERASER
    };

    BrushTool() = default;

    /// @brief Processes mouse input for painting.
    /// @param mouseRay Ray from camera through mouse cursor.
    /// @param mouseDown True if LMB is held.
    /// @param deltaTime Frame time.
    /// @param manager Foliage manager to paint into.
    /// @param history Command history for undo support.
    /// @return True if the brush consumed the input.
    bool processInput(const Ray& mouseRay, bool mouseDown, float deltaTime,
                      FoliageManager& manager, CommandHistory& history);

    /// @brief Checks if the brush tool is active (user is painting).
    bool isActive() const { return m_enabled; }

    /// @brief Sets whether the brush tool is enabled.
    void setEnabled(bool enabled) { m_enabled = enabled; }

    /// @brief Gets the current brush hit point on a surface.
    /// @param outPoint Output hit position.
    /// @param outNormal Output hit normal.
    /// @return True if the brush has a valid hit point this frame.
    bool getHitPoint(glm::vec3& outPoint, glm::vec3& outNormal) const;

    /// @brief Intersects the brush ray with the ground plane (y=0).
    /// @param ray The camera ray.
    /// @param outPoint Output intersection point.
    /// @return True if the ray hits the ground plane.
    static bool rayGroundIntersect(const Ray& ray, glm::vec3& outPoint);

    /// @brief Creates a ray from camera through screen coordinates.
    /// @param camera The camera.
    /// @param screenX Normalized screen X (0..1, left to right).
    /// @param screenY Normalized screen Y (0..1, top to bottom).
    /// @param aspectRatio Viewport aspect ratio.
    /// @return The ray.
    static Ray createRay(const Camera& camera, float screenX, float screenY,
                         float aspectRatio);

    // --- Configuration ---
    Mode mode = Mode::FOLIAGE;
    float radius = 5.0f;           ///< Brush radius in meters.
    float density = 2.0f;          ///< Instances per m² (foliage/scatter).
    float stampSpacing = 0.5f;     ///< Min distance between stamps (as fraction of radius).
    float falloff = 0.5f;          ///< Edge falloff (0=sharp, 1=full taper).
    uint32_t selectedTypeId = 0;   ///< Selected foliage/scatter type.
    uint32_t selectedSpeciesId = 0; ///< Selected tree species.

    /// @brief Foliage type config used for painting.
    FoliageTypeConfig foliageConfig;

    /// @brief Scatter type config used for painting.
    ScatterTypeConfig scatterConfig;

    /// @brief Tree species config used for placement.
    TreeSpeciesConfig treeConfig;

private:
    void beginStroke();
    void endStroke(FoliageManager& manager, CommandHistory& history);
    void stampAt(const glm::vec3& position, FoliageManager& manager);

    /// @brief Converts (key, instance) pairs to ScatterInstanceRef vector.
    static std::vector<ScatterInstanceRef> convertScatterRefs(
        const std::vector<std::pair<uint64_t, ScatterInstance>>& refs);

    bool m_enabled = false;
    bool m_painting = false;
    glm::vec3 m_lastStampPos{0.0f};
    glm::vec3 m_currentHitPoint{0.0f};
    glm::vec3 m_currentHitNormal{0.0f, 1.0f, 0.0f};
    bool m_hasHit = false;

    /// Accumulated foliage instances for the current stroke (for undo).
    std::vector<FoliageInstanceRef> m_strokeAdded;
    std::vector<FoliageInstanceRef> m_strokeRemoved;

    /// Accumulated scatter instances for the current stroke (for undo).
    std::vector<std::pair<uint64_t, ScatterInstance>> m_scatterAdded;
    std::vector<std::pair<uint64_t, ScatterInstance>> m_scatterRemoved;

    /// Accumulated tree instances for the current stroke (for undo).
    std::vector<std::pair<uint64_t, TreeInstance>> m_treesAdded;
    std::vector<std::pair<uint64_t, TreeInstance>> m_treesRemoved;

    /// @brief Converts (key, instance) pairs to TreeInstanceRef vector.
    static std::vector<TreeInstanceRef> convertTreeRefs(
        const std::vector<std::pair<uint64_t, TreeInstance>>& refs);
};

} // namespace Vestige
