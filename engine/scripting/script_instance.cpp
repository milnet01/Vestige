/// @file script_instance.cpp
/// @brief ScriptInstance implementation.
#include "scripting/script_instance.h"

namespace Vestige
{

void ScriptInstance::initialize(const ScriptGraph& graph, uint32_t entityId)
{
    m_graph = &graph;
    m_entityId = entityId;
    m_active = false;
    m_nodeInstances.clear();
    m_graphBlackboard.clear();
    m_pendingActions.clear();
    m_subscriptions.clear();

    // Create runtime node instances from the graph definition
    for (const auto& nodeDef : graph.nodes)
    {
        ScriptNodeInstance inst;
        inst.nodeId = nodeDef.id;
        inst.typeName = nodeDef.typeName;
        inst.properties = nodeDef.properties;
        m_nodeInstances[nodeDef.id] = std::move(inst);
    }

    // Initialize graph-scope variables with their defaults
    for (const auto& varDef : graph.variables)
    {
        if (varDef.scope == VariableScope::GRAPH)
        {
            m_graphBlackboard.set(varDef.name, varDef.defaultValue);
        }
    }

    rebuildCaches();
}

void ScriptInstance::rebuildCaches()
{
    m_updateNodes.clear();
    m_outputByNode.clear();
    m_inputByNode.clear();

    if (!m_graph)
    {
        return;
    }

    // Cache OnUpdate node IDs so the ScriptingSystem doesn't rescan every node
    // on every frame. A graph with N nodes and K OnUpdates goes from O(N) per
    // frame to O(K) (audit H3).
    for (const auto& [id, inst] : m_nodeInstances)
    {
        if (inst.typeName == "OnUpdate")
        {
            m_updateNodes.push_back(id);
        }
    }

    // Connection indices. Connections are immutable after load, so building
    // an index once at init is cheap and collapses the pin-lookup hot path
    // from O(total-connections) to O(pins-per-node) (audit H4). Pin names
    // get interned to PinId here so subsequent lookups compare integers
    // rather than strings (audit M10).
    for (const auto& c : m_graph->connections)
    {
        m_outputByNode[c.sourceNode].push_back({internPin(c.sourcePin), &c});
        m_inputByNode[c.targetNode].push_back({internPin(c.targetPin), &c});
    }
}

ScriptNodeInstance* ScriptInstance::getNodeInstance(uint32_t nodeId)
{
    auto it = m_nodeInstances.find(nodeId);
    return it != m_nodeInstances.end() ? &it->second : nullptr;
}

const ScriptNodeInstance* ScriptInstance::getNodeInstance(uint32_t nodeId) const
{
    auto it = m_nodeInstances.find(nodeId);
    return it != m_nodeInstances.end() ? &it->second : nullptr;
}

void ScriptInstance::addLatentAction(PendingLatentAction action)
{
    m_pendingActions.push_back(std::move(action));
}

void ScriptInstance::addSubscription(uint32_t subscriptionId)
{
    m_subscriptions.push_back(subscriptionId);
}

std::vector<uint32_t> ScriptInstance::findNodesByType(
    const std::string& typeName) const
{
    std::vector<uint32_t> result;
    for (const auto& [id, inst] : m_nodeInstances)
    {
        if (inst.typeName == typeName)
        {
            result.push_back(id);
        }
    }
    return result;
}

const ScriptConnection* ScriptInstance::findOutputConnection(
    uint32_t nodeId, PinId pinId) const
{
    auto it = m_outputByNode.find(nodeId);
    if (it == m_outputByNode.end())
    {
        return nullptr;
    }
    for (const auto& pc : it->second)
    {
        if (pc.pin == pinId)
        {
            return pc.conn;
        }
    }
    return nullptr;
}

const ScriptConnection* ScriptInstance::findOutputConnection(
    uint32_t nodeId, const std::string& pinName) const
{
    return findOutputConnection(nodeId, internPin(pinName));
}

const ScriptConnection* ScriptInstance::findInputConnection(
    uint32_t nodeId, PinId pinId) const
{
    auto it = m_inputByNode.find(nodeId);
    if (it == m_inputByNode.end())
    {
        return nullptr;
    }
    for (const auto& pc : it->second)
    {
        if (pc.pin == pinId)
        {
            return pc.conn;
        }
    }
    return nullptr;
}

const ScriptConnection* ScriptInstance::findInputConnection(
    uint32_t nodeId, const std::string& pinName) const
{
    return findInputConnection(nodeId, internPin(pinName));
}

} // namespace Vestige
