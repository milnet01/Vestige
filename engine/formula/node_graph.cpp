// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file node_graph.cpp
/// @brief Node graph implementation for the visual formula editor (Phase 9E).
#include "formula/node_graph.h"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Enum string conversions
// ---------------------------------------------------------------------------

const char* portDataTypeToString(PortDataType type)
{
    switch (type)
    {
    case PortDataType::FLOAT: return "float";
    case PortDataType::VEC2:  return "vec2";
    case PortDataType::VEC3:  return "vec3";
    case PortDataType::VEC4:  return "vec4";
    }
    return "float";
}

PortDataType portDataTypeFromString(const std::string& str)
{
    if (str == "vec2") return PortDataType::VEC2;
    if (str == "vec3") return PortDataType::VEC3;
    if (str == "vec4") return PortDataType::VEC4;
    return PortDataType::FLOAT;
}

const char* nodeCategoryToString(NodeCategory cat)
{
    switch (cat)
    {
    case NodeCategory::MATH_BASIC:    return "math_basic";
    case NodeCategory::MATH_ADVANCED: return "math_advanced";
    case NodeCategory::TRIGONOMETRY:  return "trigonometry";
    case NodeCategory::EXPONENTIAL:   return "exponential";
    case NodeCategory::CLAMPING:      return "clamping";
    case NodeCategory::INTERPOLATION: return "interpolation";
    case NodeCategory::INPUT:         return "input";
    case NodeCategory::OUTPUT:        return "output";
    case NodeCategory::TEMPLATE:      return "template";
    }
    return "math_basic";
}

NodeCategory nodeCategoryFromString(const std::string& str)
{
    if (str == "math_advanced") return NodeCategory::MATH_ADVANCED;
    if (str == "trigonometry")  return NodeCategory::TRIGONOMETRY;
    if (str == "exponential")   return NodeCategory::EXPONENTIAL;
    if (str == "clamping")      return NodeCategory::CLAMPING;
    if (str == "interpolation") return NodeCategory::INTERPOLATION;
    if (str == "input")         return NodeCategory::INPUT;
    if (str == "output")        return NodeCategory::OUTPUT;
    if (str == "template")      return NodeCategory::TEMPLATE;
    return NodeCategory::MATH_BASIC;
}

// ---------------------------------------------------------------------------
// Node helpers
// ---------------------------------------------------------------------------

Port* Node::findInput(const std::string& portName)
{
    for (auto& port : inputs)
    {
        if (port.name == portName)
        {
            return &port;
        }
    }
    return nullptr;
}

Port* Node::findOutput(const std::string& portName)
{
    for (auto& port : outputs)
    {
        if (port.name == portName)
        {
            return &port;
        }
    }
    return nullptr;
}

const Port* Node::findInput(const std::string& portName) const
{
    for (const auto& port : inputs)
    {
        if (port.name == portName)
        {
            return &port;
        }
    }
    return nullptr;
}

const Port* Node::findOutput(const std::string& portName) const
{
    for (const auto& port : outputs)
    {
        if (port.name == portName)
        {
            return &port;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// NodeGraph — Node management
// ---------------------------------------------------------------------------

PortId NodeGraph::assignPortIds(Node& node)
{
    for (auto& port : node.inputs)
    {
        port.id = m_nextPortId++;
    }
    for (auto& port : node.outputs)
    {
        port.id = m_nextPortId++;
    }
    return m_nextPortId;
}

NodeId NodeGraph::addNode(Node node)
{
    NodeId id = m_nextNodeId++;
    node.id = id;
    assignPortIds(node);
    m_nodes[id] = std::move(node);
    return id;
}

bool NodeGraph::removeNode(NodeId id)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end())
    {
        return false;
    }

    // Remove all connections involving this node
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
            [id](const Connection& c)
            {
                return c.sourceNode == id || c.targetNode == id;
            }),
        m_connections.end());

    // Update connected flags on remaining nodes
    // Build set of all connected port IDs
    std::unordered_set<PortId> connectedPorts;
    for (const auto& c : m_connections)
    {
        connectedPorts.insert(c.sourcePort);
        connectedPorts.insert(c.targetPort);
    }

    // Update all remaining nodes
    for (auto& [nodeId, n] : m_nodes)
    {
        if (nodeId == id)
        {
            continue;
        }
        for (auto& port : n.inputs)
        {
            port.connected = connectedPorts.count(port.id) > 0;
        }
        for (auto& port : n.outputs)
        {
            port.connected = connectedPorts.count(port.id) > 0;
        }
    }

    m_nodes.erase(it);
    return true;
}

