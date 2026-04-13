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
    /// Keyed by string because the on-disk schema (ScriptNodeDef::properties)
    /// uses strings; values are read by ScriptContext::readInput which falls
    /// back to this map after checking output caches and connections.
    std::map<std::string, ScriptValue> properties;

    /// @brief Cached output pin values, set during execute(). Keyed by PinId
    /// for hot-path performance — no string hashing on read/write (audit M10).
    /// Use ScriptContext::setOutput / readInput for the string-named path.
    std::unordered_map<PinId, ScriptValue> outputValues;

    /// @brief Persistent per-node runtime state for stateful nodes.
    /// Survives across execute() calls (unlike outputValues, which is cleared).
    /// Used by DoOnce, Gate, FlipFlop, ForLoop index, etc. Keyed by PinId
    /// (audit M10).
    std::unordered_map<PinId, ScriptValue> runtimeState;
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
    /// @param graph The graph asset (shared, not owned). See the lifetime
    ///   contract below — the caller must ensure the graph outlives every
    ///   ScriptInstance referencing it.
    /// @param entityId The entity this instance is attached to.
    ///
    /// **Graph lifetime contract:** this class stores a raw pointer to the
    /// graph (`m_graph`). It does NOT extend the graph's lifetime. The caller
    /// is responsible for destroying all ScriptInstances derived from a graph
    /// **before** the graph itself is destroyed. Violating this invariant
    /// produces use-after-free when the instance or its connection indices
    /// are queried. If this becomes a recurring footgun, the mitigation is to
    /// switch `m_graph` to `std::shared_ptr<const ScriptGraph>` at the cost
    /// of one extra atomic refcount per instance (audit H2).
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

    // -- Event re-entrancy guard --

    /// @brief Cap on synchronous EventBus re-entries into this instance.
    /// Each EventBus dispatch creates a fresh ScriptContext, so
    /// ScriptContext::MAX_CALL_DEPTH does not bound event-loop recursion on
    /// its own. This limit prevents infinite recursion when, e.g., a
    /// PublishEvent node re-publishes the event type its OnCustomEvent is
    /// listening for. Small values allow legitimate one-or-two-hop chains
    /// (A publishes B, B's handler publishes C) without harm.
    static constexpr int MAX_EVENT_REENTRY_DEPTH = 4;

    int eventDispatchDepth() const { return m_eventDispatchDepth; }
    void incrementEventDispatchDepth() { ++m_eventDispatchDepth; }
    void decrementEventDispatchDepth() { --m_eventDispatchDepth; }

    // -- Find nodes by type --

    /// @brief Find all node instances of a given type name. Backed by the
    /// pre-built type index (audit M9) — O(1) average map lookup with no
    /// per-call allocation. Returns an empty vector for unknown types.
    const std::vector<uint32_t>& nodesByType(const std::string& typeName) const;

    /// @brief Backwards-compatible by-value variant. Internally calls
    /// nodesByType() and copies. Prefer nodesByType() on hot paths.
    std::vector<uint32_t> findNodesByType(const std::string& typeName) const;

    // -- Hot-path caches (populated in initialize(), immutable thereafter) --

    /// @brief Cached IDs of OnUpdate nodes. Avoids a per-frame linear scan of
    /// all nodes inside ScriptingSystem::tickUpdateNodes (audit H3 → M9).
    const std::vector<uint32_t>& updateNodes() const;

    /// @brief Find the (first) connection whose source is the given node/pin.
    /// Uses a pre-built index: O(pins-per-node) instead of O(total-connections)
    /// per call (audit H4). Returns nullptr if no such connection exists.
    /// PinId form is the hot path; the string overload interns + forwards.
    const ScriptConnection* findOutputConnection(
        uint32_t nodeId, PinId pinId) const;
    const ScriptConnection* findOutputConnection(
        uint32_t nodeId, const std::string& pinName) const;

    /// @brief Find the connection whose target is the given node/pin. Returns
    /// nullptr if the pin has no incoming connection.
    const ScriptConnection* findInputConnection(
        uint32_t nodeId, PinId pinId) const;
    const ScriptConnection* findInputConnection(
        uint32_t nodeId, const std::string& pinName) const;

private:
    /// @brief Rebuild the hot-path caches (updateNodes + connection indices).
    /// Called at the end of initialize(); no runtime graph mutation is
    /// supported by this design.
    void rebuildCaches();

    struct PinConnection
    {
        PinId pin;                 ///< Interned pin id (audit M10).
        const ScriptConnection* conn;
    };

    const ScriptGraph* m_graph = nullptr;
    uint32_t m_entityId = 0;
    bool m_active = false;

    std::unordered_map<uint32_t, ScriptNodeInstance> m_nodeInstances;
    Blackboard m_graphBlackboard;
    std::vector<PendingLatentAction> m_pendingActions;
    std::vector<uint32_t> m_subscriptions; ///< EventBus subscription IDs
    int m_eventDispatchDepth = 0;

    // Hot-path caches (see audit H3/H4/M9).
    std::unordered_map<std::string, std::vector<uint32_t>> m_typeIndex;
    std::unordered_map<uint32_t, std::vector<PinConnection>> m_outputByNode;
    std::unordered_map<uint32_t, std::vector<PinConnection>> m_inputByNode;
};

} // namespace Vestige
