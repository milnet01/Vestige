// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file editor_camera.cpp
/// @brief Editor orbit camera implementation.
#include "editor/editor_camera.h"
#include "renderer/camera.h"

#include <imgui.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

EditorCamera::EditorCamera()
    : m_focusPoint(0.0f, 0.5f, 0.0f)
    , m_distance(5.0f)
    , m_yaw(90.0f)
    , m_pitch(15.0f)
    , m_targetFocusPoint(m_focusPoint)
    , m_targetDistance(m_distance)
    , m_targetYaw(m_yaw)
    , m_targetPitch(m_pitch)
    , m_position(0.0f)
    , m_orbitSensitivity(0.3f)
    , m_panSensitivity(0.005f)
    , m_zoomSensitivity(0.15f)
    , m_smoothSpeed(12.0f)
    , m_minDistance(0.3f)
    , m_maxDistance(200.0f)
    , m_minPitch(-89.0f)
    , m_maxPitch(89.0f)
{
    computePosition();
}

void EditorCamera::update(float deltaTime)
{
    // Exponential decay smoothing (frame-rate independent)
    float t = 1.0f - std::exp(-m_smoothSpeed * deltaTime);

    m_focusPoint = glm::mix(m_focusPoint, m_targetFocusPoint, t);
    m_distance = glm::mix(m_distance, m_targetDistance, t);
    m_yaw = glm::mix(m_yaw, m_targetYaw, t);
    m_pitch = glm::mix(m_pitch, m_targetPitch, t);

    computePosition();
}

void EditorCamera::processInput(bool viewportHovered)
{
    if (!viewportHovered)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Alt + LMB drag → orbit
    if (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 delta = io.MouseDelta;
        m_targetYaw -= delta.x * m_orbitSensitivity;
        m_targetPitch += delta.y * m_orbitSensitivity;
        m_targetPitch = std::clamp(m_targetPitch, m_minPitch, m_maxPitch);
    }

    // MMB drag → pan
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        ImVec2 delta = io.MouseDelta;

        // Compute view-space right and up vectors from orbit angles
        float yawRad = glm::radians(m_targetYaw);
        float pitchRad = glm::radians(m_targetPitch);

        glm::vec3 right(-std::sin(yawRad), 0.0f, std::cos(yawRad));

        glm::vec3 look(
            std::cos(pitchRad) * std::cos(yawRad),
            std::sin(pitchRad),
            std::cos(pitchRad) * std::sin(yawRad)
        );
        glm::vec3 up = glm::normalize(glm::cross(right, -look));

        // Scale pan speed proportionally to distance (feels natural)
        float panScale = m_panSensitivity * m_targetDistance;
        m_targetFocusPoint -= right * delta.x * panScale;
        m_targetFocusPoint += up * delta.y * panScale;
    }

    // Scroll → zoom (multiplicative for consistent feel at all distances)
    if (std::abs(io.MouseWheel) > 0.001f)
    {
        float zoomFactor = 1.0f - io.MouseWheel * m_zoomSensitivity;
        m_targetDistance *= zoomFactor;
        m_targetDistance = std::clamp(m_targetDistance, m_minDistance, m_maxDistance);
    }
}

void EditorCamera::applyToCamera(Camera& camera) const
{
    camera.setPosition(m_position);

    // Convert orbit angles to FPS camera yaw/pitch:
    // The orbit camera is at orbit(yaw, pitch) looking TOWARD the focus point.
    // FPS camera yaw/pitch define the LOOK direction.
    // FPS yaw = orbit_yaw + 180° (looking back toward focus)
    // FPS pitch = -orbit_pitch (inverted vertical)
    float fpsYaw = m_yaw + 180.0f;
    float fpsPitch = -m_pitch;

    camera.setYaw(fpsYaw);
    camera.setPitch(fpsPitch);
}

void EditorCamera::focusOn(const glm::vec3& target, float distance)
{
    m_targetFocusPoint = target;
    if (distance > 0.0f)
    {
        m_targetDistance = std::clamp(distance, m_minDistance, m_maxDistance);
    }
}

void EditorCamera::setFrontView()
{
    m_targetYaw = 90.0f;
    m_targetPitch = 0.0f;
}

void EditorCamera::setRightView()
{
    m_targetYaw = 180.0f;
    m_targetPitch = 0.0f;
}

void EditorCamera::setTopView()
{
    m_targetYaw = 90.0f;
    m_targetPitch = 89.0f;
}

glm::vec3 EditorCamera::getPosition() const
{
    return m_position;
}

glm::vec3 EditorCamera::getFocusPoint() const
{
    return m_focusPoint;
}

float EditorCamera::getDistance() const
{
    return m_distance;
}

void EditorCamera::syncFromCamera(const Camera& camera)
{
    glm::vec3 camPos = camera.getPosition();
    float fpsYaw = camera.getYaw();
    float fpsPitch = camera.getPitch();

    // Reverse the applyToCamera mapping:
    //   fpsYaw = orbit_yaw + 180     → orbit_yaw = fpsYaw - 180
    //   fpsPitch = -orbit_pitch      → orbit_pitch = -fpsPitch
    float orbitYaw = fpsYaw - 180.0f;
    float orbitPitch = -fpsPitch;
    orbitPitch = std::clamp(orbitPitch, m_minPitch, m_maxPitch);

    // Compute focus point: camera is at distance along the orbit direction
    float yawRad = glm::radians(orbitYaw);
    float pitchRad = glm::radians(orbitPitch);

    glm::vec3 offset(
        m_distance * std::cos(pitchRad) * std::cos(yawRad),
        m_distance * std::sin(pitchRad),
        m_distance * std::cos(pitchRad) * std::sin(yawRad)
    );

    glm::vec3 focusPoint = camPos - offset;

    // Set both current and target to avoid smoothing snap
    m_focusPoint = focusPoint;
    m_targetFocusPoint = focusPoint;
    m_yaw = orbitYaw;
    m_targetYaw = orbitYaw;
    m_pitch = orbitPitch;
    m_targetPitch = orbitPitch;
    // Keep current distance (don't change zoom level)

    computePosition();
}

void EditorCamera::computePosition()
{
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    m_position.x = m_focusPoint.x + m_distance * std::cos(pitchRad) * std::cos(yawRad);
    m_position.y = m_focusPoint.y + m_distance * std::sin(pitchRad);
    m_position.z = m_focusPoint.z + m_distance * std::cos(pitchRad) * std::sin(yawRad);
}

} // namespace Vestige
