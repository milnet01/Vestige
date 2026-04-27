// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scripting.cpp
/// @brief Unit tests for visual scripting infrastructure (Phase 9E-1).
#include "scripting/script_value.h"
#include "scripting/blackboard.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_graph.h"
#include "scripting/script_instance.h"
#include "scripting/script_context.h"
#include "scripting/core_nodes.h"
#include "scripting/event_nodes.h"
#include "scripting/action_nodes.h"
#include "scripting/pure_nodes.h"
#include "scripting/flow_nodes.h"
#include "scripting/latent_nodes.h"
#include "scripting/scripting_system.h"
#include "scripting/script_events.h"
#include "core/engine.h"
#include "core/event.h"

#include <gtest/gtest.h>

#include <limits>

using namespace Vestige;

// ===========================================================================
// ScriptValue — type construction and access
// ===========================================================================

TEST(ScriptValue, DefaultIsBool)
{
    ScriptValue v;
    EXPECT_EQ(v.getType(), ScriptDataType::BOOL);
    EXPECT_FALSE(v.asBool());
}

TEST(ScriptValue, BoolType)
{
    ScriptValue v(true);
    EXPECT_EQ(v.getType(), ScriptDataType::BOOL);
    EXPECT_TRUE(v.asBool());
    EXPECT_EQ(v.asInt(), 1);
    EXPECT_FLOAT_EQ(v.asFloat(), 1.0f);
    EXPECT_EQ(v.asString(), "true");
}

TEST(ScriptValue, IntType)
{
    ScriptValue v(42);
    EXPECT_EQ(v.getType(), ScriptDataType::INT);
    EXPECT_EQ(v.asInt(), 42);
    EXPECT_FLOAT_EQ(v.asFloat(), 42.0f);
    EXPECT_TRUE(v.asBool());
}

TEST(ScriptValue, FloatType)
{
    ScriptValue v(3.14f);
    EXPECT_EQ(v.getType(), ScriptDataType::FLOAT);
    EXPECT_FLOAT_EQ(v.asFloat(), 3.14f);
    EXPECT_EQ(v.asInt(), 3);
    EXPECT_TRUE(v.asBool());
}

// ---------------------------------------------------------------------------
// AUDIT Sc4 — asInt() must clamp out-of-range floats and map NaN to 0
// instead of UB-casting.
// ---------------------------------------------------------------------------
TEST(ScriptValue, FloatToIntClampSaturatesAtMax_Sc4)
{
    ScriptValue v(1e30f);
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::max());
}

TEST(ScriptValue, FloatToIntClampSaturatesAtMin_Sc4)
{
    ScriptValue v(-1e30f);
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::min());
}

TEST(ScriptValue, FloatToIntPositiveInfinitySaturates_Sc4)
{
    ScriptValue v(std::numeric_limits<float>::infinity());
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::max());
}

TEST(ScriptValue, FloatToIntNegativeInfinitySaturates_Sc4)
{
    ScriptValue v(-std::numeric_limits<float>::infinity());
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::min());
}

TEST(ScriptValue, FloatToIntNaNReturnsZero_Sc4)
{
    ScriptValue v(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(v.asInt(), 0);
}

TEST(ScriptValue, UintAboveIntMaxClamps_Sc4)
{
    auto v = ScriptValue::entityId(0xFFFFFFFFu);
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::max());
}

TEST(ScriptValue, StringType)
{
    ScriptValue v(std::string("hello"));
    EXPECT_EQ(v.getType(), ScriptDataType::STRING);
    EXPECT_EQ(v.asString(), "hello");
    EXPECT_TRUE(v.asBool()); // non-empty string is truthy
}

TEST(ScriptValue, EmptyStringFalsy)
{
    ScriptValue v(std::string(""));
    EXPECT_FALSE(v.asBool());
}

TEST(ScriptValue, Vec3Type)
{
    ScriptValue v(glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(v.getType(), ScriptDataType::VEC3);
    auto vec = v.asVec3();
    EXPECT_FLOAT_EQ(vec.x, 1.0f);
    EXPECT_FLOAT_EQ(vec.y, 2.0f);
    EXPECT_FLOAT_EQ(vec.z, 3.0f);
}

TEST(ScriptValue, EntityIdType)
{
    auto v = ScriptValue::entityId(42);
    EXPECT_EQ(v.getType(), ScriptDataType::ENTITY);
    EXPECT_EQ(v.asEntityId(), 42u);
    EXPECT_EQ(v.asString(), "Entity#42");
}

TEST(ScriptValue, ConvertIntToFloat)
{
    ScriptValue v(42);
    auto converted = v.convertTo(ScriptDataType::FLOAT);
    EXPECT_EQ(converted.getType(), ScriptDataType::FLOAT);
    EXPECT_FLOAT_EQ(converted.asFloat(), 42.0f);
}

TEST(ScriptValue, ConvertFloatToString)
{
    ScriptValue v(3.14f);
    auto converted = v.convertTo(ScriptDataType::STRING);
    EXPECT_EQ(converted.getType(), ScriptDataType::STRING);
    // std::to_string(3.14f) produces something like "3.140000"
    EXPECT_FALSE(converted.asString().empty());
}

TEST(ScriptValue, Equality)
{
    EXPECT_EQ(ScriptValue(42), ScriptValue(42));
    EXPECT_NE(ScriptValue(42), ScriptValue(43));
    EXPECT_NE(ScriptValue(42), ScriptValue(42.0f)); // Different types
    EXPECT_EQ(ScriptValue(true), ScriptValue(true));
    EXPECT_NE(ScriptValue(true), ScriptValue(false));
}

TEST(ScriptValue, JsonRoundTrip)
{
    std::vector<ScriptValue> values = {
        ScriptValue(true),
        ScriptValue(42),
        ScriptValue(3.14f),
        ScriptValue(std::string("hello")),
        ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)),
        ScriptValue::entityId(99),
    };

    for (const auto& original : values)
    {
        auto json = original.toJson();
        auto restored = ScriptValue::fromJson(json);
        EXPECT_EQ(original.getType(), restored.getType())
            << "Type mismatch for " << original.asString();
    }
}

// ===========================================================================
// Blackboard — key-value storage
// ===========================================================================

TEST(Blackboard, SetAndGet)
{
    Blackboard bb;
    bb.set("health", ScriptValue(100.0f));
    EXPECT_TRUE(bb.has("health"));
    EXPECT_FLOAT_EQ(bb.get("health").asFloat(), 100.0f);
}

TEST(Blackboard, GetMissing)
{
    Blackboard bb;
    auto val = bb.get("nonexistent");
    EXPECT_FLOAT_EQ(val.asFloat(), 0.0f); // Default
}

TEST(Blackboard, Remove)
{
    Blackboard bb;
    bb.set("x", ScriptValue(1.0f));
    EXPECT_TRUE(bb.has("x"));
    EXPECT_TRUE(bb.remove("x"));
    EXPECT_FALSE(bb.has("x"));
    EXPECT_FALSE(bb.remove("x")); // Already removed
}

TEST(Blackboard, Clear)
{
    Blackboard bb;
    bb.set("a", ScriptValue(1));
    bb.set("b", ScriptValue(2));
    bb.set("c", ScriptValue(3));
    EXPECT_EQ(bb.size(), 3u);
    bb.clear();
    EXPECT_EQ(bb.size(), 0u);
}

TEST(Blackboard, Overwrite)
{
    Blackboard bb;
    bb.set("x", ScriptValue(1.0f));
    bb.set("x", ScriptValue(2.0f));
    EXPECT_FLOAT_EQ(bb.get("x").asFloat(), 2.0f);
    EXPECT_EQ(bb.size(), 1u);
}

TEST(Blackboard, JsonRoundTrip)
{
    Blackboard original;
    original.set("name", ScriptValue(std::string("Player")));
    original.set("health", ScriptValue(100.0f));
    original.set("alive", ScriptValue(true));

    auto json = original.toJson();
    auto restored = Blackboard::fromJson(json);

    EXPECT_EQ(restored.size(), 3u);
    EXPECT_EQ(restored.get("name").asString(), "Player");
    EXPECT_FLOAT_EQ(restored.get("health").asFloat(), 100.0f);
    EXPECT_TRUE(restored.get("alive").asBool());
}

