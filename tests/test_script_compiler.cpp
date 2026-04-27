// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_compiler.cpp
/// @brief Unit tests for ScriptGraphCompiler (Phase 9E graph compilation).
#include "scripting/script_compiler.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_graph.h"
#include "scripting/script_templates.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace Vestige;

// Forward declarations of the free registration functions — re-registering a
// full node palette from scratch keeps the tests independent of ScriptingSystem.
namespace Vestige {
void registerCoreNodeTypes(NodeTypeRegistry& registry);
void registerEventNodeTypes(NodeTypeRegistry& registry);
void registerActionNodeTypes(NodeTypeRegistry& registry);
void registerPureNodeTypes(NodeTypeRegistry& registry);
void registerFlowNodeTypes(NodeTypeRegistry& registry);
void registerLatentNodeTypes(NodeTypeRegistry& registry);
}

namespace
{

/// @brief Populate a registry with the full shipped palette so compile runs
/// against the same types a real project would.
NodeTypeRegistry makeFullRegistry()
{
    NodeTypeRegistry reg;
    registerCoreNodeTypes(reg);
    registerEventNodeTypes(reg);
    registerActionNodeTypes(reg);
    registerPureNodeTypes(reg);
    registerFlowNodeTypes(reg);
    registerLatentNodeTypes(reg);
    return reg;
}

/// @brief Return true if `diags` contains at least one ERROR whose message
/// matches `needle` (substring). Keeps assertions readable without pinning
/// tests to exact error phrasing.
bool hasErrorContaining(const std::vector<CompileDiagnostic>& diags,
                        const std::string& needle)
{
    return std::any_of(diags.begin(), diags.end(),
                       [&](const CompileDiagnostic& d) {
                           return d.severity == CompileSeverity::ERROR &&
                                  d.message.find(needle) != std::string::npos;
                       });
}

bool hasWarningContaining(const std::vector<CompileDiagnostic>& diags,
                          const std::string& needle)
{
    return std::any_of(diags.begin(), diags.end(),
                       [&](const CompileDiagnostic& d) {
                           return d.severity == CompileSeverity::WARNING &&
                                  d.message.find(needle) != std::string::npos;
                       });
}

} // namespace

// ===========================================================================
// Happy path — existing shipped templates
// ===========================================================================

TEST(ScriptCompiler, CompilesEveryShippedTemplate)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    for (int i = 0;
         i < static_cast<int>(GameplayTemplate::DIALOGUE_TRIGGER) + 1;
         ++i)
    {
        const auto gt = static_cast<GameplayTemplate>(i);
        const ScriptGraph graph = buildGameplayTemplate(gt);

        const CompilationResult result =
            ScriptGraphCompiler::compile(graph, registry);

        ASSERT_TRUE(result.success)
            << "template " << gameplayTemplateDisplayName(gt)
            << " failed to compile; first diagnostic: "
            << (result.diagnostics.empty()
                    ? "<none>"
                    : result.diagnostics.front().message);
        EXPECT_EQ(result.compiled.nodes.size(), graph.nodes.size());
        EXPECT_FALSE(result.compiled.entryPointIndices.empty())
            << "template " << gameplayTemplateDisplayName(gt)
            << " should have at least one entry point";
    }
}

TEST(ScriptCompiler, EmptyGraphCompilesWithEntryPointWarning)
{
    const NodeTypeRegistry registry = makeFullRegistry();
    ScriptGraph empty;
    empty.name = "empty";

    const CompilationResult result =
        ScriptGraphCompiler::compile(empty, registry);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(hasWarningContaining(result.diagnostics, "no entry points"));
    EXPECT_TRUE(result.compiled.nodes.empty());
    EXPECT_TRUE(result.compiled.entryPointIndices.empty());
}

// ===========================================================================
// Error cases — validation pass coverage
// ===========================================================================

TEST(ScriptCompiler, UnknownNodeTypeIsError)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    graph.name = "bogus";
    graph.addNode("ThisNodeTypeDoesNotExist");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics, "unknown node type"));
}

TEST(ScriptCompiler, DuplicateNodeIdIsError)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    graph.name = "dup";
    graph.addNode("OnStart");
    // Manually append a second node with the same id — bypasses addNode()'s
    // id allocator, which is exactly the corruption case validate catches.
    ScriptNodeDef dup;
    dup.id = 1;
    dup.typeName = "OnUpdate";
    graph.nodes.push_back(dup);

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics, "Duplicate node id"));
}