Node* NodeGraph::getNode(NodeId id)
{
    auto it = m_nodes.find(id);
    return it != m_nodes.end() ? &it->second : nullptr;
}

const Node* NodeGraph::getNode(NodeId id) const
{
    auto it = m_nodes.find(id);
    return it != m_nodes.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// NodeGraph — Connection management
// ---------------------------------------------------------------------------

ConnectionId NodeGraph::connect(NodeId srcNodeId, PortId srcPortId,
                                NodeId tgtNodeId, PortId tgtPortId)
{
    // Cannot connect a node to itself
    if (srcNodeId == tgtNodeId)
    {
        return 0;
    }

    // Validate source node and port exist
    Node* srcNode = getNode(srcNodeId);
    if (!srcNode)
    {
        return 0;
    }

    Port* srcPort = nullptr;
    for (auto& port : srcNode->outputs)
    {
        if (port.id == srcPortId)
        {
            srcPort = &port;
            break;
        }
    }
    if (!srcPort)
    {
        return 0;
    }

    // Validate target node and port exist
    Node* tgtNode = getNode(tgtNodeId);
    if (!tgtNode)
    {
        return 0;
    }

    Port* tgtPort = nullptr;
    for (auto& port : tgtNode->inputs)
    {
        if (port.id == tgtPortId)
        {
            tgtPort = &port;
            break;
        }
    }
    if (!tgtPort)
    {
        return 0;
    }

    // Check type compatibility
    if (srcPort->dataType != tgtPort->dataType)
    {
        return 0;
    }

    // Check for duplicate connection
    for (const auto& c : m_connections)
    {
        if (c.sourceNode == srcNodeId && c.sourcePort == srcPortId &&
            c.targetNode == tgtNodeId && c.targetPort == tgtPortId)
        {
            return 0;
        }
    }

    // Check if target input already has a connection (inputs accept only one)
    for (const auto& c : m_connections)
    {
        if (c.targetNode == tgtNodeId && c.targetPort == tgtPortId)
        {
            return 0;
        }
    }

    // Check for cycles
    if (wouldCreateCycle(srcNodeId, tgtNodeId))
    {
        return 0;
    }

    // Create the connection
    ConnectionId connId = m_nextConnectionId++;
    Connection conn;
    conn.id = connId;
    conn.sourceNode = srcNodeId;
    conn.sourcePort = srcPortId;
    conn.targetNode = tgtNodeId;
    conn.targetPort = tgtPortId;
    m_connections.push_back(conn);

    // Mark ports as connected
    srcPort->connected = true;
    tgtPort->connected = true;

    return connId;
}

bool NodeGraph::disconnect(ConnectionId id)
{
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [id](const Connection& c) { return c.id == id; });

    if (it == m_connections.end())
    {
        return false;
    }

    NodeId srcNodeId = it->sourceNode;
    PortId srcPortId = it->sourcePort;
    NodeId tgtNodeId = it->targetNode;
    PortId tgtPortId = it->targetPort;

    m_connections.erase(it);

    // Update connected flags — check if any remaining connections use these ports
    bool srcStillConnected = false;
    bool tgtStillConnected = false;
    for (const auto& c : m_connections)
    {
        if (c.sourceNode == srcNodeId && c.sourcePort == srcPortId)
        {
            srcStillConnected = true;
        }
        if (c.targetNode == tgtNodeId && c.targetPort == tgtPortId)
        {
            tgtStillConnected = true;
        }
    }

    Node* srcNode = getNode(srcNodeId);
    if (srcNode)
    {
        for (auto& port : srcNode->outputs)
        {
            if (port.id == srcPortId)
            {
                port.connected = srcStillConnected;
                break;
            }
        }
    }

    Node* tgtNode = getNode(tgtNodeId);
    if (tgtNode)
    {
        for (auto& port : tgtNode->inputs)
        {
            if (port.id == tgtPortId)
            {
                port.connected = tgtStillConnected;
                break;
            }
        }
    }

    return true;
}

