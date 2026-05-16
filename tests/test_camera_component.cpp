// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_camera_component.cpp
/// @brief Unit tests for CameraComponent.
#include <gtest/gtest.h>

#include "scene/camera_component.h"
#include "scene/entity.h"
#include "scene/scene.h"

using namespace Vestige;

// --- Basic construction ---

TEST(CameraComponent, DefaultConstruction)
{
    CameraComponent cam;
    EXPECT_FLOAT_EQ(cam.fov, DEFAULT_FOV);
    EXPECT_FLOAT_EQ(cam.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(cam.farPlane, 1000.0f);
    EXPECT_FLOAT_EQ(cam.orthoSize, 10.0f);
    EXPECT_EQ(cam.projectionType, ProjectionType::PERSPECTIVE);
}

TEST(CameraComponent, Clone)
{
    CameraComponent cam;
    cam.fov = 90.0f;
    cam.nearPlane = 0.5f;
    cam.farPlane = 500.0f;
    cam.orthoSize = 20.0f;
    cam.projectionType = ProjectionType::ORTHOGRAPHIC;
    cam.setEnabled(false);

    auto cloned = cam.clone();
    ASSERT_NE(cloned, nullptr);

    auto* copy = dynamic_cast<CameraComponent*>(cloned.get());
    ASSERT_NE(copy, nullptr);
    EXPECT_FLOAT_EQ(copy->fov, 90.0f);
    EXPECT_FLOAT_EQ(copy->nearPlane, 0.5f);
    EXPECT_FLOAT_EQ(copy->farPlane, 500.0f);
    EXPECT_FLOAT_EQ(copy->orthoSize, 20.0f);
    EXPECT_EQ(copy->projectionType, ProjectionType::ORTHOGRAPHIC);
    EXPECT_FALSE(copy->isEnabled());
}

// --- Entity integration ---

TEST(CameraComponent, AttachToEntity)
{
    Entity entity("CamEntity");
    auto* cam = entity.addComponent<CameraComponent>();
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(entity.hasComponent<CameraComponent>());
    EXPECT_EQ(cam->getOwner(), &entity);
}

TEST(CameraComponent, WorldPositionFromEntity)
{
    Entity entity("CamEntity");
    entity.transform.position = glm::vec3(5.0f, 10.0f, 15.0f);
    entity.update(0.0f);

    auto* cam = entity.addComponent<CameraComponent>();
    glm::vec3 pos = cam->getWorldPosition();
    EXPECT_NEAR(pos.x, 5.0f, 0.001f);
    EXPECT_NEAR(pos.y, 10.0f, 0.001f);
    EXPECT_NEAR(pos.z, 15.0f, 0.001f);
}

TEST(CameraComponent, ForwardDirectionDefault)
{
    Entity entity("CamEntity");
    entity.update(0.0f);

    auto* cam = entity.addComponent<CameraComponent>();
    glm::vec3 forward = cam->getForward();
    // Default entity rotation (0,0,0) means forward is -Z
    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, -1.0f, 0.001f);
}

// --- Projection matrices ---

TEST(CameraComponent, PerspectiveProjectionNotIdentity)
{
    Entity entity("CamEntity");
    entity.update(0.0f);
    auto* cam = entity.addComponent<CameraComponent>();

    // Slice 18 Ts3: dropped the reverse-Z element pins (proj[2][3],
    // proj[3][2]) — those are the structural responsibility of
    // `test_camera.cpp::ProjectionMatrixStructure`, which pins the
    // full reverse-Z layout. The CameraComponent test here only needs
    // to verify the component returns a non-identity matrix when its
    // Camera's perspective settings are applied.
    glm::mat4 proj = cam->getProjectionMatrix(16.0f / 9.0f);
    EXPECT_NE(proj, glm::mat4(1.0f));
}

TEST(CameraComponent, OrthographicProjectionNotIdentity)
{
    Entity entity("CamEntity");
    entity.update(0.0f);
    auto* cam = entity.addComponent<CameraComponent>();
    cam->projectionType = ProjectionType::ORTHOGRAPHIC;
    cam->orthoSize = 10.0f;

    glm::mat4 proj = cam->getProjectionMatrix(1.0f);
    EXPECT_NE(proj, glm::mat4(1.0f));
    // Orthographic: no perspective divide (row 2 col 3 should be 0)
    EXPECT_FLOAT_EQ(proj[2][3], 0.0f);
}

TEST(CameraComponent, CullingProjectionFiniteFar)
{
    Entity entity("CamEntity");
    entity.update(0.0f);
    auto* cam = entity.addComponent<CameraComponent>();
    cam->farPlane = 500.0f;

    glm::mat4 proj = cam->getCullingProjectionMatrix(1.0f);
    EXPECT_NE(proj, glm::mat4(1.0f));
}

