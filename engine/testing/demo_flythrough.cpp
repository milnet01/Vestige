// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file demo_flythrough.cpp
/// @brief Hands-free camera fly-through for recording demo videos.
#include "testing/demo_flythrough.h"
#include "renderer/camera.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace
{
/// Smootherstep: zero velocity AND zero acceleration at both ends, so the
/// camera eases off the start pose and settles onto the last one.
float easeInOut(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * x * ((x * ((x * 6.0f) - 15.0f)) + 10.0f);
}
} // namespace

void DemoFlythrough::addKey(const glm::vec3& eye, const glm::vec3& lookAt)
{
    m_eyePath.addWaypoint(eye);
    m_lookPath.addWaypoint(lookAt);
}

void DemoFlythrough::setGroundClearance(const HeightFn& height, float clearance)
{
    m_height = height;
    m_clearance = clearance;
}

float DemoFlythrough::travelSeconds() const
{
    if (m_speed <= 0.0f)
    {
        return 0.0f;
    }
    // Smootherstep's mean speed equals the linear one, so length / speed is
    // the true travel time; only the peak speed is higher (1.875x).
    return m_eyePath.getLength(512) / m_speed;
}

FlythroughPose DemoFlythrough::poseAt(float seconds) const
{
    FlythroughPose pose;
    if (keyCount() == 0)
    {
        pose.done = true;
        return pose;
    }

    const float travel = travelSeconds();
    const float moving = seconds - m_startHold;
    const float fraction = travel > 0.0f ? easeInOut(moving / travel) : 1.0f;

    pose.eye = m_eyePath.evaluateByArcLength(fraction * m_eyePath.getLength(512));
    const glm::vec3 look =
        m_lookPath.evaluateByArcLength(fraction * m_lookPath.getLength(512));

    if (m_height)
    {
        pose.eye.y = std::max(pose.eye.y, m_height(pose.eye.x, pose.eye.z) + m_clearance);
    }

    const glm::vec3 d = look - pose.eye;
    const float len = glm::length(d);
    if (len > 1e-4f)
    {
        const glm::vec3 dir = d / len;
        pose.yaw = glm::degrees(std::atan2(dir.z, dir.x));
        pose.pitch = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    }

    pose.done = seconds >= totalSeconds();
    return pose;
}

bool DemoFlythrough::update(Camera& camera, float deltaTime)
{
    // A hitch (shader compile, asset stream-in) would otherwise jump the
    // camera forward in the video; capping the step slows the path briefly
    // instead, which reads as smooth.
    m_elapsed += std::min(deltaTime, 1.0f / 20.0f);
    const FlythroughPose pose = poseAt(m_elapsed);
    camera.setPosition(pose.eye);
    camera.setYaw(pose.yaw);
    camera.setPitch(pose.pitch);
    return pose.done;
}

} // namespace Vestige