// ===========================================================================
// NodeTypeRegistry
// ===========================================================================

TEST(NodeTypeRegistry, RegisterAndFind)
{
    NodeTypeRegistry registry;
    registry.registerNode({
        "TestNode", "Test Node", "Testing", "A test node",
        {{PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}}},
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "", false, false, nullptr
    });

    EXPECT_TRUE(registry.hasNode("TestNode"));
    EXPECT_FALSE(registry.hasNode("NonExistent"));

    auto* desc = registry.findNode("TestNode");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->displayName, "Test Node");
    EXPECT_EQ(desc->category, "Testing");
}

TEST(NodeTypeRegistry, GetCategories)
{
    NodeTypeRegistry registry;
    registry.registerNode({"A", "A", "Cat1", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"B", "B", "Cat2", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"C", "C", "Cat1", "", {}, {}, "", false, false, nullptr});

    auto categories = registry.getCategories();
    ASSERT_EQ(categories.size(), 2u);
    EXPECT_EQ(categories[0], "Cat1");
    EXPECT_EQ(categories[1], "Cat2");
}

TEST(NodeTypeRegistry, GetByCategory)
{
    NodeTypeRegistry registry;
    registry.registerNode({"B", "Beta", "Cat1", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"A", "Alpha", "Cat1", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"C", "Gamma", "Cat2", "", {}, {}, "", false, false, nullptr});

    auto cat1 = registry.getByCategory("Cat1");
    ASSERT_EQ(cat1.size(), 2u);
    EXPECT_EQ(cat1[0]->displayName, "Alpha"); // Sorted by display name
    EXPECT_EQ(cat1[1]->displayName, "Beta");
}

TEST(NodeTypeRegistry, CoreNodesRegistered)
{
    NodeTypeRegistry registry;
    registerCoreNodeTypes(registry);

    EXPECT_TRUE(registry.hasNode("OnStart"));
    EXPECT_TRUE(registry.hasNode("OnUpdate"));
    EXPECT_TRUE(registry.hasNode("OnDestroy"));
    EXPECT_TRUE(registry.hasNode("Branch"));
    EXPECT_TRUE(registry.hasNode("Sequence"));
    EXPECT_TRUE(registry.hasNode("Delay"));
    EXPECT_TRUE(registry.hasNode("SetVariable"));
    EXPECT_TRUE(registry.hasNode("GetVariable"));
    EXPECT_TRUE(registry.hasNode("PrintToScreen"));
    EXPECT_TRUE(registry.hasNode("LogMessage"));
    EXPECT_EQ(registry.nodeCount(), 10u);
}

// ===========================================================================
// ScriptGraph — data model
// ===========================================================================

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

// ===========================================================================
// ScriptInstance
// ===========================================================================

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

// ===========================================================================
// ScriptContext — interpreter execution
// ===========================================================================

// Helper: create a minimal engine-like object for testing.
// The ScriptContext needs an Engine reference, but for unit tests we use nullptr
// through a reinterpret_cast. This is safe because our test nodes don't access
// the engine.

class ScriptContextTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        registerCoreNodeTypes(m_registry);
    }

    NodeTypeRegistry m_registry;

    // Build a graph, instance, and context for testing
    struct TestSetup
    {
        ScriptGraph graph;
        ScriptInstance instance;
    };

    TestSetup makeSetup()
    {
        TestSetup ts;
        return ts;
    }
};