TEST(ScriptCompiler, DanglingConnectionSourceIsError)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    graph.name = "dangling";
    const uint32_t tgt = graph.addNode("LogMessage");
    ScriptConnection c;
    c.id = 1;
    c.sourceNode = 99999;        // missing
    c.sourcePin = "Started";
    c.targetNode = tgt;
    c.targetPin = "Exec";
    graph.connections.push_back(c);

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics,
                                   "missing source node"));
}

TEST(ScriptCompiler, UnknownPinNameIsError)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    graph.name = "badpin";
    const uint32_t src = graph.addNode("OnStart");
    const uint32_t tgt = graph.addNode("LogMessage");
    graph.addConnection(src, "NoSuchPin", tgt, "Exec");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics,
                                   "source pin 'NoSuchPin'"));
}

TEST(ScriptCompiler, PinKindMismatchIsError)
{
    NodeTypeRegistry registry;
    // Hand-rolled minimal registry — lets us wire an exec output to a data
    // input deterministically without wrestling the full palette.
    NodeTypeDescriptor producer;
    producer.typeName = "ExecSource";
    producer.category = "Test";
    PinDef execOut;
    execOut.name = "Out";
    execOut.kind = PinKind::EXECUTION;
    producer.outputDefs = { execOut };
    registry.registerNode(producer);

    NodeTypeDescriptor consumer;
    consumer.typeName = "DataSink";
    consumer.category = "Test";
    PinDef dataIn;
    dataIn.name = "Value";
    dataIn.kind = PinKind::DATA;
    dataIn.dataType = ScriptDataType::FLOAT;
    consumer.inputDefs = { dataIn };
    registry.registerNode(consumer);

    ScriptGraph graph;
    const uint32_t a = graph.addNode("ExecSource");
    const uint32_t b = graph.addNode("DataSink");
    graph.addConnection(a, "Out", b, "Value");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics, "Pin kind mismatch"));
}

TEST(ScriptCompiler, PinDataTypeMismatchIsError)
{
    NodeTypeRegistry registry;
    NodeTypeDescriptor producer;
    producer.typeName = "Vec3Source";
    producer.category = "Test";
    PinDef vout;
    vout.name = "Out";
    vout.kind = PinKind::DATA;
    vout.dataType = ScriptDataType::VEC3;
    producer.outputDefs = { vout };
    registry.registerNode(producer);

    NodeTypeDescriptor consumer;
    consumer.typeName = "BoolSink";
    consumer.category = "Test";
    PinDef bin;
    bin.name = "Flag";
    bin.kind = PinKind::DATA;
    bin.dataType = ScriptDataType::BOOL;
    consumer.inputDefs = { bin };
    registry.registerNode(consumer);

    ScriptGraph graph;
    const uint32_t a = graph.addNode("Vec3Source");
    const uint32_t b = graph.addNode("BoolSink");
    graph.addConnection(a, "Out", b, "Flag");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics, "Pin type mismatch"));
}

TEST(ScriptCompiler, DuplicateInputConnectionIsError)
{
    NodeTypeRegistry registry;
    NodeTypeDescriptor producer;
    producer.typeName = "FloatSource";
    PinDef outDef;
    outDef.name = "Out";
    outDef.kind = PinKind::DATA;
    outDef.dataType = ScriptDataType::FLOAT;
    producer.outputDefs = { outDef };
    registry.registerNode(producer);

    NodeTypeDescriptor consumer;
    consumer.typeName = "FloatSink";
    PinDef inDef;
    inDef.name = "Value";
    inDef.kind = PinKind::DATA;
    inDef.dataType = ScriptDataType::FLOAT;
    consumer.inputDefs = { inDef };
    registry.registerNode(consumer);

    ScriptGraph graph;
    const uint32_t a = graph.addNode("FloatSource");
    const uint32_t b = graph.addNode("FloatSource");
    const uint32_t sink = graph.addNode("FloatSink");
    // ScriptGraph::addConnection rejects duplicate input connections at the
    // editor boundary, so we bypass it to forge the invalid state — this
    // represents a corrupted on-disk graph slipping past fromJson() or a
    // malformed scripted editor action. The compiler is the safety net.
    ScriptConnection c1;
    c1.id = 1;
    c1.sourceNode = a;
    c1.sourcePin = "Out";
    c1.targetNode = sink;
    c1.targetPin = "Value";
    ScriptConnection c2;
    c2.id = 2;
    c2.sourceNode = b;
    c2.sourcePin = "Out";
    c2.targetNode = sink;
    c2.targetPin = "Value";
    graph.connections.push_back(c1);
    graph.connections.push_back(c2);

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(
        result.diagnostics, "already has an incoming connection"));
}

