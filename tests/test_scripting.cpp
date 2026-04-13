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
    // We need a real Engine, but for this test we create a dummy
    // Since PrintToScreen only calls Logger, it will work with any Engine*
    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));
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

    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));
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

    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));
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

    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));
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

    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));

    // Manually execute the pure node
    ctx.executeNode(getId);

    // Check the output was cached
    auto* nodeInst = instance.getNodeInstance(getId);
    ASSERT_NE(nodeInst, nullptr);
    auto outIt = nodeInst->outputValues.find("Value");
    ASSERT_NE(outIt, nodeInst->outputValues.end());
    EXPECT_EQ(outIt->second.asInt(), 99);
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

    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));
    ctx.executeNode(delayId);

    // A latent action should have been registered
    ASSERT_EQ(instance.pendingActions().size(), 1u);
    EXPECT_EQ(instance.pendingActions()[0].nodeId, delayId);
    EXPECT_EQ(instance.pendingActions()[0].outputPin, "Completed");
    EXPECT_FLOAT_EQ(instance.pendingActions()[0].remainingTime, 2.5f);
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

    Engine* dummyEngine = nullptr;
    ScriptContext ctx(instance, m_registry,
                      *reinterpret_cast<Engine*>(&dummyEngine));
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
    Engine* dummyEngine = nullptr;

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
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);

    auto* inst = f.instance.getNodeInstance(id);
    ASSERT_NE(inst, nullptr);
    EXPECT_FLOAT_EQ(inst->outputValues["Result"].asFloat(), 5.5f);
}

TEST_F(NodeLibraryTest, MathSubComputesDifference)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathSub");
    f.setProp(id, "A", ScriptValue(10.0f));
    f.setProp(id, "B", ScriptValue(4.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues["Result"].asFloat(), 6.0f);
}

TEST_F(NodeLibraryTest, MathDivGuardsAgainstZero)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("MathDiv");
    f.setProp(id, "A", ScriptValue(10.0f));
    f.setProp(id, "B", ScriptValue(0.0f));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues["Result"].asFloat(), 0.0f);
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
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues["Result"].asFloat(), 10.0f);
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
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues["Result"].asFloat(), 25.0f);
}

TEST_F(NodeLibraryTest, GetDistanceComputesEuclidean)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("GetDistance");
    f.setProp(id, "A", ScriptValue(glm::vec3(0.0f, 0.0f, 0.0f)));
    f.setProp(id, "B", ScriptValue(glm::vec3(3.0f, 4.0f, 0.0f)));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    EXPECT_FLOAT_EQ(f.instance.getNodeInstance(id)->outputValues["Distance"].asFloat(), 5.0f);
}

TEST_F(NodeLibraryTest, VectorNormalizeProducesUnit)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("VectorNormalize");
    f.setProp(id, "V", ScriptValue(glm::vec3(3.0f, 0.0f, 4.0f)));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    auto v = f.instance.getNodeInstance(id)->outputValues["Result"].asVec3();
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
                          *reinterpret_cast<Engine*>(&f.dummyEngine));
        ctx.executeNode(id);
        EXPECT_FALSE(f.instance.getNodeInstance(id)->outputValues["Result"].asBool());
    }
    // OR
    {
        PureNodeFixture f;
        uint32_t id = f.addNode("BoolOr");
        f.setProp(id, "A", ScriptValue(true));
        f.setProp(id, "B", ScriptValue(false));
        f.initialize();
        ScriptContext ctx(f.instance, m_registry,
                          *reinterpret_cast<Engine*>(&f.dummyEngine));
        ctx.executeNode(id);
        EXPECT_TRUE(f.instance.getNodeInstance(id)->outputValues["Result"].asBool());
    }
    // NOT
    {
        PureNodeFixture f;
        uint32_t id = f.addNode("BoolNot");
        f.setProp(id, "A", ScriptValue(true));
        f.initialize();
        ScriptContext ctx(f.instance, m_registry,
                          *reinterpret_cast<Engine*>(&f.dummyEngine));
        ctx.executeNode(id);
        EXPECT_FALSE(f.instance.getNodeInstance(id)->outputValues["Result"].asBool());
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
                       *reinterpret_cast<Engine*>(&lf.dummyEngine));
    lctx.executeNode(lid);
    EXPECT_TRUE(lf.instance.getNodeInstance(lid)->outputValues["Result"].asBool());

    PureNodeFixture gf;
    uint32_t gid = gf.addNode("CompareGreater");
    gf.setProp(gid, "A", ScriptValue(5.0f));
    gf.setProp(gid, "B", ScriptValue(10.0f));
    gf.initialize();
    ScriptContext gctx(gf.instance, m_registry,
                       *reinterpret_cast<Engine*>(&gf.dummyEngine));
    gctx.executeNode(gid);
    EXPECT_FALSE(gf.instance.getNodeInstance(gid)->outputValues["Result"].asBool());

    PureNodeFixture ef;
    uint32_t eid = ef.addNode("CompareEqual");
    ef.setProp(eid, "A", ScriptValue(5.0f));
    ef.setProp(eid, "B", ScriptValue(5.0f));
    ef.initialize();
    ScriptContext ectx(ef.instance, m_registry,
                       *reinterpret_cast<Engine*>(&ef.dummyEngine));
    ectx.executeNode(eid);
    EXPECT_TRUE(ef.instance.getNodeInstance(eid)->outputValues["Result"].asBool());
}