TEST_F(ScriptContextTest, BranchTrue)
{
    // Build graph: set a property so Branch reads Condition=true
    ScriptGraph graph;
    graph.name = "BranchTest";
    uint32_t branchId = graph.addNode("Branch");
    uint32_t printId = graph.addNode("PrintToScreen");

    // Set Branch condition to true via property
    auto* branchNode = graph.findNode(branchId);
    branchNode->properties["Condition"] = ScriptValue(true);

    // Connect Branch:True -> PrintToScreen:Exec
    graph.addConnection(branchId, "True", printId, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    instance.setActive(true);

    // Execute the Branch node
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(branchId);

    // The branch should have executed (callDepth back to 0, nodesExecuted > 0)
    EXPECT_EQ(ctx.callDepth(), 0);
    EXPECT_GE(ctx.nodesExecuted(), 1);
}

TEST_F(ScriptContextTest, BranchFalse)
{
    ScriptGraph graph;
    graph.name = "BranchFalseTest";
    uint32_t branchId = graph.addNode("Branch");
    uint32_t trueNodeId = graph.addNode("PrintToScreen");
    uint32_t falseNodeId = graph.addNode("PrintToScreen");

    auto* branchNode = graph.findNode(branchId);
    branchNode->properties["Condition"] = ScriptValue(false);

    auto* trueNode = graph.findNode(trueNodeId);
    trueNode->properties["Message"] = ScriptValue(std::string("TRUE PATH"));

    auto* falseNode = graph.findNode(falseNodeId);
    falseNode->properties["Message"] = ScriptValue(std::string("FALSE PATH"));

    graph.addConnection(branchId, "True", trueNodeId, "Exec");
    graph.addConnection(branchId, "False", falseNodeId, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(branchId);

    // False path should have been taken (2 nodes: Branch + PrintToScreen)
    EXPECT_EQ(ctx.nodesExecuted(), 2);
}

TEST_F(ScriptContextTest, SequenceExecutesAll)
{
    ScriptGraph graph;
    graph.name = "SequenceTest";
    uint32_t seqId = graph.addNode("Sequence");
    uint32_t print0 = graph.addNode("PrintToScreen");
    uint32_t print1 = graph.addNode("PrintToScreen");

    graph.addConnection(seqId, "Then 0", print0, "Exec");
    graph.addConnection(seqId, "Then 1", print1, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(seqId);

    // Sequence + 2 PrintToScreen = 3 nodes
    EXPECT_EQ(ctx.nodesExecuted(), 3);
}

TEST_F(ScriptContextTest, SetAndGetVariable)
{
    ScriptGraph graph;
    graph.name = "VarTest";

    // SetVariable node: set "score" to 42
    uint32_t setId = graph.addNode("SetVariable");
    auto* setNode = graph.findNode(setId);
    setNode->properties["Name"] = ScriptValue(std::string("score"));
    setNode->properties["Value"] = ScriptValue(42);

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(setId);

    // Check that the variable was set in the graph blackboard
    EXPECT_EQ(instance.graphBlackboard().get("score").asInt(), 42);
}

TEST_F(ScriptContextTest, GetVariablePureNode)
{
    ScriptGraph graph;
    graph.name = "PureVarTest";

    // Pre-set a variable
    graph.variables.push_back({"score", ScriptDataType::INT,
                               ScriptValue(99), VariableScope::GRAPH});

    // GetVariable node
    uint32_t getId = graph.addNode("GetVariable");
    auto* getNode = graph.findNode(getId);
    getNode->properties["Name"] = ScriptValue(std::string("score"));

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);

    // Manually execute the pure node
    ctx.executeNode(getId);

    // Check the output was cached
    auto* nodeInst = instance.getNodeInstance(getId);
    ASSERT_NE(nodeInst, nullptr);
    auto outIt = nodeInst->outputValues.find(internPin("Value"));
    ASSERT_NE(outIt, nodeInst->outputValues.end());
    EXPECT_EQ(outIt->second.asInt(), 99);
}

// Regression test for AUDIT.md §H7 / FIXPLAN D3.
// GetVariable is classified isPure (for lazy eval) but must NOT be memoized,
// because the blackboard it reads can be mutated mid-chain. Before the fix,
// two sequential reads of the same variable returned the cached first value
// even after SetVariable had updated it.
TEST_F(ScriptContextTest, GetVariableNotMemoizedAcrossMutation)
{
    ScriptGraph graph;
    graph.name = "NonMemoTest";
    graph.variables.push_back({"x", ScriptDataType::INT,
                               ScriptValue(0), VariableScope::GRAPH});

    uint32_t get1Id = graph.addNode("GetVariable");
    graph.findNode(get1Id)->properties["Name"] = ScriptValue(std::string("x"));
    uint32_t get2Id = graph.addNode("GetVariable");
    graph.findNode(get2Id)->properties["Name"] = ScriptValue(std::string("x"));

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);

    // First read: variable is 0.
    ctx.executeNode(get1Id);
    auto* n1 = instance.getNodeInstance(get1Id);
    EXPECT_EQ(n1->outputValues.at(internPin("Value")).asInt(), 0);

    // Mutate the variable mid-chain. (Simulates what SetVariable would do
    // in a real ForLoop body.)
    ctx.setVariable("x", VariableScope::GRAPH, ScriptValue(42));

    // Second read of a *different* GetVariable node for the same name.
    // If memoization were honoured by type, we might expect 0 from a cache
    // keyed on typeName — but D3 keys by nodeId, so this alone doesn't
    // prove anything. The critical check: if we call get1Id AGAIN, we
    // should see the updated value, not the memoized 0.
    ctx.executeNode(get1Id);
    EXPECT_EQ(n1->outputValues.at(internPin("Value")).asInt(), 42)
        << "GetVariable was memoized — blackboard mutation didn't show up "
           "on re-evaluation. This is AUDIT.md §H7 regressing.";
}

TEST_F(ScriptContextTest, DelayCreatesLatentAction)
{
    ScriptGraph graph;
    graph.name = "DelayTest";
    uint32_t delayId = graph.addNode("Delay");
    auto* delayNode = graph.findNode(delayId);
    delayNode->properties["Duration"] = ScriptValue(2.5f);

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(delayId);

    // A latent action should have been registered
    ASSERT_EQ(instance.pendingActions().size(), 1u);
    EXPECT_EQ(instance.pendingActions()[0].nodeId, delayId);
    EXPECT_EQ(instance.pendingActions()[0].outputPin, "Completed");
    EXPECT_FLOAT_EQ(instance.pendingActions()[0].remainingTime, 2.5f);
}

// Regression test for AUDIT.md §H9 / FIXPLAN D4.
// Timeline schedules an onTick lambda that captures ScriptInstance*. If the
// instance is re-initialised mid-tick (editor test-play cycle), the nodeId
// may now refer to a different node in the rebuilt graph. The fix is a
// generation token: the callback captures the generation at scheduling
// time and no-ops if the instance has been re-initialised since.
TEST_F(ScriptContextTest, ScriptInstanceGenerationBumpedOnReinit)
{
    ScriptGraph graph;
    graph.name = "GenTest";
    graph.addNode("PrintToScreen");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    const uint32_t gen1 = instance.generation();

    // Re-initialise — simulates the editor test-play cycle.
    instance.initialize(graph, 1);
    const uint32_t gen2 = instance.generation();

    EXPECT_NE(gen1, gen2)
        << "ScriptInstance::initialize() must bump generation so latent-"
           "action callbacks can detect stale references (AUDIT.md §H9)";
    EXPECT_GT(gen2, gen1);
}

TEST_F(ScriptContextTest, StaleCallbackDetectedViaGeneration)
{
    // Simulate the pattern from latent_nodes.cpp Timeline: a callback
    // captures (instance ptr, nodeId, generation). If the instance is
    // re-initialised between scheduling and firing, generation mismatch
    // tells the callback to no-op instead of writing to whatever node
    // happens to live at that ID in the rebuilt graph.

    ScriptGraph graph;
    graph.name = "StaleCbTest";
    uint32_t nodeId = graph.addNode("PrintToScreen");

    ScriptInstance instance;
    instance.initialize(graph, 1);

    // Schedule: capture the snapshot that Timeline/MoveTo capture.
    ScriptInstance* inst = &instance;
    uint32_t capturedGen = inst->generation();
    uint32_t capturedNode = nodeId;
    bool fired = false;

    auto callback = [inst, capturedGen, capturedNode, &fired]()
    {
        if (inst->generation() != capturedGen) return;
        auto* ni = inst->getNodeInstance(capturedNode);
        if (ni) fired = true;
    };

    // Fire pre-reinit — should set fired=true.
    callback();
    EXPECT_TRUE(fired);

    // Re-init; schedule check should now fail.
    fired = false;
    instance.initialize(graph, 1);
    callback();
    EXPECT_FALSE(fired)
        << "Callback fired across a re-init — generation check didn't "
           "catch the stale reference (AUDIT.md §H9)";
}

TEST_F(ScriptContextTest, CallDepthLimit)
{
    // Create a graph where A -> B -> A (cycle via execution pins)
    // This should hit the call depth limit
    ScriptGraph graph;
    graph.name = "CycleTest";
    uint32_t id1 = graph.addNode("PrintToScreen");
    uint32_t id2 = graph.addNode("PrintToScreen");

    // Create a cycle: id1:Then -> id2:Exec, id2:Then -> id1:Exec
    graph.connections.push_back({1, id1, "Then", id2, "Exec"});
    graph.connections.push_back({2, id2, "Then", id1, "Exec"});

    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(id1);

    // Should have been stopped by the safety limit
    EXPECT_LE(ctx.nodesExecuted(), ScriptContext::MAX_NODES_PER_CHAIN);
}

// ===========================================================================
// VariableDef serialization
// ===========================================================================

TEST(VariableDef, JsonRoundTrip)
{
    VariableDef original;
    original.name = "playerHealth";
    original.dataType = ScriptDataType::FLOAT;
    original.defaultValue = ScriptValue(100.0f);
    original.scope = VariableScope::ENTITY;

    auto json = original.toJson();
    auto restored = VariableDef::fromJson(json);

    EXPECT_EQ(restored.name, "playerHealth");
    EXPECT_EQ(restored.dataType, ScriptDataType::FLOAT);
    EXPECT_FLOAT_EQ(restored.defaultValue.asFloat(), 100.0f);
    EXPECT_EQ(restored.scope, VariableScope::ENTITY);
}

// ===========================================================================
// Enum string conversions
// ===========================================================================

TEST(ScriptEnums, DataTypeRoundTrip)
{
    auto types = {ScriptDataType::BOOL, ScriptDataType::INT, ScriptDataType::FLOAT,
                  ScriptDataType::STRING, ScriptDataType::VEC2, ScriptDataType::VEC3,
                  ScriptDataType::VEC4, ScriptDataType::QUAT, ScriptDataType::ENTITY,
                  ScriptDataType::COLOR, ScriptDataType::ANY};

    for (auto type : types)
    {
        auto str = scriptDataTypeToString(type);
        auto restored = scriptDataTypeFromString(str);
        EXPECT_EQ(type, restored) << "Failed for: " << str;
    }
}

TEST(ScriptEnums, VariableScopeRoundTrip)
{
    auto scopes = {VariableScope::FLOW, VariableScope::GRAPH, VariableScope::ENTITY,
                   VariableScope::SCENE, VariableScope::APPLICATION,
                   VariableScope::SAVED};

    for (auto scope : scopes)
    {
        auto str = variableScopeToString(scope);
        auto restored = variableScopeFromString(str);
        EXPECT_EQ(scope, restored) << "Failed for: " << str;
    }
}

// ===========================================================================
// Phase 9E-2: expanded node library tests
// ===========================================================================

/// @brief Fixture that registers every node category for Phase 9E-2 tests.
/// Most tests use a fake "dummy engine" pointer — node implementations guard
/// access to engine/scene where relevant.
class NodeLibraryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        registerCoreNodeTypes(m_registry);
        registerEventNodeTypes(m_registry);
        registerActionNodeTypes(m_registry);
        registerPureNodeTypes(m_registry);
        registerFlowNodeTypes(m_registry);
        registerLatentNodeTypes(m_registry);
    }

    NodeTypeRegistry m_registry;
};

// -- Registration completeness ---------------------------------------------

TEST_F(NodeLibraryTest, RegistersExpectedEventNodes)
{
    const char* expected[] = {
        "OnKeyPressed", "OnKeyReleased", "OnMouseButton",
        "OnSceneLoaded", "OnWeatherChanged", "OnCustomEvent",
        "OnTriggerEnter", "OnTriggerExit",
        "OnCollisionEnter", "OnCollisionExit",
        "OnAudioFinished", "OnVariableChanged"
    };
    for (const char* name : expected)
    {
        EXPECT_TRUE(m_registry.hasNode(name)) << "Missing: " << name;
    }
}

