// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file navigation_system.h
/// @brief Domain system for navmesh pathfinding and navigation.
#pragma once

#include "core/i_system.h"
#include "navigation/nav_mesh_builder.h"
#include "navigation/nav_mesh_query.h"
#include "navigation/nav_mesh_config.h"

#include <string>

namespace Vestige
{

/// @brief Manages navigation mesh generation and pathfinding queries.
///
/// Owns the NavMeshBuilder (Recast) and NavMeshQuery (Detour) subsystems.
/// Navmesh is baked on demand from the editor (not at startup). Agents
/// with NavAgentComponent follow paths computed by the system.
class NavigationSystem : public ISystem
{
public:
    NavigationSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void drawDebug() override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    // -- Accessors --
    NavMeshBuilder& getBuilder() { return m_builder; }
    NavMeshQuery& getQuery() { return m_query; }

    // -- Editor-facing API --

    /// @brief Bakes a navmesh from all static geometry in the current scene.
    /// @param scene The scene to bake from.
    /// @param config Build parameters (optional, uses defaults).
    /// @return True if bake succeeded.
    bool bakeNavMesh(Scene& scene, const NavMeshBuildConfig& config = {});

    /// @brief Whether a navmesh has been built.
    bool hasNavMesh() const { return m_builder.hasMesh(); }

    /// @brief Clears the baked navmesh.
    void clearNavMesh();

    // -- Runtime query API --

    /// @brief Finds a path between two world positions.
    std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end);

    /// @brief Finds the nearest point on the navmesh.
    glm::vec3 findNearestPoint(const glm::vec3& point);

private:
    static inline const std::string m_name = "Navigation";
    NavMeshBuilder m_builder;
    NavMeshQuery m_query;
    Engine* m_engine = nullptr;
};

} // namespace Vestige
