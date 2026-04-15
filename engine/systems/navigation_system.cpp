// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file navigation_system.cpp
/// @brief NavigationSystem implementation.
#include "systems/navigation_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "core/system_events.h"
#include "navigation/nav_agent_component.h"
#include "scene/component.h"

namespace Vestige
{

bool NavigationSystem::initialize(Engine& engine)
{
    m_engine = &engine;
    Logger::info("[NavigationSystem] Initialized");
    return true;
}

void NavigationSystem::shutdown()
{
    m_query.shutdown();
    m_builder.clear();
    m_engine = nullptr;
    Logger::info("[NavigationSystem] Shut down");
}

void NavigationSystem::update(float /*deltaTime*/)
{
    // Future: advance NavAgentComponent entities along their paths
}

void NavigationSystem::drawDebug()
{
    // Future: render navmesh wireframe via DebugDraw
}

bool NavigationSystem::bakeNavMesh(Scene& scene, const NavMeshBuildConfig& config)
{
    if (!m_builder.buildFromScene(scene, config))
    {
        return false;
    }

    // Initialize query with the new navmesh
    if (!m_query.initialize(m_builder.getNavMesh()))
    {
        Logger::error("[NavigationSystem] Failed to initialize query after bake");
        return false;
    }

    // Publish event
    if (m_engine)
    {
        NavMeshBakedEvent event(m_builder.getPolyCount(),
                                 m_builder.getLastBuildTimeMs());
        m_engine->getEventBus().publish(event);
    }

    return true;
}

void NavigationSystem::clearNavMesh()
{
    m_query.shutdown();
    m_builder.clear();
}

std::vector<glm::vec3> NavigationSystem::findPath(const glm::vec3& start,
                                                    const glm::vec3& end)
{
    return m_query.findPath(start, end);
}

glm::vec3 NavigationSystem::findNearestPoint(const glm::vec3& point)
{
    return m_query.findNearestPoint(point);
}

std::vector<uint32_t> NavigationSystem::getOwnedComponentTypes() const
{
    return {
        ComponentTypeId::get<NavAgentComponent>()
    };
}

} // namespace Vestige