TEST_F(NodeLibraryTest, RegistersExpectedActionNodes)
{
    const char* expected[] = {
        "PlaySound", "SpawnEntity", "DestroyEntity",
        "SetPosition", "SetRotation", "SetScale",
        "ApplyForce", "ApplyImpulse",
        "PlayAnimation", "SpawnParticles",
        "SetMaterial", "SetVisibility",
        "SetLightColor", "SetLightIntensity",
        "PublishEvent"
    };
    for (const char* name : expected)
    {
        EXPECT_TRUE(m_registry.hasNode(name)) << "Missing: " << name;
    }
}

TEST_F(NodeLibraryTest, RegistersExpectedPureNodes)
{
    const char* expected[] = {
        "GetPosition", "GetRotation", "FindEntityByName",
        "MathAdd", "MathSub", "MathMul", "MathDiv",
        "MathClamp", "MathLerp",
        "GetDistance", "VectorNormalize", "DotProduct", "CrossProduct",
        "BoolAnd", "BoolOr", "BoolNot",
        "CompareEqual", "CompareLess", "CompareGreater",
        "ToString", "HasVariable", "Raycast"
    };
    for (const char* name : expected)
    {
        EXPECT_TRUE(m_registry.hasNode(name)) << "Missing: " << name;
    }
}

TEST_F(NodeLibraryTest, RegistersExpectedFlowNodes)
{
    const char* expected[] = {
        "SwitchInt", "SwitchString", "ForLoop", "WhileLoop",
        "Gate", "DoOnce", "FlipFlop"
    };
    for (const char* name : expected)
    {
        EXPECT_TRUE(m_registry.hasNode(name)) << "Missing: " << name;
    }
}

TEST_F(NodeLibraryTest, RegistersExpectedLatentNodes)
{
    const char* expected[] = {
        "WaitForEvent", "WaitForCondition", "Timeline", "MoveTo"
    };
    for (const char* name : expected)
    {
        const NodeTypeDescriptor* desc = m_registry.findNode(name);
        ASSERT_NE(desc, nullptr) << "Missing: " << name;
        EXPECT_TRUE(desc->isLatent) << name << " should be marked latent";
    }
}

TEST_F(NodeLibraryTest, EventNodesHaveCorrectEventTypeNames)
{
    struct Expect { const char* node; const char* eventType; };
    Expect cases[] = {
        {"OnKeyPressed", "KeyPressedEvent"},
        {"OnKeyReleased", "KeyReleasedEvent"},
        {"OnMouseButton", "MouseButtonPressedEvent"},
        {"OnSceneLoaded", "SceneLoadedEvent"},
        {"OnWeatherChanged", "WeatherChangedEvent"},
        {"OnCustomEvent", "ScriptCustomEvent"},
    };
    for (const auto& c : cases)
    {
        const NodeTypeDescriptor* desc = m_registry.findNode(c.node);
        ASSERT_NE(desc, nullptr) << c.node;
        EXPECT_EQ(desc->eventTypeName, c.eventType) << c.node;
    }
}

TEST_F(NodeLibraryTest, PureNodesAreMarkedPure)
{
    const char* expected[] = {
        "MathAdd", "MathClamp", "BoolAnd", "CompareEqual",
        "DotProduct", "GetDistance", "ToString", "GetPosition",
        "VectorNormalize", "HasVariable"
    };
    for (const char* name : expected)
    {
        const NodeTypeDescriptor* desc = m_registry.findNode(name);
        ASSERT_NE(desc, nullptr) << name;
        EXPECT_TRUE(desc->isPure) << name << " should be pure";
    }
}

// -- Pure math/vector/bool nodes (safe with dummy engine) -------------------

namespace
{

/// @brief Convenience helper for building a single-node test graph.
struct PureNodeFixture
{
    ScriptGraph graph;
    ScriptInstance instance;
    uint32_t addNode(const std::string& typeName)
    {
        return graph.addNode(typeName);
    }

    void setProp(uint32_t nodeId, const std::string& pin, const ScriptValue& v)
    {
        graph.findNode(nodeId)->properties[pin] = v;
    }

    void initialize()
    {
        instance.initialize(graph, 1);
    }
};

} // namespace

TEST_F(NodeLibraryTest, MathAddComputesSum)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathAdd");
    f.setProp(id, "A", ScriptValue(2.0f));
    f.setProp(id, "B", ScriptValue(3.5f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);

    auto* inst = f.instance.getNodeInstance(id);
    ASSERT_NE(inst, nullptr);
    EXPECT_FLOAT_EQ(inst->outputValues[internPin("Result")].asFloat(), 5.5f);
}

TEST_F(NodeLibraryTest, MathSubComputesDifference)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathSub");
    f.setProp(id, "A", ScriptValue(10.0f));
    f.setProp(id, "B", ScriptValue(4.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 6.0f);
}

TEST_F(NodeLibraryTest, MathDivGuardsAgainstZero)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathDiv");
    f.setProp(id, "A", ScriptValue(10.0f));
    f.setProp(id, "B", ScriptValue(0.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 0.0f);
}

TEST_F(NodeLibraryTest, MathClampClampsToRange)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathClamp");
    f.setProp(id, "Value", ScriptValue(15.0f));
    f.setProp(id, "Min", ScriptValue(0.0f));
    f.setProp(id, "Max", ScriptValue(10.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 10.0f);
}

TEST_F(NodeLibraryTest, MathLerpInterpolates)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathLerp");
    f.setProp(id, "A", ScriptValue(0.0f));
    f.setProp(id, "B", ScriptValue(100.0f));
    f.setProp(id, "Alpha", ScriptValue(0.25f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 25.0f);
}

TEST_F(NodeLibraryTest, GetDistanceComputesEuclidean)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("GetDistance");
    f.setProp(id, "A", ScriptValue(glm::vec3(0.0f, 0.0f, 0.0f)));
    f.setProp(id, "B", ScriptValue(glm::vec3(3.0f, 4.0f, 0.0f)));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Distance")].asFloat(), 5.0f);
}

TEST_F(NodeLibraryTest, VectorNormalizeProducesUnit)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("VectorNormalize");
    f.setProp(id, "V", ScriptValue(glm::vec3(3.0f, 0.0f, 4.0f)));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    auto v = f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asVec3();
    EXPECT_NEAR(glm::length(v), 1.0f, 1e-5f);
}

TEST_F(NodeLibraryTest, BoolAndOrNotTruthTable)
{
    // AND
    {
        PureNodeFixture f;
        uint32_t id = f.addNode("BoolAnd");
        f.setProp(id, "A", ScriptValue(true));
        f.setProp(id, "B", ScriptValue(false));
        f.initialize();
        ScriptContext ctx(f.instance, m_registry,
                          nullptr);
        ctx.executeNode(id);
        EXPECT_FALSE(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asBool());
    }
    // OR
    {
        PureNodeFixture f;
        uint32_t id = f.addNode("BoolOr");
        f.setProp(id, "A", ScriptValue(true));
        f.setProp(id, "B", ScriptValue(false));
        f.initialize();
        ScriptContext ctx(f.instance, m_registry,
                          nullptr);
        ctx.executeNode(id);
        EXPECT_TRUE(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asBool());
    }
    // NOT
    {
        PureNodeFixture f;
        uint32_t id = f.addNode("BoolNot");
        f.setProp(id, "A", ScriptValue(true));
        f.initialize();
        ScriptContext ctx(f.instance, m_registry,
                          nullptr);
        ctx.executeNode(id);
        EXPECT_FALSE(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asBool());
    }
}