std::vector<const Connection*> NodeGraph::getNodeConnections(NodeId nodeId) const
{
    std::vector<const Connection*> result;
    for (const auto& c : m_connections)
    {
        if (c.sourceNode == nodeId || c.targetNode == nodeId)
        {
            result.push_back(&c);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// NodeGraph — Validation
// ---------------------------------------------------------------------------

bool NodeGraph::wouldCreateCycle(NodeId sourceNode, NodeId targetNode) const
{
    // If adding edge sourceNode -> targetNode, check if targetNode can already
    // reach sourceNode through existing connections. If yes, adding this edge
    // would create a cycle.
    if (sourceNode == targetNode)
    {
        return true;
    }

    // BFS from targetNode following outgoing connections to see if we reach sourceNode
    std::unordered_set<NodeId> visited;
    std::queue<NodeId> frontier;
    frontier.push(targetNode);
    visited.insert(targetNode);

    while (!frontier.empty())
    {
        NodeId current = frontier.front();
        frontier.pop();

        for (const auto& c : m_connections)
        {
            if (c.sourceNode == current)
            {
                if (c.targetNode == sourceNode)
                {
                    return true;
                }
                // insert() returns {iter, inserted}; use the bool directly
                // to avoid a redundant count() probe (cppcheck: stlFindInsert).
                if (visited.insert(c.targetNode).second)
                {
                    frontier.push(c.targetNode);
                }
            }
        }
    }

    return false;
}

bool NodeGraph::validate(std::string& errorOut) const
{
    if (m_nodes.empty())
    {
        return true;
    }

    // Check for cycles using topological sort (Kahn's algorithm)
    std::unordered_map<NodeId, int> inDegree;
    for (const auto& [id, node] : m_nodes)
    {
        inDegree[id] = 0;
    }
    for (const auto& c : m_connections)
    {
        inDegree[c.targetNode]++;
    }

    std::queue<NodeId> zeroQueue;
    for (const auto& [id, degree] : inDegree)
    {
        if (degree == 0)
        {
            zeroQueue.push(id);
        }
    }

    size_t processedCount = 0;
    while (!zeroQueue.empty())
    {
        NodeId current = zeroQueue.front();
        zeroQueue.pop();
        processedCount++;

        for (const auto& c : m_connections)
        {
            if (c.sourceNode == current)
            {
                inDegree[c.targetNode]--;
                if (inDegree[c.targetNode] == 0)
                {
                    zeroQueue.push(c.targetNode);
                }
            }
        }
    }

    if (processedCount != m_nodes.size())
    {
        errorOut = "Graph contains a cycle";
        return false;
    }

    // All input ports either have a connection or use their default value.
    // Since defaultValue is always initialized (0.0f), all unconnected inputs
    // are valid. This validation is a structural check only.

    return true;
}

// ---------------------------------------------------------------------------
// NodeGraph — Expression tree conversion
// ---------------------------------------------------------------------------

/// Phase 10.9 Sc2 — recursion-depth cap for graph→expression-tree
/// conversion. Matches `expression_eval.cpp::kMaxFormulaDepth` and
/// rejects pathological 100k-deep graphs that would blow the stack.
static constexpr int kMaxNodeToExprDepth = 256;

std::unique_ptr<ExprNode> NodeGraph::nodeToExpr(NodeId nodeId,
                                                 std::vector<bool>& visited,
                                                 int depth) const
{
    if (depth > kMaxNodeToExprDepth)
    {
        return nullptr;  // depth-cap reached — same nullptr semantics as
                         // cycle detection so toExpressionTree() bails out.
    }

    // Ensure visited is large enough first, so all subsequent accesses are
    // unconditionally in-bounds. The previous form
    // (`if (nodeId < visited.size() && visited[nodeId])`) relied on &&
    // short-circuit, which cppcheck's flow analysis doesn't model. The
    // restructured form below is structurally safer, but cppcheck still
    // can't prove resize() grew the container, so the first downstream
    // access carries an inline suppression (cppcheck gives up tracking
    // after that so subsequent accesses don't re-fire). Runtime-safe:
    // the resize guarantees `visited.size() > nodeId`.
    if (nodeId >= visited.size())
    {
        visited.resize(static_cast<size_t>(nodeId) + 1, false);
    }
    // Guard against cycles (should not happen in a validated graph).
    // cppcheck-suppress containerOutOfBounds  ; size invariant established by resize above
    if (visited[nodeId])
    {
        return nullptr;
    }
    visited[nodeId] = true;

    const Node* node = getNode(nodeId);
    if (!node)
    {
        return nullptr;
    }

    // Literal node: return constant
    if (node->operation == "literal")
    {
        return ExprNode::literal(node->literalValue);
    }

    // Variable node: return variable reference
    if (node->operation == "variable")
    {
        return ExprNode::variable(node->variableName);
    }

    // Output node: follow the single input connection
    if (node->operation == "output")
    {
        if (node->inputs.empty())
        {
            return ExprNode::literal(0.0f);
        }
        const Port& inputPort = node->inputs[0];
        // Find connection to this input
        for (const auto& c : m_connections)
        {
            if (c.targetNode == nodeId && c.targetPort == inputPort.id)
            {
                return nodeToExpr(c.sourceNode, visited, depth + 1);
            }
        }
        // Unconnected: use default
        return ExprNode::literal(inputPort.defaultValue);
    }

    // Helper lambda: resolve an input port to an expression
    auto resolveInput = [this, nodeId, &visited, depth](const Port& port)
        -> std::unique_ptr<ExprNode>
    {
        for (const auto& c : m_connections)
        {
            if (c.targetNode == nodeId && c.targetPort == port.id)
            {
                return nodeToExpr(c.sourceNode, visited, depth + 1);
            }
        }
        return ExprNode::literal(port.defaultValue);
    };

    // Conditional (ternary) node: 3 inputs (Condition, Then, Else), 1 output.
    // Dispatched by operation name before the generic input-count branches
    // because 3-input nodes would otherwise fall through to the literal(0)
    // fallback and silently drop logic on round-trip (former AUDIT §M10).
    if (node->operation == "conditional" && node->inputs.size() == 3)
    {
        auto cond = resolveInput(node->inputs[0]);
        auto thenExpr = resolveInput(node->inputs[1]);
        auto elseExpr = resolveInput(node->inputs[2]);
        // Phase 10.9 Slice 18 Ts2: propagate nullptr from any
        // recursive resolveInput up the call stack so the Sc2
        // depth-cap actually rejects deep chains. Without these
        // checks, the cap fires deep in the tree but the returned
        // nullptr was silently wrapped into a conditional / unaryOp /
        // binaryOp parent — hostile 100k-deep inputs still built a
        // full tree at the top level.
        if (!cond || !thenExpr || !elseExpr) return nullptr;
        return ExprNode::conditional(std::move(cond),
                                     std::move(thenExpr),
                                     std::move(elseExpr));
    }

    // Unary function nodes (1 input, 1 output)
    if (node->inputs.size() == 1)
    {
        auto arg = resolveInput(node->inputs[0]);
        if (!arg) return nullptr;  // Sc2 depth-cap propagation (see above)
        return ExprNode::unaryOp(node->operation, std::move(arg));
    }

    // Binary operation nodes (2 inputs, 1 output)
    if (node->inputs.size() == 2)
    {
        auto left = resolveInput(node->inputs[0]);
        auto right = resolveInput(node->inputs[1]);
        if (!left || !right) return nullptr;  // Sc2 depth-cap propagation
        return ExprNode::binaryOp(node->operation, std::move(left), std::move(right));
    }

    // Fallback: return literal 0
    return ExprNode::literal(0.0f);
}

std::unique_ptr<ExprNode> NodeGraph::toExpressionTree(NodeId outputNodeId) const
{
    std::string err;
    // Validate first (using a copy to avoid const issues)
    NodeGraph temp = *this;
    if (!temp.validate(err))
    {
        return nullptr;
    }

    std::vector<bool> visited;
    return nodeToExpr(outputNodeId, visited, 0);
}

/// @brief Internal helper to recursively build nodes from an expression tree.
struct FromExprHelper
{
    NodeGraph& graph;
    int depthCounter = 0;
    int yCounter = 0;

    NodeId buildNode(const ExprNode& expr, int depth)
    {
        // Phase 10.9 Sc2: cap depth so a hostile preset (100k-deep
        // unary chain) can't blow the stack on import. Returns 0 (the
        // "unreachable" fallback) which gets serialised as a no-op.
        if (depth > kMaxNodeToExprDepth)
        {
            return 0;
        }

        switch (expr.type)
        {
        case ExprNodeType::LITERAL:
        {
            Node n = NodeGraph::createLiteralNode(expr.value);
            n.posX = static_cast<float>(depth) * 200.0f;
            n.posY = static_cast<float>(yCounter++) * 100.0f;
            return graph.addNode(std::move(n));
        }

        case ExprNodeType::VARIABLE:
        {
            Node n = NodeGraph::createVariableNode(expr.name);
            n.posX = static_cast<float>(depth) * 200.0f;
            n.posY = static_cast<float>(yCounter++) * 100.0f;
            return graph.addNode(std::move(n));
        }

        case ExprNodeType::UNARY_OP:
        {
            // First build the child
            NodeId childId = buildNode(*expr.children[0], depth + 1);

            // Create function node
            Node n = NodeGraph::createFunctionNode(expr.op);
            n.posX = static_cast<float>(depth) * 200.0f;
            n.posY = static_cast<float>(yCounter++) * 100.0f;
            NodeId fnId = graph.addNode(std::move(n));

            // Connect child output to function input
            const Node* child = graph.getNode(childId);
            const Node* fn = graph.getNode(fnId);
            if (child && fn && !child->outputs.empty() && !fn->inputs.empty())
            {
                graph.connect(childId, child->outputs[0].id,
                              fnId, fn->inputs[0].id);
            }
            return fnId;
        }

        case ExprNodeType::BINARY_OP:
        {
            // Build both children
            NodeId leftId = buildNode(*expr.children[0], depth + 1);
            NodeId rightId = buildNode(*expr.children[1], depth + 1);

            // Create math node
            Node n = NodeGraph::createMathNode(expr.op);
            n.posX = static_cast<float>(depth) * 200.0f;
            n.posY = static_cast<float>(yCounter++) * 100.0f;
            NodeId mathId = graph.addNode(std::move(n));

            // Connect children
            const Node* left = graph.getNode(leftId);
            const Node* right = graph.getNode(rightId);
            const Node* math = graph.getNode(mathId);
            if (left && right && math &&
                !left->outputs.empty() && !right->outputs.empty() &&
                math->inputs.size() >= 2)
            {
                graph.connect(leftId, left->outputs[0].id,
                              mathId, math->inputs[0].id);
                graph.connect(rightId, right->outputs[0].id,
                              mathId, math->inputs[1].id);
            }
            return mathId;
        }

        case ExprNodeType::CONDITIONAL:
        {
            // Conditional (ternary) round-trip support.
            // Previously this path dropped to literal(0) — resolved by
            // adding a dedicated 3-input conditional node type whose
            // nodeToExpr() counterpart rebuilds ExprNode::conditional().
            NodeId condId = buildNode(*expr.children[0], depth + 1);
            NodeId thenId = buildNode(*expr.children[1], depth + 1);
            NodeId elseId = buildNode(*expr.children[2], depth + 1);

            Node condNode = NodeGraph::createConditionalNode();
            condNode.posX = static_cast<float>(depth) * 200.0f;
            condNode.posY = static_cast<float>(yCounter++) * 100.0f;
            NodeId condNodeId = graph.addNode(std::move(condNode));

            const Node* condPtr = graph.getNode(condNodeId);
            const Node* condSrc = graph.getNode(condId);
            const Node* thenSrc = graph.getNode(thenId);
            const Node* elseSrc = graph.getNode(elseId);
            if (condPtr && condSrc && thenSrc && elseSrc)
            {
                graph.connect(condId, condSrc->outputs[0].id,
                              condNodeId, condPtr->inputs[0].id);
                graph.connect(thenId, thenSrc->outputs[0].id,
                              condNodeId, condPtr->inputs[1].id);
                graph.connect(elseId, elseSrc->outputs[0].id,
                              condNodeId, condPtr->inputs[2].id);
            }
            return condNodeId;
        }
        }

        // Unreachable, but satisfy compiler
        return 0;
    }
};

NodeGraph NodeGraph::fromExpressionTree(const ExprNode& root)
{
    NodeGraph graph;
    FromExprHelper helper{graph, 0, 0};

    // Build all nodes from the expression tree
    NodeId rootNodeId = helper.buildNode(root, 1);

    // Create an output node at depth 0
    Node outputNode = createOutputNode();
    outputNode.posX = 0.0f;
    outputNode.posY = 0.0f;
    NodeId outputId = graph.addNode(std::move(outputNode));

    // Connect root to output
    const Node* rootNode = graph.getNode(rootNodeId);
    const Node* outNode = graph.getNode(outputId);
    if (rootNode && outNode &&
        !rootNode->outputs.empty() && !outNode->inputs.empty())
    {
        graph.connect(rootNodeId, rootNode->outputs[0].id,
                      outputId, outNode->inputs[0].id);
    }

    return graph;
}

// ---------------------------------------------------------------------------
// NodeGraph — Factory helpers
// ---------------------------------------------------------------------------

Node NodeGraph::createMathNode(const std::string& operation)
{
    Node node;
    node.operation = operation;
    node.category = NodeCategory::MATH_BASIC;

    // Determine display name
    if (operation == "+")       node.name = "Add";
    else if (operation == "-")  node.name = "Subtract";
    else if (operation == "*")  node.name = "Multiply";
    else if (operation == "/")  node.name = "Divide";
    else if (operation == "pow") { node.name = "Power"; node.category = NodeCategory::MATH_ADVANCED; }
    else if (operation == "min") { node.name = "Min"; node.category = NodeCategory::MATH_ADVANCED; }
    else if (operation == "max") { node.name = "Max"; node.category = NodeCategory::MATH_ADVANCED; }
    else if (operation == "dot") { node.name = "Dot"; node.category = NodeCategory::MATH_ADVANCED; }
    else                         node.name = operation;

    // Two inputs, one output
    Port inputA;
    inputA.name = "A";
    inputA.direction = PortDirection::INPUT;
    inputA.dataType = PortDataType::FLOAT;
    inputA.defaultValue = 0.0f;

    Port inputB;
    inputB.name = "B";
    inputB.direction = PortDirection::INPUT;
    inputB.dataType = PortDataType::FLOAT;
    inputB.defaultValue = 0.0f;

    Port output;
    output.name = "Result";
    output.direction = PortDirection::OUTPUT;
    output.dataType = PortDataType::FLOAT;

    node.inputs.push_back(inputA);
    node.inputs.push_back(inputB);
    node.outputs.push_back(output);

    return node;
}

Node NodeGraph::createFunctionNode(const std::string& function)
{
    Node node;
    node.operation = function;
    node.name = function;

    // Categorize
    if (function == "sin" || function == "cos" || function == "tan" ||
        function == "asin" || function == "acos" || function == "atan")
    {
        node.category = NodeCategory::TRIGONOMETRY;
    }
    else if (function == "exp" || function == "log" || function == "log2")
    {
        node.category = NodeCategory::EXPONENTIAL;
    }
    else if (function == "floor" || function == "ceil" || function == "saturate")
    {
        node.category = NodeCategory::CLAMPING;
    }
    else
    {
        // Covers abs/sqrt/negate and any future unknown function — all
        // classified as MATH_ADVANCED. (Previously had a redundant elif
        // for abs/sqrt/negate that set the same category — AUDIT L29.)
        node.category = NodeCategory::MATH_ADVANCED;
    }

    // Capitalize display name
    if (!function.empty())
    {
        node.name = function;
        node.name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(node.name[0])));
    }

    // One input, one output
    Port input;
    input.name = "Value";
    input.direction = PortDirection::INPUT;
    input.dataType = PortDataType::FLOAT;
    input.defaultValue = 0.0f;

    Port output;
    output.name = "Result";
    output.direction = PortDirection::OUTPUT;
    output.dataType = PortDataType::FLOAT;

    node.inputs.push_back(input);
    node.outputs.push_back(output);

    return node;
}

Node NodeGraph::createLiteralNode(float value)
{
    Node node;
    node.name = "Constant";
    node.operation = "literal";
    node.category = NodeCategory::INPUT;
    node.literalValue = value;

    // Zero inputs, one output
    Port output;
    output.name = "Value";
    output.direction = PortDirection::OUTPUT;
    output.dataType = PortDataType::FLOAT;

    node.outputs.push_back(output);

    return node;
}

Node NodeGraph::createVariableNode(const std::string& name)
{
    Node node;
    node.name = name;
    node.operation = "variable";
    node.category = NodeCategory::INPUT;
    node.variableName = name;

    // Zero inputs, one output
    Port output;
    output.name = "Value";
    output.direction = PortDirection::OUTPUT;
    output.dataType = PortDataType::FLOAT;

    node.outputs.push_back(output);

    return node;
}

Node NodeGraph::createOutputNode()
{
    Node node;
    node.name = "Output";
    node.operation = "output";
    node.category = NodeCategory::OUTPUT;

    // One input, zero outputs
    Port input;
    input.name = "Value";
    input.direction = PortDirection::INPUT;
    input.dataType = PortDataType::FLOAT;
    input.defaultValue = 0.0f;

    node.inputs.push_back(input);

    return node;
}

Node NodeGraph::createConditionalNode()
{
    Node node;
    node.name = "If";
    node.operation = "conditional";
    node.category = NodeCategory::INTERPOLATION;

    // Three inputs: condition (non-zero selects Then), then value, else value.
    Port condition;
    condition.name = "Condition";
    condition.direction = PortDirection::INPUT;
    condition.dataType = PortDataType::FLOAT;
    condition.defaultValue = 0.0f;

    Port thenPort;
    thenPort.name = "Then";
    thenPort.direction = PortDirection::INPUT;
    thenPort.dataType = PortDataType::FLOAT;
    thenPort.defaultValue = 0.0f;

    Port elsePort;
    elsePort.name = "Else";
    elsePort.direction = PortDirection::INPUT;
    elsePort.dataType = PortDataType::FLOAT;
    elsePort.defaultValue = 0.0f;

    Port output;
    output.name = "Result";
    output.direction = PortDirection::OUTPUT;
    output.dataType = PortDataType::FLOAT;

    node.inputs.push_back(condition);
    node.inputs.push_back(thenPort);
    node.inputs.push_back(elsePort);
    node.outputs.push_back(output);

    return node;
}

// ---------------------------------------------------------------------------
// NodeGraph — JSON serialization
// ---------------------------------------------------------------------------

nlohmann::json NodeGraph::toJson() const
{
    nlohmann::json j;
    j["version"] = 1;

    // Serialize nodes
    nlohmann::json nodesArray = nlohmann::json::array();
    for (const auto& [id, node] : m_nodes)
    {
        nlohmann::json nj;
        nj["id"] = node.id;
        nj["name"] = node.name;
        nj["operation"] = node.operation;
        nj["category"] = nodeCategoryToString(node.category);
        nj["posX"] = node.posX;
        nj["posY"] = node.posY;
        nj["literalValue"] = node.literalValue;
        nj["variableName"] = node.variableName;

        // Inputs
        nlohmann::json inputsArray = nlohmann::json::array();
        for (const auto& port : node.inputs)
        {
            nlohmann::json pj;
            pj["id"] = port.id;
            pj["name"] = port.name;
            pj["direction"] = "input";
            pj["dataType"] = portDataTypeToString(port.dataType);
            pj["defaultValue"] = port.defaultValue;
            pj["connected"] = port.connected;
            inputsArray.push_back(pj);
        }
        nj["inputs"] = inputsArray;

        // Outputs
        nlohmann::json outputsArray = nlohmann::json::array();
        for (const auto& port : node.outputs)
        {
            nlohmann::json pj;
            pj["id"] = port.id;
            pj["name"] = port.name;
            pj["direction"] = "output";
            pj["dataType"] = portDataTypeToString(port.dataType);
            pj["defaultValue"] = port.defaultValue;
            pj["connected"] = port.connected;
            outputsArray.push_back(pj);
        }
        nj["outputs"] = outputsArray;

        nodesArray.push_back(nj);
    }
    j["nodes"] = nodesArray;

    // Serialize connections
    nlohmann::json connsArray = nlohmann::json::array();
    for (const auto& c : m_connections)
    {
        nlohmann::json cj;
        cj["id"] = c.id;
        cj["sourceNode"] = c.sourceNode;
        cj["sourcePort"] = c.sourcePort;
        cj["targetNode"] = c.targetNode;
        cj["targetPort"] = c.targetPort;
        connsArray.push_back(cj);
    }
    j["connections"] = connsArray;

    // Save ID counters for faithful restoration
    j["nextNodeId"] = m_nextNodeId;
    j["nextPortId"] = m_nextPortId;
    j["nextConnectionId"] = m_nextConnectionId;

    return j;
}

NodeGraph NodeGraph::fromJson(const nlohmann::json& j)
{
    NodeGraph graph;

    // Version check
    int version = j.value("version", 1);
    if (version > 1)
    {
        throw std::runtime_error("NodeGraph::fromJson: unsupported version "
                                 + std::to_string(version));
    }

    // Restore ID counters
    graph.m_nextNodeId = j.value("nextNodeId", static_cast<NodeId>(1));
    graph.m_nextPortId = j.value("nextPortId", static_cast<PortId>(1));
    graph.m_nextConnectionId = j.value("nextConnectionId", static_cast<ConnectionId>(1));

    // Deserialize nodes.
    //
    // AUDIT.md §M9 / FIXPLAN: previously `m_nodes[node.id] = std::move(node)`
    // silently overwrote duplicate IDs and any serialised m_nextNodeId
    // smaller than a present id could cause future addNode() to collide
    // with existing nodes. Now: duplicate IDs throw; m_nextNodeId is
    // recomputed as max(id)+1 after the load to guarantee forward
    // uniqueness regardless of the serialised counter value.
    if (j.contains("nodes"))
    {
        NodeId maxId = 0;
        for (const auto& nj : j["nodes"])
        {
            Node node;
            node.id = nj.value("id", static_cast<NodeId>(0));
            node.name = nj.value("name", std::string{});
            node.operation = nj.value("operation", std::string{});
            node.category = nodeCategoryFromString(nj.value("category", std::string{"math_basic"}));
            node.posX = nj.value("posX", 0.0f);
            node.posY = nj.value("posY", 0.0f);
            node.literalValue = nj.value("literalValue", 0.0f);
            node.variableName = nj.value("variableName", std::string{});

            // Deserialize inputs
            if (nj.contains("inputs"))
            {
                for (const auto& pj : nj["inputs"])
                {
                    Port port;
                    port.id = pj.value("id", static_cast<PortId>(0));
                    port.name = pj.value("name", std::string{});
                    port.direction = PortDirection::INPUT;
                    port.dataType = portDataTypeFromString(pj.value("dataType", std::string{"float"}));
                    port.defaultValue = pj.value("defaultValue", 0.0f);
                    port.connected = pj.value("connected", false);
                    node.inputs.push_back(port);
                }
            }

            // Deserialize outputs
            if (nj.contains("outputs"))
            {
                for (const auto& pj : nj["outputs"])
                {
                    Port port;
                    port.id = pj.value("id", static_cast<PortId>(0));
                    port.name = pj.value("name", std::string{});
                    port.direction = PortDirection::OUTPUT;
                    port.dataType = portDataTypeFromString(pj.value("dataType", std::string{"float"}));
                    port.defaultValue = pj.value("defaultValue", 0.0f);
                    port.connected = pj.value("connected", false);
                    node.outputs.push_back(port);
                }
            }

            if (graph.m_nodes.find(node.id) != graph.m_nodes.end())
            {
                throw std::runtime_error(
                    "NodeGraph::fromJson: duplicate node id " +
                    std::to_string(node.id) + " (AUDIT.md §M9)");
            }
            if (node.id > maxId) maxId = node.id;
            graph.m_nodes[node.id] = std::move(node);
        }
        // Defensive: ensure any future addNode() picks an id strictly
        // greater than every present id, regardless of what was saved.
        if (maxId + 1 > graph.m_nextNodeId)
        {
            graph.m_nextNodeId = maxId + 1;
        }
    }

    // Deserialize connections
    if (j.contains("connections"))
    {
        for (const auto& cj : j["connections"])
        {
            Connection c;
            c.id = cj.value("id", static_cast<ConnectionId>(0));
            c.sourceNode = cj.value("sourceNode", static_cast<NodeId>(0));
            c.sourcePort = cj.value("sourcePort", static_cast<PortId>(0));
            c.targetNode = cj.value("targetNode", static_cast<NodeId>(0));
            c.targetPort = cj.value("targetPort", static_cast<PortId>(0));
            graph.m_connections.push_back(c);
        }
    }

    return graph;
}

// ---------------------------------------------------------------------------
// NodeGraph — Utility
// ---------------------------------------------------------------------------

void NodeGraph::clear()
{
    m_nodes.clear();
    m_connections.clear();
    m_nextNodeId = 1;
    m_nextPortId = 1;
    m_nextConnectionId = 1;
}

// ---------------------------------------------------------------------------
// NodeGraph — Internal helpers (unused DFS variant kept for reference)
// ---------------------------------------------------------------------------

bool NodeGraph::hasCycleDFS(NodeId current, NodeId target,
                            std::vector<bool>& visited) const
{
    if (current == target)
    {
        return true;
    }
    if (current >= visited.size())
    {
        visited.resize(static_cast<size_t>(current) + 1, false);
    }
    if (visited[current])
    {
        return false;
    }
    visited[current] = true;

    for (const auto& c : m_connections)
    {
        if (c.sourceNode == current)
        {
            if (hasCycleDFS(c.targetNode, target, visited))
            {
                return true;
            }
        }
    }

    return false;
}

} // namespace Vestige
