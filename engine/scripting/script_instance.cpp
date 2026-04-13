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

} // namespace Vestige
