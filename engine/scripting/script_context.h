/// @file script_context.h
/// @brief Execution context for interpreting visual script graphs.
#pragma once

#include "scripting/blackboard.h"
#include "scripting/script_instance.h"
#include "scripting/script_value.h"
#include "scripting/node_type_registry.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Vestige
{

class Engine;

/// @brief Execution context for one impulse through a script graph.
///
/// Created when an event fires or execution is resumed from a latent action.
/// Provides the API that node execute functions use to read inputs, write
/// outputs, trigger downstream nodes, and access engine systems.
///
/// The context walks the graph node-by-node (interpreter pattern). Data pins
/// are evaluated lazily by pulling from connected outputs.
class ScriptContext
{
public:
    /// @brief Create a context for executing within a specific instance.
    /// @param instance The script instance (owns graph + runtime state).
    /// @param registry The node type registry (for looking up execute functions).
    /// @param engine The engine (for EventBus, systems, etc.).
    ScriptContext(ScriptInstance& instance,
                  const NodeTypeRegistry& registry,
                  Engine& engine);

    // -- Data evaluation (pull from connected outputs or use default) --

    /// @brief Read a data input pin's value.
    /// If connected, evaluates the source node's output. Otherwise returns the
    /// pin's default value or the node's property override.
    ///
    /// PinId form is the hot path used by node lambdas (audit M10). The
    /// `const std::string&` overload interns the name and forwards — keeps
    /// test code and editor lookups readable.
    ScriptValue readInput(const ScriptNodeInstance& node, PinId pinId);
    ScriptValue readInput(const ScriptNodeInstance& node,
                          const std::string& pinName);

    /// @brief Typed convenience wrapper for readInput.
    template <typename T>
    T readInputAs(const ScriptNodeInstance& node, PinId pinId);
    template <typename T>
    T readInputAs(const ScriptNodeInstance& node, const std::string& pinName)
    {
        return readInputAs<T>(node, internPin(pinName));
    }

    // -- Output value setting --

    /// @brief Set a data output pin's value (called from node execute functions).
    void setOutput(const ScriptNodeInstance& node, PinId pinId,
                   const ScriptValue& value);
    void setOutput(const ScriptNodeInstance& node, const std::string& pinName,
                   const ScriptValue& value);

    // -- Execution flow (push through execution pins) --

    /// @brief Trigger an execution output pin, continuing the chain.
    /// Follows the connection from the named output pin to the next node
    /// and calls that node's execute function.
    void triggerOutput(const ScriptNodeInstance& node, PinId pinId);
    void triggerOutput(const ScriptNodeInstance& node,
                       const std::string& pinName);

    /// @brief Execute a specific node by ID (used for event entry points).
    void executeNode(uint32_t nodeId);

    // -- Variable access --

    ScriptValue getVariable(const std::string& name, VariableScope scope) const;
    void setVariable(const std::string& name, VariableScope scope,
                     const ScriptValue& value);

    // -- Latent support --

    /// @brief Register a time-based latent action (e.g. Delay).
    /// After the given seconds, the specified output pin will be triggered.
    void scheduleDelay(const ScriptNodeInstance& node,
                       const std::string& outputPin, float seconds);

    /// @brief Register a condition-based latent action.
    /// Each frame, the condition is checked. When it returns true, the output
    /// pin is triggered.
    void scheduleWaitForCondition(const ScriptNodeInstance& node,
                                  const std::string& outputPin,
                                  std::function<bool()> condition);

    // -- Engine access --

    Engine& engine() { return m_engine; }
    ScriptInstance& instance() { return m_instance; }

    // -- Scene/entity convenience (may return nullptr in headless tests) --

    /// @brief Get the active scene, or nullptr if none / engine is uninitialised.
    class Scene* activeScene();

    /// @brief Look up an entity by ID in the active scene. Returns nullptr
    /// if no scene is active or the entity doesn't exist.
    class Entity* findEntity(uint32_t entityId);

    /// @brief Resolve an entity reference: value 0 means "owner entity"
    /// (the entity this script instance is attached to). Any other value is
    /// treated as a raw entity ID looked up in the active scene.
    class Entity* resolveEntity(uint32_t entityId);

    // -- Multi-input dispatch (audit L6) --

    /// @brief PinId of the input pin whose connection routed execution into
    /// the currently-executing node. INVALID_PIN_ID for the initial entry
    /// of an execution chain (no incoming connection). Multi-input nodes
    /// (Gate's Open/Close/Toggle, future merge nodes) dispatch on this to
    /// distinguish which input fired.
    PinId entryPin() const { return m_entryPin; }

    // -- Diagnostics --

    int callDepth() const { return m_callDepth; }
    int nodesExecuted() const { return m_nodesExecuted; }

    /// @brief Maximum nested executeNode() calls within a single context.
    /// Prevents infinite recursion via a cycle of execution-pin connections.
    /// 256 leaves plenty of room for legitimate branch/sequence/switch
    /// nesting while catching pathological graphs. Counter is local to one
    /// ScriptContext and resets when the context is destroyed, so latent
    /// re-triggers and new event dispatches start fresh (see
    /// ScriptInstance::MAX_EVENT_REENTRY_DEPTH for cross-dispatch bounds).
    static constexpr int MAX_CALL_DEPTH = 256;

    /// @brief Maximum total nodes executed within a single chain. Guards
    /// against runaway non-recursive patterns (e.g., a 10,000-step sequence).
    /// 1000 covers typical gameplay logic; values well beyond that typically
    /// indicate a design problem worth surfacing.
    static constexpr int MAX_NODES_PER_CHAIN = 1000;

private:
    /// @brief Find the connection from a node's output pin to a target input.
    const ScriptConnection* findOutputConnection(uint32_t nodeId,
                                                  PinId pinId) const;

    /// @brief Find the connection feeding a node's input pin.
    const ScriptConnection* findInputConnection(uint32_t nodeId,
                                                 PinId pinId) const;

    /// @brief Evaluate a pure node's output value by calling its execute function.
    ScriptValue evaluatePureNode(uint32_t nodeId, PinId outputPinId);

    ScriptInstance& m_instance;
    const NodeTypeRegistry& m_registry;
    Engine& m_engine;

    int m_callDepth = 0;
    int m_nodesExecuted = 0;
    PinId m_entryPin = INVALID_PIN_ID; ///< Set by triggerOutput before executeNode.

    // Blackboard references for non-owned scopes
    Blackboard m_flowBlackboard; ///< Flow scope — local to this execution chain

    /// @brief Memoization cache for pure-node outputs (audit M11). Key packs
    /// `(nodeId << 32) | pinId` — a pure node read multiple times within the
    /// same execute() chain only runs once, eliminating the multiplicative
    /// cost when pure nodes are pulled inside ForLoop bodies. Cache lives on
    /// the ScriptContext stack and dies with it, so cross-tick or
    /// cross-event-dispatch staleness is impossible.
    std::unordered_map<uint64_t, ScriptValue> m_pureCache;
};

// ---------------------------------------------------------------------------
// Template implementations
// ---------------------------------------------------------------------------

template <>
inline bool ScriptContext::readInputAs<bool>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asBool();
}

template <>
inline int32_t ScriptContext::readInputAs<int32_t>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asInt();
}

template <>
inline float ScriptContext::readInputAs<float>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asFloat();
}

template <>
inline std::string ScriptContext::readInputAs<std::string>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asString();
}

template <>
inline glm::vec3 ScriptContext::readInputAs<glm::vec3>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asVec3();
}

template <>
inline glm::vec4 ScriptContext::readInputAs<glm::vec4>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asVec4();
}

template <>
inline glm::quat ScriptContext::readInputAs<glm::quat>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asQuat();
}

template <>
inline uint32_t ScriptContext::readInputAs<uint32_t>(
    const ScriptNodeInstance& node, PinId pinId)
{
    return readInput(node, pinId).asEntityId();
}

} // namespace Vestige
