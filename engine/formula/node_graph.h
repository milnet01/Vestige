// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file node_graph.h
/// @brief Node graph data structures for the visual formula editor (Phase 9E).
///
/// A NodeGraph represents a directed acyclic graph of math operations.
/// Each Node has typed input/output Ports connected via Connections.
/// The graph can be converted to/from ExpressionTree for library storage.
#pragma once

#include "formula/expression.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Unique identifier for nodes, ports, and connections.
using NodeId = uint32_t;
using PortId = uint32_t;
using ConnectionId = uint32_t;

/// @brief Data type flowing through ports.
enum class PortDataType
{
    FLOAT,
    VEC2,
    VEC3,
    VEC4
};

/// @brief Direction of a port (input receives, output sends).
enum class PortDirection
{
    INPUT,
    OUTPUT
};

/// @brief Categories of nodes available in the editor.
enum class NodeCategory
{
    MATH_BASIC,     ///< +, -, *, /, negate
    MATH_ADVANCED,  ///< pow, sqrt, abs, min, max
    TRIGONOMETRY,   ///< sin, cos, tan, asin, acos, atan
    EXPONENTIAL,    ///< exp, log, log2
    CLAMPING,       ///< clamp, saturate, floor, ceil
    INTERPOLATION,  ///< lerp, smoothstep
    INPUT,          ///< Variable inputs and constants
    OUTPUT,         ///< Formula output
    TEMPLATE        ///< Loaded from PhysicsTemplates
};

/// @brief A typed connection point on a node.
struct Port
{
    PortId id = 0;
    std::string name;              ///< Display name (e.g. "A", "Result", "Value")
    PortDirection direction;
    PortDataType dataType = PortDataType::FLOAT;
    float defaultValue = 0.0f;    ///< Used when port is unconnected (inputs only)
    bool connected = false;        ///< True if at least one connection exists
};

/// @brief A single node in the visual graph.
struct Node
{
    NodeId id = 0;
    std::string name;              ///< Display name (e.g. "Add", "Sin", "x")
    std::string operation;         ///< The operation this node performs (e.g. "+", "sin", "literal")
    NodeCategory category = NodeCategory::MATH_BASIC;

    std::vector<Port> inputs;
    std::vector<Port> outputs;

    // Visual layout (for editor persistence)
    float posX = 0.0f;
    float posY = 0.0f;

    // For literal/variable nodes
    float literalValue = 0.0f;     ///< For constant nodes
    std::string variableName;      ///< For variable input nodes

    // -- Helpers --
    Port* findInput(const std::string& portName);
    Port* findOutput(const std::string& portName);
    const Port* findInput(const std::string& portName) const;
    const Port* findOutput(const std::string& portName) const;
};

/// @brief A directed connection between two ports.
struct Connection
{
    ConnectionId id = 0;
    NodeId sourceNode = 0;
    PortId sourcePort = 0;
    NodeId targetNode = 0;
    PortId targetPort = 0;
};

/// @brief A directed acyclic graph of formula nodes.
///
/// The NodeGraph owns all nodes and connections. It provides:
/// - CRUD operations for nodes and connections
/// - Validation (cycle detection, type checking)
/// - Conversion to/from ExpressionTree
/// - JSON serialization for persistence
class NodeGraph
{
public:
    NodeGraph() = default;

    // -- Node management --
    NodeId addNode(Node node);
    bool removeNode(NodeId id);
    Node* getNode(NodeId id);
    const Node* getNode(NodeId id) const;
    const std::map<NodeId, Node>& getNodes() const { return m_nodes; }

    // -- Connection management --
    /// @brief Connect source output port to target input port.
    /// @return Connection ID, or 0 if invalid (cycle, type mismatch, already connected).
    ConnectionId connect(NodeId sourceNode, PortId sourcePort,
                         NodeId targetNode, PortId targetPort);
    bool disconnect(ConnectionId id);
    const std::vector<Connection>& getConnections() const { return m_connections; }

    /// @brief Find all connections to/from a specific node.
    std::vector<const Connection*> getNodeConnections(NodeId nodeId) const;

    // -- Validation --
    /// @brief Check if adding this connection would create a cycle.
    bool wouldCreateCycle(NodeId sourceNode, NodeId targetNode) const;

    /// @brief Validate the entire graph (no cycles, all inputs connected or have defaults).
    /// @param errorOut Receives error description on failure.
    bool validate(std::string& errorOut) const;

    // -- Conversion --
    /// @brief Convert this node graph to an expression tree.
    /// @param outputNodeId The node whose output becomes the tree root.
    /// @return The expression tree, or nullptr if graph is invalid.
    std::unique_ptr<ExprNode> toExpressionTree(NodeId outputNodeId) const;

    /// @brief Build a node graph from an existing expression tree.
    /// Positions nodes in a left-to-right layout.
    static NodeGraph fromExpressionTree(const ExprNode& root);

    // -- Factory helpers for common node types --
    static Node createMathNode(const std::string& operation);    // +, -, *, /, pow, min, max
    static Node createFunctionNode(const std::string& function); // sin, cos, sqrt, exp, etc.
    static Node createLiteralNode(float value);
    static Node createVariableNode(const std::string& name);
    static Node createOutputNode();
    /// @brief Conditional (ternary) branching node: 3 inputs (Condition, Then, Else), 1 output.
    /// Round-trips to/from ExprNode::conditional so formulas using if/then/else survive
    /// the expression-tree ↔ node-graph conversion without lossy fallback.
    static Node createConditionalNode();

    // -- JSON serialization --
    nlohmann::json toJson() const;
    static NodeGraph fromJson(const nlohmann::json& j);

    // -- Utility --
    void clear();
    size_t nodeCount() const { return m_nodes.size(); }
    size_t connectionCount() const { return m_connections.size(); }

private:
    NodeId m_nextNodeId = 1;
    PortId m_nextPortId = 1;
    ConnectionId m_nextConnectionId = 1;

    std::map<NodeId, Node> m_nodes;
    std::vector<Connection> m_connections;

    // -- Internal helpers --
    PortId assignPortIds(Node& node);
    bool hasCycleDFS(NodeId current, NodeId target,
                     std::vector<bool>& visited) const;
    std::unique_ptr<ExprNode> nodeToExpr(NodeId nodeId,
                                          std::vector<bool>& visited,
                                          int depth = 0) const;
};

// -- Enum string conversions --
const char* portDataTypeToString(PortDataType type);
PortDataType portDataTypeFromString(const std::string& str);
const char* nodeCategoryToString(NodeCategory cat);
NodeCategory nodeCategoryFromString(const std::string& str);

} // namespace Vestige
