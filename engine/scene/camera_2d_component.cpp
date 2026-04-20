// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file camera_2d_component.cpp
/// @brief Camera2DComponent + updateCamera2DFollow helper.
#include "scene/camera_2d_component.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

void Camera2DComponent::update(float /*deltaTime*/)
{
    // Camera follow is driven externally by `updateCamera2DFollow` so
    // the engine's 2D scene loop can feed it the right target position
    // (typically the player entity) without the component needing to
    // look it up.
}

std::unique_ptr<Component> Camera2DComponent::clone() const
{
    auto c = std::make_unique<Camera2DComponent>();
    c->orthoHalfHeight   = orthoHalfHeight;
    c->cameraZDistance   = cameraZDistance;
    c->followOffset      = followOffset;
    c->deadzoneHalfSize  = deadzoneHalfSize;
    c->smoothTimeSec     = smoothTimeSec;
    c->maxSpeed          = maxSpeed;
    c->clampToBounds     = clampToBounds;
    c->worldBounds       = worldBounds;
    c->position          = position;
    // Fresh clones reset transient state so a cloned camera doesn't
    // inherit a mid-sweep velocity or stale init flag.
    c->velocity          = glm::vec2(0.0f);
    c->hasInitialized    = false;
    c->setEnabled(m_isEnabled);
    return c;
}

void updateCamera2DFollow(Camera2DComponent& camera,
                          const glm::vec2& targetWorldPos,
                          float deltaTime)
{
    const glm::vec2 desired = targetWorldPos + camera.followOffset;

    // First-frame snap — avoids a visible sweep from (0, 0) on scene load.
    if (!camera.hasInitialized)
    {
        camera.position = desired;
        camera.velocity = glm::vec2(0.0f);
        camera.hasInitialized = true;
    }

    // Deadzone: find the nearest point of the rectangle centred on the
    // camera's current position that still contains the target. If the
    // target is already inside the rectangle, no movement this frame.
    glm::vec2 goal = camera.position;
    const glm::vec2 delta = desired - camera.position;
    if (std::abs(delta.x) > camera.deadzoneHalfSize.x)
    {
        const float sign = delta.x > 0 ? 1.0f : -1.0f;
        goal.x = desired.x - sign * camera.deadzoneHalfSize.x;
    }
    if (std::abs(delta.y) > camera.deadzoneHalfSize.y)
    {
        const float sign = delta.y > 0 ? 1.0f : -1.0f;
        goal.y = desired.y - sign * camera.deadzoneHalfSize.y;
    }

    if (camera.smoothTimeSec <= 0.0f || deltaTime <= 0.0f)
    {
        // Instant snap when smoothing is disabled.
        camera.position = goal;
        camera.velocity = glm::vec2(0.0f);
    }
    else
    {
        // Critically-damped spring (Game Programming Gems 4, vol. 6 — and
        // the same formula Unity's Vector3.SmoothDamp uses).
        // omega = 2 / smoothTime.
        const float omega = 2.0f / camera.smoothTimeSec;
        const float x = omega * deltaTime;
        const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        glm::vec2 change = camera.position - goal;

        // Clamp to max speed to prevent cinematic "zoom" when the goal
        // is very far away.
        const float maxChange = camera.maxSpeed * camera.smoothTimeSec;
        const float changeLen = glm::length(change);
        if (changeLen > maxChange && changeLen > 0.0f)
        {
            change *= (maxChange / changeLen);
        }

        glm::vec2 clampedGoal = camera.position - change;

        glm::vec2 temp = (camera.velocity + omega * change) * deltaTime;
        camera.velocity = (camera.velocity - omega * temp) * exp;
        glm::vec2 newPos = clampedGoal + (change + temp) * exp;

        // Prevent overshoot from carrying past the goal.
        const glm::vec2 origChange = camera.position - goal;
        if (glm::dot(goal - newPos, origChange) > 0.0f)
        {
            newPos = goal;
            camera.velocity = glm::vec2(0.0f);
        }
        camera.position = newPos;
    }

    if (camera.clampToBounds)
    {
        camera.position.x = std::clamp(camera.position.x,
                                       camera.worldBounds.x,
                                       camera.worldBounds.z);
        camera.position.y = std::clamp(camera.position.y,
                                       camera.worldBounds.y,
                                       camera.worldBounds.w);
    }
}

} // namespace Vestige
