// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file spline_path.cpp
/// @brief SplinePath implementation — Catmull-Rom spline evaluation and mesh generation.
#include "environment/spline_path.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

void SplinePath::addWaypoint(const glm::vec3& position)
{
    m_waypoints.push_back(position);
}

void SplinePath::insertWaypoint(int index, const glm::vec3& position)
{
    if (index < 0)
    {
        index = 0;
    }
    if (index > static_cast<int>(m_waypoints.size()))
    {
        index = static_cast<int>(m_waypoints.size());
    }
    m_waypoints.insert(m_waypoints.begin() + index, position);
}

void SplinePath::removeWaypoint(int index)
{
    if (index >= 0 && index < static_cast<int>(m_waypoints.size()))
    {
        m_waypoints.erase(m_waypoints.begin() + index);
    }
}

void SplinePath::setWaypointPosition(int index, const glm::vec3& position)
{
    if (index >= 0 && index < static_cast<int>(m_waypoints.size()))
    {
        m_waypoints[static_cast<size_t>(index)] = position;
    }
}

glm::vec3 SplinePath::evaluate(float t) const
{
    int n = static_cast<int>(m_waypoints.size());
    if (n == 0) return glm::vec3(0.0f);
    if (n == 1) return m_waypoints[0];
    if (n == 2) return glm::mix(m_waypoints[0], m_waypoints[1], glm::clamp(t, 0.0f, 1.0f));

    // Map global t to segment + local t
    t = std::clamp(t, 0.0f, 1.0f);
    float segmentF = t * static_cast<float>(n - 1);
    int segment = static_cast<int>(std::floor(segmentF));
    float localT = segmentF - static_cast<float>(segment);

    // Handle t=1 edge case: floor(n-1) = n-1 but last segment is n-2
    if (segment >= n - 1)
    {
        segment = n - 2;
        localT = 1.0f;
    }

    // Get 4 control points (clamped at edges)
    int i0 = std::max(0, segment - 1);
    int i1 = segment;
    int i2 = std::min(n - 1, segment + 1);
    int i3 = std::min(n - 1, segment + 2);

    return catmullRom(m_waypoints[static_cast<size_t>(i0)], m_waypoints[static_cast<size_t>(i1)],
                      m_waypoints[static_cast<size_t>(i2)], m_waypoints[static_cast<size_t>(i3)], localT);
}