TEST(ScriptCompiler, ExecutionOutputFanOutIsAllowed)
{
    // Shipped gameplay templates (buildDoorTemplate, etc.) connect a single
    // exec output to multiple targets — the compiler must accept this
    // pattern. The runtime's triggerOutput() fires only the first match
    // today, which is a runtime fidelity gap tracked separately.
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    const uint32_t start = graph.addNode("OnStart");
    const uint32_t log1 = graph.addNode("LogMessage");
    const uint32_t log2 = graph.addNode("LogMessage");
    graph.addConnection(start, "Started", log1, "Exec");
    graph.addConnection(start, "Started", log2, "Exec");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_TRUE(result.success)
        << "expected exec fan-out to compile cleanly; first diagnostic: "
        << (result.diagnostics.empty() ? "<none>"
                                        : result.diagnostics.front().message);

    // Both targets should land on the compiled output's target lists.
    const auto startIdx = result.compiled.indexForNodeId(start);
    ASSERT_NE(startIdx, std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(result.compiled.nodes[startIdx]
                  .outputs[0]
                  .targetNodeIndices.size(),
              2u);
}

// ===========================================================================
// Cycle detection
// ===========================================================================

TEST(ScriptCompiler, PureDataCycleIsError)
{
    NodeTypeRegistry registry;
    // Two pure nodes whose output feeds the other's input — the classic
    // infinite-recursion trap the interpreter would otherwise hit on first
    // evaluation.
    NodeTypeDescriptor node;
    node.typeName = "PassThrough";
    node.isPure = true;
    PinDef inDef;
    inDef.name = "In";
    inDef.kind = PinKind::DATA;
    inDef.dataType = ScriptDataType::FLOAT;
    PinDef outDef;
    outDef.name = "Out";
    outDef.kind = PinKind::DATA;
    outDef.dataType = ScriptDataType::FLOAT;
    node.inputDefs = { inDef };
    node.outputDefs = { outDef };
    registry.registerNode(node);

    ScriptGraph graph;
    const uint32_t a = graph.addNode("PassThrough");
    const uint32_t b = graph.addNode("PassThrough");
    graph.addConnection(a, "Out", b, "In");
    graph.addConnection(b, "Out", a, "In");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(hasErrorContaining(result.diagnostics,
                                   "Cycle detected in pure-data flow"));
}

// Phase 10.9 Sc7: passDetectDataCycles must use an explicit-stack DFS
// (matching the in-code comment) instead of recursive lambda invocation.
// A 10k-node pure-chain graph would have blown the C++ stack on the
// previous std::function-based recursion; the iterative version handles
// it bounded only by heap.
TEST(ScriptCompiler, DeepPureChainNoStackOverflow_Sc7)
{
    NodeTypeRegistry registry;
    NodeTypeDescriptor node;
    node.typeName = "PassThrough";
    node.isPure = true;
    PinDef inDef;
    inDef.name = "In";
    inDef.kind = PinKind::DATA;
    inDef.dataType = ScriptDataType::FLOAT;
    PinDef outDef;
    outDef.name = "Out";
    outDef.kind = PinKind::DATA;
    outDef.dataType = ScriptDataType::FLOAT;
    node.inputDefs = { inDef };
    node.outputDefs = { outDef };
    registry.registerNode(node);

    ScriptGraph graph;
    constexpr int kChainLength = 10000;
    std::vector<uint32_t> ids;
    ids.reserve(kChainLength);
    for (int i = 0; i < kChainLength; ++i)
    {
        ids.push_back(graph.addNode("PassThrough"));
    }
    for (int i = 1; i < kChainLength; ++i)
    {
        // Each node's input is fed by its predecessor's output — a long
        // acyclic pure chain. Compilation must succeed without recursing.
        graph.addConnection(ids[i - 1], "Out", ids[i], "In");
    }

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_FALSE(hasErrorContaining(result.diagnostics,
                                    "Cycle detected in pure-data flow"))
        << "10k-node acyclic pure chain misclassified as a cycle";
}

TEST(ScriptCompiler, ExecutionCycleIsAllowed)
{
    // Execution cycles are intentional — loops, re-triggers, retries. The
    // compiler must NOT flag them.
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    const uint32_t start = graph.addNode("OnStart");
    const uint32_t branch = graph.addNode("Branch");
    graph.addConnection(start, "Started", branch, "Exec");
    // True output loops back into Branch's own input — pathological but
    // legal from a compile-time perspective; the runtime's MAX_CALL_DEPTH
    // catches it. (Branch's Exec input already has one connection from
    // OnStart, so the duplicate-input rule will fire here too — that's a
    // separate, correct error; we only care that the cycle *itself* isn't
    // classified as a pure-data cycle.)
    graph.addConnection(branch, "True", branch, "Exec");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    // Accept success OR a non-ERROR set of diagnostics (e.g., Branch input
    // fan-in rule may still fire). The cycle itself must NOT be an error.
    EXPECT_FALSE(hasErrorContaining(result.diagnostics,
                                    "Cycle detected in pure-data flow"));
}

// ===========================================================================
// Entry points + reachability
// ===========================================================================

TEST(ScriptCompiler, DiscoversOnUpdateAsEntryPoint)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    const uint32_t u = graph.addNode("OnUpdate");
    (void)u;

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.compiled.entryPointIndices.size(), 1u);
    EXPECT_EQ(result.compiled.updateNodeIndices.size(), 1u);
}

