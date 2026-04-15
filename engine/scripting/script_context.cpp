// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_context.cpp
/// @brief ScriptContext interpreter implementation.
#include "scripting/script_context.h"
#include "core/engine.h"
#include "core/logger.h"

#include <cmath>

namespace Vestige
{

ScriptContext::ScriptContext(ScriptInstance& instance,
                            const NodeTypeRegistry& registry,
                            Engine* engine)
    : m_instance(instance)
    , m_registry(registry)
    , m_engine(engine)
{
}

// ---------------------------------------------------------------------------
// Data evaluation (pull)
// ---------------------------------------------------------------------------

ScriptValue ScriptContext::readInput(const ScriptNodeInstance& node, PinId pinId)
{
    // Check if there is a connection feeding this input pin
    const ScriptConnection* conn = findInputConnection(node.nodeId, pinId);

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

        const PinId sourcePinId = internPin(conn->sourcePin);
        const NodeTypeDescriptor* desc = m_registry.findNode(sourceNode->typeName);

        // Pure nodes always route through evaluatePureNode so the per-chain
        // memo cache (m_pureCache, audit M11) is authoritative. The
        // ScriptNodeInstance::outputValues map is per-instance and persists
        // across ScriptContexts — using it for pure nodes would freeze their
        // first-ever evaluation forever, which defeats the per-execution
        // semantics M11 provides.
        if (desc && desc->isPure && desc->execute)
        {
            return evaluatePureNode(conn->sourceNode, sourcePinId);
        }

        // Impure node — outputValues is the per-chain cache (cleared at the
        // start of each impure execute by executeNode). A populated entry
        // means the source already ran in this chain.
        auto cachedIt = sourceNode->outputValues.find(sourcePinId);
        if (cachedIt != sourceNode->outputValues.end())
        {
            return cachedIt->second;
        }

        // Impure node without cached value — return its default
        return ScriptValue(0.0f);
    }

    // Not connected — check property overrides, then return default. Property
    // lookup is string-keyed because the on-disk schema uses strings; we only
    // pay this cost on unconnected pins, not the hot path.
    const std::string& pinNameStr = pinName(pinId);
    auto propIt = node.properties.find(pinNameStr);
    if (propIt != node.properties.end())
    {
        return propIt->second;
    }

    // Look up the pin default from the type descriptor (compare by interned id).
    const NodeTypeDescriptor* desc = m_registry.findNode(node.typeName);
    if (desc)
    {
        for (const auto& pinDef : desc->inputDefs)
        {
            if (pinDef.id == pinId)
            {
                return pinDef.defaultValue;
            }
        }
    }

    return ScriptValue(0.0f);
}

ScriptValue ScriptContext::readInput(const ScriptNodeInstance& node,
                                    const std::string& pinNameStr)
{
    return readInput(node, internPin(pinNameStr));
}

// ---------------------------------------------------------------------------
// Output value setting
// ---------------------------------------------------------------------------

void ScriptContext::setOutput(const ScriptNodeInstance& node, PinId pinId,
                              const ScriptValue& value)
{
    // We need non-const access to set the cached value
    ScriptNodeInstance* mutableNode = m_instance.getNodeInstance(node.nodeId);
    if (mutableNode)
    {
        mutableNode->outputValues[pinId] = value;
    }
}

void ScriptContext::setOutput(const ScriptNodeInstance& node,
                              const std::string& pinNameStr,
                              const ScriptValue& value)
{
    setOutput(node, internPin(pinNameStr), value);
}

// ---------------------------------------------------------------------------
// Execution flow (push)
// ---------------------------------------------------------------------------

void ScriptContext::triggerOutput(const ScriptNodeInstance& node, PinId pinId)
{
    const ScriptConnection* conn = findOutputConnection(node.nodeId, pinId);
    if (!conn)
    {
        return; // No connection from this output — chain ends here
    }

    // Stash the target's input pin so the callee's execute lambda can read
    // it via ctx.entryPin() (audit L6 — Gate and other multi-input nodes).
    // Save/restore around the call so back-to-back triggerOutput calls in
    // the caller observe their own m_entryPin, not the last callee's.
    const PinId savedEntry = m_entryPin;
    m_entryPin = internPin(conn->targetPin);
    executeNode(conn->targetNode);
    m_entryPin = savedEntry;
}

