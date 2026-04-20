// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_character_controller_2d.cpp
/// @brief Unit tests for CharacterController2DComponent +
/// stepCharacterController2D (Phase 9F-4).
#include "physics/physics_world.h"
#include "scene/character_controller_2d_component.h"
#include "scene/collider_2d_component.h"
#include "scene/entity.h"
#include "scene/rigid_body_2d_component.h"
#include "systems/physics2d_system.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace Vestige;

namespace
{

struct ControllerFixture
{
    PhysicsWorld world;
    Physics2DSystem system;
    std::unique_ptr<Entity> character;

    ControllerFixture()
    {
        world.initialize();
        system.setPhysicsWorldForTesting(&world);

        character = std::make_unique<Entity>("Hero");
        auto* rb = character->addComponent<RigidBody2DComponent>();
        rb->type = BodyType2D::Dynamic;
        rb->mass = 1.0f;
        rb->fixedRotation = true;
        auto* cc = character->addComponent<Collider2DComponent>();
        cc->shape = ColliderShape2D::Capsule;
        cc->radius = 0.4f;
        cc->height = 1.8f;
        character->addComponent<CharacterController2DComponent>();
        system.ensureBody(*character);
    }

    ~ControllerFixture() { world.shutdown(); }

    CharacterController2DComponent& controller()
    {
        return *character->getComponent<CharacterController2DComponent>();
    }
};

} // namespace

TEST(CharacterController2D, HorizontalInputAcceleratesOnGround)
{
    ControllerFixture fx;
    fx.controller().onGround = true;
    CharacterControl2DInput input;
    input.inputX = 1.0f;

    for (int i = 0; i < 30; ++i)
    {
        stepCharacterController2D(fx.controller(), *fx.character, fx.system,
                                  input, 1.0f / 60.0f);
        fx.world.update(1.0f / 60.0f);
        fx.controller().onGround = true;  // re-assert ground each step
    }
    auto v = fx.system.getLinearVelocity(*fx.character);
    EXPECT_GT(v.x, fx.controller().maxSpeed * 0.9f);
}

TEST(CharacterController2D, JumpRequiresGroundOrCoyoteTime)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onGround = true;

    CharacterControl2DInput input;
    input.wantsJump = true;
    input.jumpHeld = true;

    const bool jumped = stepCharacterController2D(ctrl, *fx.character, fx.system,
                                                  input, 1.0f / 60.0f);
    EXPECT_TRUE(jumped);
    auto v = fx.system.getLinearVelocity(*fx.character);
    EXPECT_GT(v.y, ctrl.jumpVelocity * 0.9f);
}

TEST(CharacterController2D, CoyoteTimeAllowsLateJump)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onGround = true;
    // Advance a frame with ground, then leave ground.
    stepCharacterController2D(ctrl, *fx.character, fx.system, {}, 0.016f);
    ctrl.onGround = false;

    // Simulate a few frames of "air" within the coyote window.
    for (int i = 0; i < 3; ++i)
    {
        stepCharacterController2D(ctrl, *fx.character, fx.system, {}, 0.016f);
    }
    ASSERT_LE(ctrl.timeSinceGrounded, ctrl.coyoteTimeSec);

    // Late jump.
    CharacterControl2DInput input;
    input.wantsJump = true;
    input.jumpHeld = true;
    const bool jumped = stepCharacterController2D(ctrl, *fx.character, fx.system,
                                                  input, 0.016f);
    EXPECT_TRUE(jumped);
}

TEST(CharacterController2D, JumpBufferPersistsUntilLanding)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onGround = false;

    // Press jump mid-air — buffered for jumpBufferSec.
    CharacterControl2DInput press;
    press.wantsJump = true;
    stepCharacterController2D(ctrl, *fx.character, fx.system, press, 0.016f);
    EXPECT_GT(ctrl.jumpBufferRemaining, 0.0f);

    // Touch down within the buffer — next step should auto-jump.
    ctrl.onGround = true;
    const bool jumped = stepCharacterController2D(ctrl, *fx.character, fx.system,
                                                  {}, 0.016f);
    EXPECT_TRUE(jumped);
    EXPECT_TRUE(ctrl.jumpingFromBuffer);
}

TEST(CharacterController2D, JumpBufferExpires)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onGround = false;

    CharacterControl2DInput press;
    press.wantsJump = true;
    stepCharacterController2D(ctrl, *fx.character, fx.system, press, 0.016f);

    // Run several frames past the buffer without landing.
    for (int i = 0; i < 20; ++i)
    {
        stepCharacterController2D(ctrl, *fx.character, fx.system, {}, 0.016f);
    }
    EXPECT_FLOAT_EQ(ctrl.jumpBufferRemaining, 0.0f);

    ctrl.onGround = true;
    const bool jumped = stepCharacterController2D(ctrl, *fx.character, fx.system,
                                                  {}, 0.016f);
    EXPECT_FALSE(jumped);
}

TEST(CharacterController2D, VariableJumpCutReducesUpwardVelocity)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onGround = true;
    ctrl.variableJumpCut = 0.5f;

    CharacterControl2DInput press;
    press.wantsJump = true;
    press.jumpHeld = true;
    stepCharacterController2D(ctrl, *fx.character, fx.system, press, 0.016f);
    ctrl.onGround = false;

    auto vBeforeCut = fx.system.getLinearVelocity(*fx.character);
    ASSERT_GT(vBeforeCut.y, 0.0f);

    CharacterControl2DInput release;
    release.jumpHeld = false;
    stepCharacterController2D(ctrl, *fx.character, fx.system, release, 0.016f);

    auto vAfterCut = fx.system.getLinearVelocity(*fx.character);
    EXPECT_LT(vAfterCut.y, vBeforeCut.y * 0.6f);
}

TEST(CharacterController2D, WallSlideCapsFallSpeed)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onWall = true;
    ctrl.onGround = false;
    ctrl.wallSlideMaxSpeed = 2.0f;

    // Shove a high downward velocity at the body before the step.
    fx.system.setLinearVelocity(*fx.character, glm::vec2(0.0f, -20.0f));
    stepCharacterController2D(ctrl, *fx.character, fx.system, {}, 0.016f);
    auto v = fx.system.getLinearVelocity(*fx.character);
    EXPECT_GE(v.y, -2.5f);  // clamped (small slack for intermediate math)
}

TEST(CharacterController2D, NoInputAppliesGroundFriction)
{
    ControllerFixture fx;
    auto& ctrl = fx.controller();
    ctrl.onGround = true;
    fx.system.setLinearVelocity(*fx.character, glm::vec2(5.0f, 0.0f));

    // No input — friction should decelerate.
    for (int i = 0; i < 30; ++i)
    {
        stepCharacterController2D(ctrl, *fx.character, fx.system, {}, 1.0f / 60.0f);
    }
    auto v = fx.system.getLinearVelocity(*fx.character);
    EXPECT_LT(std::abs(v.x), 2.0f);
}

TEST(CharacterController2D, CloneResetsRuntimeState)
{
    CharacterController2DComponent original;
    original.jumpVelocity = 15.0f;
    original.onGround = true;
    original.jumpBufferRemaining = 0.05f;

    auto cloned = original.clone();
    auto* c = static_cast<CharacterController2DComponent*>(cloned.get());
    EXPECT_FLOAT_EQ(c->jumpVelocity, 15.0f);   // authoring carried
    EXPECT_FALSE(c->onGround);                 // runtime reset
    EXPECT_FLOAT_EQ(c->jumpBufferRemaining, 0.0f);
}