glm::vec3 SplinePath::evaluateTangent(float t) const
{
    int n = static_cast<int>(m_waypoints.size());
    if (n < 2) return glm::vec3(0.0f, 0.0f, 1.0f);

    t = std::clamp(t, 0.0f, 1.0f);
    float segmentF = t * static_cast<float>(n - 1);
    int segment = static_cast<int>(std::floor(segmentF));
    float localT = segmentF - static_cast<float>(segment);

    if (segment >= n - 1)
    {
        segment = n - 2;
        localT = 1.0f;
    }

    int i0 = std::max(0, segment - 1);
    int i1 = segment;
    int i2 = std::min(n - 1, segment + 1);
    int i3 = std::min(n - 1, segment + 2);

    glm::vec3 deriv = catmullRomDerivative(m_waypoints[static_cast<size_t>(i0)], m_waypoints[static_cast<size_t>(i1)],
                                            m_waypoints[static_cast<size_t>(i2)], m_waypoints[static_cast<size_t>(i3)], localT);
    float len = glm::length(deriv);
    if (len < 1e-6f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return deriv / len;
}

float SplinePath::getLength(int samples) const
{
    if (m_waypoints.size() < 2) return 0.0f;

    float length = 0.0f;
    glm::vec3 prev = evaluate(0.0f);
    for (int i = 1; i <= samples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        glm::vec3 curr = evaluate(t);
        length += glm::distance(prev, curr);
        prev = curr;
    }
    return length;
}

PathMeshData SplinePath::generatePathMesh(float halfWidth, float sampleSpacing) const
{
    PathMeshData mesh;

    if (m_waypoints.size() < 2) return mesh;

    float totalLength = getLength();
    if (totalLength < 0.001f) return mesh;

    int sampleCount = std::max(2, static_cast<int>(totalLength / sampleSpacing) + 1);
    const size_t sampleCountU = static_cast<size_t>(sampleCount);

    mesh.positions.reserve(sampleCountU * 2);
    mesh.normals.reserve(sampleCountU * 2);
    mesh.uvs.reserve(sampleCountU * 2);
    mesh.indices.reserve((sampleCountU - 1) * 6);

    float accLength = 0.0f;
    glm::vec3 prevPos = evaluate(0.0f);

    for (int i = 0; i < sampleCount; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
        glm::vec3 pos = evaluate(t);
        glm::vec3 tangent = evaluateTangent(t);

        if (i > 0) accLength += glm::distance(prevPos, pos);
        prevPos = pos;

        // Surface normal: up (for flat ground paths)
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        // Right vector: perpendicular to tangent on the XZ plane
        glm::vec3 right = glm::normalize(glm::cross(tangent, up));
        if (glm::length(right) < 1e-6f) right = glm::vec3(1.0f, 0.0f, 0.0f);

        // Left and right edge vertices
        glm::vec3 leftPos = pos + right * halfWidth + glm::vec3(0.0f, 0.01f, 0.0f);
        glm::vec3 rightPos = pos - right * halfWidth + glm::vec3(0.0f, 0.01f, 0.0f);

        mesh.positions.push_back(leftPos);
        mesh.positions.push_back(rightPos);
        mesh.normals.push_back(up);
        mesh.normals.push_back(up);

        float v = accLength / (halfWidth * 2.0f);  // UV tiling based on path length
        mesh.uvs.push_back(glm::vec2(0.0f, v));
        mesh.uvs.push_back(glm::vec2(1.0f, v));

        // Indices (triangle strip as indexed triangles)
        if (i > 0)
        {
            uint32_t base = static_cast<uint32_t>((i - 1) * 2);
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 3);
            mesh.indices.push_back(base + 2);
        }
    }

    return mesh;
}

PathMeshData SplinePath::generateStreamMesh(float halfWidth, float sampleSpacing) const
{
    PathMeshData mesh;

    if (m_waypoints.size() < 2) return mesh;

    float totalLength = getLength();
    if (totalLength < 0.001f) return mesh;

    int sampleCount = std::max(2, static_cast<int>(totalLength / sampleSpacing) + 1);
    const size_t sampleCountU = static_cast<size_t>(sampleCount);

    mesh.positions.reserve(sampleCountU * 2);
    mesh.normals.reserve(sampleCountU * 2);
    mesh.uvs.reserve(sampleCountU * 2);
    mesh.indices.reserve((sampleCountU - 1) * 6);

    float accLength = 0.0f;
    glm::vec3 prevPos = evaluate(0.0f);

    for (int i = 0; i < sampleCount; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
        glm::vec3 pos = evaluate(t);
        glm::vec3 tangent = evaluateTangent(t);

        if (i > 0) accLength += glm::distance(prevPos, pos);
        prevPos = pos;

        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(tangent, up));
        if (glm::length(right) < 1e-6f) right = glm::vec3(1.0f, 0.0f, 0.0f);

        // Water sits at a slight offset above the ground
        float waterY = pos.y + 0.005f;

        glm::vec3 leftPos = pos + right * halfWidth;
        leftPos.y = waterY;
        glm::vec3 rightPos = pos - right * halfWidth;
        rightPos.y = waterY;

        mesh.positions.push_back(leftPos);
        mesh.positions.push_back(rightPos);
        mesh.normals.push_back(up);
        mesh.normals.push_back(up);

        // UV: tile along length, 0..1 across width
        float v = accLength / (halfWidth * 4.0f);
        mesh.uvs.push_back(glm::vec2(0.0f, v));
        mesh.uvs.push_back(glm::vec2(1.0f, v));

        if (i > 0)
        {
            uint32_t base = static_cast<uint32_t>((i - 1) * 2);
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 3);
            mesh.indices.push_back(base + 2);
        }
    }

    return mesh;
}

nlohmann::json SplinePath::serialize() const
{
    nlohmann::json j;
    j["name"] = name;
    j["width"] = width;
    j["materialPath"] = materialPath;

    nlohmann::json wps = nlohmann::json::array();
    for (const auto& wp : m_waypoints)
    {
        wps.push_back({wp.x, wp.y, wp.z});
    }
    j["waypoints"] = wps;
    return j;
}

