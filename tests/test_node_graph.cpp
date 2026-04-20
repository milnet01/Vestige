// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_node_graph.cpp
/// @brief Unit tests for NodeGraph data structures (Phase 9E node editor).
#include "formula/node_graph.h"
#include "formula/expression.h"

#include <gtest/gtest.h>

using namespace Vestige;

// ===========================================================================
// Factory helpers — node creation
// ===========================================================================

TEST(NodeGraph_Factory, CreateMathNodeAdd)
{
    Node node = NodeGraph::createMathNode("+");
    EXPECT_EQ(node.name, "Add");
    EXPECT_EQ(node.operation, "+");
    EXPECT_EQ(node.category, NodeCategory::MATH_BASIC);
    ASSERT_EQ(node.inputs.size(), 2u);
    ASSERT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.inputs[0].name, "A");
    EXPECT_EQ(node.inputs[1].name, "B");
    EXPECT_EQ(node.outputs[0].name, "Result");
    EXPECT_EQ(node.inputs[0].direction, PortDirection::INPUT);
    EXPECT_EQ(node.outputs[0].direction, PortDirection::OUTPUT);
}

TEST(NodeGraph_Factory, CreateMathNodeSubtract)
{
    Node node = NodeGraph::createMathNode("-");
    EXPECT_EQ(node.name, "Subtract");
    EXPECT_EQ(node.operation, "-");
}

TEST(NodeGraph_Factory, CreateMathNodeMultiply)
{
    Node node = NodeGraph::createMathNode("*");
    EXPECT_EQ(node.name, "Multiply");
}

TEST(NodeGraph_Factory, CreateMathNodeDivide)
{
    Node node = NodeGraph::createMathNode("/");
    EXPECT_EQ(node.name, "Divide");
}

TEST(NodeGraph_Factory, CreateMathNodePow)
{
    Node node = NodeGraph::createMathNode("pow");
    EXPECT_EQ(node.name, "Power");
    EXPECT_EQ(node.category, NodeCategory::MATH_ADVANCED);
}

TEST(NodeGraph_Factory, CreateMathNodeMinMax)
{
    Node nodeMin = NodeGraph::createMathNode("min");
    EXPECT_EQ(nodeMin.name, "Min");
    EXPECT_EQ(nodeMin.category, NodeCategory::MATH_ADVANCED);

    Node nodeMax = NodeGraph::createMathNode("max");
    EXPECT_EQ(nodeMax.name, "Max");
}

TEST(NodeGraph_Factory, CreateFunctionNodeSin)
{
    Node node = NodeGraph::createFunctionNode("sin");
    EXPECT_EQ(node.name, "Sin");
    EXPECT_EQ(node.operation, "sin");
    EXPECT_EQ(node.category, NodeCategory::TRIGONOMETRY);
    ASSERT_EQ(node.inputs.size(), 1u);
    ASSERT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.inputs[0].name, "Value");
    EXPECT_EQ(node.outputs[0].name, "Result");
}

TEST(NodeGraph_Factory, CreateFunctionNodeCos)
{
    Node node = NodeGraph::createFunctionNode("cos");
    EXPECT_EQ(node.category, NodeCategory::TRIGONOMETRY);
}

TEST(NodeGraph_Factory, CreateFunctionNodeSqrt)
{
    Node node = NodeGraph::createFunctionNode("sqrt");
    EXPECT_EQ(node.category, NodeCategory::MATH_ADVANCED);
}

TEST(NodeGraph_Factory, CreateFunctionNodeExp)
{
    Node node = NodeGraph::createFunctionNode("exp");
    EXPECT_EQ(node.category, NodeCategory::EXPONENTIAL);
}

TEST(NodeGraph_Factory, CreateFunctionNodeFloor)
{
    Node node = NodeGraph::createFunctionNode("floor");
    EXPECT_EQ(node.category, NodeCategory::CLAMPING);
}

TEST(NodeGraph_Factory, CreateLiteralNode)
{
    Node node = NodeGraph::createLiteralNode(42.0f);
    EXPECT_EQ(node.name, "Constant");
    EXPECT_EQ(node.operation, "literal");
    EXPECT_EQ(node.category, NodeCategory::INPUT);
    EXPECT_FLOAT_EQ(node.literalValue, 42.0f);
    EXPECT_TRUE(node.inputs.empty());
    ASSERT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.outputs[0].name, "Value");
}

TEST(NodeGraph_Factory, CreateVariableNode)
{
    Node node = NodeGraph::createVariableNode("windSpeed");
    EXPECT_EQ(node.name, "windSpeed");
    EXPECT_EQ(node.operation, "variable");
    EXPECT_EQ(node.category, NodeCategory::INPUT);
    EXPECT_EQ(node.variableName, "windSpeed");
    EXPECT_TRUE(node.inputs.empty());
    ASSERT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.outputs[0].name, "Value");
}

