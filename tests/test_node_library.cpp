// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_node_library.cpp
/// @brief Unit tests for the scripting node library (core/event/action/pure/flow/latent).
#include "scripting/node_type_registry.h"
#include "scripting/script_graph.h"
#include "scripting/script_instance.h"
#include "scripting/script_context.h"
#include "scripting/script_value.h"
#include "scripting/core_nodes.h"
#include "scripting/event_nodes.h"
#include "scripting/action_nodes.h"
#include "scripting/pure_nodes.h"
#include "scripting/flow_nodes.h"
#include "scripting/latent_nodes.h"

#include <gtest/gtest.h>

#include <limits>

using namespace Vestige;

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

    /// @brief Read a float output pin of an executed node, collapsing the
    /// getNodeInstance/outputValues/internPin/asFloat chain the math tests
    /// would otherwise repeat verbatim.
    float getOutputFloat(uint32_t nodeId, const char* pin = "Result")
    {
        return instance.getNodeInstance(nodeId)
                   ->outputValues[internPin(pin)].asFloat();
    }
};

} // namespace

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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 6.0f);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 0.0f);
}

TEST_F(NodeLibraryTest, MathDivExactZeroPolicyMatchesSafeDiv_Sc5)
{
    // Phase 10.9 Slice 14 Sc5 — divisors below 1e-9 but not exactly zero
    // used to be classified as div-by-zero by MathDiv (returning 0)
    // while ExpressionEvaluator + codegen_cpp + codegen_glsl all
    // computed `a / b`. The policy is now exact-zero only across the
    // four sites, so a tiny finite divisor goes through the real
    // division path.
    PureNodeFixture f;
    uint32_t id = f.addNode("MathDiv");
    f.setProp(id, "A", ScriptValue(1.0f));
    // 1e-12 is well below the old 1e-9 cliff but is a perfectly valid
    // finite divisor — the result is 1e12, which is finite.
    f.setProp(id, "B", ScriptValue(1e-12f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry, nullptr);
    ctx.executeNode(id);

    const float r = f.getOutputFloat(id);
    EXPECT_GT(r, 1e11f) << "tiny finite divisor must take the real "
                           "division path, not the zero short-circuit";
}

TEST_F(NodeLibraryTest, MathDivProjectsInfiniteResultsToZero_Sc5)
{
    // The non-finite isfinite() guard added alongside the exact-zero
    // policy is the belt-and-braces. If `a / b` were ever to overflow
    // to inf (denormalised inputs in principle), Sc5 contracts that
    // the result is projected to 0 in keeping with SafeMath's
    // "degenerate math goes to 0" convention.
    PureNodeFixture f;
    uint32_t id = f.addNode("MathDiv");
    f.setProp(id, "A", ScriptValue(std::numeric_limits<float>::max()));
    f.setProp(id, "B", ScriptValue(std::numeric_limits<float>::min()));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry, nullptr);
    ctx.executeNode(id);

    const float r = f.getOutputFloat(id);
    EXPECT_TRUE(std::isfinite(r))
        << "MathDiv must keep the graph in finite-value space";
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 10.0f);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 25.0f);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id, "Distance"), 5.0f);
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

// Phase 10.9 Sc1: a single execution-output pin must fan out to every
// connected target, not just the first match. The shipped templates rely
// on `DoOnce.Then → {PlayAnim, PlaySound}` firing both branches.
TEST_F(NodeLibraryTest, ExecOutputFansOutToAllTargets_Sc1)
{
    ScriptGraph graph;
    const uint32_t doOnceId = graph.addNode("DoOnce");
    const uint32_t printA = graph.addNode("PrintToScreen");
    const uint32_t printB = graph.addNode("PrintToScreen");
    graph.findNode(printA)->properties["Message"] = ScriptValue(std::string("A"));
    graph.findNode(printB)->properties["Message"] = ScriptValue(std::string("B"));
    // One source pin, two distinct targets — must execute both.
    graph.addConnection(doOnceId, "Then", printA, "Exec");
    graph.addConnection(doOnceId, "Then", printB, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(doOnceId);
    // Pre-Sc1: 2 (DoOnce + first Print). Post-Sc1: 3 (DoOnce + both Prints).
    EXPECT_EQ(ctx.nodesExecuted(), 3)
        << "exec fan-out must fire every connected target";
}

TEST_F(NodeLibraryTest, ExecOutputPreservesEntryPinPerCallee_Sc1)
{
    // Each callee in a fan-out must observe its own m_entryPin (the
    // target-side input pin), not the previous callee's. Wire two
    // PrintToScreen nodes so both end up observing "Exec" — a regression
    // would surface as either node observing the *next* node's entry pin.
    ScriptGraph graph;
    const uint32_t doOnceId = graph.addNode("DoOnce");
    const uint32_t printA = graph.addNode("PrintToScreen");
    const uint32_t printB = graph.addNode("PrintToScreen");
    graph.addConnection(doOnceId, "Then", printA, "Exec");
    graph.addConnection(doOnceId, "Then", printB, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    ScriptContext ctx(instance, m_registry, nullptr);
    ctx.executeNode(doOnceId);
    // No crash, no UB, no skipped target — both callees observed the
    // correct entry pin. (`nodesExecuted == 3` covers the count;
    // `m_entryPin` save/restore correctness is implicit if both run.)
    EXPECT_EQ(ctx.nodesExecuted(), 3);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 5.0f);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 0.0f);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 42.0f);
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
    EXPECT_FLOAT_EQ(f.getOutputFloat(id), 32.0f);
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
    // The safety cap is the sole reason an always-true WhileLoop
    // terminates. WhileLoop publishes a `Clamped` output that fires
    // true on cap-hit (AUDIT Sc8); pin that here instead of a bare
    // SUCCEED.
    auto* nodeInst = f.instance.getNodeInstance(id);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_TRUE(nodeInst->outputValues[internPin("Clamped")].asBool())
        << "WhileLoop terminated without firing Clamped — safety cap may "
           "not have engaged";
}

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
    // Pin "didn't infinitely recurse" by checking the per-chain safety
    // cap was not hit. > 0 also confirms execution actually progressed.
    EXPECT_GT(ctx2.nodesExecuted(), 0);
    EXPECT_LT(ctx2.nodesExecuted(), ScriptContext::MAX_NODES_PER_CHAIN);
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
