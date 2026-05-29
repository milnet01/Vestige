// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_graph.cpp
/// @brief Unit tests for ScriptGraph, serialization, loading, and ScriptInstance.
#include "scripting/script_graph.h"
#include "scripting/script_instance.h"
#include "scripting/node_type_registry.h"
#include "scripting/core_nodes.h"
#include "scripting/script_value.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(ScriptGraph, AddAndFindNode)
{
    ScriptGraph graph;
    uint32_t id = graph.addNode("Branch", 100.0f, 200.0f);
    EXPECT_NE(id, 0u);

    auto* node = graph.findNode(id);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->typeName, "Branch");
    EXPECT_FLOAT_EQ(node->posX, 100.0f);
    EXPECT_FLOAT_EQ(node->posY, 200.0f);
}

TEST(ScriptGraph, RemoveNode)
{
    ScriptGraph graph;
    uint32_t id1 = graph.addNode("A");
    uint32_t id2 = graph.addNode("B");
    graph.addConnection(id1, "Out", id2, "In");

    EXPECT_TRUE(graph.removeNode(id1));
    EXPECT_EQ(graph.findNode(id1), nullptr);
    EXPECT_EQ(graph.connections.size(), 0u); // Connection cleaned up
}

TEST(ScriptGraph, AddConnection)
{
    ScriptGraph graph;
    uint32_t id1 = graph.addNode("A");
    uint32_t id2 = graph.addNode("B");

    uint32_t connId = graph.addConnection(id1, "Out", id2, "In");
    EXPECT_NE(connId, 0u);
    EXPECT_EQ(graph.connections.size(), 1u);
}

TEST(ScriptGraph, RejectSelfConnection)
{
    ScriptGraph graph;
    uint32_t id1 = graph.addNode("A");
    EXPECT_EQ(graph.addConnection(id1, "Out", id1, "In"), 0u);
}

TEST(ScriptGraph, RejectDuplicateConnection)
{
    ScriptGraph graph;
    uint32_t id1 = graph.addNode("A");
    uint32_t id2 = graph.addNode("B");

    EXPECT_NE(graph.addConnection(id1, "Out", id2, "In"), 0u);
    EXPECT_EQ(graph.addConnection(id1, "Out", id2, "In"), 0u); // Duplicate
}

TEST(ScriptGraph, RejectSecondInputConnection)
{
    ScriptGraph graph;
    uint32_t id1 = graph.addNode("A");
    uint32_t id2 = graph.addNode("B");
    uint32_t id3 = graph.addNode("C");

    EXPECT_NE(graph.addConnection(id1, "Out", id3, "In"), 0u);
    EXPECT_EQ(graph.addConnection(id2, "Out", id3, "In"), 0u); // Already has input
}

TEST(ScriptGraph, Validate)
{
    ScriptGraph graph;
    uint32_t id1 = graph.addNode("A");
    uint32_t id2 = graph.addNode("B");
    graph.addConnection(id1, "Out", id2, "In");

    std::string error;
    EXPECT_TRUE(graph.validate(error));
    EXPECT_TRUE(error.empty());
}

TEST(ScriptGraph, ValidateDetectsDuplicateNodeId)
{
    ScriptGraph graph;
    graph.nodes.push_back({1, "A", 0.0f, 0.0f, {}});
    graph.nodes.push_back({1, "B", 0.0f, 0.0f, {}}); // Duplicate ID

    std::string error;
    EXPECT_FALSE(graph.validate(error));
    EXPECT_NE(error.find("Duplicate"), std::string::npos);
}

