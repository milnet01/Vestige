// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ruler_tool.h
/// @brief Two-click measurement tool for the editor viewport.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

class Camera;
struct Ray;

/// @brief Two-click measurement tool — click two points to measure distance.
///
/// When active, the first click sets point A and the second sets point B.
/// A line is drawn between the points with the distance displayed.
/// The measurement persists until cleared or a new measurement starts.
class RulerTool
{
public:
    /// @brief Measurement state.
    enum class State
    {
        INACTIVE,       ///< Tool is off.
        WAITING_A,      ///< Waiting for first click.
        WAITING_B,      ///< First point set, waiting for second click.
        MEASURED        ///< Both points set, displaying measurement.
    };

    RulerTool() = default;

    /// @brief Starts a new measurement (enters WAITING_A state).
    void startMeasurement();

    /// @brief Cancels the current measurement and deactivates the tool.
    void cancel();

    /// @brief Processes a viewport click. Call when the user clicks in the viewport.
    /// @param hitPoint The world-space position of the click (from ray-scene intersection).
    /// @return True if the tool consumed the click.
    bool processClick(const glm::vec3& hitPoint);

    /// @brief Queues debug draw lines for the measurement visualization.
    /// Call during the debug draw phase.
    void queueDebugDraw() const;

    /// @brief Gets the current state.
    State getState() const { return m_state; }

    /// @brief Returns true if the tool is actively consuming viewport clicks.
    /// Includes MEASURED because processClick() restarts the measurement
    /// on a click in that state — callers must not route those clicks to
    /// other tools.
    bool isActive() const
    {
        return m_state == State::WAITING_A
            || m_state == State::WAITING_B
            || m_state == State::MEASURED;
    }

    /// @brief Returns true if a measurement result is available.
    bool hasMeasurement() const { return m_state == State::MEASURED; }

    /// @brief Gets the measured distance in meters (valid when hasMeasurement() is true).
    float getDistance() const { return m_distance; }

    /// @brief Gets point A (valid when state >= WAITING_B).
    const glm::vec3& getPointA() const { return m_pointA; }

    /// @brief Gets point B (valid when hasMeasurement() is true).
    const glm::vec3& getPointB() const { return m_pointB; }

private:
    State m_state = State::INACTIVE;
    glm::vec3 m_pointA{0.0f};
    glm::vec3 m_pointB{0.0f};
    float m_distance = 0.0f;
};

} // namespace Vestige
