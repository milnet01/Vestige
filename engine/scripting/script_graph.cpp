/// @file script_graph.cpp
/// @brief ScriptGraph implementation — node/connection management and JSON I/O.
#include "scripting/script_graph.h"
#include "core/logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Vestige
{

namespace
{

/// @brief Truncate a string read from JSON to prevent memory abuse via crafted
/// .vscript files. Returns a copy clamped to MAX_STRING_BYTES bytes.
std::string clampString(std::string s, size_t maxBytes)
{
    if (s.size() > maxBytes)
    {
        Logger::warning("[ScriptGraph] String exceeded " +
                        std::to_string(maxBytes) +
                        " bytes — truncated");
        s.resize(maxBytes);
    }
    return s;
}

/// @brief Reject file paths that attempt traversal outside the caller's
/// expected scope. Previously this only rejected `..` components, giving
/// false confidence — absolute paths like `/etc/passwd` passed through.
///
/// AUDIT.md §M6 / FIXPLAN: tighten to also reject absolute paths and
/// explicit root-references (`~`). Callers that need to load from
/// arbitrary paths can opt in by resolving + prefix-checking against a
/// known asset root using std::filesystem::canonical upstream.
bool isPathTraversalSafe(const std::string& filePath)
{
    if (filePath.empty())
    {
        return false;
    }

    std::filesystem::path p(filePath);

    // Absolute paths escape the implicit working-directory scope.
    if (p.is_absolute())
    {
        return false;
    }

    // Leading `~` is shell tilde-expansion — not resolved by std::filesystem
    // but user-visible home reference. Reject to avoid surprise.
    if (!filePath.empty() && filePath.front() == '~')
    {
        return false;
    }

    for (const auto& part : p)
    {
        if (part == "..")
        {
            return false;
        }
    }
    return true;
}

} // namespace

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

    // AUDIT.md §M5 / FIXPLAN: clamp pin names on the editor path too.
    // The on-disk load routes through clampString in fromJson (see below);
    // the editor previously did not, leaving unbounded pin-name growth
    // possible via repeated addConnection calls from a scripted editor
    // action.
    const std::string clampedSrcPin = clampString(srcPin, MAX_STRING_BYTES);
    const std::string clampedTgtPin = clampString(tgtPin, MAX_STRING_BYTES);

    // Duplicate check
    for (const auto& c : connections)
    {
        if (c.sourceNode == srcNode && c.sourcePin == clampedSrcPin &&
            c.targetNode == tgtNode && c.targetPin == clampedTgtPin)
        {
            return 0;
        }
    }

    // Check that target input does not already have a connection
    // (each input pin accepts only one connection)
    for (const auto& c : connections)
    {
        if (c.targetNode == tgtNode && c.targetPin == clampedTgtPin)
        {
            return 0;
        }
    }

    ScriptConnection conn;
    conn.id = nextConnectionId++;
    conn.sourceNode = srcNode;
    conn.sourcePin = clampedSrcPin;
    conn.targetNode = tgtNode;
    conn.targetPin = clampedTgtPin;
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
    graph.name = clampString(j.value("name", std::string{}), MAX_STRING_BYTES);
    graph.nextNodeId = j.value("nextNodeId", static_cast<uint32_t>(1));
    graph.nextConnectionId = j.value("nextConnectionId", static_cast<uint32_t>(1));

    // Nodes — cap count to reject crafted graphs that would exhaust memory.
    if (j.contains("nodes") && j["nodes"].is_array())
    {
        const auto& nodesArr = j["nodes"];
        if (nodesArr.size() > MAX_NODES)
        {
            Logger::error("[ScriptGraph] Node count " +
                          std::to_string(nodesArr.size()) +
                          " exceeds MAX_NODES (" + std::to_string(MAX_NODES) +
                          ") — truncating");
        }
        const size_t nodeLimit = std::min<size_t>(nodesArr.size(), MAX_NODES);
        graph.nodes.reserve(nodeLimit);
        for (size_t i = 0; i < nodeLimit; ++i)
        {
            const auto& nj = nodesArr[i];
            ScriptNodeDef node;
            node.id = nj.value("id", static_cast<uint32_t>(0));
            node.typeName = clampString(nj.value("type", std::string{}),
                                        MAX_STRING_BYTES);
            node.posX = nj.value("posX", 0.0f);
            node.posY = nj.value("posY", 0.0f);

            if (nj.contains("properties") && nj["properties"].is_object())
            {
                for (auto it = nj["properties"].begin();
                     it != nj["properties"].end(); ++it)
                {
                    std::string key = clampString(it.key(), MAX_STRING_BYTES);
                    node.properties[key] = ScriptValue::fromJson(it.value());
                }
            }

            graph.nodes.push_back(std::move(node));
        }
    }

    // Connections — cap count.
    if (j.contains("connections") && j["connections"].is_array())
    {
        const auto& connsArr = j["connections"];
        if (connsArr.size() > MAX_CONNECTIONS)
        {
            Logger::error("[ScriptGraph] Connection count " +
                          std::to_string(connsArr.size()) +
                          " exceeds MAX_CONNECTIONS (" +
                          std::to_string(MAX_CONNECTIONS) + ") — truncating");
        }
        const size_t connLimit = std::min<size_t>(connsArr.size(),
                                                   MAX_CONNECTIONS);
        graph.connections.reserve(connLimit);
        for (size_t i = 0; i < connLimit; ++i)
        {
            const auto& cj = connsArr[i];
            ScriptConnection c;
            c.id = cj.value("id", static_cast<uint32_t>(0));
            c.sourceNode = cj.value("srcNode", static_cast<uint32_t>(0));
            c.sourcePin = clampString(cj.value("srcPin", std::string{}),
                                       MAX_STRING_BYTES);
            c.targetNode = cj.value("tgtNode", static_cast<uint32_t>(0));
            c.targetPin = clampString(cj.value("tgtPin", std::string{}),
                                       MAX_STRING_BYTES);
            graph.connections.push_back(c);
        }
    }

    // Variables — cap count.
    if (j.contains("variables") && j["variables"].is_array())
    {
        const auto& varsArr = j["variables"];
        if (varsArr.size() > MAX_VARIABLES)
        {
            Logger::error("[ScriptGraph] Variable count " +
                          std::to_string(varsArr.size()) +
                          " exceeds MAX_VARIABLES (" +
                          std::to_string(MAX_VARIABLES) + ") — truncating");
        }
        const size_t varLimit = std::min<size_t>(varsArr.size(), MAX_VARIABLES);
        graph.variables.reserve(varLimit);
        for (size_t i = 0; i < varLimit; ++i)
        {
            graph.variables.push_back(VariableDef::fromJson(varsArr[i]));
        }
    }

    return graph;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

ScriptGraph ScriptGraph::loadFromFile(const std::string& filePath)
{
    // Reject paths containing `..` components before touching the filesystem.
    if (!isPathTraversalSafe(filePath))
    {
        Logger::error("[ScriptGraph] Rejected path (traversal): " + filePath);
        return {};
    }

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

        // Post-load validation: reject graphs with dangling connections or
        // duplicate node IDs rather than failing opaquely during execution.
        std::string validateErr;
        if (!graph.validate(validateErr))
        {
            Logger::error("[ScriptGraph] Validation failed for " + filePath +
                          ": " + validateErr);
            return {};
        }

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