TEST(ScriptGraph, JsonRoundTrip)
{
    ScriptGraph original;
    original.name = "TestGraph";
    uint32_t id1 = original.addNode("OnStart", 0.0f, 0.0f);
    uint32_t id2 = original.addNode("PrintToScreen", 200.0f, 0.0f);
    original.addConnection(id1, "Started", id2, "Exec");

    original.variables.push_back({"health", ScriptDataType::FLOAT,
                                  ScriptValue(100.0f), VariableScope::GRAPH});

    auto json = original.toJson();
    auto restored = ScriptGraph::fromJson(json);

    EXPECT_EQ(restored.name, "TestGraph");
    EXPECT_EQ(restored.nodes.size(), 2u);
    EXPECT_EQ(restored.connections.size(), 1u);
    EXPECT_EQ(restored.variables.size(), 1u);
    EXPECT_EQ(restored.variables[0].name, "health");
}

TEST(ScriptInstance, InitializeFromGraph)
{
    ScriptGraph graph;
    graph.name = "Test";
    graph.addNode("OnStart");
    graph.addNode("Branch");

    graph.variables.push_back({"score", ScriptDataType::INT,
                               ScriptValue(0), VariableScope::GRAPH});

    ScriptInstance instance;
    instance.initialize(graph, 42);

    EXPECT_EQ(instance.entityId(), 42u);
    EXPECT_FALSE(instance.isActive());
    EXPECT_EQ(instance.nodeInstances().size(), 2u);
    EXPECT_EQ(instance.graphBlackboard().get("score").asInt(), 0);
}

TEST(ScriptInstance, FindNodesByType)
{
    ScriptGraph graph;
    graph.addNode("OnStart");
    graph.addNode("Branch");
    graph.addNode("OnStart"); // Second OnStart

    ScriptInstance instance;
    instance.initialize(graph, 1);

    auto starts = instance.findNodesByType("OnStart");
    EXPECT_EQ(starts.size(), 2u);

    auto branches = instance.findNodesByType("Branch");
    EXPECT_EQ(branches.size(), 1u);
}

TEST(ScriptGraphSerialization, EmptyGraphRoundTrips)
{
    ScriptGraph g;
    g.name = "empty";
    auto j = g.toJson();
    auto r = ScriptGraph::fromJson(j);
    EXPECT_EQ(r.name, "empty");
    EXPECT_EQ(r.nodes.size(), 0u);
    EXPECT_EQ(r.connections.size(), 0u);
    EXPECT_EQ(r.variables.size(), 0u);
}

TEST(ScriptGraphSerialization, SingleNodeRoundTrips)
{
    ScriptGraph g;
    g.addNode("PrintToScreen");
    auto r = ScriptGraph::fromJson(g.toJson());
    ASSERT_EQ(r.nodes.size(), 1u);
    EXPECT_EQ(r.nodes[0].typeName, "PrintToScreen");
}

TEST(ScriptGraphSerialization, MalformedJsonFieldsAreIgnored)
{
    // Nodes array with wrong type — should be ignored, not crash.
    nlohmann::json j;
    j["version"] = 1;
    j["nodes"] = "not-an-array";
    j["connections"] = 42;
    j["variables"] = nullptr;
    auto r = ScriptGraph::fromJson(j);
    EXPECT_EQ(r.nodes.size(), 0u);
    EXPECT_EQ(r.connections.size(), 0u);
    EXPECT_EQ(r.variables.size(), 0u);
}

TEST(ScriptGraphSerialization, NodeCapEnforced)
{
    nlohmann::json j;
    j["nodes"] = nlohmann::json::array();
    for (size_t i = 0; i < ScriptGraph::MAX_NODES + 5; ++i)
    {
        nlohmann::json n;
        n["id"] = static_cast<uint32_t>(i + 1);
        n["type"] = "PrintToScreen";
        j["nodes"].push_back(n);
    }
    auto r = ScriptGraph::fromJson(j);
    EXPECT_LE(r.nodes.size(), ScriptGraph::MAX_NODES);
}

TEST(ScriptGraphSerialization, StringBytesCapTruncatesLongNames)
{
    nlohmann::json j;
    std::string huge(ScriptGraph::MAX_STRING_BYTES + 100, 'x');
    j["name"] = huge;
    auto r = ScriptGraph::fromJson(j);
    EXPECT_LE(r.name.size(), ScriptGraph::MAX_STRING_BYTES);
}

