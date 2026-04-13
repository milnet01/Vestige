/// @file script_graph.cpp
/// @brief ScriptGraph implementation — node/connection management and JSON I/O.
#include "scripting/script_graph.h"
#include "core/logger.h"

#include <algorithm>
#include <fstream>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Node management
// ---------------------------------------------------------------------------

uint32_t ScriptGraph::addNode(const std::string& typeName, float posX, float posY)
{
    ScriptNodeDef node;
    node.id = nextNodeId++;
    node.typeName = typeName;
    node.posX = posX;
    node.posY = posY;
    nodes.push_back(std::move(node));
    return nodes.back().id;
}

bool ScriptGraph::removeNode(uint32_t nodeId)
{
    auto nodeIt = std::find_if(nodes.begin(), nodes.end(),
        [nodeId](const ScriptNodeDef& n) { return n.id == nodeId; });

    if (nodeIt == nodes.end())
    {
        return false;
    }

    // Remove all connections involving this node
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [nodeId](const ScriptConnection& c)
            {
                return c.sourceNode == nodeId || c.targetNode == nodeId;
            }),
        connections.end());

    nodes.erase(nodeIt);
    return true;
}

ScriptNodeDef* ScriptGraph::findNode(uint32_t nodeId)
{
    for (auto& node : nodes)
    {
        if (node.id == nodeId)
        {
            return &node;
        }
    }
    return nullptr;
}

