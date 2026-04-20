// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file character_controller_2d_component.cpp
/// @brief CharacterController2DComponent + step helper.
#include "scene/character_controller_2d_component.h"
#include "scene/entity.h"
#include "scene/rigid_body_2d_component.h"
#include "systems/physics2d_system.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

void CharacterController2DComponent::update(float /*deltaTime*/)
{
    // Stepped externally via `stepCharacterController2D` so gameplay
    // code can provide per-frame input + contact state.
}

std::unique_ptr<Component> CharacterController2DComponent::clone() const
{
    auto c = std::make_unique<CharacterController2DComponent>();
    c->maxSpeed         = maxSpeed;
    c->acceleration     = acceleration;
    c->airAcceleration  = airAcceleration;
    c->groundFriction   = groundFriction;
    c->jumpVelocity     = jumpVelocity;
    c->variableJumpCut  = variableJumpCut;
    c->coyoteTimeSec    = coyoteTimeSec;
    c->jumpBufferSec    = jumpBufferSec;
    c->wallSlideMaxSpeed = wallSlideMaxSpeed;
    // Runtime state is reset — a clone starts fresh (not falling, no
    // buffered jump).
    c->setEnabled(m_isEnabled);
    return c;
}

bool stepCharacterController2D(CharacterController2DComponent& c,
                               Entity& entity,
                               Physics2DSystem& physics,
                               const CharacterControl2DInput& input,
                               float dt)
{
    auto* rb = entity.getComponent<RigidBody2DComponent>();
    if (!rb || rb->bodyId.IsInvalid() || dt <= 0.0f)
    {
        return false;
    }

    // Advance timers.
    if (c.onGround)
    {
        c.timeSinceGrounded = 0.0f;
    }
    else
    {
        c.timeSinceGrounded += dt;
    }

    if (input.wantsJump)
    {
        c.jumpBufferRemaining = c.jumpBufferSec;
    }
    else if (c.jumpBufferRemaining > 0.0f)
    {
        c.jumpBufferRemaining = std::max(0.0f, c.jumpBufferRemaining - dt);
    }
    c.jumpingFromBuffer = false;

    glm::vec2 velocity = physics.getLinearVelocity(entity);

    // --- Horizontal movement ---
    const float accel = c.onGround ? c.acceleration : c.airAcceleration;
    if (std::abs(input.inputX) > 0.01f)
    {
        const float target = input.inputX * c.maxSpeed;
        const float dv = target - velocity.x;
        const float step = std::min(std::abs(dv), accel * dt);
        velocity.x += (dv >= 0.0f ? step : -step);
    }
    else if (c.onGround)
    {
        // Ground friction deceleration when no input.
        const float step = std::min(std::abs(velocity.x), c.groundFriction * dt);
        velocity.x -= (velocity.x > 0.0f ? step : -step);
    }

    // --- Jumping ---
    bool jumpedThisFrame = false;
    const bool canJump = c.onGround || c.timeSinceGrounded <= c.coyoteTimeSec;
    if (c.jumpBufferRemaining > 0.0f && canJump)
    {
        velocity.y = c.jumpVelocity;
        c.jumpBufferRemaining = 0.0f;
        c.jumpingFromBuffer = true;
        jumpedThisFrame = true;
        // Consume coyote: you can't double-dip by releasing mid-buffer.
        c.timeSinceGrounded = c.coyoteTimeSec + 1.0f;
    }

    // --- Variable-height jump: cut upward velocity when button released ---
    if (!input.jumpHeld && velocity.y > 0.0f && !jumpedThisFrame)
    {
        velocity.y *= std::clamp(c.variableJumpCut, 0.0f, 1.0f);
    }

    // --- Wall slide: cap falling speed when hugging a wall ---
    if (c.onWall && !c.onGround && velocity.y < -c.wallSlideMaxSpeed)
    {
        velocity.y = -c.wallSlideMaxSpeed;
    }

    physics.setLinearVelocity(entity, velocity);
    return jumpedThisFrame;
}

} // namespace Vestige
