// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file system_registry.cpp
/// @brief SystemRegistry implementation -- domain system lifecycle and dispatch.
#include "core/system_registry.h"
#include "core/logger.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <chrono>
#include <unordered_set>

namespace Vestige
{

bool SystemRegistry::initializeAll(Engine& engine)
{
    if (m_initialized)
    {
        Logger::warning("SystemRegistry: already initialized");
        return false;
    }

    // Track how many systems initialized cleanly so failure-path cleanup
    // (Phase 10.9 Slice 1 F10) can tear down exactly that prefix in reverse.
    // System N itself gets no shutdown() — its initialize() returned false,
    // meaning resources were not acquired.
    size_t initializedCount = 0;
    for (auto& system : m_systems)
    {
        Logger::info("SystemRegistry: initializing '" + system->getSystemName() + "'");

        if (!system->initialize(engine))
        {
            Logger::error("SystemRegistry: failed to initialize '"
                          + system->getSystemName() + "'");

            // F10: shutdown the 0..initializedCount-1 prefix in reverse so
            // GL/AL/Jolt resources those systems acquired are released
            // synchronously, not leaked until process exit. shutdownAll()
            // early-returns on !m_initialized (which stays false here), so
            // without this rollback the prefix would be orphaned.
            for (size_t i = initializedCount; i-- > 0;)
            {
                Logger::info("SystemRegistry: rolling back '"
                             + m_systems[i]->getSystemName() + "'");
                m_systems[i]->shutdown();
                m_systems[i]->setActive(false);
            }
            return false;
        }
        ++initializedCount;
    }

    m_initialized = true;
    Logger::info("SystemRegistry: initialized " + std::to_string(m_systems.size())
                 + " systems");
    return true;
}

void SystemRegistry::shutdownAll()
{
    if (!m_initialized)
    {
        return;
    }

    // Shutdown in reverse registration order
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it)
    {
        Logger::info("SystemRegistry: shutting down '" + (*it)->getSystemName() + "'");
        (*it)->shutdown();
        (*it)->setActive(false);
    }

    m_initialized = false;
    Logger::info("SystemRegistry: all systems shut down");
}

void SystemRegistry::clear()
{
    if (m_systems.empty())
    {
        return;
    }

    // Drop the type lookup first — any stale getSystem<T>() call after
    // clear() should return nullptr rather than dangle into freed memory.
    m_typeMap.clear();

    // Destroy in reverse registration order, mirroring shutdownAll(), so a
    // system registered later (which may depend on systems registered
    // earlier) is torn down first.
    while (!m_systems.empty())
    {
        m_systems.pop_back();
    }

    // shutdownAll() already cleared this; reset defensively in case clear()
    // is called without a prior shutdownAll().
    m_initialized = false;
}

void SystemRegistry::updateAll(float deltaTime)
{
    for (auto& system : m_systems)
    {
        if (!system->isActive())
        {
            continue;
        }

        auto start = std::chrono::steady_clock::now();
        system->update(deltaTime);
        auto end = std::chrono::steady_clock::now();

        float ms = std::chrono::duration<float, std::milli>(end - start).count();
        system->m_lastUpdateTimeMs = ms;

        if (system->isOverBudget())
        {
            Logger::warning("SystemRegistry: '" + system->getSystemName()
                            + "' over budget (" + std::to_string(ms) + "ms / "
                            + std::to_string(system->getFrameBudgetMs()) + "ms)");
        }
    }
}

void SystemRegistry::fixedUpdateAll(float fixedDeltaTime)
{
    for (auto& system : m_systems)
    {
        if (!system->isActive())
        {
            continue;
        }

        system->fixedUpdate(fixedDeltaTime);
    }
}

void SystemRegistry::submitRenderDataAll(SceneRenderData& renderData)
{
    for (auto& system : m_systems)
    {
        if (!system->isActive())
        {
            continue;
        }

        system->submitRenderData(renderData);
    }
}

void SystemRegistry::onSceneLoadAll(Scene& scene)
{
    activateSystemsForScene(scene);

    for (auto& system : m_systems)
    {
        if (!system->isActive())
        {
            continue;
        }

        system->onSceneLoad(scene);
    }
}

void SystemRegistry::onSceneUnloadAll(Scene& scene)
{
    for (auto& system : m_systems)
    {
        if (!system->isActive())
        {
            continue;
        }

        system->onSceneUnload(scene);
    }
}

void SystemRegistry::activateSystemsForScene(Scene& scene)
{
    // Collect all component type IDs present in the scene
    std::unordered_set<uint32_t> sceneComponentTypes;
    scene.forEachEntity([&sceneComponentTypes](const Entity& entity)
    {
        auto typeIds = entity.getComponentTypeIds();
        for (uint32_t id : typeIds)
        {
            sceneComponentTypes.insert(id);
        }
    });

    // Activate systems whose owned components are present, or that are force-active
    for (auto& system : m_systems)
    {
        if (system->isForceActive())
        {
            system->setActive(true);
            continue;
        }

        auto ownedTypes = system->getOwnedComponentTypes();
        if (ownedTypes.empty())
        {
            // Systems with no owned components default to active
            system->setActive(true);
            continue;
        }

        bool hasMatchingComponent = false;
        for (uint32_t typeId : ownedTypes)
        {
            if (sceneComponentTypes.count(typeId) > 0)
            {
                hasMatchingComponent = true;
                break;
            }
        }

        system->setActive(hasMatchingComponent);

        if (hasMatchingComponent)
        {
            Logger::info("SystemRegistry: auto-activated '" + system->getSystemName()
                         + "' (matching components found)");
        }
    }
}

void SystemRegistry::drawDebugAll()
{
    for (auto& system : m_systems)
    {
        if (!system->isActive())
        {
            continue;
        }

        system->drawDebug();
    }
}

std::vector<SystemMetrics> SystemRegistry::getSystemMetrics() const
{
    std::vector<SystemMetrics> metrics;
    metrics.reserve(m_systems.size());

    for (const auto& system : m_systems)
    {
        SystemMetrics m;
        m.name = system->getSystemName();
        m.updateTimeMs = system->getLastUpdateTimeMs();
        m.budgetMs = system->getFrameBudgetMs();
        m.overBudget = system->isOverBudget();
        m.active = system->isActive();
        metrics.push_back(std::move(m));
    }

    return metrics;
}

float SystemRegistry::getTotalUpdateTimeMs() const
{
    float total = 0.0f;
    for (const auto& system : m_systems)
    {
        if (system->isActive())
        {
            total += system->getLastUpdateTimeMs();
        }
    }
    return total;
}

} // namespace Vestige
