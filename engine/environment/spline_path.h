// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file spline_path.h
/// @brief Catmull-Rom spline for paths and streams with mesh generation.
#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Generated mesh data from a spline path.
struct PathMeshData
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices;
};

/// @brief A Catmull-Rom spline for paths, roads, and streams.
///
/// The spline passes through all control points. Mesh generation creates
/// a triangle strip along the spline for rendering as a path surface.
class SplinePath
{
public:
    SplinePath() = default;

    /// @brief Adds a waypoint at the end of the spline.
    void addWaypoint(const glm::vec3& position);

    /// @brief Inserts a waypoint at the given index.
    void insertWaypoint(int index, const glm::vec3& position);

    /// @brief Removes a waypoint by index.
    void removeWaypoint(int index);

    /// @brief Moves a waypoint to a new position.
    void setWaypointPosition(int index, const glm::vec3& position);

    /// @brief Gets the number of waypoints.
    int getWaypointCount() const { return static_cast<int>(m_waypoints.size()); }

    /// @brief Gets all waypoints.
    const std::vector<glm::vec3>& getWaypoints() const { return m_waypoints; }

    /// @brief Evaluates the spline at parameter t (0 = start, 1 = end).
    /// @return Interpolated position along the spline.
    glm::vec3 evaluate(float t) const;

    /// @brief Evaluates the tangent (derivative) at parameter t.
    /// @return Normalized tangent vector.
    glm::vec3 evaluateTangent(float t) const;

    /// @brief Evaluates the spline at arc length s from the start (in meters).
    ///
    /// Provides constant-speed parameterisation: advancing s by a fixed
    /// amount moves a fixed distance along the curve, regardless of local
    /// curvature. Required by the Phase 10.8 cinematic camera (CM7) so
    /// playback speed stays constant through curved sections.
    ///
    /// Out-of-range s clamps to the spline endpoints.
    glm::vec3 evaluateByArcLength(float s) const;

    /// @brief Approximates the total arc length by sampling.
    /// @param samples Number of segments to sample. Higher = more accurate.
    float getLength(int samples = 100) const;

    /// @brief Generates a triangle strip mesh along the spline.
    /// @param width Path half-width in meters.
    /// @param sampleSpacing Distance between samples along the spline.
    /// @return Mesh data (positions, normals, UVs, indices).
    PathMeshData generatePathMesh(float width, float sampleSpacing = 0.5f) const;

    /// @brief Generates a water surface mesh for a stream along the spline.
    /// Similar to generatePathMesh but with Y offset and different UV mapping.
    /// @param halfWidth Stream half-width in meters.
    /// @param sampleSpacing Distance between samples.
    /// @return Mesh data suitable for WaterRenderer integration.
    PathMeshData generateStreamMesh(float halfWidth, float sampleSpacing = 0.5f) const;

    /// @brief Serializes the spline to JSON.
    nlohmann::json serialize() const;

    /// @brief Deserializes the spline from JSON.
    void deserialize(const nlohmann::json& j);

    // Path metadata
    std::string name = "Path";
    float width = 1.5f;
    std::string materialPath;

private:
    /// @brief Evaluates a single Catmull-Rom segment.
    /// @param p0 Point before the segment start.
    /// @param p1 Segment start.
    /// @param p2 Segment end.
    /// @param p3 Point after the segment end.
    /// @param t Local parameter (0..1) within the segment.
    static glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                 const glm::vec3& p2, const glm::vec3& p3, float t);

    /// @brief Evaluates the Catmull-Rom derivative for a single segment.
    static glm::vec3 catmullRomDerivative(const glm::vec3& p0, const glm::vec3& p1,
                                           const glm::vec3& p2, const glm::vec3& p3, float t);

    std::vector<glm::vec3> m_waypoints;
};

} // namespace Vestige