TEST(NodeGraph_Factory, CreateOutputNode)
{
    Node node = NodeGraph::createOutputNode();
    EXPECT_EQ(node.name, "Output");
    EXPECT_EQ(node.operation, "output");
    EXPECT_EQ(node.category, NodeCategory::OUTPUT);
    ASSERT_EQ(node.inputs.size(), 1u);
    EXPECT_TRUE(node.outputs.empty());
    EXPECT_EQ(node.inputs[0].name, "Value");
}

TEST(NodeGraph_Factory, CreateConditionalNode)
{
    Node node = NodeGraph::createConditionalNode();
    EXPECT_EQ(node.name, "If");
    EXPECT_EQ(node.operation, "conditional");
    ASSERT_EQ(node.inputs.size(), 3u);
    ASSERT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.inputs[0].name, "Condition");
    EXPECT_EQ(node.inputs[1].name, "Then");
    EXPECT_EQ(node.inputs[2].name, "Else");
    EXPECT_EQ(node.outputs[0].name, "Result");
    EXPECT_EQ(node.inputs[0].direction, PortDirection::INPUT);
    EXPECT_EQ(node.outputs[0].direction, PortDirection::OUTPUT);
}

// ===========================================================================
// Port lookup helpers
// ===========================================================================

TEST(NodeGraph_Port, FindInputByName)
{
    Node node = NodeGraph::createMathNode("+");
    EXPECT_NE(node.findInput("A"), nullptr);
    EXPECT_NE(node.findInput("B"), nullptr);
    EXPECT_EQ(node.findInput("C"), nullptr);
}

TEST(NodeGraph_Port, FindOutputByName)
{
    Node node = NodeGraph::createMathNode("+");
    EXPECT_NE(node.findOutput("Result"), nullptr);
    EXPECT_EQ(node.findOutput("A"), nullptr);
}

TEST(NodeGraph_Port, ConstFindInput)
{
    const Node node = NodeGraph::createMathNode("+");
    const Port* port = node.findInput("A");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->name, "A");
}

TEST(NodeGraph_Port, ConstFindOutput)
{
    const Node node = NodeGraph::createFunctionNode("sin");
    const Port* port = node.findOutput("Result");
    ASSERT_NE(port, nullptr);
    EXPECT_EQ(port->name, "Result");
}

// ===========================================================================
// Node management
// ===========================================================================

TEST(NodeGraph_Nodes, AddNodeAssignsIds)
{
    NodeGraph graph;
    NodeId id1 = graph.addNode(NodeGraph::createMathNode("+"));
    NodeId id2 = graph.addNode(NodeGraph::createLiteralNode(1.0f));

    EXPECT_NE(id1, 0u);
    EXPECT_NE(id2, 0u);
    EXPECT_NE(id1, id2);
    EXPECT_EQ(graph.nodeCount(), 2u);
}

TEST(NodeGraph_Nodes, AddNodeAssignsPortIds)
{
    NodeGraph graph;
    NodeId id = graph.addNode(NodeGraph::createMathNode("+"));
    const Node* node = graph.getNode(id);
    ASSERT_NE(node, nullptr);

    // All ports should have unique nonzero IDs
    EXPECT_NE(node->inputs[0].id, 0u);
    EXPECT_NE(node->inputs[1].id, 0u);
    EXPECT_NE(node->outputs[0].id, 0u);
    EXPECT_NE(node->inputs[0].id, node->inputs[1].id);
    EXPECT_NE(node->inputs[0].id, node->outputs[0].id);
}

TEST(NodeGraph_Nodes, GetNodeReturnsNullForInvalid)
{
    NodeGraph graph;
    EXPECT_EQ(graph.getNode(999), nullptr);
}

TEST(NodeGraph_Nodes, GetNodeConst)
{
    NodeGraph graph;
    NodeId id = graph.addNode(NodeGraph::createLiteralNode(5.0f));

    const NodeGraph& cref = graph;
    const Node* node = cref.getNode(id);
    ASSERT_NE(node, nullptr);
    EXPECT_FLOAT_EQ(node->literalValue, 5.0f);
}

TEST(NodeGraph_Nodes, RemoveNodeSucceeds)
{
    NodeGraph graph;
    NodeId id = graph.addNode(NodeGraph::createMathNode("+"));
    EXPECT_EQ(graph.nodeCount(), 1u);

    EXPECT_TRUE(graph.removeNode(id));
    EXPECT_EQ(graph.nodeCount(), 0u);
    EXPECT_EQ(graph.getNode(id), nullptr);
}

TEST(NodeGraph_Nodes, RemoveNonexistentFails)
{
    NodeGraph graph;
    EXPECT_FALSE(graph.removeNode(999));
}

TEST(NodeGraph_Nodes, RemoveNodeCleansUpConnections)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit = graph.getNode(litId);
    const Node* add = graph.getNode(addId);
    ASSERT_NE(lit, nullptr);
    ASSERT_NE(add, nullptr);

    ConnectionId connId = graph.connect(litId, lit->outputs[0].id,
                                        addId, add->inputs[0].id);
    EXPECT_NE(connId, 0u);
    EXPECT_EQ(graph.connectionCount(), 1u);

    // Remove the literal node — connection should be cleaned up
    EXPECT_TRUE(graph.removeNode(litId));
    EXPECT_EQ(graph.connectionCount(), 0u);

    // The add node's input port should no longer be marked connected
    const Node* addAfter = graph.getNode(addId);
    ASSERT_NE(addAfter, nullptr);
    EXPECT_FALSE(addAfter->inputs[0].connected);
}