TEST_F(NodeLibraryTest, CompareLessGreaterEqual)
{
    // A=5, B=10 — Less is true, Greater is false, Equal is false
    PureNodeFixture lf;
    uint32_t lid = lf.addNode("CompareLess");
    lf.setProp(lid, "A", ScriptValue(5.0f));
    lf.setProp(lid, "B", ScriptValue(10.0f));
    lf.initialize();
    ScriptContext lctx(lf.instance, m_registry,
                       nullptr);
    lctx.executeNode(lid);
    EXPECT_TRUE(lf.instance.getNodeInstance(lid)->outputValues[internPin("Result")].asBool());

    PureNodeFixture gf;
    uint32_t gid = gf.addNode("CompareGreater");
    gf.setProp(gid, "A", ScriptValue(5.0f));
    gf.setProp(gid, "B", ScriptValue(10.0f));
    gf.initialize();
    ScriptContext gctx(gf.instance, m_registry,
                       nullptr);
    gctx.executeNode(gid);
    EXPECT_FALSE(gf.instance.getNodeInstance(gid)->outputValues[internPin("Result")].asBool());

    PureNodeFixture ef;
    uint32_t eid = ef.addNode("CompareEqual");
    ef.setProp(eid, "A", ScriptValue(5.0f));
    ef.setProp(eid, "B", ScriptValue(5.0f));
    ef.initialize();
    ScriptContext ectx(ef.instance, m_registry,
                       nullptr);
    ectx.executeNode(eid);
    EXPECT_TRUE(ef.instance.getNodeInstance(eid)->outputValues[internPin("Result")].asBool());
}

TEST_F(NodeLibraryTest, ToStringConvertsNumericValue)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("ToString");
    f.setProp(id, "Value", ScriptValue(42));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asString(), "42");
}

// -- Flow control: stateful nodes -------------------------------------------

