// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scene.cpp
/// @brief Unit tests for the Scene and SceneManager systems.
#include "scene/camera_component.h"
#include "renderer/material.h"
#include "scene/light_component.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

using namespace Vestige;

TEST(SceneTest, CreateEntity)
{
    Scene scene("TestScene");
    Entity* entity = scene.createEntity("Box");

    EXPECT_NE(entity, nullptr);
    EXPECT_EQ(entity->getName(), "Box");
}

TEST(SceneTest, FindEntity)
{
    Scene scene("TestScene");
    scene.createEntity("Alpha");
    scene.createEntity("Beta");

    EXPECT_NE(scene.findEntity("Alpha"), nullptr);
    EXPECT_NE(scene.findEntity("Beta"), nullptr);
    EXPECT_EQ(scene.findEntity("Gamma"), nullptr);
}

TEST(SceneTest, GetNameAndRoot)
{
    Scene scene("MyScene");
    EXPECT_EQ(scene.getName(), "MyScene");
    EXPECT_NE(scene.getRoot(), nullptr);
}

TEST(SceneTest, UpdateDoesNotCrash)
{
    Scene scene("TestScene");
    scene.createEntity("A");
    scene.createEntity("B");
    EXPECT_NO_THROW(scene.update(0.016f));
}

// --- SceneManager tests ---

TEST(SceneManagerTest, CreateScene)
{
    SceneManager manager;
    Scene* scene = manager.createScene("Level1");

    EXPECT_NE(scene, nullptr);
    EXPECT_EQ(scene->getName(), "Level1");
    EXPECT_EQ(manager.getSceneCount(), 1u);
}

TEST(SceneManagerTest, SetActiveScene)
{
    SceneManager manager;
    manager.createScene("Level1");
    manager.createScene("Level2");

    EXPECT_TRUE(manager.setActiveScene("Level2"));
    EXPECT_EQ(manager.getActiveScene()->getName(), "Level2");
}

TEST(SceneManagerTest, FirstSceneBecomesActive)
{
    SceneManager manager;
    manager.createScene("First");

    EXPECT_NE(manager.getActiveScene(), nullptr);
    EXPECT_EQ(manager.getActiveScene()->getName(), "First");
}

TEST(SceneManagerTest, SetActiveSceneInvalidName)
{
    SceneManager manager;
    manager.createScene("Level1");
    EXPECT_FALSE(manager.setActiveScene("NonExistent"));
}

TEST(SceneManagerTest, RemoveScene)
{
    SceneManager manager;
    manager.createScene("Level1");
    EXPECT_EQ(manager.getSceneCount(), 1u);
    manager.removeScene("Level1");
    EXPECT_EQ(manager.getSceneCount(), 0u);
}

TEST(SceneManagerTest, UpdateDoesNotCrashWithNoActiveScene)
{
    SceneManager manager;
    EXPECT_NO_THROW(manager.update(0.016f));
}

// --- forEachEntity tests ---

TEST(SceneTest, ForEachEntityVisitsAll)
{
    Scene scene("TestScene");
    scene.createEntity("A");
    scene.createEntity("B");
    scene.createEntity("C");

    int count = 0;
    scene.forEachEntity([&](Entity& entity)
    {
        (void)entity;
        count++;
    });

    EXPECT_EQ(count, 3);
}

TEST(SceneTest, ForEachEntityVisitsChildren)
{
    Scene scene("TestScene");
    Entity* parent = scene.createEntity("Parent");
    parent->addChild(std::make_unique<Entity>("Child1"));
    parent->addChild(std::make_unique<Entity>("Child2"));
    scene.rebuildEntityIndex();

    std::vector<std::string> names;
    scene.forEachEntity([&](Entity& entity)
    {
        names.push_back(entity.getName());
    });

    EXPECT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Parent");
    EXPECT_EQ(names[1], "Child1");
    EXPECT_EQ(names[2], "Child2");
}

TEST(SceneTest, ForEachEntityEmptyScene)
{
    Scene scene("Empty");
    int count = 0;
    scene.forEachEntity([&](Entity& entity)
    {
        (void)entity;
        count++;
    });
    EXPECT_EQ(count, 0);
}

TEST(SceneTest, ForEachEntityConst)
{
    Scene scene("TestScene");
    scene.createEntity("A");
    scene.createEntity("B");

    const Scene& constScene = scene;
    int count = 0;
    constScene.forEachEntity([&](const Entity& entity)
    {
        (void)entity;
        count++;
    });

    EXPECT_EQ(count, 2);
}

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 3 S1 (also closes S7): Scene::removeEntity must null
// m_activeCamera when the removed subtree owns it. The active-camera
// field is a raw CameraComponent* with no ownership — if the entity
// carrying that component is deleted, the pointer dangles. Renderer
// code dereferences m_activeCamera every frame, so the fix prevents a
// use-after-free crash after "delete camera entity" from the editor.
// ---------------------------------------------------------------------------

TEST(SceneEntityLifecycle, RemoveActiveCameraEntityNullsActiveCamera_S1)
{
    Scene scene("RemoveActiveCamera");
    Entity* cam = scene.createEntity("Camera");
    auto* camComp = cam->addComponent<CameraComponent>();
    scene.setActiveCamera(camComp);

    ASSERT_EQ(scene.getActiveCamera(), camComp)
        << "precondition: active camera must be set before removal";

    const bool removed = scene.removeEntity(cam->getId());

    EXPECT_TRUE(removed);
    EXPECT_EQ(scene.getActiveCamera(), nullptr)
        << "after removing the entity that owns the active camera, the "
           "active-camera pointer must be nulled to avoid dangling — "
           "renderer dereferences this every frame";
}

