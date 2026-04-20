// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_compiler.h
/// @brief ScriptGraphCompiler — compile a ScriptGraph into a validated,
/// index-based intermediate representation (CompiledScriptGraph).
///
/// The existing `ScriptInstance` interpreter walks the graph at runtime using
/// hash-map lookups for node IDs and pin names. That's fine at the engine's
/// current scale, but it means graph errors (unknown node types, dangling
/// connections, type mismatches) only surface when a chain actually executes —
/// sometimes long after load, sometimes never if the broken subgraph isn't
/// reached. The compiler fixes that by running a dedicated validation pass at
/// load time and emitting a flat, index-based representation that is trivial
/// to consume from future codegen or runtime back-ends.
///
/// The compiler is intentionally conservative — it reports every error and
/// warning it can, but produces a CompiledScriptGraph even when non-fatal
/// warnings exist. Callers inspect `success` to decide whether to run the
/// graph, and `diagnostics` to surface issues to the editor.
#pragma once

#include "scripting/script_graph.h"
#include "scripting/script_value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class NodeTypeRegistry;
struct NodeTypeDescriptor;

/// @brief Severity of a compiler diagnostic.
enum class CompileSeverity
{
    INFO,     ///< Informational, surfaced in verbose mode only.
    WARNING,  ///< Non-fatal: compiled graph is still usable.
    ERROR     ///< Fatal: the compiled graph is invalid and MUST NOT run.
};

/// @brief A single diagnostic produced by the compiler.
///
/// Diagnostics carry enough location information that the editor can highlight
/// the offending node or connection. `nodeId` / `connectionId` are 0 when the
/// diagnostic is graph-wide (e.g. "no entry points found").
struct CompileDiagnostic
{
    CompileSeverity severity = CompileSeverity::INFO;
    std::string message;
    uint32_t nodeId = 0;        ///< 0 if not node-scoped.
    uint32_t connectionId = 0;  ///< 0 if not connection-scoped.
};

/// @brief A single input pin's resolved wiring in the compiled graph.
///
/// Exactly one of the following is true:
/// - `connected == true`: the pin pulls its value from
///   `sourceNodeIndex`/`sourceOutputPinIndex` at run time.
/// - `connected == false`: the pin uses its descriptor default (or a property
///   override, resolved separately from `ScriptNodeDef::properties`).
struct CompiledInputPin
{
    bool connected = false;
    std::size_t sourceNodeIndex = 0;       ///< Index into CompiledScriptGraph::nodes.
    std::size_t sourceOutputPinIndex = 0;  ///< Index into the source node's outputs.
    uint32_t connectionId = 0;             ///< For traceability back to ScriptConnection.
};

/// @brief A single output pin's resolved wiring in the compiled graph.
///
/// Output pins (both exec and data) can fan out to many targets. Data
/// fan-out is a universal pattern (one value feeds multiple readers);
/// execution fan-out is rarer but used by the shipped gameplay templates
/// (DoOnce.Then → PlayAnimation + PlaySound, etc.) so the compiler accepts
/// it.
struct CompiledOutputPin
{
    std::vector<std::size_t> targetNodeIndices;     ///< Index into CompiledScriptGraph::nodes.
    std::vector<std::size_t> targetInputPinIndices; ///< Index into the target node's inputs.
    std::vector<uint32_t>    connectionIds;         ///< Parallel to the two vectors above.
};

/// @brief A single node in the compiled graph (flat representation).
///
/// Ownership: the descriptor pointer is not owned — it aliases the node type
/// registry entry that was live at compile time. Callers must ensure the
/// registry outlives any CompiledScriptGraph derived from it. (The engine
/// registry is a singleton populated at startup, so this is automatic in
/// practice; tests that wire up throwaway registries need to keep them alive
/// for the compiled graph's lifetime.)
struct CompiledNode
{
    uint32_t nodeId = 0;  ///< Original ScriptNodeDef::id — used for log context.
    const NodeTypeDescriptor* descriptor = nullptr;

