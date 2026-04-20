// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file character_controller_2d_component.h
/// @brief CharacterController2DComponent — platformer movement with
/// coyote time, jump buffering, and wall slide (Phase 9F-4).
///
/// The controller sits on top of a Physics2D body — it doesn't do
/// collision detection itself. Each frame, gameplay code (or a
/// scripting node) calls `stepCharacterController2D` with the frame's
/// input state (inputX, wantsJump, jumpHeld) and the current ground
/// contact from Physics2DSystem. The helper updates timers and writes
/// a target velocity back onto the body.
#pragma once

#include "scene/component.h"

#include <memory>

namespace Vestige
{

class Entity;
class Physics2DSystem;

/// @brief Per-frame input snapshot fed to the controller.
struct CharacterControl2DInput
{
    float inputX     = 0.0f;   ///< -1 = full left, +1 = full right.
    bool  wantsJump  = false;  ///< Edge-triggered: true the frame the
                               ///< jump button was pressed.
    bool  jumpHeld   = false;  ///< Sustained: true while the button is down.
};

class CharacterController2DComponent : public Component
{
public:
    CharacterController2DComponent() = default;
    ~CharacterController2DComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    // --- Authored tuning ---

    float maxSpeed         = 8.0f;   ///< m/s target horizontal speed.
    float acceleration     = 60.0f;  ///< m/s^2 on-ground acceleration.
    float airAcceleration  = 25.0f;  ///< m/s^2 in-air acceleration.
    float groundFriction   = 40.0f;  ///< m/s^2 deceleration when no input.
    float jumpVelocity     = 12.0f;  ///< Initial upward velocity on jump.
    float variableJumpCut  = 0.5f;   ///< Multiplier applied to upward velocity
                                     ///  when jump is released early (0..1).
    float coyoteTimeSec    = 0.12f;  ///< Window to jump after leaving ground.
    float jumpBufferSec    = 0.10f;  ///< Window before landing that jump
                                     ///  remembers a press.
    float wallSlideMaxSpeed = 2.0f;  ///< Max downward speed while wall-sliding.

    // --- Runtime state (controller writes) ---

    bool  onGround              = false;
    bool  onWall                = false;
    float timeSinceGrounded     = 999.0f;  ///< Seconds since last ground frame.
    float jumpBufferRemaining   = 0.0f;    ///< Seconds of buffered jump left.
    bool  jumpingFromBuffer     = false;   ///< Set for one frame after a buffered jump fires.
    int   wallDirection         = 0;       ///< -1 = wall on left, +1 = right, 0 = none.
};

/// @brief Advances a 2D character controller by @p dt seconds.
///
/// Reads the current body velocity + ground flags, applies the input,
/// and writes a new linear velocity to the Physics2D body. Requires:
///   - a `Physics2DSystem` to drive (null-safe — no-op if null)
///   - a `RigidBody2DComponent` on @p entity (no-op if missing)
///   - ground/wall contact flags pre-populated on the controller
///     (callers feed these from the physics contact listener — Phase
///     9F-4 ships a minimal pipeline where they can be set manually
///     from a raycast or from external gameplay logic).
///
/// The helper returns `true` if a jump actually fired this frame so
/// gameplay code can play a sound / spawn particles.
bool stepCharacterController2D(CharacterController2DComponent& controller,
                               Entity& entity,
                               Physics2DSystem& physics,
                               const CharacterControl2DInput& input,
                               float deltaTime);

} // namespace Vestige