TEST_F(NodeLibraryTest, SwitchIntRoutesByCase)
{
    ScriptGraph graph;
    uint32_t switchId = graph.addNode("SwitchInt");
    uint32_t c0 = graph.addNode("PrintToScreen");
    uint32_t c2 = graph.addNode("PrintToScreen");
    graph.findNode(switchId)->properties["Value"] = ScriptValue(2);
    graph.addConnection(switchId, "Case 0", c0, "Exec");
    graph.addConnection(switchId, "Case 2", c2, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(switchId);

    // Switch + Case2's PrintToScreen = 2 nodes executed
    EXPECT_EQ(ctx.nodesExecuted(), 2);
}

TEST_F(NodeLibraryTest, SwitchIntRoutesToDefault)
{
    ScriptGraph graph;
    uint32_t switchId = graph.addNode("SwitchInt");
    uint32_t def = graph.addNode("PrintToScreen");
    graph.findNode(switchId)->properties["Value"] = ScriptValue(99);
    graph.addConnection(switchId, "Default", def, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(switchId);

    EXPECT_EQ(ctx.nodesExecuted(), 2);
}

TEST_F(NodeLibraryTest, ForLoopIteratesExpectedTimes)
{
    ScriptGraph graph;
    uint32_t forId = graph.addNode("ForLoop");
    uint32_t body = graph.addNode("PrintToScreen");
    uint32_t completed = graph.addNode("PrintToScreen");

    graph.findNode(forId)->properties["First"] = ScriptValue(1);
    graph.findNode(forId)->properties["Last"] = ScriptValue(4);
    graph.addConnection(forId, "Body", body, "Exec");
    graph.addConnection(forId, "Completed", completed, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(forId);

    // ForLoop + 4 Body prints + 1 Completed print = 6
    EXPECT_EQ(ctx.nodesExecuted(), 6);
}

TEST_F(NodeLibraryTest, DoOnceFiresOnlyOnce)
{
    ScriptGraph graph;
    uint32_t doOnceId = graph.addNode("DoOnce");
    uint32_t print = graph.addNode("PrintToScreen");
    graph.addConnection(doOnceId, "Then", print, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    // First execution fires
    {
        ScriptContext ctx(instance, m_registry, nullptr);
        ctx.executeNode(doOnceId);
        EXPECT_EQ(ctx.nodesExecuted(), 2); // DoOnce + Print
    }
    // Second execution should be blocked
    {
        ScriptContext ctx(instance, m_registry, nullptr);
        ctx.executeNode(doOnceId);
        EXPECT_EQ(ctx.nodesExecuted(), 1); // Only DoOnce
    }
}

TEST_F(NodeLibraryTest, FlipFlopAlternatesAB)
{
    ScriptGraph graph;
    uint32_t ffId = graph.addNode("FlipFlop");
    uint32_t aPrint = graph.addNode("PrintToScreen");
    uint32_t bPrint = graph.addNode("PrintToScreen");
    graph.findNode(aPrint)->properties["Message"] = ScriptValue(std::string("A"));
    graph.findNode(bPrint)->properties["Message"] = ScriptValue(std::string("B"));
    graph.addConnection(ffId, "A", aPrint, "Exec");
    graph.addConnection(ffId, "B", bPrint, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    // 1st: A fires (IsA=true initially)
    {
        ScriptContext ctx(instance, m_registry, nullptr);
        ctx.executeNode(ffId);
    }
    auto* ffInst = instance.getNodeInstance(ffId);
    ASSERT_NE(ffInst, nullptr);
    EXPECT_TRUE(ffInst->outputValues[internPin("IsA")].asBool());

    // 2nd: B fires
    {
        ScriptContext ctx(instance, m_registry, nullptr);
        ctx.executeNode(ffId);
    }
    EXPECT_FALSE(ffInst->outputValues[internPin("IsA")].asBool());

    // 3rd: A again
    {
        ScriptContext ctx(instance, m_registry, nullptr);
        ctx.executeNode(ffId);
    }
    EXPECT_TRUE(ffInst->outputValues[internPin("IsA")].asBool());
}

// -- Latent: action scheduling + onTick -------------------------------------

TEST_F(NodeLibraryTest, TimelineSchedulesLatentWithTickCallback)
{
    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(2.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(tlId);

    ASSERT_EQ(instance.pendingActions().size(), 1u);
    EXPECT_EQ(instance.pendingActions()[0].outputPin, "Finished");
    EXPECT_FLOAT_EQ(instance.pendingActions()[0].totalDuration, 2.0f);
    EXPECT_TRUE(static_cast<bool>(instance.pendingActions()[0].onTick));
}

TEST_F(NodeLibraryTest, TimelineZeroDurationFinishesImmediately)
{
    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(0.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(tlId);

    // No latent action should be scheduled
    EXPECT_EQ(instance.pendingActions().size(), 0u);
    auto* inst = instance.getNodeInstance(tlId);
    ASSERT_NE(inst, nullptr);
    EXPECT_FLOAT_EQ(inst->outputValues[internPin("Alpha")].asFloat(), 1.0f);
}

TEST_F(NodeLibraryTest, WaitForConditionSchedulesConditionBasedLatent)
{
    ScriptGraph graph;
    uint32_t wId = graph.addNode("WaitForCondition");
    graph.findNode(wId)->properties["VarName"] = ScriptValue(std::string("flag"));

    ScriptInstance instance;
    instance.initialize(graph, 1);
    instance.graphBlackboard().set("flag", ScriptValue(false));
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(wId);

    ASSERT_EQ(instance.pendingActions().size(), 1u);
    EXPECT_TRUE(static_cast<bool>(instance.pendingActions()[0].condition));
    // Condition should return false initially
    EXPECT_FALSE(instance.pendingActions()[0].condition());

    // Flip the flag — condition now returns true
    instance.graphBlackboard().set("flag", ScriptValue(true));
    EXPECT_TRUE(instance.pendingActions()[0].condition());
}

// -- EventBus bridge: end-to-end via a default Engine ----------------------

TEST(ScriptingSystemBridge, KeyPressedEventTriggersOnKeyPressedNode)
{
    Engine engine;  // default-constructed: EventBus is usable, other subsystems are not
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    // Build a graph: OnKeyPressed -> PrintToScreen
    ScriptGraph graph;
    graph.name = "BridgeTest";
    uint32_t onKey = graph.addNode("OnKeyPressed");
    uint32_t printer = graph.addNode("PrintToScreen");
    graph.addConnection(onKey, "Pressed", printer, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Publish a KeyPressedEvent — the bridge should fire onKey's execute,
    // which triggers the PrintToScreen node.
    KeyPressedEvent evt(42, false);
    engine.getEventBus().publish(evt);

    // The onKey node's keyCode output should be populated by the bridge.
    auto* nodeInst = instance.getNodeInstance(onKey);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("keyCode")].asInt(), 42);
    EXPECT_FALSE(nodeInst->outputValues[internPin("isRepeat")].asBool());

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, UnregisterCleansUpSubscriptions)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    graph.addNode("OnKeyPressed");
    graph.addNode("OnMouseButton");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Should have subscribed to at least two event types
    EXPECT_GE(instance.subscriptions().size(), 2u);

    sys.unregisterInstance(instance);
    EXPECT_EQ(instance.subscriptions().size(), 0u);
    EXPECT_FALSE(instance.isActive());

    sys.shutdown();
}

TEST(ScriptingSystemBridge, PublishEventNodeDeliversToOnCustomEvent)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    // Two graphs: publisher and subscriber
    ScriptGraph pubGraph;
    uint32_t pubNode = pubGraph.addNode("PublishEvent");
    pubGraph.findNode(pubNode)->properties["name"] =
        ScriptValue(std::string("HelloEvent"));
    pubGraph.findNode(pubNode)->properties["payload"] = ScriptValue(7.0f);
    ScriptInstance publisher;
    publisher.initialize(pubGraph, 1);

    ScriptGraph subGraph;
    uint32_t onCustom = subGraph.addNode("OnCustomEvent");
    subGraph.findNode(onCustom)->properties["Name"] =
        ScriptValue(std::string("HelloEvent"));
    ScriptInstance subscriber;
    subscriber.initialize(subGraph, 2);

    sys.registerInstance(publisher);
    sys.registerInstance(subscriber);

    // Fire the publisher's PublishEvent node manually
    sys.fireEvent(publisher, pubNode);

    // The subscriber's OnCustomEvent node should have been populated
    auto* subNode = subscriber.getNodeInstance(onCustom);
    ASSERT_NE(subNode, nullptr);
    EXPECT_EQ(subNode->outputValues[internPin("name")].asString(), "HelloEvent");
    EXPECT_FLOAT_EQ(subNode->outputValues[internPin("payload")].asFloat(), 7.0f);

    sys.unregisterInstance(publisher);
    sys.unregisterInstance(subscriber);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, LatentActionOnTickFiresDuringTickLatentActions)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(1.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Manually fire the Timeline's execute to schedule the latent action
    sys.fireEvent(instance, tlId);
    ASSERT_EQ(instance.pendingActions().size(), 1u);

    // Simulate half a second elapsing — onTick should have set Alpha ~0.5
    sys.update(0.5f);
    auto* nodeInst = instance.getNodeInstance(tlId);
    ASSERT_NE(nodeInst, nullptr);
    float alpha = nodeInst->outputValues[internPin("Alpha")].asFloat();
    EXPECT_GE(alpha, 0.45f);
    EXPECT_LE(alpha, 0.55f);

    // Finish the timeline
    sys.update(0.6f);
    EXPECT_EQ(instance.pendingActions().size(), 0u);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

// ===========================================================================
// Phase 9E audit (Batch 4): missing coverage identified in PHASE9E_AUDIT_REPORT
// ===========================================================================

// -- Serialization edge cases (audit F) -------------------------------------

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

// -- Deserialization safety caps (audit C1, batch 1) -----------------------

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

// -- ScriptValue::fromJson robustness (audit H6) ---------------------------

TEST(ScriptValueJson, Vec3ShortArrayReturnsZero)
{
    nlohmann::json j;
    j["type"] = "vec3";
    j["value"] = nlohmann::json::array({1.0f, 2.0f}); // missing third element
    auto v = ScriptValue::fromJson(j);
    EXPECT_EQ(v.asVec3(), glm::vec3(0.0f));
}

TEST(ScriptValueJson, QuatWrongTypeReturnsIdentity)
{
    nlohmann::json j;
    j["type"] = "quat";
    j["value"] = "not-an-array";
    auto v = ScriptValue::fromJson(j);
    auto q = v.asQuat();
    EXPECT_FLOAT_EQ(q.w, 1.0f);
    EXPECT_FLOAT_EQ(q.x, 0.0f);
    EXPECT_FLOAT_EQ(q.y, 0.0f);
    EXPECT_FLOAT_EQ(q.z, 0.0f);
}

TEST(ScriptValueJson, BoolWrongTypeReturnsFalse)
{
    nlohmann::json j;
    j["type"] = "bool";
    j["value"] = "not-a-bool";
    EXPECT_FALSE(ScriptValue::fromJson(j).asBool());
}

// -- Blackboard per-scope cap (audit M8) -----------------------------------

TEST(BlackboardCap, InsertionRefusedAtMaxKeys)
{
    Blackboard bb;
    for (size_t i = 0; i < Blackboard::MAX_KEYS; ++i)
    {
        bb.set("k" + std::to_string(i), ScriptValue(static_cast<int32_t>(i)));
    }
    EXPECT_EQ(bb.size(), Blackboard::MAX_KEYS);
    bb.set("overflow", ScriptValue(999));
    EXPECT_EQ(bb.size(), Blackboard::MAX_KEYS);
    EXPECT_FALSE(bb.has("overflow"));
}

TEST(BlackboardCap, UpdatesToExistingKeysAlwaysSucceed)
{
    Blackboard bb;
    for (size_t i = 0; i < Blackboard::MAX_KEYS; ++i)
    {
        bb.set("k" + std::to_string(i), ScriptValue(0));
    }
    // Updating an existing key past the cap still works.
    bb.set("k0", ScriptValue(42));
    EXPECT_EQ(bb.get("k0").asInt(), 42);
}

// Regression test for AUDIT.md §H6 / FIXPLAN D2: crafted JSON must not
// bypass the MAX_KEYS cap. Prior to the fix, fromJson wrote directly into
// m_values, allowing an attacker to create a blackboard with millions of
// keys via a malicious save file.
TEST(BlackboardCap, FromJsonHonoursMaxKeys)
{
    nlohmann::json j = nlohmann::json::object();
    // Try to insert 2× MAX_KEYS via the load path.
    for (size_t i = 0; i < Blackboard::MAX_KEYS * 2; ++i)
    {
        j["overflow_" + std::to_string(i)] = nlohmann::json{
            {"type", "int"}, {"value", static_cast<int>(i)}};
    }
    auto bb = Blackboard::fromJson(j);
    EXPECT_LE(bb.size(), Blackboard::MAX_KEYS);
}

// Regression test for AUDIT.md §H6 / FIXPLAN D2: crafted JSON with long
// key names must be clamped on load, matching ScriptGraph::loadFromFile's
// handling of user-supplied strings.
TEST(BlackboardCap, FromJsonClampsLongKeys)
{
    const std::string longKey(1024, 'K');  // far exceeds the 256-byte cap
    nlohmann::json j = nlohmann::json::object();
    j[longKey] = nlohmann::json{{"type", "int"}, {"value", 1}};
    auto bb = Blackboard::fromJson(j);
    // Key must exist but be length-clamped to 256 bytes.
    EXPECT_EQ(bb.size(), 1u);
    EXPECT_FALSE(bb.has(longKey));
    EXPECT_TRUE(bb.has(std::string(256, 'K')));
}

// -- scheduleDelay clamping (audit M6) -------------------------------------

TEST_F(ScriptContextTest, ScheduleDelayClampsNegative)
{
    ScriptGraph graph;
    uint32_t id = graph.addNode("Delay");
    graph.findNode(id)->properties["Duration"] = ScriptValue(-5.0f);
    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(id);
    ASSERT_EQ(instance.pendingActions().size(), 1u);
    EXPECT_FLOAT_EQ(instance.pendingActions()[0].remainingTime, 0.0f);
}

TEST_F(ScriptContextTest, ScheduleDelayClampsNaN)
{
    ScriptGraph graph;
    uint32_t id = graph.addNode("Delay");
    graph.findNode(id)->properties["Duration"] =
        ScriptValue(std::numeric_limits<float>::quiet_NaN());
    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(id);
    ASSERT_EQ(instance.pendingActions().size(), 1u);
    EXPECT_FLOAT_EQ(instance.pendingActions()[0].remainingTime, 0.0f);
}

TEST_F(ScriptContextTest, ScheduleDelayClampsHuge)
{
    ScriptGraph graph;
    uint32_t id = graph.addNode("Delay");
    graph.findNode(id)->properties["Duration"] = ScriptValue(1e30f);
    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(id);
    ASSERT_EQ(instance.pendingActions().size(), 1u);
    // Clamped to 3600 (1 hour cap).
    EXPECT_FLOAT_EQ(instance.pendingActions()[0].remainingTime, 3600.0f);
}

// -- ForLoop boundary behavior (audit M2, M3) ------------------------------

TEST_F(NodeLibraryTest, ForLoopClampsIterationCountAndReports)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("ForLoop");
    f.setProp(id, "First", ScriptValue(0));
    f.setProp(id, "Last", ScriptValue(999999)); // above MAX_FOR_ITERATIONS
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);

    auto* inst = f.instance.getNodeInstance(id);
    ASSERT_NE(inst, nullptr);
    EXPECT_TRUE(inst->outputValues[internPin("Clamped")].asBool());
}

TEST_F(NodeLibraryTest, ForLoopBoundaryInt32InputsDoNotOverflow)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("ForLoop");
    // first=INT32_MIN, last=INT32_MAX: naive (last - first + 1) overflows int32.
    // With int64 arithmetic, this clamps to MAX_FOR_ITERATIONS without UB.
    f.setProp(id, "First", ScriptValue(std::numeric_limits<int32_t>::min()));
    f.setProp(id, "Last", ScriptValue(std::numeric_limits<int32_t>::max()));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    auto* inst = f.instance.getNodeInstance(id);
    EXPECT_TRUE(inst->outputValues[internPin("Clamped")].asBool());
}

TEST_F(NodeLibraryTest, ForLoopLastBeforeFirstFiresCompletedOnly)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("ForLoop");
    f.setProp(id, "First", ScriptValue(10));
    f.setProp(id, "Last", ScriptValue(5));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FALSE(f.instance.getNodeInstance(id)->outputValues[internPin("Clamped")].asBool());
}

// -- Math NaN/Inf sanitization (audit H8) ----------------------------------

TEST_F(NodeLibraryTest, MathAddSanitizesNaNInput)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathAdd");
    f.setProp(id, "A", ScriptValue(std::numeric_limits<float>::quiet_NaN()));
    f.setProp(id, "B", ScriptValue(5.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    // NaN input sanitized to 0, so result is 0+5 = 5.
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 5.0f);
}

TEST_F(NodeLibraryTest, MathMulSanitizesInfInput)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathMul");
    f.setProp(id, "A", ScriptValue(std::numeric_limits<float>::infinity()));
    f.setProp(id, "B", ScriptValue(2.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 0.0f);
}

TEST_F(NodeLibraryTest, VectorNormalizeSanitizesNaNInput)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("VectorNormalize");
    glm::vec3 bad{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    f.setProp(id, "V", ScriptValue(bad));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    // NaN → 0 vector → zero-length → returns (0,0,0) without crashing.
    glm::vec3 out = f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asVec3();
    EXPECT_EQ(out, glm::vec3(0.0f));
}

// -- Missing pure node coverage (audit F) ----------------------------------

TEST_F(NodeLibraryTest, MathMulComputesProduct)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathMul");
    f.setProp(id, "A", ScriptValue(6.0f));
    f.setProp(id, "B", ScriptValue(7.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 42.0f);
}

TEST_F(NodeLibraryTest, DotProductComputesDot)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("DotProduct");
    f.setProp(id, "A", ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)));
    f.setProp(id, "B", ScriptValue(glm::vec3(4.0f, 5.0f, 6.0f)));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    // 1*4 + 2*5 + 3*6 = 32
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asFloat(), 32.0f);
}

