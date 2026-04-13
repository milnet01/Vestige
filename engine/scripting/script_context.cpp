/// @file script_context.cpp
/// @brief ScriptContext interpreter implementation.
#include "scripting/script_context.h"
#include "core/logger.h"

namespace Vestige
{

ScriptContext::ScriptContext(ScriptInstance& instance,
                            const NodeTypeRegistry& registry,
                            Engine& engine)
    : m_instance(instance)
    , m_registry(registry)
    , m_engine(engine)
{
}

// ---------------------------------------------------------------------------
// Data evaluation (pull)
// ---------------------------------------------------------------------------

ScriptValue ScriptContext::readInput(const ScriptNodeInstance& node,
                                    const std::string& pinName)
{
    // Check if there is a connection feeding this input pin
    const ScriptConnection* conn = findInputConnection(node.nodeId, pinName);

    if (conn)
    {
        // Connected — evaluate the source node's output
        const auto* sourceNode = m_instance.getNodeInstance(conn->sourceNode);
        if (!sourceNode)
        {
            Logger::warning("[ScriptContext] Missing source node " +
                            std::to_string(conn->sourceNode));
            return ScriptValue(0.0f);
        }

        // Check if the source already has a cached output value
        auto cachedIt = sourceNode->outputValues.find(conn->sourcePin);
        if (cachedIt != sourceNode->outputValues.end())
        {
            return cachedIt->second;
        }

        // Source has no cached value — evaluate if it's a pure node
        const NodeTypeDescriptor* desc = m_registry.findNode(sourceNode->typeName);
        if (desc && desc->isPure && desc->execute)
        {
            return evaluatePureNode(conn->sourceNode, conn->sourcePin);
        }

        // Impure node without cached value — return its default
        return ScriptValue(0.0f);
    }

    // Not connected — check property overrides, then return default
    auto propIt = node.properties.find(pinName);
    if (propIt != node.properties.end())
    {
        return propIt->second;
    }

    // Look up the pin default from the type descriptor
    const NodeTypeDescriptor* desc = m_registry.findNode(node.typeName);
    if (desc)
    {
        for (const auto& pinDef : desc->inputDefs)
        {
            if (pinDef.name == pinName)
            {
                return pinDef.defaultValue;
            }
        }
    }

    return ScriptValue(0.0f);
}

// ---------------------------------------------------------------------------
// Output value setting
// ---------------------------------------------------------------------------

void ScriptContext::setOutput(const ScriptNodeInstance& node,
                              const std::string& pinName,
                              const ScriptValue& value)
{
    // We need non-const access to set the cached value
    ScriptNodeInstance* mutableNode = m_instance.getNodeInstance(node.nodeId);
    if (mutableNode)
    {
        mutableNode->outputValues[pinName] = value;
    }
}

// ---------------------------------------------------------------------------
// Execution flow (push)
// ---------------------------------------------------------------------------

void ScriptContext::triggerOutput(const ScriptNodeInstance& node,
                                  const std::string& pinName)
{
    const ScriptConnection* conn = findOutputConnection(node.nodeId, pinName);
    if (!conn)
    {
        return; // No connection from this output — chain ends here
    }

    executeNode(conn->targetNode);
}

void ScriptContext::executeNode(uint32_t nodeId)
{
    // Safety: call depth limit
    if (m_callDepth >= MAX_CALL_DEPTH)
    {
        Logger::error("[ScriptContext] Max call depth (" +
                      std::to_string(MAX_CALL_DEPTH) +
                      ") exceeded — possible infinite recursion in graph '" +
                      m_instance.graph().name + "'");
        return;
    }

    // Safety: node execution count limit
    if (m_nodesExecuted >= MAX_NODES_PER_CHAIN)
    {
        Logger::error("[ScriptContext] Max nodes per chain (" +
                      std::to_string(MAX_NODES_PER_CHAIN) +
                      ") exceeded — possible infinite loop in graph '" +
                      m_instance.graph().name + "'");
        return;
    }

    ScriptNodeInstance* nodeInst = m_instance.getNodeInstance(nodeId);
    if (!nodeInst)
    {
        Logger::warning("[ScriptContext] Node " + std::to_string(nodeId) +
                        " not found in instance");
        return;
    }

    const NodeTypeDescriptor* desc = m_registry.findNode(nodeInst->typeName);
    if (!desc || !desc->execute)
    {
        Logger::warning("[ScriptContext] No execute function for node type '" +
                        nodeInst->typeName + "'");
        return;
    }

    // Clear cached output values before execution (impure nodes re-evaluate)
    if (!desc->isPure)
    {
        nodeInst->outputValues.clear();
    }

    ++m_callDepth;
    ++m_nodesExecuted;
    desc->execute(*this, *nodeInst);
    --m_callDepth;
}

// ---------------------------------------------------------------------------
// Variable access
// ---------------------------------------------------------------------------

ScriptValue ScriptContext::getVariable(const std::string& name,
                                       VariableScope scope) const
{
    switch (scope)
    {
    case VariableScope::FLOW:
        return m_flowBlackboard.get(name);
    case VariableScope::GRAPH:
        return m_instance.graphBlackboard().get(name);
    // Entity, Scene, Application, and Saved scopes are managed by
    // ScriptingSystem and accessed through ScriptComponent / global blackboards.
    // For now, fall through to graph scope as a placeholder.
    default:
        return m_instance.graphBlackboard().get(name);
    }
}

void ScriptContext::setVariable(const std::string& name, VariableScope scope,
                                const ScriptValue& value)
{
    switch (scope)
    {
    case VariableScope::FLOW:
        m_flowBlackboard.set(name, value);
        break;
    case VariableScope::GRAPH:
        m_instance.graphBlackboard().set(name, value);
        break;
    default:
        // Entity/Scene/Application/Saved will be routed through ScriptingSystem
        m_instance.graphBlackboard().set(name, value);
        break;
    }
}

// ---------------------------------------------------------------------------
// Latent support
// ---------------------------------------------------------------------------

void ScriptContext::scheduleDelay(const ScriptNodeInstance& node,
                                  const std::string& outputPin, float seconds)
{
    PendingLatentAction action;
    action.nodeId = node.nodeId;
    action.outputPin = outputPin;
    action.remainingTime = seconds;
    m_instance.addLatentAction(std::move(action));
}

void ScriptContext::scheduleWaitForCondition(const ScriptNodeInstance& node,
                                             const std::string& outputPin,
                                             std::function<bool()> condition)
{
    PendingLatentAction action;
    action.nodeId = node.nodeId;
    action.outputPin = outputPin;
    action.remainingTime = -1.0f; // Not time-based
    action.condition = std::move(condition);
    m_instance.addLatentAction(std::move(action));
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

const ScriptConnection* ScriptContext::findOutputConnection(
    uint32_t nodeId, const std::string& pinName) const
{
    for (const auto& c : m_instance.graph().connections)
    {
        if (c.sourceNode == nodeId && c.sourcePin == pinName)
        {
            return &c;
        }
    }
    return nullptr;
}

const ScriptConnection* ScriptContext::findInputConnection(
    uint32_t nodeId, const std::string& pinName) const
{
    for (const auto& c : m_instance.graph().connections)
    {
        if (c.targetNode == nodeId && c.targetPin == pinName)
        {
            return &c;
        }
    }
    return nullptr;
}

ScriptValue ScriptContext::evaluatePureNode(uint32_t nodeId,
                                             const std::string& outputPin)
{
    ScriptNodeInstance* nodeInst = m_instance.getNodeInstance(nodeId);
    if (!nodeInst)
    {
        return ScriptValue(0.0f);
    }

    const NodeTypeDescriptor* desc = m_registry.findNode(nodeInst->typeName);
    if (!desc || !desc->execute)
    {
        return ScriptValue(0.0f);
    }

    // Safety check for recursive evaluation
    if (m_callDepth >= MAX_CALL_DEPTH)
    {
        Logger::error("[ScriptContext] Max call depth exceeded evaluating pure node '"
                      + nodeInst->typeName + "'");
        return ScriptValue(0.0f);
    }

    ++m_callDepth;
    ++m_nodesExecuted;
    desc->execute(*this, *nodeInst);
    --m_callDepth;

    // Return the cached output value
    auto it = nodeInst->outputValues.find(outputPin);
    if (it != nodeInst->outputValues.end())
    {
        return it->second;
    }

    return ScriptValue(0.0f);
}

} // namespace Vestige
