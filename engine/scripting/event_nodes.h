// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file event_nodes.h
/// @brief Registration of event-driven script node types.
#pragma once

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Register event nodes (graph entry points wired to engine events).
///
/// Covers: input events (OnKeyPressed, OnKeyReleased, OnMouseButton),
/// scene events (OnSceneLoaded), environmental events (OnWeatherChanged),
/// and user-defined events (OnCustomEvent). Trigger/collision events are
/// reserved for future engine support and registered as palette stubs.
void registerEventNodeTypes(NodeTypeRegistry& registry);

} // namespace Vestige
