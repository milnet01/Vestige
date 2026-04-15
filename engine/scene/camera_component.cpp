// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file camera_component.cpp
/// @brief Camera component implementation.
#include "scene/camera_component.h"
#include "scene/entity.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Vestige
{

CameraComponent::CameraComponent()
    : m_camera(glm::vec3(0.0f), DEFAULT_YAW, DEFAULT_PITCH)
{
}

void CameraComponent::update(float /* deltaTime */)
{
    // Mark the internal camera as needing re-sync from entity transform.
    m_cameraDirty = true;
}

std::unique_ptr<Component> CameraComponent::clone() const
{
    auto copy = std::make_unique<CameraComponent>();
    copy->fov = fov;
    copy->nearPlane = nearPlane;
    copy->farPlane = farPlane;
    copy->orthoSize = orthoSize;
    copy->projectionType = projectionType;
    copy->setEnabled(isEnabled());
    return copy;
}

glm::mat4 CameraComponent::getViewMatrix() const
{
    if (!m_owner)
    {
        return glm::mat4(1.0f);
    }

    // View matrix = inverse of the camera entity's world transform.
    // The entity's world matrix encodes position and orientation.
    // We only want translation + rotation (ignore scale for camera).
    glm::mat4 world = m_owner->getWorldMatrix();

    // Extract the 3x3 rotation (normalize columns to remove scale).
    glm::vec3 right = glm::normalize(glm::vec3(world[0]));
    glm::vec3 up = glm::normalize(glm::vec3(world[1]));
    glm::vec3 forward = glm::normalize(glm::vec3(world[2]));
    glm::vec3 pos = glm::vec3(world[3]);

    // Build view matrix by hand (inverse of pure rotation+translation):
    //   V = transpose(R) * translate(-pos)
    glm::mat4 view(1.0f);
    view[0][0] = right.x;    view[1][0] = right.y;    view[2][0] = right.z;
    view[0][1] = up.x;       view[1][1] = up.y;       view[2][1] = up.z;
    view[0][2] = forward.x;  view[1][2] = forward.y;  view[2][2] = forward.z;
    view[3][0] = -glm::dot(right, pos);
    view[3][1] = -glm::dot(up, pos);
    view[3][2] = -glm::dot(forward, pos);

    return view;
}

glm::mat4 CameraComponent::getProjectionMatrix(float aspectRatio) const
{
    if (projectionType == ProjectionType::ORTHOGRAPHIC)
    {
        float halfW = orthoSize * aspectRatio;
        float halfH = orthoSize;
        // Reverse-Z orthographic: near maps to 1.0, far maps to 0.0
        glm::mat4 ortho = glm::ortho(-halfW, halfW, -halfH, halfH, farPlane, nearPlane);
        return ortho;
    }

    // Perspective: reverse-Z infinite far plane (matches Camera::getProjectionMatrix)
    float fovRad = glm::radians(fov);
    float f = 1.0f / std::tan(fovRad * 0.5f);

    glm::mat4 proj(0.0f);
    proj[0][0] = f / aspectRatio;
    proj[1][1] = f;
    proj[2][3] = -1.0f;
    proj[3][2] = nearPlane;

    return proj;
}

glm::mat4 CameraComponent::getCullingProjectionMatrix(float aspectRatio) const
{
    if (projectionType == ProjectionType::ORTHOGRAPHIC)
    {
        float halfW = orthoSize * aspectRatio;
        float halfH = orthoSize;
        return glm::ortho(-halfW, halfW, -halfH, halfH, nearPlane, farPlane);
    }

    return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

glm::vec3 CameraComponent::getWorldPosition() const
{
    if (!m_owner)
    {
        return glm::vec3(0.0f);
    }
    return m_owner->getWorldPosition();
}

glm::vec3 CameraComponent::getForward() const
{
    if (!m_owner)
    {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    glm::mat4 world = m_owner->getWorldMatrix();
    // Camera forward is -Z in local space
    return -glm::normalize(glm::vec3(world[2]));
}

glm::vec3 CameraComponent::getRight() const
{
    if (!m_owner)
    {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    glm::mat4 world = m_owner->getWorldMatrix();
    return glm::normalize(glm::vec3(world[0]));
}

glm::vec3 CameraComponent::getUp() const
{
    if (!m_owner)
    {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    glm::mat4 world = m_owner->getWorldMatrix();
    return glm::normalize(glm::vec3(world[1]));
}

const Camera& CameraComponent::getCamera() const
{
    if (m_cameraDirty && m_owner)
    {
        // Sync internal Camera from entity transform so getViewMatrix etc. match.
        glm::vec3 pos = m_owner->getWorldPosition();
        m_camera.setPosition(pos);

        // Derive yaw/pitch from entity rotation (entity uses degrees: x=pitch, y=yaw)
        glm::vec3 rot = m_owner->transform.rotation;
        m_camera.setYaw(rot.y);
        m_camera.setPitch(rot.x);

        m_cameraDirty = false;
    }

    return m_camera;
}

void CameraComponent::syncFromCamera(const Camera& camera)
{
    fov = camera.getFov();

    if (m_owner)
    {
        m_owner->transform.position = camera.getPosition();
        m_owner->transform.rotation.x = camera.getPitch();
        m_owner->transform.rotation.y = camera.getYaw();
        m_owner->transform.rotation.z = 0.0f;
    }

    m_cameraDirty = true;
}

void CameraComponent::syncToCamera(Camera& camera) const
{
    if (m_owner)
    {
        camera.setPosition(m_owner->getWorldPosition());
        camera.setYaw(m_owner->transform.rotation.y);
        camera.setPitch(m_owner->transform.rotation.x);
    }
}

} // namespace Vestige
