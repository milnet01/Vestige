/// @file core_nodes.h
/// @brief Registration of the 10 core visual scripting node types.
#pragma once

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Register all core node types into the given registry.
///
/// Core nodes (Phase 9E-1):
/// - OnStart, OnUpdate, OnDestroy (lifecycle events)
/// - Branch, Sequence (flow control)
/// - Delay (latent)
/// - SetVariable, GetVariable (blackboard)
/// - PrintToScreen, LogMessage (debug)
void registerCoreNodeTypes(NodeTypeRegistry& registry);

} // namespace Vestige