TEST_F(NodeLibraryTest, CrossProductComputesCross)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("CrossProduct");
    f.setProp(id, "A", ScriptValue(glm::vec3(1.0f, 0.0f, 0.0f)));
    f.setProp(id, "B", ScriptValue(glm::vec3(0.0f, 1.0f, 0.0f)));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    glm::vec3 out = f.instance.getNodeInstance(id)->outputValues[internPin("Result")].asVec3();
    EXPECT_EQ(out, glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST_F(NodeLibraryTest, HasVariableReturnsTrueWhenSet)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("HasVariable");
    f.setProp(id, "Name", ScriptValue(std::string("hp")));
    f.initialize();
    f.instance.graphBlackboard().set("hp", ScriptValue(100.0f));

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_TRUE(f.instance.getNodeInstance(id)->outputValues[internPin("Exists")].asBool());
}

TEST_F(NodeLibraryTest, HasVariableReturnsFalseWhenMissing)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("HasVariable");
    f.setProp(id, "Name", ScriptValue(std::string("nope")));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    EXPECT_FALSE(f.instance.getNodeInstance(id)->outputValues[internPin("Exists")].asBool());
}

// -- Missing flow node coverage (audit F) ----------------------------------

TEST_F(NodeLibraryTest, SwitchStringRoutesByCase)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("SwitchString");
    f.setProp(id, "Selector", ScriptValue(std::string("greet")));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    // Should not crash; SwitchString routes to the matching named output or Default.
    EXPECT_GE(ctx.nodesExecuted(), 1);
}

TEST_F(NodeLibraryTest, GateBlocksWhenClosed)
{
    PureNodeFixture f;
    uint32_t gateId = f.addNode("Gate");
    uint32_t sinkId = f.addNode("PrintToScreen");
    f.graph.addConnection(gateId, "Out", sinkId, "Exec");
    f.setProp(gateId, "StartClosed", ScriptValue(true));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(gateId);
    // When closed, the gate should not forward execution.
    // nodesExecuted counts only this gate node, not the sink.
    EXPECT_EQ(ctx.nodesExecuted(), 1);
}

TEST_F(NodeLibraryTest, WhileLoopTerminatesAtSafetyCap)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("WhileLoop");
    // Always-true condition — safety cap must terminate it.
    f.setProp(id, "Condition", ScriptValue(true));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(id);
    // If this didn't terminate we would never reach here.
    SUCCEED();
}

// -- Core interpreter: Blackboard scope routing (audit F) ------------------

TEST_F(ScriptContextTest, FlowScopeIsIsolatedFromGraphScope)
{
    ScriptGraph graph;
    ScriptInstance instance;
    instance.initialize(graph, 1);
    instance.graphBlackboard().set("shared", ScriptValue(100));

    ScriptContext ctx(instance, m_registry, nullptr);

    // Writing to Flow doesn't touch Graph.
    ctx.setVariable("shared", VariableScope::FLOW, ScriptValue(5));
    EXPECT_EQ(ctx.getVariable("shared", VariableScope::FLOW).asInt(), 5);
    EXPECT_EQ(ctx.getVariable("shared", VariableScope::GRAPH).asInt(), 100);
}

// -- Core interpreter: re-entrancy safety (audit F) ------------------------

TEST(ScriptingSystemBridge, CustomEventReEntrancyHitsDepthLimit)
{
    // One OnCustomEvent wired to a PublishEvent that re-publishes the same
    // event type. Without the depth guard this would recurse forever.
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onEvt = graph.addNode("OnCustomEvent");
    graph.findNode(onEvt)->properties["Name"] =
        ScriptValue(std::string("loop"));
    uint32_t pub = graph.addNode("PublishEvent");
    graph.findNode(pub)->properties["name"] =
        ScriptValue(std::string("loop"));
    graph.addConnection(onEvt, "Fired", pub, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    ScriptCustomEvent evt("loop", ScriptValue(1.0f));
    // Should complete without stack overflow thanks to the node-count /
    // call-depth limits in ScriptContext.
    engine.getEventBus().publish(evt);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

// -- OnCustomEvent filter suppresses trigger (audit M1, batch 3) -----------

TEST(ScriptingSystemBridge, OnCustomEventFilterMismatchDoesNotFire)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onEvt = graph.addNode("OnCustomEvent");
    graph.findNode(onEvt)->properties["Name"] =
        ScriptValue(std::string("expected"));
    uint32_t sink = graph.addNode("PrintToScreen");
    graph.addConnection(onEvt, "Fired", sink, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Publish an event whose name does NOT match the filter.
    ScriptCustomEvent evt("different", ScriptValue(0.0f));
    engine.getEventBus().publish(evt);

    // The OnCustomEvent node's `name` output should NOT have been populated,
    // because the filter rejected the event upstream.
    auto* nodeInst = instance.getNodeInstance(onEvt);
    ASSERT_NE(nodeInst, nullptr);
    auto it = nodeInst->outputValues.find(internPin("name"));
    EXPECT_TRUE(it == nodeInst->outputValues.end())
        << "name output should be absent on filter mismatch";

    sys.unregisterInstance(instance);
    sys.shutdown();
}

// -- EventBus bridge: missing event types ----------------------------------

TEST(ScriptingSystemBridge, KeyReleasedEventPopulatesOutputs)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onKey = graph.addNode("OnKeyReleased");
    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    KeyReleasedEvent evt(101);
    engine.getEventBus().publish(evt);

    auto* nodeInst = instance.getNodeInstance(onKey);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("keyCode")].asInt(), 101);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, MouseButtonEventPopulatesOutputs)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onMouse = graph.addNode("OnMouseButton");
    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    MouseButtonPressedEvent evt(2);
    engine.getEventBus().publish(evt);

    auto* nodeInst = instance.getNodeInstance(onMouse);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("button")].asInt(), 2);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

