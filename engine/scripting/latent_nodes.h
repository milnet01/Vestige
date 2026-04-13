/// @file latent_nodes.h
/// @brief Registration of latent (suspend-and-resume) script node types.
#pragma once

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Register latent nodes: WaitForEvent, WaitForCondition, Timeline,
/// MoveTo. These suspend execution and resume asynchronously via
/// PendingLatentAction entries processed each frame by ScriptingSystem.
void registerLatentNodeTypes(NodeTypeRegistry& registry);

} // namespace Vestige
