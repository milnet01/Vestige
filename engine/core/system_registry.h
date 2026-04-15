// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file system_registry.h
/// @brief Manages all domain system instances and their lifecycle.
#pragma once

#include "core/i_system.h"

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class Engine;
class Scene;
struct SceneRenderData;
class PerformanceProfiler;

/// @brief Per-system timing metrics snapshot.
struct SystemMetrics
{
    std::string name;
    float updateTimeMs = 0.0f;
    float fixedUpdateTimeMs = 0.0f;
    float budgetMs = 0.0f;
    bool overBudget = false;
    bool active = false;
};

/// @brief Central registry that manages all domain system instances.
///
/// Systems are registered during engine initialization and updated each frame.
/// The registry handles:
/// - Lifecycle management (initialize, shutdown in reverse order)
/// - Per-frame dispatch (update, fixedUpdate, submitRenderData)
/// - Auto-activation based on scene component types
/// - Performance monitoring (per-system timing)
///
/// Usage:
/// @code
///   SystemRegistry registry;
///   auto* terrain = registry.registerSystem<TerrainSystem>();
///   auto* water   = registry.registerSystem<WaterSystem>();
///   registry.initializeAll(engine);
///   // ... in game loop:
///   registry.updateAll(deltaTime);
///   registry.submitRenderDataAll(renderData);
///   // ... on shutdown:
///   registry.shutdownAll();
/// @endcode
class SystemRegistry
{
public:
    SystemRegistry() = default;
    ~SystemRegistry() = default;

    // Non-copyable, non-movable
    SystemRegistry(const SystemRegistry&) = delete;
    SystemRegistry& operator=(const SystemRegistry&) = delete;
    SystemRegistry(SystemRegistry&&) = delete;
    SystemRegistry& operator=(SystemRegistry&&) = delete;

    // -----------------------------------------------------------------------
    // Registration
    // -----------------------------------------------------------------------

    /// @brief Registers a new domain system. Must be called before initializeAll().
    /// @tparam T The system type (must derive from ISystem).
    /// @tparam Args Constructor argument types.
    /// @param args Arguments forwarded to T's constructor.
    /// @return Pointer to the registered system (owned by the registry).
    template <typename T, typename... Args>
    T* registerSystem(Args&&... args)
    {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = system.get();
        m_typeMap[std::type_index(typeid(T))] = ptr;
        m_systems.push_back(std::move(system));
        return ptr;
    }

    /// @brief Retrieves a registered system by type.
    /// @tparam T The system type to look up.
    /// @return Pointer to the system, or nullptr if not registered.
    template <typename T>
    T* getSystem()
    {
        auto it = m_typeMap.find(std::type_index(typeid(T)));
        if (it != m_typeMap.end())
        {
            return static_cast<T*>(it->second);
        }
        return nullptr;
    }

    /// @brief Retrieves a registered system by type (const version).
    template <typename T>
    const T* getSystem() const
    {
        auto it = m_typeMap.find(std::type_index(typeid(T)));
        if (it != m_typeMap.end())
        {
            return static_cast<const T*>(it->second);
        }
        return nullptr;
    }

    /// @brief Returns the number of registered systems.
    size_t getSystemCount() const { return m_systems.size(); }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// @brief Initializes all registered systems in registration order.
    /// @param engine Reference to the engine for shared infrastructure access.
    /// @return True if all systems initialized successfully.
    bool initializeAll(Engine& engine);

    /// @brief Shuts down all systems in reverse registration order.
    ///
    /// This calls each system's `shutdown()` method but does NOT destroy the
    /// system instances. Pair with `clear()` to actually release them while
    /// shared infrastructure (Renderer, Window, GL context) is still alive.
    void shutdownAll();

    /// @brief Destroys all owned systems in reverse registration order.
    ///
    /// AUDIT.md §H17: must be called from `Engine::shutdown()` while the
    /// Renderer, Window, and GL context are still alive — otherwise system
    /// destructors run during `~Engine` member cleanup, after those
    /// dependencies have already been freed, and dereference dangling
    /// pointers / dead GL handles. The original symptom was a SEGV reported
    /// by ASan immediately after the "Engine shutdown complete" log line.
    ///
    /// Idempotent: safe to call on an already-empty registry, or after
    /// another `clear()`. Calling `shutdownAll()` is not required first
    /// (system destructors will run normally) but is the conventional order.
    void clear();

    // -----------------------------------------------------------------------
    // Per-frame dispatch
    // -----------------------------------------------------------------------

    /// @brief Calls update() on all active systems. Measures per-system timing.
    /// @param deltaTime Variable-rate frame delta in seconds.
    void updateAll(float deltaTime);

    /// @brief Calls fixedUpdate() on all active systems.
    /// @param fixedDeltaTime Fixed timestep in seconds.
    void fixedUpdateAll(float fixedDeltaTime);

    /// @brief Calls submitRenderData() on all active systems.
    /// @param renderData The render data collection to populate.
    void submitRenderDataAll(SceneRenderData& renderData);

    // -----------------------------------------------------------------------
    // Scene lifecycle
    // -----------------------------------------------------------------------

    /// @brief Notifies all systems of a scene load and auto-activates based on
    ///        component types present in the scene.
    /// @param scene The newly loaded scene.
    void onSceneLoadAll(Scene& scene);

    /// @brief Notifies all systems of a scene unload.
    /// @param scene The scene about to be unloaded.
    void onSceneUnloadAll(Scene& scene);

    /// @brief Scans scene entities for component types and activates systems
    ///        whose owned component types are present. Force-active systems
    ///        are always activated.
    /// @param scene The scene to scan.
    void activateSystemsForScene(Scene& scene);

    // -----------------------------------------------------------------------
    // Debug and metrics
    // -----------------------------------------------------------------------

    /// @brief Calls drawDebug() on all active systems.
    void drawDebugAll();

    /// @brief Returns per-system timing metrics for the last frame.
    std::vector<SystemMetrics> getSystemMetrics() const;

    /// @brief Returns the total update time across all systems in milliseconds.
    float getTotalUpdateTimeMs() const;

private:
    /// @brief All registered systems in registration order.
    std::vector<std::unique_ptr<ISystem>> m_systems;

    /// @brief Type-indexed lookup map for getSystem<T>().
    std::unordered_map<std::type_index, ISystem*> m_typeMap;

    /// @brief Whether initializeAll() has been called.
    bool m_initialized = false;
};

} // namespace Vestige
