// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_compiler.cpp
/// @brief ScriptGraphCompiler — ScriptGraph → CompiledScriptGraph passes.
#include "scripting/script_compiler.h"

#include "scripting/node_type_registry.h"
#include "scripting/script_common.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Vestige
{

// ---------------------------------------------------------------------------
// CompiledScriptGraph helpers
// ---------------------------------------------------------------------------

std::size_t CompiledScriptGraph::indexForNodeId(uint32_t nodeId) const
{
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].nodeId == nodeId)
        {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

// ---------------------------------------------------------------------------
// CompilationResult helpers
// ---------------------------------------------------------------------------

std::size_t CompilationResult::countAt(CompileSeverity severity) const
{
    return static_cast<std::size_t>(
        std::count_if(diagnostics.begin(), diagnostics.end(),
                      [severity](const CompileDiagnostic& d) {
                          return d.severity == severity;
                      }));
}

bool CompilationResult::hasIssues() const
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const CompileDiagnostic& d) {
                           return d.severity == CompileSeverity::WARNING ||
                                  d.severity == CompileSeverity::ERROR;
                       });
}

// ---------------------------------------------------------------------------
// Compilation passes (file-local)
// ---------------------------------------------------------------------------

namespace
{

/// @brief Append a diagnostic of the given severity to the result.
void addDiag(CompilationResult& result, CompileSeverity severity,
             std::string message, uint32_t nodeId = 0,
             uint32_t connectionId = 0)
{
    CompileDiagnostic diag;
    diag.severity = severity;
    diag.message = std::move(message);
    diag.nodeId = nodeId;
    diag.connectionId = connectionId;
    result.diagnostics.push_back(std::move(diag));
}

/// @brief Locate a pin by name within a list; SIZE_MAX if not found.
std::size_t indexOfPin(const std::vector<PinDef>& pins, const std::string& name)
{
    for (std::size_t i = 0; i < pins.size(); ++i)
    {
        if (pins[i].name == name)
        {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();
}

/// @brief Pass 1 — resolve node descriptors, check uniqueness.
///
/// Populates `result.compiled.nodes` with one entry per ScriptNodeDef in the
/// input graph. A node whose type is not registered still gets an entry (so
/// later-pass diagnostics can reference it by index), but with a null
/// descriptor and an ERROR diagnostic.
void passResolveNodes(const ScriptGraph& graph,
                      const NodeTypeRegistry& registry,
                      CompilationResult& result,
                      std::unordered_map<uint32_t, std::size_t>& idToIndex)
{
    std::unordered_set<uint32_t> seenIds;
    result.compiled.nodes.reserve(graph.nodes.size());

    for (const auto& nodeDef : graph.nodes)
    {
        if (!seenIds.insert(nodeDef.id).second)
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Duplicate node id " + std::to_string(nodeDef.id),
                    nodeDef.id);
            // Skip duplicates: indexing one of them still keeps the pass 3
            // connection checks meaningful; counting both would double-report.
            continue;
        }

        CompiledNode node;
        node.nodeId = nodeDef.id;
        node.descriptor = registry.findNode(nodeDef.typeName);
        if (!node.descriptor)
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Node " + std::to_string(nodeDef.id) +
                        " references unknown node type '" +
                        nodeDef.typeName + "'",
                    nodeDef.id);
        }
        else
        {
            node.inputs.resize(node.descriptor->inputDefs.size());
            node.outputs.resize(node.descriptor->outputDefs.size());

            // An event node has no execution inputs and at least one
            // execution output — the canonical shape of a graph root. We
            // also accept `category == "Events"` so stubs with an empty
            // eventTypeName (OnTriggerEnter, OnCollisionEnter pending
            // EventBus wiring) still count as entry points — the gameplay
            // templates depend on that.
            const bool isEvent = !node.descriptor->eventTypeName.empty();
            const bool isEventsCategory =
                node.descriptor->category == "Events";
            const bool isExplicitEntry =
                nodeDef.typeName == "OnStart" ||
                nodeDef.typeName == "OnUpdate";
            node.isEntryPoint =
                isEvent || isEventsCategory || isExplicitEntry;
        }

        idToIndex[nodeDef.id] = result.compiled.nodes.size();
        result.compiled.nodes.push_back(std::move(node));
    }
}