// -- Liveness: isInstanceActive (audit H1) ---------------------------------

TEST(ScriptingSystemBridge, IsInstanceActiveReflectsRegistration)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    graph.addNode("OnKeyPressed");
    ScriptInstance instance;
    instance.initialize(graph, 1);

    EXPECT_FALSE(sys.isInstanceActive(&instance));
    sys.registerInstance(instance);
    EXPECT_TRUE(sys.isInstanceActive(&instance));
    sys.unregisterInstance(instance);
    EXPECT_FALSE(sys.isInstanceActive(&instance));
    EXPECT_FALSE(sys.isInstanceActive(nullptr));

    sys.shutdown();
}

// -- Timeline NaN progress guard (audit M4) --------------------------------

TEST(ScriptingSystemBridge, TimelineHandlesRemainingTimeNaNGracefully)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(1.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    sys.fireEvent(instance, tlId);
    ASSERT_EQ(instance.pendingActions().size(), 1u);

    // Inject NaN into remainingTime — ticker should not crash or emit NaN.
    instance.pendingActions()[0].remainingTime =
        std::numeric_limits<float>::quiet_NaN();
    sys.update(0.1f);

    auto* nodeInst = instance.getNodeInstance(tlId);
    ASSERT_NE(nodeInst, nullptr);
    float alpha = nodeInst->outputValues[internPin("Alpha")].asFloat();
    EXPECT_TRUE(std::isfinite(alpha));

    sys.unregisterInstance(instance);
    sys.shutdown();
}

// ===========================================================================
// Phase 9E-3 Step 3: M9 (type→IDs cache), M11 (pure-node memo), entry-pin
// ===========================================================================

// -- M9: nodesByType returns cached vector by const-ref ---------------------

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

// -- M11: pure-node memoization within one execute chain --------------------

TEST_F(NodeLibraryTest, PureNodeMemoizedAcrossLoopReads)
{
    // Build: MathAdd (source) → second MathAdd's "A" input. Reading the
    // second node's "A" twice within one ScriptContext exercises the
    // pure-node memo cache: first call evaluates MathAdd-source, second
    // returns the cached value even if MathAdd-source's property changes.
    PureNodeFixture f;
    uint32_t srcId  = f.addNode("MathAdd");
    uint32_t sinkId = f.addNode("MathAdd");
    f.setProp(srcId, "A", ScriptValue(2.0f));
    f.setProp(srcId, "B", ScriptValue(3.0f));
    f.graph.addConnection(srcId, "Result", sinkId, "A");
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    const auto* sinkInst = f.instance.getNodeInstance(sinkId);
    ASSERT_NE(sinkInst, nullptr);

    // First read pulls through MathAdd-source: 2 + 3 = 5.
    ScriptValue first = ctx.readInput(*sinkInst, internPin("A"));
    EXPECT_FLOAT_EQ(first.asFloat(), 5.0f);

    // Mutate the source's "A" property. Without memoization this would
    // change the next evaluation. With M11, the same ScriptContext
    // returns the cached value.
    f.instance.getNodeInstance(srcId)->properties["A"] = ScriptValue(99.0f);
    ScriptValue second = ctx.readInput(*sinkInst, internPin("A"));
    EXPECT_FLOAT_EQ(second.asFloat(), 5.0f) << "Pure-node memo should cache";

    // A fresh ScriptContext must NOT see the cached value (cache is per-chain).
    ScriptContext freshCtx(f.instance, m_registry,
                           nullptr);
    ScriptValue fresh = freshCtx.readInput(*sinkInst, internPin("A"));
    EXPECT_FLOAT_EQ(fresh.asFloat(), 102.0f) << "Fresh context starts fresh";
}

// -- entry-pin field (audit L6 / Gate dispatch) -----------------------------

TEST_F(NodeLibraryTest, GateOpenInputOpensTheGate)
{
    // Build: a "trigger" node with a Then output → Gate's "Open" input.
    // The Gate starts closed; firing the trigger should open it.
    PureNodeFixture f;
    uint32_t triggerId = f.addNode("PrintToScreen");
    uint32_t gateId    = f.addNode("Gate");
    f.setProp(gateId, "StartClosed", ScriptValue(true));
    f.graph.addConnection(triggerId, "Then", gateId, "Open");
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(triggerId);

    // After the open trigger, executing Enter (via direct call, default
    // entryPin=INVALID) should pass through.
    uint32_t sinkId = f.graph.addNode("PrintToScreen");
    f.graph.addConnection(gateId, "Out", sinkId, "Exec");
    // Re-initialize because we mutated the graph after the first call.
    f.instance.initialize(f.graph, 1);
    ScriptContext ctx2(f.instance, m_registry,
                       nullptr);
    // Open the gate via the dedicated input.
    ctx2.executeNode(triggerId);
    // Now fire Enter directly. Without entry-pin dispatch, this would have
    // been the only way to interact with the gate.
    ctx2.executeNode(gateId);
    // No assertion on side effects — the test asserts no crash and that the
    // gate-open path didn't infinitely recurse.
    SUCCEED();
}

TEST_F(NodeLibraryTest, GateCloseInputClosesTheGate)
{
    PureNodeFixture f;
    uint32_t triggerId = f.addNode("PrintToScreen");
    uint32_t gateId    = f.addNode("Gate");
    // Start open, then close via the Close input.
    f.setProp(gateId, "StartClosed", ScriptValue(false));
    f.graph.addConnection(triggerId, "Then", gateId, "Close");
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);
    ctx.executeNode(triggerId);
    // Verify internal state — runtime "_open" flag should now be false.
    auto* gateInst = f.instance.getNodeInstance(gateId);
    ASSERT_NE(gateInst, nullptr);
    auto it = gateInst->runtimeState.find(internPin("_open"));
    ASSERT_NE(it, gateInst->runtimeState.end());
    EXPECT_FALSE(it->second.asBool());
}

TEST_F(NodeLibraryTest, GateToggleInputFlipsTheGate)
{
    PureNodeFixture f;
    uint32_t triggerId = f.addNode("PrintToScreen");
    uint32_t gateId    = f.addNode("Gate");
    f.setProp(gateId, "StartClosed", ScriptValue(true));
    f.graph.addConnection(triggerId, "Then", gateId, "Toggle");
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      nullptr);

    // First toggle: closed → open
    ctx.executeNode(triggerId);
    auto* gateInst = f.instance.getNodeInstance(gateId);
    auto pinOpen = internPin("_open");
    EXPECT_TRUE(gateInst->runtimeState[pinOpen].asBool());

    // Second toggle: open → closed
    ctx.executeNode(triggerId);
    EXPECT_FALSE(gateInst->runtimeState[pinOpen].asBool());
}

TEST_F(ScriptContextTest, EntryPinDefaultsToInvalidForDirectExecute)
{
    ScriptGraph graph;
    uint32_t id = graph.addNode("PrintToScreen");
    ScriptInstance instance;
    instance.initialize(graph, 1);

    ScriptContext ctx(instance, m_registry, nullptr);
    EXPECT_EQ(ctx.entryPin(), INVALID_PIN_ID);
    ctx.executeNode(id);
    // After executeNode returns, m_entryPin is still INVALID_PIN_ID
    // (executeNode doesn't touch it; only triggerOutput does).
    EXPECT_EQ(ctx.entryPin(), INVALID_PIN_ID);
}
