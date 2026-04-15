// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file camera_component.h
/// @brief Camera component for attaching cameras to entities in the scene graph.
#pragma once

#include "scene/component.h"
#include "renderer/camera.h"

#include <glm/glm.hpp>

#include <memory>

namespace Vestige
{

/// @brief Projection mode for the camera.
enum class ProjectionType : uint8_t
{
    PERSPECTIVE,
    ORTHOGRAPHIC
};

/// @brief Attaches a camera to an entity. Uses the entity's Transform for position/orientation.
///
/// Only one CameraComponent per scene should be active at a time. The scene tracks the active
/// camera via Scene::setActiveCamera(). The renderer uses the active camera's view and projection
/// matrices for rendering.
class CameraComponent : public Component
{
public:
    CameraComponent();

    /// @brief Syncs entity transform to internal camera state each frame.
    void update(float deltaTime) override;

    std::unique_ptr<Component> clone() const override;

    // --- Projection properties ---

    /// @brief Vertical field of view in degrees (perspective mode only).
    float fov = DEFAULT_FOV;

    /// @brief Near clipping plane distance in meters.
    float nearPlane = 0.1f;

    /// @brief Far clipping plane for culling (rendering uses infinite far with reverse-Z).
    float farPlane = 1000.0f;

    /// @brief Half-height of the orthographic view volume (orthographic mode only).
    float orthoSize = 10.0f;

    /// @brief Projection type (perspective or orthographic).
    ProjectionType projectionType = ProjectionType::PERSPECTIVE;

    // --- Matrices ---

    /// @brief Gets the view matrix derived from the entity's world transform.
    glm::mat4 getViewMatrix() const;

    /// @brief Gets the reverse-Z infinite far projection matrix for rendering.
    /// @param aspectRatio Viewport width / height.
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    /// @brief Gets a standard finite-far projection matrix for frustum culling.
    /// @param aspectRatio Viewport width / height.
    glm::mat4 getCullingProjectionMatrix(float aspectRatio) const;

    // --- Convenience ---

    /// @brief Gets the camera's world-space position (from entity transform).
    glm::vec3 getWorldPosition() const;

    /// @brief Gets the camera's forward direction (entity's -Z axis in world space).
    glm::vec3 getForward() const;

    /// @brief Gets the camera's right direction (entity's +X axis in world space).
    glm::vec3 getRight() const;

    /// @brief Gets the camera's up direction (entity's +Y axis in world space).
    glm::vec3 getUp() const;

    /// @brief Returns a Camera object matching this component's current state.
    /// Used for backward compatibility with renderer APIs that take const Camera&.
    const Camera& getCamera() const;

    /// @brief Copies current state from a standalone Camera (for initialization or sync).
    /// Updates the owning entity's transform to match the camera's position and orientation.
    void syncFromCamera(const Camera& camera);

    /// @brief Copies current state to a standalone Camera.
    void syncToCamera(Camera& camera) const;

private:
    mutable Camera m_camera;  ///< Internal camera for backward compat (lazy-updated)
    mutable bool m_cameraDirty = true;  ///< True when m_camera needs re-sync
};

} // namespace Vestige