    /// @brief Inputs parallel to descriptor->inputDefs by index.
    std::vector<CompiledInputPin> inputs;

    /// @brief Outputs parallel to descriptor->outputDefs by index.
    std::vector<CompiledOutputPin> outputs;

    bool isEntryPoint = false;  ///< True for event roots (OnStart, OnUpdate, OnKeyPressed, ...).
    bool isReachable = false;   ///< True if an execution path from any entry point reaches this node.
};

/// @brief Validated, flat, index-based representation of a ScriptGraph.
///
/// Produced by `ScriptGraphCompiler::compile`. Once a graph has compiled
/// successfully, callers can walk `nodes` + `entryPointIndices` directly
/// without ever hashing a node ID or pin name — a small but genuine win on
/// cold-start, and the foundation for a future bytecode back-end.
struct CompiledScriptGraph
{
    std::string graphName;
    std::vector<CompiledNode> nodes;

    /// @brief Indices into `nodes` for every entry-point node discovered.
    /// Includes event nodes (any descriptor with non-empty eventTypeName),
    /// OnStart, and OnUpdate. Stable across compilations for the same input
    /// graph, sorted ascending by index.
    std::vector<std::size_t> entryPointIndices;

    /// @brief Indices into `nodes` for OnUpdate nodes specifically — hot path
    /// that ScriptingSystem::tickUpdateNodes drives every frame.
    std::vector<std::size_t> updateNodeIndices;

    /// @brief Resolve a ScriptNodeDef::id to its index in `nodes`. Returns
    /// SIZE_MAX if the id doesn't belong to this compiled graph.
    std::size_t indexForNodeId(uint32_t nodeId) const;
};

/// @brief Output of a compile() run.
///
/// `success` is true iff no diagnostic has severity ERROR. The compiled graph
/// is populated even when warnings exist; callers decide whether to use it.
struct CompilationResult
{
    bool success = false;
    std::vector<CompileDiagnostic> diagnostics;
    CompiledScriptGraph compiled;

    /// @brief Convenience: count diagnostics at the given severity.
    std::size_t countAt(CompileSeverity severity) const;

    /// @brief Convenience: true if any diagnostic is WARNING or ERROR.
    bool hasIssues() const;
};

/// @brief Stateless graph → IR compilation front end.
class ScriptGraphCompiler
{
public:
    /// @brief Compile a ScriptGraph against the given node type registry.
    ///
    /// Validation passes (in order):
    /// 1. Node type exists in registry.
    /// 2. Node IDs are unique.
    /// 3. Connection endpoints reference existing nodes + pins by name.
    /// 4. Pin kind (execution vs data) matches across the connection.
    /// 5. Pin data types are compatible (exact match, ANY wildcard, or
    ///    whitelisted implicit conversion — see `areTypesCompatible`).
    /// 6. Each input pin has at most one incoming connection.
    /// 7. No cycles in pure-data edges (execution cycles are allowed — loops
    ///    and re-triggers are legitimate, and execution-output fan-out is a
    ///    deliberate template pattern).
    /// 8. Entry points exist (warning only — a library graph may be a pure
    ///    helper).
    /// 9. Reachability classification (warning for unreachable impure nodes).
    ///
    /// The compiler always returns a CompilationResult even on catastrophic
    /// input (null registry, empty graph). It never throws.
    static CompilationResult compile(const ScriptGraph& graph,
                                     const NodeTypeRegistry& registry);

    /// @brief Pin-type compatibility check used by pass 5.
    ///
    /// Returns true when a source pin typed `source` can feed a target pin
    /// typed `target`. ANY matches anything. Numeric widening (INT → FLOAT,
    /// ENTITY → INT) is allowed; narrowing is not. Same-type pairs are
    /// always allowed.
    static bool areTypesCompatible(ScriptDataType source,
                                   ScriptDataType target);
};

} // namespace Vestige
