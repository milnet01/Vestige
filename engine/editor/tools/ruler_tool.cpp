// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ruler_tool.cpp
/// @brief RulerTool implementation.
#include "editor/tools/ruler_tool.h"
#include "renderer/debug_draw.h"

#include <glm/glm.hpp>

namespace Vestige
{

void RulerTool::startMeasurement()
{
    m_state = State::WAITING_A;
    m_pointA = glm::vec3(0.0f);
    m_pointB = glm::vec3(0.0f);
    m_distance = 0.0f;
}

void RulerTool::cancel()
{
    m_state = State::INACTIVE;
}

bool RulerTool::processClick(const glm::vec3& hitPoint)
{
    if (m_state == State::WAITING_A)
    {
        m_pointA = hitPoint;
        m_state = State::WAITING_B;
        return true;
    }
    else if (m_state == State::WAITING_B)
    {
        m_pointB = hitPoint;
        m_distance = glm::length(m_pointB - m_pointA);
        m_state = State::MEASURED;
        return true;
    }
    else if (m_state == State::MEASURED)
    {
        // Start a new measurement
        m_pointA = hitPoint;
        m_state = State::WAITING_B;
        return true;
    }

    return false;
}

void RulerTool::queueDebugDraw() const
{
    if (m_state == State::INACTIVE || m_state == State::WAITING_A)
    {
        return;
    }

    glm::vec3 color(0.0f, 1.0f, 1.0f); // Cyan measurement line

    if (m_state == State::WAITING_B)
    {
        // Draw a marker at point A
        float markerSize = 0.15f;
        DebugDraw::line(m_pointA - glm::vec3(markerSize, 0, 0),
                        m_pointA + glm::vec3(markerSize, 0, 0), color);
        DebugDraw::line(m_pointA - glm::vec3(0, markerSize, 0),
                        m_pointA + glm::vec3(0, markerSize, 0), color);
        DebugDraw::line(m_pointA - glm::vec3(0, 0, markerSize),
                        m_pointA + glm::vec3(0, 0, markerSize), color);
    }
    else if (m_state == State::MEASURED)
    {
        // Draw measurement line
        DebugDraw::line(m_pointA, m_pointB, color);

        // Draw markers at both ends
        float markerSize = 0.15f;
        DebugDraw::line(m_pointA - glm::vec3(markerSize, 0, 0),
                        m_pointA + glm::vec3(markerSize, 0, 0), color);
        DebugDraw::line(m_pointA - glm::vec3(0, markerSize, 0),
                        m_pointA + glm::vec3(0, markerSize, 0), color);
        DebugDraw::line(m_pointA - glm::vec3(0, 0, markerSize),
                        m_pointA + glm::vec3(0, 0, markerSize), color);

        DebugDraw::line(m_pointB - glm::vec3(markerSize, 0, 0),
                        m_pointB + glm::vec3(markerSize, 0, 0), color);
        DebugDraw::line(m_pointB - glm::vec3(0, markerSize, 0),
                        m_pointB + glm::vec3(0, markerSize, 0), color);
        DebugDraw::line(m_pointB - glm::vec3(0, 0, markerSize),
                        m_pointB + glm::vec3(0, 0, markerSize), color);

        // Draw midpoint text position marker (vertical tick for label anchor)
        glm::vec3 midpoint = (m_pointA + m_pointB) * 0.5f;
        DebugDraw::line(midpoint, midpoint + glm::vec3(0, 0.3f, 0), color);
    }
}

} // namespace Vestige