void ScriptContext::triggerOutput(const ScriptNodeInstance& node,
                                  const std::string& pinNameStr)
{
    triggerOutput(node, internPin(pinNameStr));
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
    // Clamp to [0, MAX_DELAY_SECONDS] to reject negative, NaN, or huge values
    // that would make the action effectively never fire (or fire immediately
    // with garbage state). 1 hour is a reasonable upper bound for gameplay.
    constexpr float MAX_DELAY_SECONDS = 3600.0f;
    float clamped = seconds;
    if (!std::isfinite(clamped) || clamped < 0.0f)
    {
        Logger::warning("[ScriptContext] scheduleDelay received invalid "
                        "seconds (" + std::to_string(seconds) +
                        ") — clamping to 0");
        clamped = 0.0f;
    }
    else if (clamped > MAX_DELAY_SECONDS)
    {
        Logger::warning("[ScriptContext] scheduleDelay seconds (" +
                        std::to_string(seconds) + ") exceeds max (" +
                        std::to_string(MAX_DELAY_SECONDS) + ") — clamping");
        clamped = MAX_DELAY_SECONDS;
    }

    PendingLatentAction action;
    action.nodeId = node.nodeId;
    action.outputPin = outputPin;
    action.remainingTime = clamped;
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
// Scene/entity helpers
// ---------------------------------------------------------------------------

Scene* ScriptContext::activeScene()
{
    // m_engine is nullable — unit tests construct ScriptContext with nullptr
    // when they only exercise the interpreter (AUDIT.md §H5, FIXPLAN D1).
    if (!m_engine)
    {
        return nullptr;
    }
    return m_engine->getSceneManager().getActiveScene();
}

Entity* ScriptContext::findEntity(uint32_t entityId)
{
    if (entityId == 0)
    {
        return nullptr;
    }
    Scene* scene = activeScene();
    if (!scene)
    {
        return nullptr;
    }
    return scene->findEntityById(entityId);
}

Entity* ScriptContext::resolveEntity(uint32_t entityId)
{
    // 0 means "owner entity" — this script's attached entity.
    uint32_t resolved = (entityId == 0) ? m_instance.entityId() : entityId;
    return findEntity(resolved);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

const ScriptConnection* ScriptContext::findOutputConnection(
    uint32_t nodeId, PinId pinId) const
{
    // Delegates to ScriptInstance's pre-built index (audit H4).
    return m_instance.findOutputConnection(nodeId, pinId);
}

const ScriptConnection* ScriptContext::findInputConnection(
    uint32_t nodeId, PinId pinId) const
{
    return m_instance.findInputConnection(nodeId, pinId);
}

ScriptValue ScriptContext::evaluatePureNode(uint32_t nodeId, PinId outputPinId)
{
    // Per-execution memoization (audit M11). Keyed by (nodeId, pinId) packed
    // into a uint64_t. A pure node read N times inside one execute() chain
    // (e.g. via a ForLoop body that pulls the same value each iteration) now
    // runs once instead of N times. The cache dies with this ScriptContext,
    // so latent re-triggers and new event dispatches start fresh.
    //
    // D3/AUDIT.md §H7 — nodes that *read* mutable state (GetVariable,
    // FindEntityByName, HasVariable, etc.) are classified isPure=true for
    // lazy evaluation but are NOT memoizable: their output depends on the
    // blackboard/scene at call time, not just on (nodeId, pinId). Gate them
    // with desc->memoizable, which defaults to true so stateless pure nodes
    // (Add, Multiply, Compare) keep the optimization.
    const uint64_t cacheKey = (static_cast<uint64_t>(nodeId) << 32) |
                              static_cast<uint64_t>(outputPinId);

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

    const bool canMemoize = desc->memoizable;

    if (canMemoize)
    {
        auto cacheIt = m_pureCache.find(cacheKey);
        if (cacheIt != m_pureCache.end())
        {
            return cacheIt->second;
        }
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
    auto it = nodeInst->outputValues.find(outputPinId);
    ScriptValue result = (it != nodeInst->outputValues.end())
                             ? it->second
                             : ScriptValue(0.0f);
    if (canMemoize)
    {
        m_pureCache.emplace(cacheKey, result);
    }
    return result;
}

} // namespace Vestige
