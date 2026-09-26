// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file demo_flythrough.h
/// @brief Hands-free camera fly-through for recording demo videos.
#pragma once

#include "environment/spline_path.h"

#include <glm/glm.hpp>

#include <functional>

namespace Vestige
{

class Camera;

/// @brief One sampled camera pose along the fly-through.
struct FlythroughPose
{
    glm::vec3 eye{0.0f};
    float yaw = -90.0f;   ///< Degrees, Camera convention (−90 faces −Z)
    float pitch = 0.0f;   ///< Degrees
    bool done = false;    ///< True once the path and the end hold are over
};

/// @brief Flies the camera along a fixed path with no human input
///        (CLI: --demo-flythrough), then reports that it is finished.
///
/// Two Catmull-Rom splines run side by side: the eye path and a look-at
/// path. Both are sampled by arc length at the same fraction, so the eye
/// moves at a steady speed and the view turns smoothly. The fraction is
/// eased in and out so the camera starts and stops gently.
///
/// This is the camera-only first piece of 3D_E-0696 (scripted demo mode):
/// the path is built in code, there is no script file yet.
class DemoFlythrough
{
public:
    /// @brief Returns the ground height at world (x, z).
    using HeightFn = std::function<float(float, float)>;

    /// @brief Adds a keyframe: where the eye passes and what it looks at.
    void addKey(const glm::vec3& eye, const glm::vec3& lookAt);

    /// @brief Seconds to hold still before moving (lets the scene settle).
    void setStartHold(float seconds) { m_startHold = seconds; }

    /// @brief Seconds to hold on the last pose before reporting done.
    void setEndHold(float seconds) { m_endHold = seconds; }

    /// @brief Eye speed along the path in metres per second (before easing).
    void setSpeed(float metresPerSecond) { m_speed = metresPerSecond; }

    /// @brief Keeps the eye at least this far above the ground (metres).
    void setGroundClearance(const HeightFn& height, float clearance);

    /// @brief Number of keyframes added.
    int keyCount() const { return m_eyePath.getWaypointCount(); }

    /// @brief Seconds of movement, excluding the holds.
    float travelSeconds() const;

    /// @brief Total run time in seconds, holds included.
    float totalSeconds() const { return m_startHold + travelSeconds() + m_endHold; }

    /// @brief Pose at a time since the start of the run. Pure: no state.
    FlythroughPose poseAt(float seconds) const;

    /// @brief Advances by one frame and applies the pose to the camera.
    /// @return True when the run is over and the engine should exit.
    bool update(Camera& camera, float deltaTime);

private:
    SplinePath m_eyePath;
    SplinePath m_lookPath;
    HeightFn m_height;
    float m_clearance = 0.0f;
    float m_startHold = 2.0f;
    float m_endHold = 1.0f;
    float m_speed = 4.0f;
    float m_elapsed = 0.0f;
};

} // namespace Vestige
