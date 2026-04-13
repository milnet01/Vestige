/// @file scripting_system.h
/// @brief Domain system for visual scripting — manages script lifecycle and execution.
#pragma once

#include "core/i_system.h"
#include "scripting/blackboard.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_instance.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Domain system that manages visual script lifecycle and execution.
///
/// Responsibilities:
/// - Registers built-in node types at initialization
/// - Loads script graphs and creates instances on scene load
/// - Subscribes event nodes to the EventBus
/// - Ticks OnUpdate nodes and latent actions each frame
/// - Manages scene and application blackboards
/// - Cleans up on scene unload
class ScriptingSystem : public ISystem
{
public:
    // -- ISystem interface --
    const std::string& getSystemName() const override;

    /// @brief Initialise the scripting system.
    ///
    /// NOT idempotent in the current implementation (AUDIT.md §L1): calling
    /// initialize() twice registers the built-in node types twice, which
    /// NodeTypeRegistry handles without crashing but produces duplicate
    /// entries. Callers must pair every initialize() with a shutdown()
    /// before re-initialising. The engine lifecycle guarantees this by
    /// construction; unit tests that need to re-initialise should call
    /// shutdown() first.
    bool initialize(Engine& engine) override;

    /// @brief Tear down the scripting system.
    ///
    /// Destroys all script instances, blackboards, and node registry
    /// entries so the system is safe to initialise again.
    void shutdown() override;
    void update(float deltaTime) override;
    void onSceneLoad(Scene& scene) override;
    void onSceneUnload(Scene& scene) override;

    // -- Node type registry --
    NodeTypeRegistry& nodeRegistry() { return m_nodeRegistry; }
    const NodeTypeRegistry& nodeRegistry() const { return m_nodeRegistry; }

    // -- Engine access (for subscription callbacks that need it) --
    Engine* engine() { return m_engine; }
    const Engine* engine() const { return m_engine; }

    // -- Blackboards --
    Blackboard& sceneBlackboard() { return m_sceneBlackboard; }
    Blackboard& appBlackboard() { return m_appBlackboard; }

    // -- Active instances (for debugging/profiling) --
    const std::vector<ScriptInstance*>& activeInstances() const
    {
        return m_activeInstances;
    }

    /// @brief Manually execute an event on a specific instance (for testing).
    void fireEvent(ScriptInstance& instance, uint32_t nodeId);

    /// @brief Register a script instance for active execution. Subscribes its
    /// event nodes to the EventBus and adds it to the active list.
    ///
    /// Call this after the instance's graph has been assigned to make the
    /// script live. Call unregisterInstance() before destroying the instance.
    void registerInstance(ScriptInstance& instance);

    /// @brief Unregister a script instance. Unsubscribes event nodes, cancels
    /// pending latent actions, and removes it from the active list.
    void unregisterInstance(ScriptInstance& instance);

    /// @brief Check whether a given instance pointer is currently registered.
    /// Used by EventBus callbacks to guard against calling back into an
    /// instance that has already been unregistered or destroyed.
    bool isInstanceActive(const ScriptInstance* instance) const;

private:
    /// @brief Register all built-in node types.
    void registerCoreNodes();

    /// @brief Subscribe all event nodes in the given instance's graph to the
    /// EventBus. Handled by eventTypeName string dispatch.
    void subscribeEventNodes(ScriptInstance& instance);

    /// @brief Tick all pending latent actions (timers, conditions).
    void tickLatentActions(float deltaTime);

    /// @brief Tick OnUpdate nodes on all active instances.
    void tickUpdateNodes(float deltaTime);

    static const std::string SYSTEM_NAME;

    Engine* m_engine = nullptr;
    NodeTypeRegistry m_nodeRegistry;

    std::vector<ScriptInstance*> m_activeInstances;

    Blackboard m_sceneBlackboard;
    Blackboard m_appBlackboard;
};

} // namespace Vestige