// --- View matrix ---

TEST(CameraComponent, ViewMatrixFromEntityTransform)
{
    Entity entity("CamEntity");
    entity.transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
    entity.update(0.0f);

    auto* cam = entity.addComponent<CameraComponent>();
    glm::mat4 view = cam->getViewMatrix();

    // The view matrix should translate world (10,0,0) to camera origin (0,0,0).
    // Applying view to the camera's own position should give near-zero.
    glm::vec4 viewPos = view * glm::vec4(10.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(viewPos.x, 0.0f, 0.001f);
    EXPECT_NEAR(viewPos.y, 0.0f, 0.001f);
    EXPECT_NEAR(viewPos.z, 0.0f, 0.001f);
}

TEST(CameraComponent, NoOwnerReturnsIdentityView)
{
    CameraComponent cam;
    // No owner entity — should return identity
    glm::mat4 view = cam.getViewMatrix();
    EXPECT_EQ(view, glm::mat4(1.0f));
}

// --- Backward compatibility ---

TEST(CameraComponent, GetCameraReturnsSyncedCamera)
{
    Entity entity("CamEntity");
    entity.transform.position = glm::vec3(3.0f, 7.0f, -2.0f);
    entity.transform.rotation = glm::vec3(-15.0f, 45.0f, 0.0f);  // pitch=-15, yaw=45
    entity.update(0.0f);

    auto* comp = entity.addComponent<CameraComponent>();
    comp->update(0.0f);  // Mark dirty

    const Camera& camera = comp->getCamera();
    EXPECT_NEAR(camera.getPosition().x, 3.0f, 0.001f);
    EXPECT_NEAR(camera.getPosition().y, 7.0f, 0.001f);
    EXPECT_NEAR(camera.getPosition().z, -2.0f, 0.001f);
    EXPECT_NEAR(camera.getYaw(), 45.0f, 0.001f);
    EXPECT_NEAR(camera.getPitch(), -15.0f, 0.001f);
}

TEST(CameraComponent, SyncFromCamera)
{
    Entity entity("CamEntity");
    entity.update(0.0f);

    auto* comp = entity.addComponent<CameraComponent>();

    Camera cam(glm::vec3(1.0f, 2.0f, 3.0f), -90.0f, 10.0f);
    comp->syncFromCamera(cam);

    EXPECT_NEAR(entity.transform.position.x, 1.0f, 0.001f);
    EXPECT_NEAR(entity.transform.position.y, 2.0f, 0.001f);
    EXPECT_NEAR(entity.transform.position.z, 3.0f, 0.001f);
    EXPECT_NEAR(entity.transform.rotation.x, 10.0f, 0.001f);  // pitch
    EXPECT_NEAR(entity.transform.rotation.y, -90.0f, 0.001f);  // yaw
    EXPECT_FLOAT_EQ(comp->fov, DEFAULT_FOV);
}

TEST(CameraComponent, SyncToCamera)
{
    Entity entity("CamEntity");
    entity.transform.position = glm::vec3(5.0f, 6.0f, 7.0f);
    entity.transform.rotation = glm::vec3(-30.0f, 120.0f, 0.0f);
    entity.update(0.0f);

    auto* comp = entity.addComponent<CameraComponent>();

    Camera cam;
    comp->syncToCamera(cam);

    EXPECT_NEAR(cam.getPosition().x, 5.0f, 0.001f);
    EXPECT_NEAR(cam.getPosition().y, 6.0f, 0.001f);
    EXPECT_NEAR(cam.getPosition().z, 7.0f, 0.001f);
    EXPECT_NEAR(cam.getYaw(), 120.0f, 0.001f);
    EXPECT_NEAR(cam.getPitch(), -30.0f, 0.001f);
}

// --- Scene active camera ---

TEST(CameraComponent, SceneActiveCamera)
{
    Scene scene("TestScene");
    EXPECT_EQ(scene.getActiveCamera(), nullptr);

    Entity* camEntity = scene.createEntity("Camera");
    auto* cam = camEntity->addComponent<CameraComponent>();

    scene.setActiveCamera(cam);
    EXPECT_EQ(scene.getActiveCamera(), cam);

    scene.setActiveCamera(nullptr);
    EXPECT_EQ(scene.getActiveCamera(), nullptr);
}

TEST(CameraComponent, ClearEntitiesResetsActiveCamera)
{
    Scene scene("TestScene");
    Entity* camEntity = scene.createEntity("Camera");
    auto* cam = camEntity->addComponent<CameraComponent>();
    scene.setActiveCamera(cam);

    scene.clearEntities();
    EXPECT_EQ(scene.getActiveCamera(), nullptr);
}