// ===========================================================================
// Connection management
// ===========================================================================

TEST(NodeGraph_Connection, ValidConnectionSucceeds)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(3.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit = graph.getNode(litId);
    const Node* add = graph.getNode(addId);

    ConnectionId connId = graph.connect(litId, lit->outputs[0].id,
                                        addId, add->inputs[0].id);
    EXPECT_NE(connId, 0u);
    EXPECT_EQ(graph.connectionCount(), 1u);

    // Ports should be marked connected
    const Node* litAfter = graph.getNode(litId);
    const Node* addAfter = graph.getNode(addId);
    EXPECT_TRUE(litAfter->outputs[0].connected);
    EXPECT_TRUE(addAfter->inputs[0].connected);
}

TEST(NodeGraph_Connection, SelfConnectionRejected)
{
    NodeGraph graph;
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));
    const Node* add = graph.getNode(addId);

    ConnectionId connId = graph.connect(addId, add->outputs[0].id,
                                        addId, add->inputs[0].id);
    EXPECT_EQ(connId, 0u);
}

TEST(NodeGraph_Connection, InvalidNodeRejected)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    const Node* lit = graph.getNode(litId);

    // Target node doesn't exist
    ConnectionId connId = graph.connect(litId, lit->outputs[0].id,
                                        999, 999);
    EXPECT_EQ(connId, 0u);
}

TEST(NodeGraph_Connection, InvalidPortRejected)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));
    const Node* lit = graph.getNode(litId);

    // Invalid port ID on target
    ConnectionId connId = graph.connect(litId, lit->outputs[0].id,
                                        addId, 999);
    EXPECT_EQ(connId, 0u);
}

TEST(NodeGraph_Connection, DuplicateConnectionRejected)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit = graph.getNode(litId);
    const Node* add = graph.getNode(addId);

    ConnectionId first = graph.connect(litId, lit->outputs[0].id,
                                       addId, add->inputs[0].id);
    EXPECT_NE(first, 0u);

    // Same connection again should fail
    ConnectionId second = graph.connect(litId, lit->outputs[0].id,
                                        addId, add->inputs[0].id);
    EXPECT_EQ(second, 0u);
    EXPECT_EQ(graph.connectionCount(), 1u);
}

TEST(NodeGraph_Connection, InputAlreadyConnectedRejected)
{
    NodeGraph graph;
    NodeId lit1Id = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId lit2Id = graph.addNode(NodeGraph::createLiteralNode(2.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit1 = graph.getNode(lit1Id);
    const Node* lit2 = graph.getNode(lit2Id);
    const Node* add = graph.getNode(addId);

    // Connect lit1 to input A
    ConnectionId first = graph.connect(lit1Id, lit1->outputs[0].id,
                                       addId, add->inputs[0].id);
    EXPECT_NE(first, 0u);

    // Try connecting lit2 to the same input A — should fail
    ConnectionId second = graph.connect(lit2Id, lit2->outputs[0].id,
                                        addId, add->inputs[0].id);
    EXPECT_EQ(second, 0u);
}

TEST(NodeGraph_Connection, TypeMismatchRejected)
{
    NodeGraph graph;

    // Create a literal node with FLOAT output
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));

    // Create a custom node with VEC3 input
    Node customNode;
    customNode.name = "VecInput";
    customNode.operation = "custom";
    Port vecInput;
    vecInput.name = "In";
    vecInput.direction = PortDirection::INPUT;
    vecInput.dataType = PortDataType::VEC3;
    customNode.inputs.push_back(vecInput);
    NodeId customId = graph.addNode(std::move(customNode));

    const Node* lit = graph.getNode(litId);
    const Node* custom = graph.getNode(customId);

    ConnectionId connId = graph.connect(litId, lit->outputs[0].id,
                                        customId, custom->inputs[0].id);
    EXPECT_EQ(connId, 0u);
}

