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
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void onSceneLoad(Scene& scene) override;
    void onSceneUnload(Scene& scene) override;

    // -- Node type registry --
    NodeTypeRegistry& nodeRegistry() { return m_nodeRegistry; }
    const NodeTypeRegistry& nodeRegistry() const { return m_nodeRegistry; }

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

private:
    /// @brief Register all built-in node types.
    void registerCoreNodes();

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
