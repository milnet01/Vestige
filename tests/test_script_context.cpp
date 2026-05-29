// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_context.cpp
/// @brief Unit tests for ScriptContext node execution and latent actions.
#include "scripting/script_context.h"
#include "scripting/script_graph.h"
#include "scripting/script_instance.h"
#include "scripting/node_type_registry.h"
#include "scripting/core_nodes.h"
#include "scripting/script_value.h"

#include <gtest/gtest.h>

#include <limits>

using namespace Vestige;

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