/// @brief Pass 3/4/5/6 — resolve and validate every connection.
///
/// Walks every connection once and, where the endpoints resolve cleanly,
/// installs the resolved wiring into the corresponding CompiledInputPin /
/// CompiledOutputPin. Records ERROR diagnostics for every failure so the
/// editor can surface them all rather than stopping at the first broken
/// connection.
void passResolveConnections(
    const ScriptGraph& graph,
    CompilationResult& result,
    const std::unordered_map<uint32_t, std::size_t>& idToIndex)
{
    // Tracks which input pins have already been connected — each input pin
    // may be the target of at most one connection (pass 6 rule).
    //
    // Execution outputs MAY fan out (one source pin → many target nodes);
    // the shipped templates rely on that and the runtime's
    // ScriptContext::triggerOutput now fires every match (Phase 10.9 Sc1).
    std::unordered_set<uint64_t> inputPinsConnected;

    auto packKey = [](std::size_t nodeIndex, std::size_t pinIndex) {
        return (static_cast<uint64_t>(nodeIndex) << 32) |
               static_cast<uint64_t>(pinIndex);
    };

    for (const auto& conn : graph.connections)
    {
        auto srcIt = idToIndex.find(conn.sourceNode);
        auto tgtIt = idToIndex.find(conn.targetNode);
        if (srcIt == idToIndex.end())
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Connection references missing source node " +
                        std::to_string(conn.sourceNode),
                    conn.sourceNode, conn.id);
            continue;
        }
        if (tgtIt == idToIndex.end())
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Connection references missing target node " +
                        std::to_string(conn.targetNode),
                    conn.targetNode, conn.id);
            continue;
        }

        CompiledNode& src = result.compiled.nodes[srcIt->second];
        CompiledNode& tgt = result.compiled.nodes[tgtIt->second];
        if (!src.descriptor || !tgt.descriptor)
        {
            // Endpoint type failed resolution in pass 1 — pin lookup would
            // spuriously fail. The ERROR for the missing type is already
            // recorded; suppress cascade noise here.
            continue;
        }

        const std::size_t srcPinIdx =
            indexOfPin(src.descriptor->outputDefs, conn.sourcePin);
        if (srcPinIdx == std::numeric_limits<std::size_t>::max())
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Connection source pin '" + conn.sourcePin +
                        "' not found on node type '" +
                        src.descriptor->typeName + "'",
                    conn.sourceNode, conn.id);
            continue;
        }

        const std::size_t tgtPinIdx =
            indexOfPin(tgt.descriptor->inputDefs, conn.targetPin);
        if (tgtPinIdx == std::numeric_limits<std::size_t>::max())
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Connection target pin '" + conn.targetPin +
                        "' not found on node type '" +
                        tgt.descriptor->typeName + "'",
                    conn.targetNode, conn.id);
            continue;
        }

        const PinDef& srcPin = src.descriptor->outputDefs[srcPinIdx];
        const PinDef& tgtPin = tgt.descriptor->inputDefs[tgtPinIdx];

        if (srcPin.kind != tgtPin.kind)
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Pin kind mismatch: source '" + conn.sourcePin +
                        "' is " + (srcPin.kind == PinKind::EXECUTION
                                       ? "execution"
                                       : "data") +
                        ", target '" + conn.targetPin + "' is " +
                        (tgtPin.kind == PinKind::EXECUTION
                             ? "execution"
                             : "data"),
                    conn.targetNode, conn.id);
            continue;
        }

        if (srcPin.kind == PinKind::DATA &&
            !ScriptGraphCompiler::areTypesCompatible(srcPin.dataType,
                                                     tgtPin.dataType))
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Pin type mismatch on connection " +
                        std::to_string(conn.id) +
                        ": source '" + conn.sourcePin +
                        "' and target '" + conn.targetPin +
                        "' have incompatible data types",
                    conn.targetNode, conn.id);
            continue;
        }

        // Pass 6 — target input fan-in ≤ 1.
        const uint64_t inputKey = packKey(tgtIt->second, tgtPinIdx);
        if (!inputPinsConnected.insert(inputKey).second)
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Input pin '" + conn.targetPin +
                        "' on node " + std::to_string(conn.targetNode) +
                        " already has an incoming connection — "
                        "each input accepts at most one",
                    conn.targetNode, conn.id);
            continue;
        }

        // All checks passed — install the wiring.
        CompiledInputPin& inputPin = tgt.inputs[tgtPinIdx];
        inputPin.connected = true;
        inputPin.sourceNodeIndex = srcIt->second;
        inputPin.sourceOutputPinIndex = srcPinIdx;
        inputPin.connectionId = conn.id;

        CompiledOutputPin& outputPin = src.outputs[srcPinIdx];
        outputPin.targetNodeIndices.push_back(tgtIt->second);
        outputPin.targetInputPinIndices.push_back(tgtPinIdx);
        outputPin.connectionIds.push_back(conn.id);
    }
}

