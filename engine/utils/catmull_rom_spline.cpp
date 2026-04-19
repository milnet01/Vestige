// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file catmull_rom_spline.cpp
/// @brief Catmull-Rom spline evaluation implementation.
#include "utils/catmull_rom_spline.h"

#include <cmath>

namespace Vestige
{

CatmullRomSpline::CatmullRomSpline(const std::vector<glm::vec3>& controlPoints)
    : m_points(controlPoints)
{
}

void CatmullRomSpline::addPoint(const glm::vec3& point)
{
    m_points.push_back(point);
}

void CatmullRomSpline::clear()
{
    m_points.clear();
}

glm::vec3 CatmullRomSpline::evaluate(float t) const
{
    if (m_points.empty())
    {
        return glm::vec3(0.0f);
    }
    if (m_points.size() == 1)
    {
        return m_points[0];
    }

    // Clamp t to valid range [0, pointCount - 1]
    float maxT = static_cast<float>(m_points.size() - 1);
    t = glm::clamp(t, 0.0f, maxT);

    // Determine which segment we're in
    int segment = static_cast<int>(std::floor(t));
    float localT = t - static_cast<float>(segment);

    // Clamp segment to last valid segment
    int lastSegment = static_cast<int>(m_points.size()) - 2;
    if (segment >= lastSegment)
    {
        segment = lastSegment;
        if (t >= maxT)
        {
            localT = 1.0f;
        }
    }

    // Get the four control points, clamping at boundaries
    int i0 = glm::clamp(segment - 1, 0, static_cast<int>(m_points.size()) - 1);
    int i1 = segment;
    int i2 = segment + 1;
    int i3 = glm::clamp(segment + 2, 0, static_cast<int>(m_points.size()) - 1);

    const glm::vec3& p0 = m_points[static_cast<size_t>(i0)];
    const glm::vec3& p1 = m_points[static_cast<size_t>(i1)];
    const glm::vec3& p2 = m_points[static_cast<size_t>(i2)];
    const glm::vec3& p3 = m_points[static_cast<size_t>(i3)];

    // Catmull-Rom formula:
    // q(t) = 0.5 * ((2*P1) + (-P0 + P2)*t + (2*P0 - 5*P1 + 4*P2 - P3)*t^2
    //         + (-P0 + 3*P1 - 3*P2 + P3)*t^3)
    float t2 = localT * localT;
    float t3 = t2 * localT;

    glm::vec3 result = 0.5f * (
        (2.0f * p1)
        + (-p0 + p2) * localT
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );

    return result;
}

glm::vec3 CatmullRomSpline::evaluateTangent(float t) const
{
    if (m_points.size() < 2)
    {
        return glm::vec3(0.0f, 0.0f, 1.0f); // Default forward direction
    }

    // Clamp t to valid range [0, pointCount - 1]
    float maxT = static_cast<float>(m_points.size() - 1);
    t = glm::clamp(t, 0.0f, maxT);

    // Determine which segment we're in
    int segment = static_cast<int>(std::floor(t));
    float localT = t - static_cast<float>(segment);

    // Clamp segment to last valid segment
    int lastSegment = static_cast<int>(m_points.size()) - 2;
    if (segment >= lastSegment)
    {
        segment = lastSegment;
        if (t >= maxT)
        {
            localT = 1.0f;
        }
    }

    // Get the four control points, clamping at boundaries
    int i0 = glm::clamp(segment - 1, 0, static_cast<int>(m_points.size()) - 1);
    int i1 = segment;
    int i2 = segment + 1;
    int i3 = glm::clamp(segment + 2, 0, static_cast<int>(m_points.size()) - 1);

    const glm::vec3& p0 = m_points[static_cast<size_t>(i0)];
    const glm::vec3& p1 = m_points[static_cast<size_t>(i1)];
    const glm::vec3& p2 = m_points[static_cast<size_t>(i2)];
    const glm::vec3& p3 = m_points[static_cast<size_t>(i3)];

    // Derivative of Catmull-Rom:
    // q'(t) = 0.5 * ((-P0 + P2) + (4*P0 - 10*P1 + 8*P2 - 2*P3)*t
    //         + (-3*P0 + 9*P1 - 9*P2 + 3*P3)*t^2)
    float t2 = localT * localT;

    glm::vec3 tangent = 0.5f * (
        (-p0 + p2)
        + (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) * localT
        + (-3.0f * p0 + 9.0f * p1 - 9.0f * p2 + 3.0f * p3) * t2
    );

    return tangent;
}

std::vector<glm::vec3> CatmullRomSpline::sample(int samplesPerSegment) const
{
    std::vector<glm::vec3> result;

    if (m_points.size() < 2)
    {
        if (!m_points.empty())
        {
            result.push_back(m_points[0]);
        }
        return result;
    }

    int segmentCount = static_cast<int>(m_points.size()) - 1;
    int totalSamples = segmentCount * samplesPerSegment + 1; // +1 for the final point
    result.reserve(static_cast<size_t>(totalSamples));

    for (int i = 0; i < totalSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
        result.push_back(evaluate(t));
    }

    return result;
}

float CatmullRomSpline::getApproxLength(int samplesPerSegment) const
{
    std::vector<glm::vec3> points = sample(samplesPerSegment);

    float length = 0.0f;
    for (size_t i = 1; i < points.size(); ++i)
    {
        length += glm::distance(points[i - 1], points[i]);
    }

    return length;
}

} // namespace Vestige
