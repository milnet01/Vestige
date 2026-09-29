// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_debug.h
/// @brief Debug wireframe visualization for physics collision shapes.
#pragma once

#include "physics/physics_world.h"

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
    /// @brief Queues wireframe collision shapes for all bodies into
    ///        DebugDraw. The caller's DebugDraw::flush draws them, with the
    ///        editor's other overlays and against scene depth.
    void draw(const PhysicsWorld& world);

    /// @brief Toggles debug visualization on/off.
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    void drawConstraints(const PhysicsWorld& world,
                          const JPH::BodyInterface& bodyInterface);

    bool m_enabled = false;
};

} // namespace Vestige