TEST_F(NodeLibraryTest, ToStringConvertsNumericValue)
{
    PureNodeFixture f;
    uint32_t id = f.addNode("ToString");
    f.setProp(id, "Value", ScriptValue(42));
    f.initialize();

    ScriptContext ctx(f.instance, m_registry,
                      *reinterpret_cast<Engine*>(&f.dummyEngine));
    ctx.executeNode(id);
    EXPECT_EQ(f.instance.getNodeInstance(id)->outputValues["Result"].asString(), "42");
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

    Engine* dummy = nullptr;
    ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
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

    Engine* dummy = nullptr;
    ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
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

    Engine* dummy = nullptr;
    ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
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

    Engine* dummy = nullptr;
    // First execution fires
    {
        ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
        ctx.executeNode(doOnceId);
        EXPECT_EQ(ctx.nodesExecuted(), 2); // DoOnce + Print
    }
    // Second execution should be blocked
    {
        ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
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

    Engine* dummy = nullptr;
    // 1st: A fires (IsA=true initially)
    {
        ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
        ctx.executeNode(ffId);
    }
    auto* ffInst = instance.getNodeInstance(ffId);
    ASSERT_NE(ffInst, nullptr);
    EXPECT_TRUE(ffInst->outputValues["IsA"].asBool());

    // 2nd: B fires
    {
        ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
        ctx.executeNode(ffId);
    }
    EXPECT_FALSE(ffInst->outputValues["IsA"].asBool());

    // 3rd: A again
    {
        ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
        ctx.executeNode(ffId);
    }
    EXPECT_TRUE(ffInst->outputValues["IsA"].asBool());
}

// -- Latent: action scheduling + onTick -------------------------------------

TEST_F(NodeLibraryTest, TimelineSchedulesLatentWithTickCallback)
{
    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(2.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);

    Engine* dummy = nullptr;
    ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
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

    Engine* dummy = nullptr;
    ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
    ctx.executeNode(tlId);

    // No latent action should be scheduled
    EXPECT_EQ(instance.pendingActions().size(), 0u);
    auto* inst = instance.getNodeInstance(tlId);
    ASSERT_NE(inst, nullptr);
    EXPECT_FLOAT_EQ(inst->outputValues["Alpha"].asFloat(), 1.0f);
}

TEST_F(NodeLibraryTest, WaitForConditionSchedulesConditionBasedLatent)
{
    ScriptGraph graph;
    uint32_t wId = graph.addNode("WaitForCondition");
    graph.findNode(wId)->properties["VarName"] = ScriptValue(std::string("flag"));

    ScriptInstance instance;
    instance.initialize(graph, 1);
    instance.graphBlackboard().set("flag", ScriptValue(false));

    Engine* dummy = nullptr;
    ScriptContext ctx(instance, m_registry, *reinterpret_cast<Engine*>(&dummy));
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
    EXPECT_EQ(nodeInst->outputValues["keyCode"].asInt(), 42);
    EXPECT_FALSE(nodeInst->outputValues["isRepeat"].asBool());

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
    EXPECT_EQ(subNode->outputValues["name"].asString(), "HelloEvent");
    EXPECT_FLOAT_EQ(subNode->outputValues["payload"].asFloat(), 7.0f);

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
    float alpha = nodeInst->outputValues["Alpha"].asFloat();
    EXPECT_GE(alpha, 0.45f);
    EXPECT_LE(alpha, 0.55f);

    // Finish the timeline
    sys.update(0.6f);
    EXPECT_EQ(instance.pendingActions().size(), 0u);

    sys.unregisterInstance(instance);
    sys.shutdown();
}