TEST(NodeGraph_Connection, CycleDetectedAndRejected)
{
    // Build: A -> B -> C, then try C -> A (would create cycle)
    NodeGraph graph;
    NodeId aId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId bId = graph.addNode(NodeGraph::createFunctionNode("sin"));
    NodeId cId = graph.addNode(NodeGraph::createFunctionNode("cos"));

    const Node* a = graph.getNode(aId);
    const Node* b = graph.getNode(bId);
    const Node* c = graph.getNode(cId);

    // A -> B
    ConnectionId ab = graph.connect(aId, a->outputs[0].id,
                                    bId, b->inputs[0].id);
    EXPECT_NE(ab, 0u);

    // B -> C
    ConnectionId bc = graph.connect(bId, b->outputs[0].id,
                                    cId, c->inputs[0].id);
    EXPECT_NE(bc, 0u);

    // C -> A would create a cycle, but A has no inputs (literal node).
    // Use math nodes instead for a proper test.
    NodeGraph graph2;
    NodeId m1 = graph2.addNode(NodeGraph::createFunctionNode("sin"));
    NodeId m2 = graph2.addNode(NodeGraph::createFunctionNode("cos"));
    NodeId m3 = graph2.addNode(NodeGraph::createFunctionNode("sqrt"));

    const Node* n1 = graph2.getNode(m1);
    const Node* n2 = graph2.getNode(m2);
    const Node* n3 = graph2.getNode(m3);

    // m1 -> m2
    ConnectionId c12 = graph2.connect(m1, n1->outputs[0].id,
                                      m2, n2->inputs[0].id);
    EXPECT_NE(c12, 0u);

    // m2 -> m3
    ConnectionId c23 = graph2.connect(m2, n2->outputs[0].id,
                                      m3, n3->inputs[0].id);
    EXPECT_NE(c23, 0u);

    // m3 -> m1 would create a cycle
    ConnectionId c31 = graph2.connect(m3, n3->outputs[0].id,
                                      m1, n1->inputs[0].id);
    EXPECT_EQ(c31, 0u);
}

TEST(NodeGraph_Connection, DisconnectSucceeds)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit = graph.getNode(litId);
    const Node* add = graph.getNode(addId);

    ConnectionId connId = graph.connect(litId, lit->outputs[0].id,
                                        addId, add->inputs[0].id);
    EXPECT_NE(connId, 0u);
    EXPECT_EQ(graph.connectionCount(), 1u);

    EXPECT_TRUE(graph.disconnect(connId));
    EXPECT_EQ(graph.connectionCount(), 0u);

    // Ports should no longer be marked connected
    const Node* litAfter = graph.getNode(litId);
    const Node* addAfter = graph.getNode(addId);
    EXPECT_FALSE(litAfter->outputs[0].connected);
    EXPECT_FALSE(addAfter->inputs[0].connected);
}

TEST(NodeGraph_Connection, DisconnectInvalidFails)
{
    NodeGraph graph;
    EXPECT_FALSE(graph.disconnect(999));
}

TEST(NodeGraph_Connection, GetNodeConnections)
{
    NodeGraph graph;
    NodeId lit1Id = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId lit2Id = graph.addNode(NodeGraph::createLiteralNode(2.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit1 = graph.getNode(lit1Id);
    const Node* lit2 = graph.getNode(lit2Id);
    const Node* add = graph.getNode(addId);

    graph.connect(lit1Id, lit1->outputs[0].id, addId, add->inputs[0].id);
    graph.connect(lit2Id, lit2->outputs[0].id, addId, add->inputs[1].id);

    auto addConns = graph.getNodeConnections(addId);
    EXPECT_EQ(addConns.size(), 2u);

    auto lit1Conns = graph.getNodeConnections(lit1Id);
    EXPECT_EQ(lit1Conns.size(), 1u);
}

// ===========================================================================
// Cycle detection
// ===========================================================================

TEST(NodeGraph_Cycle, WouldCreateCycleSelfLoop)
{
    NodeGraph graph;
    NodeId id = graph.addNode(NodeGraph::createMathNode("+"));
    EXPECT_TRUE(graph.wouldCreateCycle(id, id));
}

TEST(NodeGraph_Cycle, NoCycleInLinearChain)
{
    NodeGraph graph;
    NodeId a = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId b = graph.addNode(NodeGraph::createFunctionNode("sin"));

    const Node* na = graph.getNode(a);
    const Node* nb = graph.getNode(b);

    graph.connect(a, na->outputs[0].id, b, nb->inputs[0].id);

    // Adding b -> (new node) should not be a cycle
    NodeId c = graph.addNode(NodeGraph::createFunctionNode("cos"));
    EXPECT_FALSE(graph.wouldCreateCycle(b, c));
}

// ===========================================================================
// Graph validation
// ===========================================================================

TEST(NodeGraph_Validation, EmptyGraphIsValid)
{
    NodeGraph graph;
    std::string error;
    EXPECT_TRUE(graph.validate(error));
}

TEST(NodeGraph_Validation, SimpleValidGraph)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId sinId = graph.addNode(NodeGraph::createFunctionNode("sin"));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* lit = graph.getNode(litId);
    const Node* sinN = graph.getNode(sinId);
    const Node* out = graph.getNode(outId);

    graph.connect(litId, lit->outputs[0].id, sinId, sinN->inputs[0].id);
    graph.connect(sinId, sinN->outputs[0].id, outId, out->inputs[0].id);

    std::string error;
    EXPECT_TRUE(graph.validate(error));
}

TEST(NodeGraph_Validation, UnconnectedInputsAreValid)
{
    // Unconnected inputs use default values (always initialized to 0.0f)
    NodeGraph graph;
    graph.addNode(NodeGraph::createMathNode("+"));

    std::string error;
    EXPECT_TRUE(graph.validate(error));
}

// ===========================================================================
// Expression tree conversion
// ===========================================================================

TEST(NodeGraph_ExprTree, SimpleLiteralConversion)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(42.0f));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* lit = graph.getNode(litId);
    const Node* out = graph.getNode(outId);

    graph.connect(litId, lit->outputs[0].id, outId, out->inputs[0].id);

    auto expr = graph.toExpressionTree(outId);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(expr->value, 42.0f);
}