const ScriptNodeDef* ScriptGraph::findNode(uint32_t nodeId) const
{
    for (const auto& node : nodes)
    {
        if (node.id == nodeId)
        {
            return &node;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

uint32_t ScriptGraph::addConnection(uint32_t srcNode, const std::string& srcPin,
                                    uint32_t tgtNode, const std::string& tgtPin)
{
    // Self-connection check
    if (srcNode == tgtNode)
    {
        return 0;
    }

    // Validate source and target nodes exist
    if (!findNode(srcNode) || !findNode(tgtNode))
    {
        return 0;
    }

    // Duplicate check
    for (const auto& c : connections)
    {
        if (c.sourceNode == srcNode && c.sourcePin == srcPin &&
            c.targetNode == tgtNode && c.targetPin == tgtPin)
        {
            return 0;
        }
    }

    // Check that target input does not already have a connection
    // (each input pin accepts only one connection)
    for (const auto& c : connections)
    {
        if (c.targetNode == tgtNode && c.targetPin == tgtPin)
        {
            return 0;
        }
    }

    ScriptConnection conn;
    conn.id = nextConnectionId++;
    conn.sourceNode = srcNode;
    conn.sourcePin = srcPin;
    conn.targetNode = tgtNode;
    conn.targetPin = tgtPin;
    connections.push_back(conn);
    return conn.id;
}

bool ScriptGraph::removeConnection(uint32_t connectionId)
{
    auto it = std::find_if(connections.begin(), connections.end(),
        [connectionId](const ScriptConnection& c) { return c.id == connectionId; });

    if (it == connections.end())
    {
        return false;
    }

    connections.erase(it);
    return true;
}

std::vector<const ScriptConnection*> ScriptGraph::getNodeConnections(
    uint32_t nodeId) const
{
    std::vector<const ScriptConnection*> result;
    for (const auto& c : connections)
    {
        if (c.sourceNode == nodeId || c.targetNode == nodeId)
        {
            result.push_back(&c);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool ScriptGraph::validate(std::string& errorOut) const
{
    // Check all connections reference valid nodes
    for (const auto& c : connections)
    {
        if (!findNode(c.sourceNode))
        {
            errorOut = "Connection " + std::to_string(c.id) +
                       " references missing source node " +
                       std::to_string(c.sourceNode);
            return false;
        }
        if (!findNode(c.targetNode))
        {
            errorOut = "Connection " + std::to_string(c.id) +
                       " references missing target node " +
                       std::to_string(c.targetNode);
            return false;
        }
    }

    // Check for duplicate node IDs
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        for (size_t j = i + 1; j < nodes.size(); ++j)
        {
            if (nodes[i].id == nodes[j].id)
            {
                errorOut = "Duplicate node ID: " + std::to_string(nodes[i].id);
                return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

nlohmann::json ScriptGraph::toJson() const
{
    nlohmann::json j;
    j["version"] = version;
    j["name"] = name;
    j["nextNodeId"] = nextNodeId;
    j["nextConnectionId"] = nextConnectionId;

    // Nodes
    auto& nodesArr = j["nodes"] = nlohmann::json::array();
    for (const auto& node : nodes)
    {
        nlohmann::json nj;
        nj["id"] = node.id;
        nj["type"] = node.typeName;
        nj["posX"] = node.posX;
        nj["posY"] = node.posY;

        if (!node.properties.empty())
        {
            nlohmann::json props = nlohmann::json::object();
            for (const auto& [key, val] : node.properties)
            {
                props[key] = val.toJson();
            }
            nj["properties"] = props;
        }

        nodesArr.push_back(nj);
    }

    // Connections
    auto& connsArr = j["connections"] = nlohmann::json::array();
    for (const auto& c : connections)
    {
        nlohmann::json cj;
        cj["id"] = c.id;
        cj["srcNode"] = c.sourceNode;
        cj["srcPin"] = c.sourcePin;
        cj["tgtNode"] = c.targetNode;
        cj["tgtPin"] = c.targetPin;
        connsArr.push_back(cj);
    }

    // Variables
    auto& varsArr = j["variables"] = nlohmann::json::array();
    for (const auto& v : variables)
    {
        varsArr.push_back(v.toJson());
    }

    return j;
}

ScriptGraph ScriptGraph::fromJson(const nlohmann::json& j)
{
    ScriptGraph graph;
    graph.version = j.value("version", 1);
    graph.name = j.value("name", std::string{});
    graph.nextNodeId = j.value("nextNodeId", static_cast<uint32_t>(1));
    graph.nextConnectionId = j.value("nextConnectionId", static_cast<uint32_t>(1));

    // Nodes
    if (j.contains("nodes"))
    {
        for (const auto& nj : j["nodes"])
        {
            ScriptNodeDef node;
            node.id = nj.value("id", static_cast<uint32_t>(0));
            node.typeName = nj.value("type", std::string{});
            node.posX = nj.value("posX", 0.0f);
            node.posY = nj.value("posY", 0.0f);

            if (nj.contains("properties") && nj["properties"].is_object())
            {
                for (auto it = nj["properties"].begin();
                     it != nj["properties"].end(); ++it)
                {
                    node.properties[it.key()] = ScriptValue::fromJson(it.value());
                }
            }

            graph.nodes.push_back(std::move(node));
        }
    }

    // Connections
    if (j.contains("connections"))
    {
        for (const auto& cj : j["connections"])
        {
            ScriptConnection c;
            c.id = cj.value("id", static_cast<uint32_t>(0));
            c.sourceNode = cj.value("srcNode", static_cast<uint32_t>(0));
            c.sourcePin = cj.value("srcPin", std::string{});
            c.targetNode = cj.value("tgtNode", static_cast<uint32_t>(0));
            c.targetPin = cj.value("tgtPin", std::string{});
            graph.connections.push_back(c);
        }
    }

    // Variables
    if (j.contains("variables"))
    {
        for (const auto& vj : j["variables"])
        {
            graph.variables.push_back(VariableDef::fromJson(vj));
        }
    }

    return graph;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

ScriptGraph ScriptGraph::loadFromFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        Logger::error("[ScriptGraph] Failed to open: " + filePath);
        return {};
    }

    try
    {
        nlohmann::json j = nlohmann::json::parse(file);
        auto graph = fromJson(j);
        Logger::info("[ScriptGraph] Loaded: " + filePath + " (" +
                     std::to_string(graph.nodes.size()) + " nodes, " +
                     std::to_string(graph.connections.size()) + " connections)");
        return graph;
    }
    catch (const nlohmann::json::exception& e)
    {
        Logger::error("[ScriptGraph] Parse error in " + filePath + ": " + e.what());
        return {};
    }
}

bool ScriptGraph::saveToFile(const std::string& filePath) const
{
    std::ofstream file(filePath);
    if (!file.is_open())
    {
        Logger::error("[ScriptGraph] Failed to write: " + filePath);
        return false;
    }

    try
    {
        file << toJson().dump(4);
        Logger::info("[ScriptGraph] Saved: " + filePath);
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::error("[ScriptGraph] Write error for " + filePath + ": " + e.what());
        return false;
    }
}

} // namespace Vestige
