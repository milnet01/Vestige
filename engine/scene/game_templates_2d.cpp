// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file game_templates_2d.cpp
/// @brief Side-Scroller + Shmup starter scenes (Phase 9F-5).
#include "scene/game_templates_2d.h"

#include "scene/camera_2d_component.h"
#include "scene/character_controller_2d_component.h"
#include "scene/collider_2d_component.h"
#include "scene/entity.h"
#include "scene/rigid_body_2d_component.h"
#include "scene/scene.h"
#include "scene/sprite_component.h"
#include "scene/tilemap_component.h"

namespace Vestige
{

namespace
{

Entity* addPlatform(Entity& parent, const std::string& name,
                    const glm::vec2& centre, const glm::vec2& halfExtents,
                    const GameTemplate2DConfig& config)
{
    auto child = std::make_unique<Entity>(name);
    child->transform.position = glm::vec3(centre, 0.0f);
    auto* rb = child->addComponent<RigidBody2DComponent>();
    rb->type = BodyType2D::Static;
    auto* cc = child->addComponent<Collider2DComponent>();
    cc->shape = ColliderShape2D::Box;
    cc->halfExtents = halfExtents;
    if (config.atlas)
    {
        auto* sprite = child->addComponent<SpriteComponent>();
        sprite->atlas = config.atlas;
        sprite->frameName = config.platformFrameName;
        sprite->pixelsPerUnit = config.pixelsPerUnit;
        sprite->sortingLayer = 0;
        sprite->orderInLayer = 0;
    }
    return parent.addChild(std::move(child));
}

} // namespace

Entity* createSideScrollerTemplate(Scene& scene, const GameTemplate2DConfig& config)
{
    // Root anchor so the template lives as a single subtree — easier to
    // delete or duplicate later.
    auto* root = scene.createEntity("SideScrollerWorld");
    if (!root)
    {
        return nullptr;
    }

    // --- Player ---
    auto playerOwned = std::make_unique<Entity>("Player");
    playerOwned->transform.position = glm::vec3(0.0f, 3.0f, 0.0f);
    {
        auto* rb = playerOwned->addComponent<RigidBody2DComponent>();
        rb->type = BodyType2D::Dynamic;
        rb->mass = 1.0f;
        rb->fixedRotation = true;
        rb->linearDamping = 0.0f;

        auto* cc = playerOwned->addComponent<Collider2DComponent>();
        cc->shape = ColliderShape2D::Capsule;
        cc->radius = 0.4f;
        cc->height = 1.8f;

        playerOwned->addComponent<CharacterController2DComponent>();

        if (config.atlas)
        {
            auto* sprite = playerOwned->addComponent<SpriteComponent>();
            sprite->atlas = config.atlas;
            sprite->frameName = config.playerFrameName;
            sprite->pixelsPerUnit = config.pixelsPerUnit;
            sprite->sortingLayer = 10;
            sprite->orderInLayer = 0;
        }
    }
    Entity* player = root->addChild(std::move(playerOwned));

    // --- Ground: wide static edge-chain (frictional floor) ---
    {
        auto ground = std::make_unique<Entity>("Ground");
        ground->transform.position = glm::vec3(0.0f, -1.0f, 0.0f);
        auto* rb = ground->addComponent<RigidBody2DComponent>();
        rb->type = BodyType2D::Static;
        auto* cc = ground->addComponent<Collider2DComponent>();
        cc->shape = ColliderShape2D::Box;
        cc->halfExtents = glm::vec2(20.0f, 0.5f);
        cc->zThickness = 0.2f;
        root->addChild(std::move(ground));
    }

    // --- Two starter platforms a designer can move / duplicate ---
    addPlatform(*root, "Platform_A", glm::vec2(5.0f, 1.5f), glm::vec2(1.5f, 0.25f), config);
    addPlatform(*root, "Platform_B", glm::vec2(10.0f, 3.0f), glm::vec2(1.5f, 0.25f), config);

    // --- Camera2D — follows Player ---
    {
        auto cam = std::make_unique<Entity>("Camera");
        cam->transform.position = glm::vec3(0.0f, 2.0f, -10.0f);
        auto* c2d = cam->addComponent<Camera2DComponent>();
        c2d->orthoHalfHeight = 5.0f;
        c2d->followOffset = glm::vec2(0.0f, 2.0f);
        c2d->deadzoneHalfSize = glm::vec2(1.5f, 0.5f);
        c2d->smoothTimeSec = 0.2f;
        c2d->clampToBounds = true;
        c2d->worldBounds = glm::vec4(-20.0f, -2.0f, 20.0f, 15.0f);
        root->addChild(std::move(cam));
    }

    // Player pointer used by callers wanting to name the target entity.
    (void)player;
    return root;
}

Entity* createShmupTemplate(Scene& scene, const GameTemplate2DConfig& config)
{
    auto* root = scene.createEntity("ShmupWorld");
    if (!root)
    {
        return nullptr;
    }

    // --- Player (kinematic; input drives velocity directly) ---
    {
        auto owned = std::make_unique<Entity>("Player");
        owned->transform.position = glm::vec3(0.0f, -5.0f, 0.0f);

        auto* rb = owned->addComponent<RigidBody2DComponent>();
        rb->type = BodyType2D::Kinematic;
        rb->gravityScale = 0.0f;

        auto* cc = owned->addComponent<Collider2DComponent>();
        cc->shape = ColliderShape2D::Box;
        cc->halfExtents = glm::vec2(0.4f, 0.4f);

        if (config.atlas)
        {
            auto* sprite = owned->addComponent<SpriteComponent>();
            sprite->atlas = config.atlas;
            sprite->frameName = config.playerFrameName;
            sprite->pixelsPerUnit = config.pixelsPerUnit;
            sprite->sortingLayer = 10;
        }
        root->addChild(std::move(owned));
    }

    // --- Scrolling backdrop (tilemap stub — designer paints tiles later) ---
    {
        auto owned = std::make_unique<Entity>("ScrollingBackdrop");
        auto* tm = owned->addComponent<TilemapComponent>();
        tm->atlas = config.atlas;
        tm->tileWorldSize = 1.0f;
        tm->pixelsPerUnit = config.pixelsPerUnit;
        tm->sortingLayer = -100;  // behind everything
        tm->addLayer("stars", 16, 32);
        root->addChild(std::move(owned));
    }

    // --- Camera2D — fixed, world-bounded ---
    {
        auto cam = std::make_unique<Entity>("Camera");
        cam->transform.position = glm::vec3(0.0f, 0.0f, -10.0f);
        auto* c2d = cam->addComponent<Camera2DComponent>();
        c2d->orthoHalfHeight = 8.0f;
        c2d->smoothTimeSec = 0.0f;      // locked — shmup camera doesn't follow
        c2d->clampToBounds = true;
        c2d->worldBounds = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);  // pinned to (0,0)
        root->addChild(std::move(cam));
    }

    return root;
}

} // namespace Vestige
