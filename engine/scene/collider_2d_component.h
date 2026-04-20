// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file collider_2d_component.h
/// @brief Collider2DComponent — 2D collision shape authoring (Phase 9F-2).
///
/// The collider is a shape *description*; Physics2DSystem translates it
/// into a `JPH::Shape` at body-creation time (box / circle / capsule /
/// convex polygon / edge chain, all extruded to a thin Z-slab so the
/// shared 3D Jolt world can collide against them).
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Vestige
{

enum class ColliderShape2D
{
    Box,        ///< Axis-aligned-in-local box; halfExtents.
    Circle,     ///< radius.
    Capsule,    ///< radius + height (vertical capsule along local Y).
    Polygon,    ///< Convex polygon, vertices CCW.
    EdgeChain   ///< Open polyline (N chained segments). Static-only.
};

class Collider2DComponent : public Component
{
public:
    Collider2DComponent() = default;
    ~Collider2DComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    ColliderShape2D shape = ColliderShape2D::Box;

    /// @brief Half-extents for Box shape, in world units.
    glm::vec2 halfExtents = glm::vec2(0.5f, 0.5f);

    /// @brief Radius for Circle / Capsule shape.
    float radius = 0.5f;

    /// @brief Total height for Capsule (including end caps). radius
    /// must be ≤ height / 2 for a valid capsule.
    float height = 2.0f;

    /// @brief Vertices for Polygon (CCW) or EdgeChain (sequential).
    /// Units: world units, in the component's local frame.
    std::vector<glm::vec2> vertices;

    /// @brief Trigger mode — fires collision/trigger events but does not
    /// resolve contacts.
    bool isSensor = false;

    /// @brief Thickness of the thin Z-slab used to represent the shape
    /// in the shared 3D Jolt world. Small but non-zero so CCD / contact
    /// resolution work; too large and it can poke through 3D geometry
    /// adjacent to a 2D scene.
    float zThickness = 0.1f;

    /// @brief Half-Z offset from the body origin. Zero keeps the shape
    /// centred on Z = body.Z. Useful for layered 2D worlds where one
    /// layer sits at Z = 0, another at Z = -1.
    float zOffset = 0.0f;
};

} // namespace Vestige
