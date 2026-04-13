/// @file action_nodes.h
/// @brief Registration of impure action script node types.
#pragma once

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Register action nodes — side-effectful operations that change
/// scene, entity, physics, audio, material, or lighting state.
///
/// All action nodes have an "Exec" input pin and a "Then" output pin for
/// chaining. They can be triggered by any event or other action node.
void registerActionNodeTypes(NodeTypeRegistry& registry);

} // namespace Vestige
