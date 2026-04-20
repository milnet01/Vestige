// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_game_templates_2d.cpp
/// @brief Unit tests for Side-Scroller + Shmup starter templates (Phase 9F-5).
#include "scene/camera_2d_component.h"
#include "scene/character_controller_2d_component.h"
#include "scene/collider_2d_component.h"
#include "scene/game_templates_2d.h"
#include "scene/rigid_body_2d_component.h"
#include "scene/scene.h"
#include "scene/sprite_component.h"
#include "scene/tilemap_component.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

Entity* findByName(Entity& root, const std::string& name)
{
    if (root.getName() == name)
    {
        return &root;
    }
    for (const auto& child : root.getChildren())
    {
        if (auto* f = findByName(*child, name))
        {
            return f;
        }
    }
    return nullptr;
}

} // namespace

TEST(GameTemplates2D, SideScrollerSpawnsExpectedEntities)
{
    Scene scene("SideScrollerTest");
    auto* root = createSideScrollerTemplate(scene);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getName(), "SideScrollerWorld");

    EXPECT_NE(findByName(*root, "Player"), nullptr);
    EXPECT_NE(findByName(*root, "Ground"), nullptr);
    EXPECT_NE(findByName(*root, "Platform_A"), nullptr);
    EXPECT_NE(findByName(*root, "Platform_B"), nullptr);
    EXPECT_NE(findByName(*root, "Camera"), nullptr);
}

TEST(GameTemplates2D, SideScrollerPlayerHasExpectedComponents)
{
    Scene scene("PlayerTest");
    auto* root = createSideScrollerTemplate(scene);
    ASSERT_NE(root, nullptr);
    auto* player = findByName(*root, "Player");
    ASSERT_NE(player, nullptr);

    auto* rb = player->getComponent<RigidBody2DComponent>();
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(rb->type, BodyType2D::Dynamic);
    EXPECT_TRUE(rb->fixedRotation);

    auto* cc = player->getComponent<Collider2DComponent>();
    ASSERT_NE(cc, nullptr);
    EXPECT_EQ(cc->shape, ColliderShape2D::Capsule);

    EXPECT_NE(player->getComponent<CharacterController2DComponent>(), nullptr);
    // No atlas => no sprite component by design (designer fills it in).
    EXPECT_EQ(player->getComponent<SpriteComponent>(), nullptr);
}

TEST(GameTemplates2D, SideScrollerCameraFollowsAndClamps)
{
    Scene scene("CameraTest");
    auto* root = createSideScrollerTemplate(scene);
    ASSERT_NE(root, nullptr);
    auto* cam = findByName(*root, "Camera");
    ASSERT_NE(cam, nullptr);
    auto* c2d = cam->getComponent<Camera2DComponent>();
    ASSERT_NE(c2d, nullptr);
    EXPECT_TRUE(c2d->clampToBounds);
    EXPECT_GT(c2d->orthoHalfHeight, 0.0f);
    EXPECT_GT(c2d->deadzoneHalfSize.x, 0.0f);
}

TEST(GameTemplates2D, SideScrollerGroundIsStaticBox)
{
    Scene scene("GroundTest");
    auto* root = createSideScrollerTemplate(scene);
    auto* ground = findByName(*root, "Ground");
    ASSERT_NE(ground, nullptr);
    auto* rb = ground->getComponent<RigidBody2DComponent>();
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(rb->type, BodyType2D::Static);
    auto* cc = ground->getComponent<Collider2DComponent>();
    ASSERT_NE(cc, nullptr);
    EXPECT_EQ(cc->shape, ColliderShape2D::Box);
}

TEST(GameTemplates2D, ShmupSpawnsExpectedEntities)
{
    Scene scene("ShmupTest");
    auto* root = createShmupTemplate(scene);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getName(), "ShmupWorld");
    EXPECT_NE(findByName(*root, "Player"), nullptr);
    EXPECT_NE(findByName(*root, "ScrollingBackdrop"), nullptr);
    EXPECT_NE(findByName(*root, "Camera"), nullptr);
}

TEST(GameTemplates2D, ShmupPlayerIsKinematicNoGravity)
{
    Scene scene("ShmupPlayer");
    auto* root = createShmupTemplate(scene);
    auto* player = findByName(*root, "Player");
    ASSERT_NE(player, nullptr);
    auto* rb = player->getComponent<RigidBody2DComponent>();
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(rb->type, BodyType2D::Kinematic);
    EXPECT_FLOAT_EQ(rb->gravityScale, 0.0f);
    // No controller — shmup player is direct-input.
    EXPECT_EQ(player->getComponent<CharacterController2DComponent>(), nullptr);
}

TEST(GameTemplates2D, ShmupBackdropIsTilemap)
{
    Scene scene("ShmupBackdrop");
    auto* root = createShmupTemplate(scene);
    auto* backdrop = findByName(*root, "ScrollingBackdrop");
    ASSERT_NE(backdrop, nullptr);
    auto* tm = backdrop->getComponent<TilemapComponent>();
    ASSERT_NE(tm, nullptr);
    ASSERT_EQ(tm->layers.size(), 1u);
    EXPECT_EQ(tm->layers[0].name, "stars");
    EXPECT_LT(tm->sortingLayer, 0);  // behind everything else
}

TEST(GameTemplates2D, ShmupCameraLockedNoSmoothing)
{
    Scene scene("ShmupCam");
    auto* root = createShmupTemplate(scene);
    auto* cam = findByName(*root, "Camera");
    auto* c2d = cam->getComponent<Camera2DComponent>();
    ASSERT_NE(c2d, nullptr);
    EXPECT_FLOAT_EQ(c2d->smoothTimeSec, 0.0f);
    EXPECT_TRUE(c2d->clampToBounds);
}

TEST(GameTemplates2D, FailsSoftOnUnattachedScene)
{
    // A Scene with no managed lifetime still works — createEntity is
    // always valid.
    Scene scene("Smoke");
    auto* r1 = createSideScrollerTemplate(scene);
    auto* r2 = createShmupTemplate(scene);
    EXPECT_NE(r1, nullptr);
    EXPECT_NE(r2, nullptr);
    EXPECT_NE(r1, r2);
}
