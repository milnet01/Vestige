/// @file system_registry.cpp
/// @brief SystemRegistry implementation -- domain system lifecycle and dispatch.
#include "core/system_registry.h"
#include "core/i_system.h"
#include "core/logger.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <algorithm>
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

    for (auto& system : m_systems)
    {
        Logger::info("SystemRegistry: initializing '" + system->getSystemName() + "'");

        if (!system->initialize(engine))
        {
            Logger::error("SystemRegistry: failed to initialize '"
                          + system->getSystemName() + "'");
            return false;
        }
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
