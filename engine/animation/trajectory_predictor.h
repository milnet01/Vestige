/// @file trajectory_predictor.h
/// @brief Spring-damper trajectory prediction from player input.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Predicts future character trajectory using a critically damped spring.
///
/// Converts raw player input (gamepad stick / keyboard) into a smooth desired
/// future path. The spring-damper system smooths velocity changes and provides
/// analytical future position prediction at arbitrary times.
///
/// Based on Daniel Holden's spring-damper formulation (theorangeduck.com).
class TrajectoryPredictor
{
public:
    /// @brief Updates the trajectory with new player input.
    /// @param inputDir Desired movement direction (XZ, camera-relative, normalized or zero).
    /// @param inputSpeed Desired speed (0 = stop, 1 = walk, higher = run).
    /// @param cameraYaw Camera Y rotation in radians.
    /// @param dt Delta time in seconds.
    void update(const glm::vec2& inputDir, float inputSpeed,
                float cameraYaw, float dt);

    /// @brief Predicts future trajectory positions and directions.
    /// @param outPositions Output XZ positions relative to current character position.
    /// @param outDirections Output XZ facing directions (normalized).
    /// @param sampleTimes Time offsets in seconds (positive = future).
    /// @param count Number of sample points.
    void predictTrajectory(glm::vec2* outPositions,
                           glm::vec2* outDirections,
                           const float* sampleTimes, int count) const;

    /// @brief Gets the current smoothed velocity (XZ).
    glm::vec2 getCurrentVelocity() const;

    /// @brief Gets the current smoothed position (XZ).
    glm::vec2 getCurrentPosition() const;

    /// @brief Gets the current facing direction (XZ, normalized).
    glm::vec2 getCurrentDirection() const;

    /// @brief Sets the velocity simulation halflife (default 0.27s).
    void setVelocityHalflife(float halflife);

    /// @brief Sets the facing direction simulation halflife (default 0.27s).
    void setFacingHalflife(float halflife);

    /// @brief Resets state to zero velocity at the given position.
    void reset(const glm::vec2& position = glm::vec2(0.0f));

private:
    /// @brief Critically damped spring update for a single axis.
    static void springUpdate(float& position, float& velocity,
                             float& acceleration, float goalVelocity,
                             float halflife, float dt);

    /// @brief Predicts spring state at a future time analytically.
    static void springPredict(float position, float velocity,
                              float acceleration, float goalVelocity,
                              float halflife, float futureTime,
                              float& outPosition, float& outVelocity);

    /// @brief Converts halflife to damping coefficient.
    static float halflifeToDamping(float halflife);

    /// @brief Fast negative exponential approximation.
    static float fastNegexp(float x);

    // Position state (XZ)
    glm::vec2 m_position = glm::vec2(0.0f);
    glm::vec2 m_velocity = glm::vec2(0.0f);
    glm::vec2 m_acceleration = glm::vec2(0.0f);

    // Facing direction state
    float m_facingAngle = 0.0f;
    float m_facingVelocity = 0.0f;
    float m_facingAcceleration = 0.0f;

    // Goal (from input)
    glm::vec2 m_goalVelocity = glm::vec2(0.0f);
    float m_goalFacingAngle = 0.0f;

    // Parameters
    float m_velocityHalflife = 0.27f;
    float m_facingHalflife = 0.27f;
};

} // namespace Vestige
