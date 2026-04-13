/// @file script_graph.h
/// @brief Script graph asset — the serializable definition of a visual script.
#pragma once

#include "scripting/blackboard.h"
#include "scripting/script_common.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief A complete visual script graph, serializable as a .vscript JSON asset.
///
/// Contains nodes, connections between them, and variable definitions. This is
/// the "template" from which ScriptInstance objects are created at runtime.
struct ScriptGraph
{
    static constexpr int CURRENT_VERSION = 1;

    int version = CURRENT_VERSION;
    std::string name;

    std::vector<ScriptNodeDef> nodes;
    std::vector<ScriptConnection> connections;
    std::vector<VariableDef> variables;

    // -- ID generation (for editor use) --
    uint32_t nextNodeId = 1;
    uint32_t nextConnectionId = 1;

    // -- Node management --

    /// @brief Add a node and assign it an ID.
    /// @return The assigned node ID.
    uint32_t addNode(const std::string& typeName, float posX = 0.0f,
                     float posY = 0.0f);

    /// @brief Remove a node and all its connections.
    bool removeNode(uint32_t nodeId);

    /// @brief Find a node by ID.
    ScriptNodeDef* findNode(uint32_t nodeId);
    const ScriptNodeDef* findNode(uint32_t nodeId) const;

    // -- Connection management --

    /// @brief Add a connection between two pins.
    /// @return Connection ID, or 0 if invalid.
    uint32_t addConnection(uint32_t srcNode, const std::string& srcPin,
                           uint32_t tgtNode, const std::string& tgtPin);

    /// @brief Remove a connection by ID.
    bool removeConnection(uint32_t connectionId);

    /// @brief Find all connections involving a specific node.
    std::vector<const ScriptConnection*> getNodeConnections(uint32_t nodeId) const;

    // -- Validation --

    /// @brief Validate graph structure (no missing node references, etc.).
    /// @param errorOut Receives error description on failure.
    bool validate(std::string& errorOut) const;

    // -- Serialization --

    nlohmann::json toJson() const;
    static ScriptGraph fromJson(const nlohmann::json& j);

    /// @brief Load a ScriptGraph from a .vscript file.
    static ScriptGraph loadFromFile(const std::string& filePath);

    /// @brief Save this ScriptGraph to a .vscript file.
    bool saveToFile(const std::string& filePath) const;
};

} // namespace Vestige