TEST(ScriptGraphLoad, RejectsPathTraversal)
{
    // Any path containing a `..` component should be refused.
    auto r = ScriptGraph::loadFromFile("../evil.vscript");
    EXPECT_EQ(r.nodes.size(), 0u);
    auto r2 = ScriptGraph::loadFromFile("assets/../../evil.vscript");
    EXPECT_EQ(r2.nodes.size(), 0u);
}

TEST(ScriptGraphLoad, RejectsAbsolutePath)
{
    // AUDIT.md §M6 / FIXPLAN: absolute paths bypass the implicit cwd scope
    // and previously passed the `..`-only check. Now rejected outright.
    auto r = ScriptGraph::loadFromFile("/etc/passwd");
    EXPECT_EQ(r.nodes.size(), 0u);
}

TEST(ScriptGraphLoad, RejectsHomeTildePath)
{
    // `~` is shell-only tilde expansion; std::filesystem does not resolve
    // it. Rejecting avoids a surprise where `~/foo` reads a relative file
    // named literally "~".
    auto r = ScriptGraph::loadFromFile("~/evil.vscript");
    EXPECT_EQ(r.nodes.size(), 0u);
}

TEST(ScriptGraphLoad, RejectsEmptyPath)
{
    auto r = ScriptGraph::loadFromFile("");
    EXPECT_EQ(r.nodes.size(), 0u);
}

TEST(ScriptGraph, AddConnectionClampsPinNames)
{
    // AUDIT.md §M5 / FIXPLAN: editor path also clamps pin-name strings
    // so unbounded names from programmatic calls cannot bypass the load-
    // path clampString guards.
    ScriptGraph g;
    uint32_t a = g.addNode("PrintToScreen");
    uint32_t b = g.addNode("PrintToScreen");
    const std::string huge(10 * 1024, 'x');
    uint32_t connId = g.addConnection(a, huge, b, huge);
    ASSERT_NE(connId, 0u);

    // Find the connection and verify pin names are clamped.
    bool found = false;
    for (const auto& c : g.connections)
    {
        if (c.id == connId)
        {
            EXPECT_LE(c.sourcePin.size(), ScriptGraph::MAX_STRING_BYTES);
            EXPECT_LE(c.targetPin.size(), ScriptGraph::MAX_STRING_BYTES);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ScriptInstanceTypeCache, NodesByTypeReturnsCachedVector)
{
    ScriptGraph g;
    g.addNode("OnStart");
    g.addNode("Branch");
    g.addNode("OnStart");

    ScriptInstance inst;
    inst.initialize(g, 1);

    const auto& starts1 = inst.nodesByType("OnStart");
    const auto& starts2 = inst.nodesByType("OnStart");
    EXPECT_EQ(starts1.size(), 2u);
    // Repeated calls return the same cached storage — no per-call allocation.
    EXPECT_EQ(&starts1, &starts2);
}

TEST(ScriptInstanceTypeCache, NodesByTypeUnknownReturnsStableEmpty)
{
    ScriptGraph g;
    ScriptInstance inst;
    inst.initialize(g, 1);

    const auto& a = inst.nodesByType("DoesNotExist");
    const auto& b = inst.nodesByType("AlsoMissing");
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(&a, &b); // both share the static-empty fallback
}

TEST(ScriptInstanceTypeCache, UpdateNodesEqualsNodesByTypeOnUpdate)
{
    ScriptGraph g;
    g.addNode("OnStart");
    g.addNode("OnUpdate");
    g.addNode("OnUpdate");

    ScriptInstance inst;
    inst.initialize(g, 1);

    EXPECT_EQ(inst.updateNodes().size(), 2u);
    // updateNodes() is now a thin wrapper over nodesByType("OnUpdate").
    EXPECT_EQ(&inst.updateNodes(), &inst.nodesByType("OnUpdate"));
}
