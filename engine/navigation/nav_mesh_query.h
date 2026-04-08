/// @file nav_mesh_query.h
/// @brief Wraps Detour for navmesh pathfinding queries.
#pragma once

#include <glm/glm.hpp>

#include <vector>

// Forward declarations for Detour types
class dtNavMesh;
class dtNavMeshQuery;

namespace Vestige
{

/// @brief Provides pathfinding queries on a built navmesh.
///
/// Wraps dtNavMeshQuery for A* path queries and nearest-point lookups.
/// Must be initialized with a built navmesh from NavMeshBuilder.
class NavMeshQuery
{
public:
    NavMeshQuery();
    ~NavMeshQuery();

    // Non-copyable
    NavMeshQuery(const NavMeshQuery&) = delete;
    NavMeshQuery& operator=(const NavMeshQuery&) = delete;

    /// @brief Initializes the query object with a navmesh.
    /// @param navMesh The Detour navmesh to query against.
    /// @return True if initialization succeeded.
    bool initialize(dtNavMesh* navMesh);

    /// @brief Releases the query object.
    void shutdown();

    /// @brief Whether the query is ready for use.
    bool isReady() const { return m_query != nullptr; }

    /// @brief Finds a path between two world positions.
    /// @param start Start position in world space.
    /// @param end End position in world space.
    /// @return Vector of waypoint positions, or empty if no path found.
    std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end);

    /// @brief Finds the nearest point on the navmesh to a world position.
    /// @param point World position to snap.
    /// @return Nearest navmesh position, or the input point if not on navmesh.
    glm::vec3 findNearestPoint(const glm::vec3& point);

    /// @brief Tests if a world position is on the navmesh.
    /// @param point World position to test.
    /// @return True if the point is on the navmesh surface.
    bool isPointOnNavMesh(const glm::vec3& point);

private:
    dtNavMeshQuery* m_query = nullptr;

    /// @brief Maximum polygons in a path query.
    static constexpr int MAX_PATH_POLYS = 256;
};

} // namespace Vestige
