/// @file script_context.h
/// @brief Execution context for interpreting visual script graphs.
#pragma once

#include "scripting/blackboard.h"
#include "scripting/script_instance.h"
#include "scripting/script_value.h"
#include "scripting/node_type_registry.h"

#include <string>

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
    ScriptValue readInput(const ScriptNodeInstance& node,
                          const std::string& pinName);

    /// @brief Typed convenience wrapper for readInput.
    template <typename T>
    T readInputAs(const ScriptNodeInstance& node, const std::string& pinName);

    // -- Output value setting --

    /// @brief Set a data output pin's value (called from node execute functions).
    void setOutput(const ScriptNodeInstance& node, const std::string& pinName,
                   const ScriptValue& value);

    // -- Execution flow (push through execution pins) --

    /// @brief Trigger an execution output pin, continuing the chain.
    /// Follows the connection from the named output pin to the next node
    /// and calls that node's execute function.
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

    // -- Diagnostics --

    int callDepth() const { return m_callDepth; }
    int nodesExecuted() const { return m_nodesExecuted; }

    static constexpr int MAX_CALL_DEPTH = 256;
    static constexpr int MAX_NODES_PER_CHAIN = 1000;

private:
    /// @brief Find the connection from a node's output pin to a target input.
    const ScriptConnection* findOutputConnection(uint32_t nodeId,
                                                  const std::string& pinName) const;

    /// @brief Find the connection feeding a node's input pin.
    const ScriptConnection* findInputConnection(uint32_t nodeId,
                                                 const std::string& pinName) const;

    /// @brief Evaluate a pure node's output value by calling its execute function.
    ScriptValue evaluatePureNode(uint32_t nodeId, const std::string& outputPin);

    ScriptInstance& m_instance;
    const NodeTypeRegistry& m_registry;
    Engine& m_engine;

    int m_callDepth = 0;
    int m_nodesExecuted = 0;

    // Blackboard references for non-owned scopes
    Blackboard m_flowBlackboard; ///< Flow scope — local to this execution chain
};

// ---------------------------------------------------------------------------
// Template implementations
// ---------------------------------------------------------------------------

template <>
inline bool ScriptContext::readInputAs<bool>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asBool();
}

template <>
inline int32_t ScriptContext::readInputAs<int32_t>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asInt();
}

template <>
inline float ScriptContext::readInputAs<float>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asFloat();
}

template <>
inline std::string ScriptContext::readInputAs<std::string>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asString();
}

template <>
inline glm::vec3 ScriptContext::readInputAs<glm::vec3>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asVec3();
}

template <>
inline glm::vec4 ScriptContext::readInputAs<glm::vec4>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asVec4();
}

template <>
inline glm::quat ScriptContext::readInputAs<glm::quat>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asQuat();
}

template <>
inline uint32_t ScriptContext::readInputAs<uint32_t>(
    const ScriptNodeInstance& node, const std::string& pinName)
{
    return readInput(node, pinName).asEntityId();
}

} // namespace Vestige