void SplinePath::deserialize(const nlohmann::json& j)
{
    name = j.value("name", "Path");
    width = j.value("width", 1.5f);
    materialPath = j.value("materialPath", "");

    m_waypoints.clear();
    if (j.contains("waypoints") && j["waypoints"].is_array())
    {
        for (const auto& wp : j["waypoints"])
        {
            m_waypoints.push_back(glm::vec3(
                wp[0].get<float>(), wp[1].get<float>(), wp[2].get<float>()));
        }
    }
}

// --- Catmull-Rom math ---
//
// Centripetal Catmull-Rom (Yuksel et al. 2011, "Parameterization and
// Applications of Catmull-Rom Curves", Computer-Aided Design 43.7).
// Knots are spaced by chord^alpha with alpha = 0.5 — guaranteed no
// self-intersection or cusps for any non-degenerate control points,
// regardless of inter-point spacing. Uniform CR (alpha = 0) overshoots
// when adjacent spacings are uneven; the test SplinePathTest.
// CentripetalAvoidsCusp pins the canonical Yuksel failure case.
//
// Implemented via the Barry-Goldman recursive form because it stays
// trivially correct for any alpha and reads top-to-bottom. The derivative
// uses a centred finite difference — callers normalise the result, so
// the small numerical error is irrelevant for tangent direction.

namespace
{
    constexpr float kCentripetalAlpha = 0.5f;
    constexpr float kMinKnotDelta = 1e-6f;  // Guards coincident control points.

    float knotDelta(const glm::vec3& a, const glm::vec3& b)
    {
        const float d = glm::distance(a, b);
        return std::max(std::pow(d, kCentripetalAlpha), kMinKnotDelta);
    }
}

glm::vec3 SplinePath::catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                  const glm::vec3& p2, const glm::vec3& p3, float t)
{
    // Knot times t0 < t1 < t2 < t3 spaced by chord^alpha.
    const float t0 = 0.0f;
    const float t1 = t0 + knotDelta(p0, p1);
    const float t2 = t1 + knotDelta(p1, p2);
    const float t3 = t2 + knotDelta(p2, p3);

    // Map local u in [0,1] to global parameter in [t1, t2].
    const float u = std::clamp(t, 0.0f, 1.0f);
    const float tt = t1 + u * (t2 - t1);

    // Barry-Goldman pyramid (three levels of weighted lerps).
    const glm::vec3 A1 = ((t1 - tt) / (t1 - t0)) * p0 + ((tt - t0) / (t1 - t0)) * p1;
    const glm::vec3 A2 = ((t2 - tt) / (t2 - t1)) * p1 + ((tt - t1) / (t2 - t1)) * p2;
    const glm::vec3 A3 = ((t3 - tt) / (t3 - t2)) * p2 + ((tt - t2) / (t3 - t2)) * p3;
    const glm::vec3 B1 = ((t2 - tt) / (t2 - t0)) * A1 + ((tt - t0) / (t2 - t0)) * A2;
    const glm::vec3 B2 = ((t3 - tt) / (t3 - t1)) * A2 + ((tt - t1) / (t3 - t1)) * A3;
    return         ((t2 - tt) / (t2 - t1)) * B1 + ((tt - t1) / (t2 - t1)) * B2;
}

glm::vec3 SplinePath::catmullRomDerivative(const glm::vec3& p0, const glm::vec3& p1,
                                             const glm::vec3& p2, const glm::vec3& p3, float t)
{
    // Centred finite difference in local u-space. Endpoint-clamp avoids
    // sampling outside the segment; callers normalise so the magnitude
    // skew at the clamp point is harmless.
    constexpr float kEps = 1e-3f;
    const float uLo = std::max(0.0f, t - kEps);
    const float uHi = std::min(1.0f, t + kEps);
    const glm::vec3 lo = catmullRom(p0, p1, p2, p3, uLo);
    const glm::vec3 hi = catmullRom(p0, p1, p2, p3, uHi);
    return (hi - lo) / (uHi - uLo);
}

} // namespace Vestige
