/// @file pure_nodes.h
/// @brief Registration of pure (no-side-effect) script node types.
#pragma once

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Register pure data nodes — math, vector, boolean, and comparison
/// operations plus read-only entity queries. Pure nodes have no exec pins
/// and are evaluated lazily when their outputs are pulled.
void registerPureNodeTypes(NodeTypeRegistry& registry);

} // namespace Vestige
