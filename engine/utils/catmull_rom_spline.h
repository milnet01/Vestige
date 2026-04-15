// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file catmull_rom_spline.h
/// @brief Catmull-Rom spline evaluation for path/road generation.
#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Vestige
{

/// @brief Catmull-Rom spline through a set of control points.
class CatmullRomSpline
{
public:
    CatmullRomSpline() = default;
    explicit CatmullRomSpline(const std::vector<glm::vec3>& controlPoints);

    /// @brief Add a control point.
    void addPoint(const glm::vec3& point);

    /// @brief Clear all control points.
    void clear();

    /// @brief Get number of control points.
    size_t getPointCount() const { return m_points.size(); }

    /// @brief Get control point at index.
    const glm::vec3& getPoint(size_t index) const { return m_points[index]; }

    /// @brief Evaluate the spline at parameter t (0 to pointCount-1).
    glm::vec3 evaluate(float t) const;

    /// @brief Evaluate the tangent (first derivative) at parameter t.
    glm::vec3 evaluateTangent(float t) const;

    /// @brief Sample the spline into evenly-spaced points.
    /// @param samplesPerSegment Number of samples between each pair of control points.
    /// @return Vector of sampled positions along the spline.
    std::vector<glm::vec3> sample(int samplesPerSegment = 10) const;

    /// @brief Get approximate total arc length.
    float getApproxLength(int samplesPerSegment = 20) const;

private:
    std::vector<glm::vec3> m_points;
};

} // namespace Vestige