/// @brief Pass 8 — detect cycles in pure-data edges.
///
/// Walks every compiled node's pure-input chain, refusing recursive self-
/// dependencies. Execution cycles are intentionally ignored: Branch/Sequence/
/// loops legitimately create execution-pin cycles and that's what makes the
/// WhileLoop node work. Pure-data cycles on the other hand produce infinite
/// recursion when the interpreter tries to evaluate the first output.
void passDetectDataCycles(CompilationResult& result)
{
    const auto& nodes = result.compiled.nodes;
    enum class Mark : uint8_t { UNVISITED = 0, IN_PROGRESS, DONE };
    std::vector<Mark> marks(nodes.size(), Mark::UNVISITED);

    // Iterative DFS with an explicit stack — Phase 10.9 Sc7. The previous
    // implementation was a `std::function` recursive lambda whose comment
    // promised explicit stacking. A 5000-node pure chain blew the C++ call
    // stack; the explicit stack here handles arbitrary depths bounded only
    // by available heap. Each frame stores (node_index, next-input cursor)
    // so a frame can resume scanning inputs after a child descent returns.
    struct Frame { std::size_t idx; std::size_t cursor; };
    std::vector<Frame> stack;
    std::vector<std::size_t> cycleStart;

    for (std::size_t root = 0; root < nodes.size(); ++root)
    {
        if (marks[root] != Mark::UNVISITED)
        {
            continue;
        }

        marks[root] = Mark::IN_PROGRESS;
        stack.push_back({root, 0});
        bool cycleFound = false;

        while (!stack.empty())
        {
            // Read the back frame's fields by index — never hold a reference
            // across `stack.push_back` since the vector may reallocate.
            const std::size_t idx    = stack.back().idx;
            std::size_t       cursor = stack.back().cursor;

            const CompiledNode& n = nodes[idx];
            bool descended = false;

            // Only pure nodes contribute to the data-cycle check — impure
            // nodes break the cycle because their outputs are cached and
            // read lazily, not recomputed on each pull.
            if (n.descriptor && n.descriptor->isPure)
            {
                while (cursor < n.inputs.size())
                {
                    const std::size_t inputIdx = cursor++;
                    if (!n.inputs[inputIdx].connected)
                    {
                        continue;
                    }
                    const PinDef& inDef = n.descriptor->inputDefs[inputIdx];
                    if (inDef.kind != PinKind::DATA)
                    {
                        continue;
                    }
                    const std::size_t tgt = n.inputs[inputIdx].sourceNodeIndex;
                    if (marks[tgt] == Mark::DONE)
                    {
                        continue;
                    }
                    if (marks[tgt] == Mark::IN_PROGRESS)
                    {
                        cycleStart.push_back(tgt);
                        cycleFound = true;
                        break;
                    }
                    // Descend: persist the advanced cursor on the parent
                    // frame so the resume point is correct, then push the
                    // child.
                    stack.back().cursor = cursor;
                    marks[tgt] = Mark::IN_PROGRESS;
                    stack.push_back({tgt, 0});
                    descended = true;
                    break;
                }
            }

            if (cycleFound)
            {
                break;
            }
            if (!descended)
            {
                // All inputs exhausted (or non-pure node) — finalise and pop.
                marks[idx] = Mark::DONE;
                stack.pop_back();
            }
        }

        if (cycleFound)
        {
            addDiag(result, CompileSeverity::ERROR,
                    "Cycle detected in pure-data flow involving node " +
                        std::to_string(nodes[cycleStart.back()].nodeId),
                    nodes[cycleStart.back()].nodeId);
            // Reset so we surface at most one cycle per connected component
            // instead of spamming the same cycle from every entry.
            cycleStart.clear();
            stack.clear();
            std::fill(marks.begin(), marks.end(), Mark::DONE);
            return;
        }
    }
}