TEST(NodeGraph_ExprTree, SimpleVariableConversion)
{
    NodeGraph graph;
    NodeId varId = graph.addNode(NodeGraph::createVariableNode("x"));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* var = graph.getNode(varId);
    const Node* out = graph.getNode(outId);

    graph.connect(varId, var->outputs[0].id, outId, out->inputs[0].id);

    auto expr = graph.toExpressionTree(outId);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(expr->name, "x");
}

TEST(NodeGraph_ExprTree, AddTwoVariables)
{
    // Build graph: a + b -> output
    NodeGraph graph;
    NodeId aId = graph.addNode(NodeGraph::createVariableNode("a"));
    NodeId bId = graph.addNode(NodeGraph::createVariableNode("b"));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* a = graph.getNode(aId);
    const Node* b = graph.getNode(bId);
    const Node* add = graph.getNode(addId);
    const Node* out = graph.getNode(outId);

    graph.connect(aId, a->outputs[0].id, addId, add->inputs[0].id);
    graph.connect(bId, b->outputs[0].id, addId, add->inputs[1].id);
    graph.connect(addId, add->outputs[0].id, outId, out->inputs[0].id);

    auto expr = graph.toExpressionTree(outId);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, ExprNodeType::BINARY_OP);
    EXPECT_EQ(expr->op, "+");
    ASSERT_EQ(expr->children.size(), 2u);
    EXPECT_EQ(expr->children[0]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(expr->children[0]->name, "a");
    EXPECT_EQ(expr->children[1]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(expr->children[1]->name, "b");
}

TEST(NodeGraph_ExprTree, SinOfVariable)
{
    // Build graph: sin(x) -> output
    NodeGraph graph;
    NodeId xId = graph.addNode(NodeGraph::createVariableNode("x"));
    NodeId sinId = graph.addNode(NodeGraph::createFunctionNode("sin"));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* x = graph.getNode(xId);
    const Node* sinN = graph.getNode(sinId);
    const Node* out = graph.getNode(outId);

    graph.connect(xId, x->outputs[0].id, sinId, sinN->inputs[0].id);
    graph.connect(sinId, sinN->outputs[0].id, outId, out->inputs[0].id);

    auto expr = graph.toExpressionTree(outId);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, ExprNodeType::UNARY_OP);
    EXPECT_EQ(expr->op, "sin");
    ASSERT_EQ(expr->children.size(), 1u);
    EXPECT_EQ(expr->children[0]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(expr->children[0]->name, "x");
}

TEST(NodeGraph_ExprTree, UnconnectedInputUsesDefault)
{
    // Build graph: add(unconnected A=0, unconnected B=0) -> output
    NodeGraph graph;
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* add = graph.getNode(addId);
    const Node* out = graph.getNode(outId);

    graph.connect(addId, add->outputs[0].id, outId, out->inputs[0].id);

    auto expr = graph.toExpressionTree(outId);
    ASSERT_NE(expr, nullptr);
    EXPECT_EQ(expr->type, ExprNodeType::BINARY_OP);
    EXPECT_EQ(expr->op, "+");
    // Both children should be literals with value 0 (the defaults)
    ASSERT_EQ(expr->children.size(), 2u);
    EXPECT_EQ(expr->children[0]->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(expr->children[0]->value, 0.0f);
    EXPECT_EQ(expr->children[1]->type, ExprNodeType::LITERAL);
    EXPECT_FLOAT_EQ(expr->children[1]->value, 0.0f);
}

TEST(NodeGraph_ExprTree, FromExpressionTreeLiteral)
{
    auto expr = ExprNode::literal(7.0f);
    NodeGraph graph = NodeGraph::fromExpressionTree(*expr);

    // Should have at least 2 nodes: the literal and an output
    EXPECT_GE(graph.nodeCount(), 2u);
    EXPECT_GE(graph.connectionCount(), 1u);
}

TEST(NodeGraph_ExprTree, FromExpressionTreeBinaryOp)
{
    // a + b
    auto expr = ExprNode::binaryOp("+",
        ExprNode::variable("a"),
        ExprNode::variable("b"));

    NodeGraph graph = NodeGraph::fromExpressionTree(*expr);

    // Should have: variable "a", variable "b", add node, output node = 4 nodes
    EXPECT_EQ(graph.nodeCount(), 4u);
    // Connections: a->add, b->add, add->output = 3
    EXPECT_EQ(graph.connectionCount(), 3u);
}

TEST(NodeGraph_ExprTree, FromExpressionTreeUnaryOp)
{
    // sin(x)
    auto expr = ExprNode::unaryOp("sin", ExprNode::variable("x"));
    NodeGraph graph = NodeGraph::fromExpressionTree(*expr);

    // variable "x", sin node, output node = 3 nodes
    EXPECT_EQ(graph.nodeCount(), 3u);
    // x->sin, sin->output = 2
    EXPECT_EQ(graph.connectionCount(), 2u);
}

TEST(NodeGraph_ExprTree, RoundTripExprToGraphToExpr)
{
    // Build: a + b
    auto original = ExprNode::binaryOp("+",
        ExprNode::variable("a"),
        ExprNode::variable("b"));

    // ExprNode -> NodeGraph
    NodeGraph graph = NodeGraph::fromExpressionTree(*original);

    // Find the output node
    NodeId outputId = 0;
    for (const auto& [id, node] : graph.getNodes())
    {
        if (node.operation == "output")
        {
            outputId = id;
            break;
        }
    }
    ASSERT_NE(outputId, 0u);

    // NodeGraph -> ExprNode
    auto roundTripped = graph.toExpressionTree(outputId);
    ASSERT_NE(roundTripped, nullptr);
    EXPECT_EQ(roundTripped->type, ExprNodeType::BINARY_OP);
    EXPECT_EQ(roundTripped->op, "+");
    ASSERT_EQ(roundTripped->children.size(), 2u);
    EXPECT_EQ(roundTripped->children[0]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(roundTripped->children[0]->name, "a");
    EXPECT_EQ(roundTripped->children[1]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(roundTripped->children[1]->name, "b");
}

TEST(NodeGraph_ExprTree, RoundTripSinExpr)
{
    auto original = ExprNode::unaryOp("sin", ExprNode::variable("theta"));
    NodeGraph graph = NodeGraph::fromExpressionTree(*original);

    NodeId outputId = 0;
    for (const auto& [id, node] : graph.getNodes())
    {
        if (node.operation == "output")
        {
            outputId = id;
            break;
        }
    }
    ASSERT_NE(outputId, 0u);

    auto roundTripped = graph.toExpressionTree(outputId);
    ASSERT_NE(roundTripped, nullptr);
    EXPECT_EQ(roundTripped->type, ExprNodeType::UNARY_OP);
    EXPECT_EQ(roundTripped->op, "sin");
    ASSERT_EQ(roundTripped->children.size(), 1u);
    EXPECT_EQ(roundTripped->children[0]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(roundTripped->children[0]->name, "theta");
}

TEST(NodeGraph_ExprTree, FromExpressionTreeConditional)
{
    // x > 0 ? a : b  — model the comparison itself as a variable "cmp"
    // so the 3-input conditional is exercised without requiring a
    // dedicated comparison node type.
    auto expr = ExprNode::conditional(
        ExprNode::variable("cmp"),
        ExprNode::variable("a"),
        ExprNode::variable("b"));

    NodeGraph graph = NodeGraph::fromExpressionTree(*expr);

    // variables cmp/a/b (3) + conditional + output = 5 nodes
    EXPECT_EQ(graph.nodeCount(), 5u);
    // cmp→cond[0], a→cond[1], b→cond[2], cond→output = 4 connections
    EXPECT_EQ(graph.connectionCount(), 4u);
}

TEST(NodeGraph_ExprTree, RoundTripConditionalExpr)
{
    auto original = ExprNode::conditional(
        ExprNode::variable("cond"),
        ExprNode::variable("thenVal"),
        ExprNode::variable("elseVal"));

    NodeGraph graph = NodeGraph::fromExpressionTree(*original);

    NodeId outputId = 0;
    for (const auto& [id, node] : graph.getNodes())
    {
        if (node.operation == "output")
        {
            outputId = id;
            break;
        }
    }
    ASSERT_NE(outputId, 0u);

    auto roundTripped = graph.toExpressionTree(outputId);
    ASSERT_NE(roundTripped, nullptr);
    EXPECT_EQ(roundTripped->type, ExprNodeType::CONDITIONAL);
    ASSERT_EQ(roundTripped->children.size(), 3u);
    EXPECT_EQ(roundTripped->children[0]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(roundTripped->children[0]->name, "cond");
    EXPECT_EQ(roundTripped->children[1]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(roundTripped->children[1]->name, "thenVal");
    EXPECT_EQ(roundTripped->children[2]->type, ExprNodeType::VARIABLE);
    EXPECT_EQ(roundTripped->children[2]->name, "elseVal");
}

TEST(NodeGraph_ExprTree, RoundTripNestedConditional)
{
    // if(outerCond, if(innerCond, a, b), c)
    auto original = ExprNode::conditional(
        ExprNode::variable("outerCond"),
        ExprNode::conditional(
            ExprNode::variable("innerCond"),
            ExprNode::variable("a"),
            ExprNode::variable("b")),
        ExprNode::variable("c"));

    NodeGraph graph = NodeGraph::fromExpressionTree(*original);

    NodeId outputId = 0;
    for (const auto& [id, node] : graph.getNodes())
    {
        if (node.operation == "output")
        {
            outputId = id;
            break;
        }
    }
    ASSERT_NE(outputId, 0u);

    auto roundTripped = graph.toExpressionTree(outputId);
    ASSERT_NE(roundTripped, nullptr);
    EXPECT_EQ(roundTripped->type, ExprNodeType::CONDITIONAL);
    ASSERT_EQ(roundTripped->children.size(), 3u);

    // Outer condition preserved
    EXPECT_EQ(roundTripped->children[0]->name, "outerCond");
    // Then-branch is a nested conditional
    EXPECT_EQ(roundTripped->children[1]->type, ExprNodeType::CONDITIONAL);
    ASSERT_EQ(roundTripped->children[1]->children.size(), 3u);
    EXPECT_EQ(roundTripped->children[1]->children[0]->name, "innerCond");
    EXPECT_EQ(roundTripped->children[1]->children[1]->name, "a");
    EXPECT_EQ(roundTripped->children[1]->children[2]->name, "b");
    // Else-branch preserved
    EXPECT_EQ(roundTripped->children[2]->name, "c");
}

// ===========================================================================
// JSON serialization round-trip
// ===========================================================================

TEST(NodeGraph_Json, EmptyGraphRoundTrip)
{
    NodeGraph graph;
    nlohmann::json j = graph.toJson();
    NodeGraph restored = NodeGraph::fromJson(j);

    EXPECT_EQ(restored.nodeCount(), 0u);
    EXPECT_EQ(restored.connectionCount(), 0u);
}

TEST(NodeGraph_Json, SingleNodeRoundTrip)
{
    NodeGraph graph;
    graph.addNode(NodeGraph::createLiteralNode(3.14f));

    nlohmann::json j = graph.toJson();
    NodeGraph restored = NodeGraph::fromJson(j);

    EXPECT_EQ(restored.nodeCount(), 1u);

    // Find the node and check its values
    const auto& nodes = restored.getNodes();
    ASSERT_EQ(nodes.size(), 1u);
    const Node& node = nodes.begin()->second;
    EXPECT_EQ(node.operation, "literal");
    EXPECT_FLOAT_EQ(node.literalValue, 3.14f);
    EXPECT_EQ(node.outputs.size(), 1u);
    EXPECT_EQ(node.outputs[0].name, "Value");
}

TEST(NodeGraph_Json, FullGraphRoundTrip)
{
    // Build: a + b -> output
    NodeGraph graph;
    NodeId aId = graph.addNode(NodeGraph::createVariableNode("a"));
    NodeId bId = graph.addNode(NodeGraph::createVariableNode("b"));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));
    NodeId outId = graph.addNode(NodeGraph::createOutputNode());

    const Node* a = graph.getNode(aId);
    const Node* b = graph.getNode(bId);
    const Node* add = graph.getNode(addId);
    const Node* out = graph.getNode(outId);

    graph.connect(aId, a->outputs[0].id, addId, add->inputs[0].id);
    graph.connect(bId, b->outputs[0].id, addId, add->inputs[1].id);
    graph.connect(addId, add->outputs[0].id, outId, out->inputs[0].id);

    // Set positions to verify persistence
    graph.getNode(aId)->posX = 100.0f;
    graph.getNode(aId)->posY = 50.0f;

    nlohmann::json j = graph.toJson();
    NodeGraph restored = NodeGraph::fromJson(j);

    EXPECT_EQ(restored.nodeCount(), 4u);
    EXPECT_EQ(restored.connectionCount(), 3u);

    // Verify positions preserved
    const Node* restoredA = restored.getNode(aId);
    ASSERT_NE(restoredA, nullptr);
    EXPECT_FLOAT_EQ(restoredA->posX, 100.0f);
    EXPECT_FLOAT_EQ(restoredA->posY, 50.0f);
}

TEST(NodeGraph_Json, VersionField)
{
    NodeGraph graph;
    nlohmann::json j = graph.toJson();
    EXPECT_EQ(j["version"], 1);
}

TEST(NodeGraph_Json, PreservesNodeCategories)
{
    NodeGraph graph;
    graph.addNode(NodeGraph::createMathNode("+"));
    graph.addNode(NodeGraph::createFunctionNode("sin"));
    graph.addNode(NodeGraph::createLiteralNode(1.0f));
    graph.addNode(NodeGraph::createOutputNode());

    nlohmann::json j = graph.toJson();
    NodeGraph restored = NodeGraph::fromJson(j);

    for (const auto& [id, node] : restored.getNodes())
    {
        if (node.operation == "+")
            EXPECT_EQ(node.category, NodeCategory::MATH_BASIC);
        else if (node.operation == "sin")
            EXPECT_EQ(node.category, NodeCategory::TRIGONOMETRY);
        else if (node.operation == "literal")
            EXPECT_EQ(node.category, NodeCategory::INPUT);
        else if (node.operation == "output")
            EXPECT_EQ(node.category, NodeCategory::OUTPUT);
    }
}

TEST(NodeGraph_Json, PreservesPortDataTypes)
{
    NodeGraph graph;
    NodeId id = graph.addNode(NodeGraph::createMathNode("+"));

    nlohmann::json j = graph.toJson();
    NodeGraph restored = NodeGraph::fromJson(j);

    const Node* node = restored.getNode(id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->inputs[0].dataType, PortDataType::FLOAT);
    EXPECT_EQ(node->outputs[0].dataType, PortDataType::FLOAT);
}

TEST(NodeGraph_Json, DuplicateNodeIdThrows)
{
    // AUDIT.md §M9 / FIXPLAN: fromJson must refuse duplicate node IDs
    // rather than silently overwriting.
    nlohmann::json j;
    j["version"] = 1;
    j["nextNodeId"] = 100;
    j["nextPortId"] = 1;
    j["nextConnectionId"] = 1;
    j["nodes"] = nlohmann::json::array();
    nlohmann::json n1;
    n1["id"] = 42;
    n1["operation"] = "literal";
    n1["category"] = "input";
    n1["literalValue"] = 1.0f;
    j["nodes"].push_back(n1);
    nlohmann::json n2;
    n2["id"] = 42;        // deliberate duplicate
    n2["operation"] = "literal";
    n2["category"] = "input";
    n2["literalValue"] = 2.0f;
    j["nodes"].push_back(n2);
    j["connections"] = nlohmann::json::array();

    EXPECT_THROW(NodeGraph::fromJson(j), std::runtime_error);
}

TEST(NodeGraph_Json, NextNodeIdReclampedAgainstMax)
{
    // §M9: if serialised m_nextNodeId is <= any present id, fromJson
    // must recompute to max(id)+1 so subsequent addNode() calls never
    // collide with existing nodes.
    NodeGraph g;
    NodeId high = 0;
    for (int i = 0; i < 5; ++i)
    {
        high = g.addNode(NodeGraph::createLiteralNode(float(i)));
    }

    // Tamper with the serialised counter to simulate an old or hostile
    // save with nextNodeId smaller than a present id.
    nlohmann::json j = g.toJson();
    j["nextNodeId"] = 1;

    NodeGraph restored = NodeGraph::fromJson(j);
    NodeId fresh = restored.addNode(NodeGraph::createLiteralNode(42.0f));
    EXPECT_GT(fresh, high)
        << "addNode after fromJson with tampered nextNodeId must still "
           "produce a strictly greater id — AUDIT.md §M9.";
}

// ===========================================================================
// Enum string conversions
// ===========================================================================

TEST(NodeGraph_Enum, PortDataTypeRoundTrip)
{
    EXPECT_EQ(portDataTypeFromString(portDataTypeToString(PortDataType::FLOAT)), PortDataType::FLOAT);
    EXPECT_EQ(portDataTypeFromString(portDataTypeToString(PortDataType::VEC2)), PortDataType::VEC2);
    EXPECT_EQ(portDataTypeFromString(portDataTypeToString(PortDataType::VEC3)), PortDataType::VEC3);
    EXPECT_EQ(portDataTypeFromString(portDataTypeToString(PortDataType::VEC4)), PortDataType::VEC4);
}

TEST(NodeGraph_Enum, NodeCategoryRoundTrip)
{
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::MATH_BASIC)), NodeCategory::MATH_BASIC);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::MATH_ADVANCED)), NodeCategory::MATH_ADVANCED);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::TRIGONOMETRY)), NodeCategory::TRIGONOMETRY);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::EXPONENTIAL)), NodeCategory::EXPONENTIAL);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::CLAMPING)), NodeCategory::CLAMPING);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::INTERPOLATION)), NodeCategory::INTERPOLATION);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::INPUT)), NodeCategory::INPUT);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::OUTPUT)), NodeCategory::OUTPUT);
    EXPECT_EQ(nodeCategoryFromString(nodeCategoryToString(NodeCategory::TEMPLATE)), NodeCategory::TEMPLATE);
}

TEST(NodeGraph_Enum, UnknownStringDefaultsGracefully)
{
    EXPECT_EQ(portDataTypeFromString("unknown"), PortDataType::FLOAT);
    EXPECT_EQ(nodeCategoryFromString("unknown"), NodeCategory::MATH_BASIC);
}

// ===========================================================================
// Clear
// ===========================================================================

TEST(NodeGraph_Clear, ResetsEverything)
{
    NodeGraph graph;
    NodeId litId = graph.addNode(NodeGraph::createLiteralNode(1.0f));
    NodeId addId = graph.addNode(NodeGraph::createMathNode("+"));

    const Node* lit = graph.getNode(litId);
    const Node* add = graph.getNode(addId);
    graph.connect(litId, lit->outputs[0].id, addId, add->inputs[0].id);

    EXPECT_GT(graph.nodeCount(), 0u);
    EXPECT_GT(graph.connectionCount(), 0u);

    graph.clear();
    EXPECT_EQ(graph.nodeCount(), 0u);
    EXPECT_EQ(graph.connectionCount(), 0u);

    // After clear, new nodes should start with fresh IDs
    NodeId newId = graph.addNode(NodeGraph::createLiteralNode(2.0f));
    EXPECT_EQ(newId, 1u);
}
