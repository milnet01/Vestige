// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file rigid_body_2d_component.h
/// @brief RigidBody2DComponent — 2D Jolt-backed rigid body (Phase 9F-2).
///
/// Uses the shared 3D Jolt PhysicsSystem with `EAllowedDOFs::Plane2D`
/// per-body, locking Z translation and X/Y rotation. A Physics2DSystem
/// owns creation/step/teardown; this component is pure state.
#pragma once

#include "scene/component.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <glm/glm.hpp>

#include <memory>

namespace Vestige
{

enum class BodyType2D
{
    Static,     ///< Never moves. Infinite mass. Kinematic queries hit it.
    Kinematic,  ///< Moved by code, not physics. Ignores forces but pushes
                ///  dynamic bodies.
    Dynamic     ///< Participates in simulation. Forces / impulses / gravity.
};

/// @brief 2D rigid body state — geometry is on Collider2DComponent.
class RigidBody2DComponent : public Component
{
public:
    RigidBody2DComponent() = default;
    ~RigidBody2DComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    // -- Authored config (read by Physics2DSystem at create-time) --

    BodyType2D type = BodyType2D::Dynamic;
    float mass           = 1.0f;
    float friction       = 0.5f;
    float restitution    = 0.0f;
    float linearDamping  = 0.0f;
    float angularDamping = 0.05f;
    float gravityScale   = 1.0f;

    /// @brief Locks Z-rotation on top of Plane2D — useful for platformer
    /// characters that should stay upright regardless of contacts.
    bool fixedRotation = false;

    /// @brief Collision category bits (masked against other bodies' maskBits).
    uint32_t categoryBits = 0x0001;
    uint32_t maskBits     = 0xFFFFFFFFu;

    // -- Runtime (populated by Physics2DSystem) --

    JPH::BodyID bodyId;  ///< Invalid (all bits set) until the system creates it.

    /// @brief Cached linear velocity from the last step. Written by the
    /// system's post-step pass so gameplay code reads it in the XY plane
    /// without touching the Jolt body interface directly.
    glm::vec2 linearVelocity = glm::vec2(0.0f);

    /// @brief Cached angular velocity around Z (radians / sec).
    float angularVelocity = 0.0f;
};

} // namespace Vestige
