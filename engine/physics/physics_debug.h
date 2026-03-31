/// @file physics_debug.h
/// @brief Debug wireframe visualization for physics collision shapes.
#pragma once

#include "physics/physics_world.h"
#include "renderer/debug_draw.h"
#include "renderer/camera.h"

namespace Vestige
{

/// @brief Draws wireframe overlays for all physics bodies.
///
/// Colors:
/// - Green: static bodies
/// - Blue: dynamic bodies
/// - Yellow: kinematic bodies
class PhysicsDebugDraw
{
public:
    /// @brief Renders wireframe collision shapes for all bodies.
    void draw(const PhysicsWorld& world, DebugDraw& debugDraw,
              const Camera& camera, float aspectRatio);

    /// @brief Toggles debug visualization on/off.
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    bool m_enabled = false;
};

} // namespace Vestige
