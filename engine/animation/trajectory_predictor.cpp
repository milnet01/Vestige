// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file trajectory_predictor.cpp
/// @brief Spring-damper trajectory prediction implementation.

#include "animation/trajectory_predictor.h"

#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Spring math (Holden's formulation)
// ---------------------------------------------------------------------------

float TrajectoryPredictor::halflifeToDamping(float halflife)
{
    return (4.0f * 0.69314718056f) / (halflife + 1e-8f);
}

float TrajectoryPredictor::fastNegexp(float x)
{
    return 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
}

void TrajectoryPredictor::springUpdate(float& position, float& velocity,
                                       float& acceleration, float goalVelocity,
                                       float halflife, float dt)
{
    float y = halflifeToDamping(halflife) / 2.0f;
    float j0 = velocity - goalVelocity;
    float j1 = acceleration + j0 * y;
    float eydt = fastNegexp(y * dt);

    position = eydt * ((-j1 / (y * y)) + ((-j0 - j1 * dt) / y))
             + (j1 / (y * y)) + j0 / y + goalVelocity * dt + position;
    velocity = eydt * (j0 + j1 * dt) + goalVelocity;
    acceleration = eydt * (acceleration - j1 * y * dt);
}

void TrajectoryPredictor::springPredict(float position, float velocity,
                                        float acceleration, float goalVelocity,
                                        float halflife, float futureTime,
                                        float& outPosition, float& outVelocity)
{
    float y = halflifeToDamping(halflife) / 2.0f;
    float j0 = velocity - goalVelocity;
    float j1 = acceleration + j0 * y;
    float eydt = fastNegexp(y * futureTime);

    outPosition = eydt * ((-j1 / (y * y)) + ((-j0 - j1 * futureTime) / y))
                + (j1 / (y * y)) + j0 / y + goalVelocity * futureTime + position;
    outVelocity = eydt * (j0 + j1 * futureTime) + goalVelocity;
}

// ---------------------------------------------------------------------------
// TrajectoryPredictor
// ---------------------------------------------------------------------------

void TrajectoryPredictor::update(const glm::vec2& inputDir, float inputSpeed,
                                 float cameraYaw, float dt)
{
    // Convert camera-relative input to world-space goal velocity
    float cosYaw = std::cos(cameraYaw);
    float sinYaw = std::sin(cameraYaw);

    glm::vec2 worldDir(
        cosYaw * inputDir.x + sinYaw * inputDir.y,
        -sinYaw * inputDir.x + cosYaw * inputDir.y
    );

    float dirLength = glm::length(worldDir);
    if (dirLength > 0.001f)
    {
        worldDir /= dirLength;
        m_goalVelocity = worldDir * inputSpeed;
        m_goalFacingAngle = std::atan2(worldDir.x, worldDir.y);
    }
    else
    {
        m_goalVelocity = glm::vec2(0.0f);
        // Keep current facing when stopped
    }

    // Update position spring (X and Z independently)
    springUpdate(m_position.x, m_velocity.x, m_acceleration.x,
                 m_goalVelocity.x, m_velocityHalflife, dt);
    springUpdate(m_position.y, m_velocity.y, m_acceleration.y,
                 m_goalVelocity.y, m_velocityHalflife, dt);

    // Update facing angle spring
    // Wrap the difference to [-pi, pi]
    float facingDiff = m_goalFacingAngle - m_facingAngle;
    while (facingDiff > 3.14159265f) facingDiff -= 6.28318530f;
    while (facingDiff < -3.14159265f) facingDiff += 6.28318530f;

    float facingGoalVel = facingDiff / (m_facingHalflife + 1e-8f);
    springUpdate(m_facingAngle, m_facingVelocity, m_facingAcceleration,
                 facingGoalVel, m_facingHalflife, dt);
}

void TrajectoryPredictor::predictTrajectory(glm::vec2* outPositions,
                                            glm::vec2* outDirections,
                                            const float* sampleTimes,
                                            int count) const
{
    for (int i = 0; i < count; ++i)
    {
        float t = sampleTimes[i];

        // Predict future position
        float px, py, vx, vy;
        springPredict(m_position.x, m_velocity.x, m_acceleration.x,
                      m_goalVelocity.x, m_velocityHalflife, t, px, vx);
        springPredict(m_position.y, m_velocity.y, m_acceleration.y,
                      m_goalVelocity.y, m_velocityHalflife, t, py, vy);

        // Output position relative to current position
        outPositions[i] = glm::vec2(px - m_position.x, py - m_position.y);

        // Predict future facing direction
        float futureAngle = m_facingAngle;
        float futureAngVel = m_facingVelocity;
        // Simple integration for facing (angular spring is less critical)
        futureAngle += futureAngVel * t;

        outDirections[i] = glm::vec2(std::sin(futureAngle), std::cos(futureAngle));
    }
}

glm::vec2 TrajectoryPredictor::getCurrentVelocity() const
{
    return m_velocity;
}

glm::vec2 TrajectoryPredictor::getCurrentPosition() const
{
    return m_position;
}

glm::vec2 TrajectoryPredictor::getCurrentDirection() const
{
    return glm::vec2(std::sin(m_facingAngle), std::cos(m_facingAngle));
}

void TrajectoryPredictor::setVelocityHalflife(float halflife)
{
    m_velocityHalflife = halflife;
}

void TrajectoryPredictor::setFacingHalflife(float halflife)
{
    m_facingHalflife = halflife;
}

void TrajectoryPredictor::reset(const glm::vec2& position)
{
    m_position = position;
    m_velocity = glm::vec2(0.0f);
    m_acceleration = glm::vec2(0.0f);
    m_facingAngle = 0.0f;
    m_facingVelocity = 0.0f;
    m_facingAcceleration = 0.0f;
    m_goalVelocity = glm::vec2(0.0f);
    m_goalFacingAngle = 0.0f;
}

} // namespace Vestige
