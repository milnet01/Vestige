// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file flow_nodes.h
/// @brief Registration of flow control script node types.
#pragma once

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Register flow control nodes: SwitchInt, SwitchString, ForLoop,
/// WhileLoop, Gate, DoOnce, FlipFlop. These nodes route execution through
/// a graph without performing side effects on the scene.
///
/// Stateful nodes (DoOnce, Gate, FlipFlop, ForLoop) persist counters and
/// flags in ScriptNodeInstance::runtimeState so they survive across exec
/// chains within one ScriptInstance lifetime.
void registerFlowNodeTypes(NodeTypeRegistry& registry);

} // namespace Vestige
