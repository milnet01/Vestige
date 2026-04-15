// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file nav_mesh_builder.h
/// @brief Wraps Recast for navmesh generation from scene geometry.
#pragma once

#include "navigation/nav_mesh_config.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Forward declarations for Detour types
class dtNavMesh;

namespace Vestige
{

class Scene;

/// @brief Generates a navigation mesh from scene triangle geometry using Recast.
///
/// Collects vertex/index data from all static mesh entities in a scene,
/// voxelizes the geometry, and builds a Detour-compatible navmesh.
/// This is an editor-time operation (not real-time).
class NavMeshBuilder
{
public:
    NavMeshBuilder();
    ~NavMeshBuilder();

    // Non-copyable
    NavMeshBuilder(const NavMeshBuilder&) = delete;
    NavMeshBuilder& operator=(const NavMeshBuilder&) = delete;

    /// @brief Builds a navmesh from all static geometry in the scene.
    /// @param scene The scene to extract geometry from.
    /// @param config Build parameters (cell size, agent dimensions, etc.).
    /// @return True if the navmesh was built successfully.
    bool buildFromScene(Scene& scene, const NavMeshBuildConfig& config = {});

    /// @brief Releases the built navmesh.
    void clear();

    /// @brief Whether a navmesh has been built.
    bool hasMesh() const { return m_navMesh != nullptr; }

    /// @brief Gets the built Detour navmesh (for query use).
    dtNavMesh* getNavMesh() { return m_navMesh; }
    const dtNavMesh* getNavMesh() const { return m_navMesh; }

    /// @brief Gets the number of polygons in the built navmesh.
    int getPolyCount() const { return m_polyCount; }

    /// @brief Gets the time taken for the last build in milliseconds.
    float getLastBuildTimeMs() const { return m_lastBuildTimeMs; }

private:
    dtNavMesh* m_navMesh = nullptr;
    int m_polyCount = 0;
    float m_lastBuildTimeMs = 0.0f;

    /// @brief Collects all triangle geometry from the scene.
    void collectSceneGeometry(Scene& scene,
                              std::vector<float>& vertices,
                              std::vector<int>& indices);
};

} // namespace Vestige
