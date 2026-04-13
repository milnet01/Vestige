/// @file script_instance.h
/// @brief Runtime state for one visual script graph attached to one entity.
#pragma once

#include "scripting/blackboard.h"
#include "scripting/script_graph.h"
#include "scripting/script_value.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class Engine;

/// @brief Runtime state for a single pin on a node instance.
struct ScriptPinState
{
    ScriptValue cachedValue; ///< Last evaluated value (for data pins)
};

/// @brief Runtime state for a single node in an active script.
struct ScriptNodeInstance
{
    uint32_t nodeId = 0;
    std::string typeName;

    /// @brief Per-instance property values (from graph definition + overrides).
    std::map<std::string, ScriptValue> properties;

    /// @brief Cached output pin values, set during execute().
    std::unordered_map<std::string, ScriptValue> outputValues;

    /// @brief Persistent per-node runtime state for stateful nodes.
    /// Survives across execute() calls (unlike outputValues, which is cleared).
    /// Used by DoOnce, Gate, FlipFlop, ForLoop index, etc.
    std::unordered_map<std::string, ScriptValue> runtimeState;
};

/// @brief A pending latent action (e.g. Delay timer, WaitForEvent, Timeline).
struct PendingLatentAction
{
    uint32_t nodeId = 0;
    std::string outputPin;  ///< Output pin to trigger when the action completes

    /// @brief Remaining time in seconds (time-based actions). Set to < 0 to mean
    /// "not time-based" (condition-only). Reaching 0 fires outputPin.
    float remainingTime = 0.0f;

    /// @brief Total duration (for progress calculation). 0 if not used.
    float totalDuration = 0.0f;

    /// @brief For condition-based waits — returns true when the wait is satisfied.
    std::function<bool()> condition;

    /// @brief Optional per-frame callback invoked before completion. Receives
    /// progress in [0,1] (1 = complete). Used by Timeline / MoveTo.
    std::function<void(float progress)> onTick;
};

/// @brief Runtime instance of a ScriptGraph, owning execution state.
///
/// Created when a ScriptComponent activates a graph. Destroyed when the
/// component is removed or the scene unloads.
class ScriptInstance
{
public:
    ScriptInstance() = default;

    /// @brief Initialize from a graph definition.
    /// @param graph The graph asset (shared, not owned).
    /// @param entityId The entity this instance is attached to.
    void initialize(const ScriptGraph& graph, uint32_t entityId);

    /// @brief Get the source graph.
    const ScriptGraph& graph() const { return *m_graph; }

    /// @brief Get the entity this instance belongs to.
    uint32_t entityId() const { return m_entityId; }

    /// @brief Whether this instance is currently active (processing events).
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

    // -- Node instances --

    ScriptNodeInstance* getNodeInstance(uint32_t nodeId);
    const ScriptNodeInstance* getNodeInstance(uint32_t nodeId) const;
    const std::unordered_map<uint32_t, ScriptNodeInstance>& nodeInstances() const
    {
        return m_nodeInstances;
    }

    // -- Graph-scope blackboard --

    Blackboard& graphBlackboard() { return m_graphBlackboard; }
    const Blackboard& graphBlackboard() const { return m_graphBlackboard; }

    // -- Latent actions --

    void addLatentAction(PendingLatentAction action);
    std::vector<PendingLatentAction>& pendingActions() { return m_pendingActions; }

    // -- Event subscriptions (for cleanup) --

    void addSubscription(uint32_t subscriptionId);
    const std::vector<uint32_t>& subscriptions() const { return m_subscriptions; }
    void clearSubscriptions() { m_subscriptions.clear(); }

    // -- Find nodes by type --

    /// @brief Find all node instances of a given type name.
    std::vector<uint32_t> findNodesByType(const std::string& typeName) const;

private:
    const ScriptGraph* m_graph = nullptr;
    uint32_t m_entityId = 0;
    bool m_active = false;

    std::unordered_map<uint32_t, ScriptNodeInstance> m_nodeInstances;
    Blackboard m_graphBlackboard;
    std::vector<PendingLatentAction> m_pendingActions;
    std::vector<uint32_t> m_subscriptions; ///< EventBus subscription IDs
};

} // namespace Vestige
