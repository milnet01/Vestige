// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file node_type_registry.h
/// @brief Registry of all available visual scripting node types.
#pragma once

#include "scripting/script_common.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class ScriptContext;
struct ScriptNodeInstance;

/// @brief Describes a node type available in the editor palette.
///
/// New node types are registered at startup (in core_nodes.cpp, event_nodes.cpp,
/// etc.) and looked up by typeName when creating node instances or loading graphs.
struct NodeTypeDescriptor
{
    std::string typeName;     ///< Unique identifier (e.g. "Branch", "PlaySound")
    std::string displayName;  ///< Human-readable name for the palette
    std::string category;     ///< Palette category ("Flow Control", "Audio", etc.)
    std::string tooltip;      ///< One-line description for hover tooltip

    std::vector<PinDef> inputDefs;
    std::vector<PinDef> outputDefs;

    /// @brief For event nodes: the engine event type this subscribes to.
    /// Empty string means this is not an event node.
    std::string eventTypeName;

    bool isPure = false;    ///< Pure nodes have no execution pins, evaluate lazily
    bool isLatent = false;  ///< Latent nodes can suspend execution

    /// @brief The execute function.
    /// For impure nodes: called when execution flow reaches this node.
    /// For pure nodes: called when output data is pulled.
    using ExecuteFn = std::function<void(ScriptContext&, const ScriptNodeInstance&)>;
    ExecuteFn execute;

    /// @brief Whether this pure node's output can be memoized within a chain.
    ///
    /// Defaults to ``true`` so existing pure nodes (Add, Multiply, Compare)
    /// keep their M11 per-execution-memo optimization. Set to ``false`` for
    /// nodes that *read* mutable state mid-chain — e.g. GetVariable reads
    /// the blackboard, which may be mutated by SetVariable in the same
    /// chain; FindEntityByName queries the scene, which a spawn/destroy
    /// elsewhere may change. Memoizing these caused WhileLoop.Condition
    /// to freeze at its first value and GetVariable inside a loop body
    /// to return stale reads (AUDIT.md §H7 / §H8, FIXPLAN D3).
    ///
    /// Placed at the end of the struct so existing aggregate initializers
    /// (which use positional construction up through ``execute``) continue
    /// to compile without touching every call site.
    bool memoizable = true;

    /// @brief Pin-name → index lookup tables (AUDIT.md §M3 / FIXPLAN H2).
    ///
    /// Populated by NodeTypeRegistry::registerNode() at startup so the
    /// editor's per-frame connection renderer can resolve pin names in
    /// O(1) instead of scanning inputDefs/outputDefs each frame. Kept
    /// in sync with inputDefs/outputDefs; do not mutate directly —
    /// re-register the descriptor to change pin sets.
    std::unordered_map<std::string, size_t> inputIndexByName = {};
    std::unordered_map<std::string, size_t> outputIndexByName = {};
};

/// @brief Registry mapping type names to their descriptors.
///
/// Populated at engine startup. Used by:
/// - Editor: to populate the node palette and create new nodes
/// - Runtime: to look up execute functions when interpreting graphs
/// - Serializer: to validate loaded graph data
class NodeTypeRegistry
{
public:
    /// @brief Register a node type. Overwrites if typeName already exists.
    void registerNode(NodeTypeDescriptor descriptor);

    /// @brief Look up a descriptor by type name.
    /// @return Pointer to the descriptor, or nullptr if not found.
    const NodeTypeDescriptor* findNode(const std::string& typeName) const;

    /// @brief Get all registered node types.
    const std::unordered_map<std::string, NodeTypeDescriptor>& allNodes() const
    {
        return m_nodes;
    }

    /// @brief Get all node types in a specific category, sorted by display name.
    std::vector<const NodeTypeDescriptor*> getByCategory(
        const std::string& category) const;

    /// @brief Get all unique category names, sorted alphabetically.
    std::vector<std::string> getCategories() const;

    /// @brief Check if a type name is registered.
    bool hasNode(const std::string& typeName) const;

    /// @brief Get the total number of registered node types.
    size_t nodeCount() const { return m_nodes.size(); }

private:
    std::unordered_map<std::string, NodeTypeDescriptor> m_nodes;
};

} // namespace Vestige
