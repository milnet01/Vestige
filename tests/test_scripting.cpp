/// @file test_scripting.cpp
/// @brief Unit tests for visual scripting infrastructure (Phase 9E-1).
#include "scripting/script_value.h"
#include "scripting/blackboard.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_graph.h"
#include "scripting/script_instance.h"
#include "scripting/script_context.h"
#include "scripting/core_nodes.h"

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