/// @brief Pass 10 — reachability classification.
///
/// BFS from every entry point over execution edges. Nodes touched by the
/// traversal are marked reachable. Pure data producers that feed a reachable
/// impure node are also treated as reachable (so constants wired into
/// OnKeyPressed → SetVariable don't spuriously warn).
void passReachability(CompilationResult& result,
                      const ScriptGraph& /*unused*/)
{
    auto& nodes = result.compiled.nodes;
    std::vector<std::size_t> frontier;

    // Seed BFS with every entry point and collect the indices in the
    // compiled graph's entry-point list while we're at it.
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].isEntryPoint)
        {
            nodes[i].isReachable = true;
            frontier.push_back(i);
            result.compiled.entryPointIndices.push_back(i);
            if (nodes[i].descriptor &&
                nodes[i].descriptor->typeName == "OnUpdate")
            {
                result.compiled.updateNodeIndices.push_back(i);
            }
        }
    }

    // BFS over execution-output edges. Data edges are handled in the second
    // pass below so we don't falsely mark a pure-only subgraph reachable.
    while (!frontier.empty())
    {
        const std::size_t idx = frontier.back();
        frontier.pop_back();
        const CompiledNode& n = nodes[idx];
        if (!n.descriptor)
        {
            continue;
        }
        for (std::size_t p = 0; p < n.outputs.size(); ++p)
        {
            if (n.descriptor->outputDefs[p].kind != PinKind::EXECUTION)
            {
                continue;
            }
            for (std::size_t tgt : n.outputs[p].targetNodeIndices)
            {
                if (!nodes[tgt].isReachable)
                {
                    nodes[tgt].isReachable = true;
                    frontier.push_back(tgt);
                }
            }
        }
    }

    // Second sweep — any pure node that has a reachable impure consumer is
    // transitively reachable. Iterate until stable; the graph is small so a
    // fixpoint loop is cheaper than building a reverse-data-edge index.
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].isReachable || !nodes[i].descriptor)
            {
                continue;
            }
            if (!nodes[i].descriptor->isPure)
            {
                continue;
            }
            // Scan this pure node's outputs — if any target is reachable, we
            // become reachable too.
            bool feedsReachable = false;
            for (const auto& outPin : nodes[i].outputs)
            {
                for (std::size_t tgt : outPin.targetNodeIndices)
                {
                    if (nodes[tgt].isReachable)
                    {
                        feedsReachable = true;
                        break;
                    }
                }
                if (feedsReachable)
                {
                    break;
                }
            }
            if (feedsReachable)
            {
                nodes[i].isReachable = true;
                changed = true;
            }
        }
    }

    // Warnings: any impure node that's unreachable is dead logic. Pure
    // constants / library helpers with no consumer are often deliberate, so
    // we only warn for impure ones to avoid noise on template graphs.
    for (const auto& n : nodes)
    {
        if (!n.descriptor || n.isReachable)
        {
            continue;
        }
        if (!n.descriptor->isPure)
        {
            addDiag(result, CompileSeverity::WARNING,
                    "Node " + std::to_string(n.nodeId) + " ('" +
                        n.descriptor->typeName +
                        "') is unreachable — no execution path from any "
                        "entry point leads here",
                    n.nodeId);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Compiler front end
// ---------------------------------------------------------------------------

bool ScriptGraphCompiler::areTypesCompatible(ScriptDataType source,
                                             ScriptDataType target)
{
    if (source == target)
    {
        return true;
    }
    if (source == ScriptDataType::ANY || target == ScriptDataType::ANY)
    {
        return true;
    }
    // Numeric widening — matches ScriptValue's runtime coercions so the
    // compile-time check doesn't reject connections the interpreter accepts.
    if (source == ScriptDataType::INT && target == ScriptDataType::FLOAT)
    {
        return true;
    }
    if (source == ScriptDataType::BOOL &&
        (target == ScriptDataType::INT || target == ScriptDataType::FLOAT))
    {
        return true;
    }
    if (source == ScriptDataType::ENTITY && target == ScriptDataType::INT)
    {
        return true;
    }
    if (source == ScriptDataType::COLOR && target == ScriptDataType::VEC4)
    {
        return true;
    }
    if (source == ScriptDataType::VEC4 && target == ScriptDataType::COLOR)
    {
        return true;
    }
    // Everything flows to STRING — matches ScriptValue::asString() coverage
    // so ToString chains don't trip a compile error.
    if (target == ScriptDataType::STRING)
    {
        return true;
    }
    return false;
}

CompilationResult ScriptGraphCompiler::compile(
    const ScriptGraph& graph, const NodeTypeRegistry& registry)
{
    CompilationResult result;
    result.compiled.graphName = graph.name;

    std::unordered_map<uint32_t, std::size_t> idToIndex;
    idToIndex.reserve(graph.nodes.size());

    passResolveNodes(graph, registry, result, idToIndex);
    passResolveConnections(graph, result, idToIndex);
    passDetectDataCycles(result);
    passReachability(result, graph);

    // Pass 9 — entry-point existence is a warning, not an error. A helper
    // library graph may be pure-data-only by design.
    if (result.compiled.entryPointIndices.empty())
    {
        addDiag(result, CompileSeverity::WARNING,
                "Graph has no entry points (event, OnStart, or OnUpdate "
                "nodes) — it will never execute at runtime");
    }

    result.success = (result.countAt(CompileSeverity::ERROR) == 0);
    return result;
}

} // namespace Vestige
