// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

/// @brief Result of a path query with partial-result flagging.
///
/// Phase 10.9 Slice 3 S8. Before this struct, `findPath` collapsed
/// the Detour `DT_PARTIAL_RESULT` flag into the returned vector's
/// emptiness — success-but-partial looked identical to
/// success-but-complete, so agents arrived 20m short of unreachable
/// targets with no hook for AI to notice or re-plan.
struct PathResult
{
    /// @brief Waypoint positions in world space. Empty if no path.
    std::vector<glm::vec3> waypoints;

    /// @brief True when Detour returned `DT_PARTIAL_RESULT` — the
    /// path reaches a best-guess polygon close to the requested end,
    /// not the end itself. AI consumers should treat this as "target
    /// unreachable, decide what to do next" (re-plan, notify,
    /// give up) rather than "arrived".
    bool partial = false;
};

namespace detail
{

/// @brief Extracts the DT_PARTIAL_RESULT flag from a Detour status.
///
/// Exposed for testability: the bit arithmetic is exercised directly
/// by unit tests so we don't need to build a real Recast/Detour nav
/// mesh to validate partial-flag propagation. Returns true only
/// when the status is both successful and carries DT_PARTIAL_RESULT
/// — a failed status never reports partial, even if the partial bit
/// is incidentally set (failure means no path at all).
/// @param dtStatus Raw `dtStatus` value from a `dtNavMeshQuery` call.
/// @return True iff the status is successful and partial.
bool isPartialPathStatus(unsigned int dtStatus);

} // namespace detail

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

    /// @brief Finds a path between two world positions, surfacing
    /// Detour's `DT_PARTIAL_RESULT` flag.
    /// @param start Start position in world space.
    /// @param end End position in world space.
    /// @return A `PathResult` whose `waypoints` are the path (empty
    /// if no path) and whose `partial` bit is true when the path
    /// is a best-guess stop short of the requested target. AI
    /// consumers should branch on `partial` to decide between
    /// arriving, re-planning, or notifying.
    PathResult findPathWithStatus(const glm::vec3& start, const glm::vec3& end);

    /// @brief Finds a path between two world positions.
    /// @param start Start position in world space.
    /// @param end End position in world space.
    /// @return Vector of waypoint positions, or empty if no path found.
    /// @note Discards the partial-result flag — callers that need to
    /// distinguish complete from partial paths should use
    /// `findPathWithStatus`. This overload is kept for call sites
    /// (e.g. `NavigationSystem::findPath`) that do not yet consume
    /// the flag.
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