TEST(SceneEntityLifecycle, RemoveDescendantCameraEntityNullsActiveCamera_S1)
{
    // The active-camera entity is nested under a parent. Removing the
    // parent deletes the subtree including the camera; the recursion
    // has to spot the active camera inside that subtree, not just at
    // the top-level entity being removed.
    Scene scene("RemoveParent");
    Entity* rig = scene.createEntity("CameraRig");
    Entity* cam = rig->addChild(std::make_unique<Entity>("NestedCamera"));
    auto* camComp = cam->addComponent<CameraComponent>();
    scene.setActiveCamera(camComp);

    const bool removed = scene.removeEntity(rig->getId());

    EXPECT_TRUE(removed);
    EXPECT_EQ(scene.getActiveCamera(), nullptr)
        << "removing a parent whose descendant owns the active camera "
           "must still null the active-camera pointer";
}

TEST(SceneEntityLifecycle, RemoveUnrelatedEntityDoesNotAffectActiveCamera_S1)
{
    // False-positive guard: removing an entity that has no bearing on
    // the active camera must leave the active-camera pointer alone.
    Scene scene("KeepActiveCamera");
    Entity* cam = scene.createEntity("Camera");
    auto* camComp = cam->addComponent<CameraComponent>();
    scene.setActiveCamera(camComp);

    Entity* prop = scene.createEntity("UnrelatedProp");
    scene.removeEntity(prop->getId());

    EXPECT_EQ(scene.getActiveCamera(), camComp)
        << "removing an entity without the active camera must leave the "
           "pointer intact";
}

TEST(SceneEntityLifecycle, RemoveCameraComponentNullsActiveCamera_0852)
{
    // The entity stays; only its CameraComponent goes. The scene must not
    // hand back the freed component.
    Scene scene("RemoveCameraComponent");
    Entity* cam = scene.createEntity("Camera");
    auto* camComp = cam->addComponent<CameraComponent>();
    scene.setActiveCamera(camComp);
    ASSERT_EQ(scene.getActiveCamera(), camComp);

    cam->removeComponent<CameraComponent>();

    EXPECT_EQ(scene.getActiveCamera(), nullptr)
        << "the active camera component was freed; the scene must look it "
           "up again rather than return a dangling pointer";
}

TEST(SceneEntityLifecycle, ClearEntitiesNullsActiveCamera_S1)
{
    // clearEntities already nulls m_activeCamera (scene.cpp:125). This
    // test pins that invariant so a future refactor doesn't lose it
    // while the fix lives nearby.
    Scene scene("ClearScene");
    Entity* cam = scene.createEntity("Camera");
    auto* camComp = cam->addComponent<CameraComponent>();
    scene.setActiveCamera(camComp);

    { Scene::Replacement replacement(scene); }

    EXPECT_EQ(scene.getActiveCamera(), nullptr);
}

// 3D_E-0857: a spot light's direction is in its entity's space, so turning
// the entity turns the beam. The parent's rotation counts too.
TEST(SceneTest, SpotLightDirectionFollowsEntityRotation_0857)
{
    Scene scene("Spot");
    Entity* parent = scene.createEntity("Parent");
    Entity* lamp = scene.createEntity("Lamp");
    ASSERT_TRUE(scene.reparentEntity(lamp->getId(), parent->getId()));
    auto* spot = lamp->addComponent<SpotLightComponent>();
    spot->light.direction = glm::vec3(0.0f, -1.0f, 0.0f);

    // Unrotated: the direction is unchanged.
    SceneRenderData flat = scene.collectRenderData();
    ASSERT_EQ(flat.spotLights.size(), 1u);
    EXPECT_NEAR(flat.spotLights[0].direction.y, -1.0f, 1e-5f);

    // +90 degrees about Z on the entity turns straight down into +X;
    // a further +90 about Y on the parent turns +X into -Z.
    lamp->transform.rotation = glm::vec3(0.0f, 0.0f, 90.0f);
    parent->transform.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
    scene.update(0.0f);
    SceneRenderData turned = scene.collectRenderData();
    ASSERT_EQ(turned.spotLights.size(), 1u);
    const glm::vec3 d = turned.spotLights[0].direction;
    EXPECT_NEAR(d.x, 0.0f, 1e-5f);
    EXPECT_NEAR(d.y, 0.0f, 1e-5f);
    EXPECT_NEAR(d.z, -1.0f, 1e-5f);
    // The component keeps its own (local) direction.
    EXPECT_NEAR(spot->light.direction.y, -1.0f, 1e-5f);

    // A zero-scale entity has no direction to turn: keep the authored one,
    // not a NaN.
    lamp->transform.scale = glm::vec3(0.0f);
    scene.update(0.0f);
    SceneRenderData squashed = scene.collectRenderData();
    ASSERT_EQ(squashed.spotLights.size(), 1u);
    EXPECT_FLOAT_EQ(squashed.spotLights[0].direction.y, -1.0f);
}

// 3D_E-0874: a glowing object that is switched off (its MeshRenderer
// disabled) is not drawn, so it must not light its surroundings either.
TEST(SceneTest, DisabledRendererGivesNoEmissiveLight)
{
    Scene scene("Glow");
    Entity* lamp = scene.createEntity("Lamp");
    auto material = std::make_shared<Material>();
    material->setEmissive(glm::vec3(1.0f, 0.5f, 0.0f));
    auto* renderer = lamp->addComponent<MeshRenderer>(nullptr, material);
    lamp->addComponent<EmissiveLightComponent>();

    EXPECT_EQ(scene.collectRenderData().pointLights.size(), 1u);

    renderer->setEnabled(false);
    EXPECT_EQ(scene.collectRenderData().pointLights.size(), 0u);
}
