// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file nav_mesh_query.cpp
/// @brief NavMeshQuery implementation — Detour pathfinding.
#include "navigation/nav_mesh_query.h"
#include "core/logger.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

namespace Vestige
{

namespace detail
{

bool isPartialPathStatus(unsigned int status)
{
    // Only a successful query can have a "partial path". A failed
    // status never produces waypoints, so reporting it as partial
    // would be misleading — callers distinguish failure (empty
    // waypoints) from partial (non-empty but short) and passing the
    // partial bit through on failure would poison that contract.
    return dtStatusSucceed(status) && dtStatusDetail(status, DT_PARTIAL_RESULT);
}

} // namespace detail

NavMeshQuery::NavMeshQuery() = default;

NavMeshQuery::~NavMeshQuery()
{
    shutdown();
}

bool NavMeshQuery::initialize(dtNavMesh* navMesh)
{
    if (!navMesh)
    {
        return false;
    }

    shutdown();

    m_query = dtAllocNavMeshQuery();
    if (!m_query)
    {
        Logger::error("[NavMeshQuery] Failed to allocate query object");
        return false;
    }

    dtStatus status = m_query->init(navMesh, 2048);
    if (dtStatusFailed(status))
    {
        Logger::error("[NavMeshQuery] Failed to initialize query");
        dtFreeNavMeshQuery(m_query);
        m_query = nullptr;
        return false;
    }

    return true;
}

void NavMeshQuery::shutdown()
{
    if (m_query)
    {
        dtFreeNavMeshQuery(m_query);
        m_query = nullptr;
    }
}

PathResult NavMeshQuery::findPathWithStatus(const glm::vec3& start, const glm::vec3& end)
{
    PathResult out;

    if (!m_query)
    {
        return out;
    }

    // Search extents (half-size of the search box around the query point)
    float extents[3] = { 2.0f, 4.0f, 2.0f };
    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);

    // Find nearest polygons to start and end
    dtPolyRef startRef = 0, endRef = 0;
    float startNearest[3], endNearest[3];

    float startPos[3] = { start.x, start.y, start.z };
    float endPos[3] = { end.x, end.y, end.z };

    m_query->findNearestPoly(startPos, extents, &filter, &startRef, startNearest);
    m_query->findNearestPoly(endPos, extents, &filter, &endRef, endNearest);

    if (startRef == 0 || endRef == 0)
    {
        return out;
    }

    // Find polygon path
    dtPolyRef pathPolys[MAX_PATH_POLYS];
    int pathCount = 0;

    dtStatus status = m_query->findPath(startRef, endRef, startNearest, endNearest,
                                         &filter, pathPolys, &pathCount, MAX_PATH_POLYS);
    if (dtStatusFailed(status) || pathCount == 0)
    {
        return out;
    }

    // S8: surface Detour's partial-result flag so AI consumers can
    // notice when the path stops short of the requested target
    // (e.g. target inside a disconnected island, or behind an
    // obstacle Detour cannot route around).
    out.partial = detail::isPartialPathStatus(status);

    // Convert polygon path to waypoints using string pulling
    float straightPath[MAX_PATH_POLYS * 3];
    int straightPathCount = 0;

    m_query->findStraightPath(startNearest, endNearest,
                               pathPolys, pathCount,
                               straightPath, nullptr, nullptr,
                               &straightPathCount, MAX_PATH_POLYS, 0);

    out.waypoints.reserve(static_cast<size_t>(straightPathCount));
    for (int i = 0; i < straightPathCount; ++i)
    {
        int idx = i * 3;
        out.waypoints.emplace_back(straightPath[idx], straightPath[idx + 1], straightPath[idx + 2]);
    }

    return out;
}

std::vector<glm::vec3> NavMeshQuery::findPath(const glm::vec3& start, const glm::vec3& end)
{
    // Forward to the status-surfacing overload and drop the flag.
    // Call sites that need the partial flag should migrate to
    // `findPathWithStatus`.
    return findPathWithStatus(start, end).waypoints;
}

glm::vec3 NavMeshQuery::findNearestPoint(const glm::vec3& point)
{
    if (!m_query)
    {
        return point;
    }

    float extents[3] = { 2.0f, 4.0f, 2.0f };
    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);

    float pos[3] = { point.x, point.y, point.z };
    dtPolyRef ref = 0;
    float nearest[3];

    m_query->findNearestPoly(pos, extents, &filter, &ref, nearest);

    if (ref == 0)
    {
        return point;
    }

    return glm::vec3(nearest[0], nearest[1], nearest[2]);
}

bool NavMeshQuery::isPointOnNavMesh(const glm::vec3& point)
{
    if (!m_query)
    {
        return false;
    }

    float extents[3] = { 0.1f, 0.5f, 0.1f };
    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);

    float pos[3] = { point.x, point.y, point.z };
    dtPolyRef ref = 0;
    float nearest[3];

    m_query->findNearestPoly(pos, extents, &filter, &ref, nearest);

    return ref != 0;
}

} // namespace Vestige