TEST(ScriptCompiler, UnreachableImpureNodeWarns)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    // OnStart -> LogMessage ... plus a floating LogMessage with no incoming
    // execution edge.
    const uint32_t start = graph.addNode("OnStart");
    const uint32_t connected = graph.addNode("LogMessage");
    graph.addConnection(start, "Started", connected, "Exec");
    graph.addNode("LogMessage");  // island

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(hasWarningContaining(result.diagnostics, "is unreachable"));
}

// ===========================================================================
// Type compatibility rules
// ===========================================================================

TEST(ScriptCompiler, TypeCompatibilityMatrix)
{
    // Same-type and ANY-wildcard accept everything.
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::FLOAT, ScriptDataType::FLOAT));
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::VEC3, ScriptDataType::ANY));
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::ANY, ScriptDataType::VEC3));

    // Numeric widening — mirrors ScriptValue coercions.
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::INT, ScriptDataType::FLOAT));
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::BOOL, ScriptDataType::INT));
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::ENTITY, ScriptDataType::INT));

    // COLOR <-> VEC4 should round-trip.
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::COLOR, ScriptDataType::VEC4));
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::VEC4, ScriptDataType::COLOR));

    // Everything flows to STRING via ScriptValue::asString().
    EXPECT_TRUE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::VEC3, ScriptDataType::STRING));

    // Narrowing / unrelated types are rejected.
    EXPECT_FALSE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::FLOAT, ScriptDataType::INT));
    EXPECT_FALSE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::VEC3, ScriptDataType::BOOL));
    EXPECT_FALSE(ScriptGraphCompiler::areTypesCompatible(
        ScriptDataType::QUAT, ScriptDataType::FLOAT));
}

// ===========================================================================
// Compiled graph layout
// ===========================================================================

TEST(ScriptCompiler, ResolvedWiringMatchesConnections)
{
    const NodeTypeRegistry registry = makeFullRegistry();

    ScriptGraph graph;
    const uint32_t start = graph.addNode("OnStart");
    const uint32_t log = graph.addNode("LogMessage");
    graph.addConnection(start, "Started", log, "Exec");

    const CompilationResult result =
        ScriptGraphCompiler::compile(graph, registry);
    ASSERT_TRUE(result.success);

    const std::size_t startIdx = result.compiled.indexForNodeId(start);
    const std::size_t logIdx = result.compiled.indexForNodeId(log);
    ASSERT_NE(startIdx, std::numeric_limits<std::size_t>::max());
    ASSERT_NE(logIdx, std::numeric_limits<std::size_t>::max());

    // LogMessage's "In" should resolve to (startIdx, 0) — OnStart has one
    // output pin "Start" at index 0.
    const auto& logNode = result.compiled.nodes[logIdx];
    ASSERT_FALSE(logNode.inputs.empty());
    EXPECT_TRUE(logNode.inputs[0].connected);
    EXPECT_EQ(logNode.inputs[0].sourceNodeIndex, startIdx);
    EXPECT_EQ(logNode.inputs[0].sourceOutputPinIndex, 0u);

    // OnStart's "Start" should fan out to (logIdx, 0).
    const auto& startNode = result.compiled.nodes[startIdx];
    ASSERT_FALSE(startNode.outputs.empty());
    ASSERT_EQ(startNode.outputs[0].targetNodeIndices.size(), 1u);
    EXPECT_EQ(startNode.outputs[0].targetNodeIndices[0], logIdx);
    EXPECT_EQ(startNode.outputs[0].targetInputPinIndices[0], 0u);
}
