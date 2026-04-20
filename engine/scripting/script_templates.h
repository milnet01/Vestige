// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_templates.h
/// @brief Pre-built gameplay script templates (Phase 9E-4).
///
/// Each factory returns a self-contained ``ScriptGraph`` wiring registered
/// node types into a common gameplay pattern — a starting point designers
/// can drop on an entity and tweak, rather than wiring the same event →
/// action chain from scratch every time.
///
/// The templates reference only node types that are currently registered
/// (see ``event_nodes.cpp`` / ``action_nodes.cpp`` / ``core_nodes.cpp``).
/// Event nodes that are still engine-side stubs (``OnTriggerEnter`` /
/// ``OnCollisionEnter``) are used deliberately so the templates are ready
/// the moment trigger / collision events are wired through the EventBus.
#pragma once

#include "scripting/script_graph.h"

namespace Vestige
{

/// @brief Identifiers for the pre-built gameplay templates.
enum class GameplayTemplate
{
    DOOR_OPENS,          ///< Proximity trigger opens + sounds
    COLLECTIBLE_ITEM,    ///< Pickup on overlap, increments score, destroys self
    DAMAGE_ZONE,         ///< Publishes damage event on trigger enter
    CHECKPOINT,          ///< Records last-saved spawn point on trigger enter
    DIALOGUE_TRIGGER,    ///< Fires a dialogue once when a player enters a volume
};

/// @brief Build one of the pre-built gameplay templates.
///
/// Each graph has a stable name (``graph.name``) matching the template so
/// loaded instances round-trip cleanly through JSON.
///
/// @param which Which template to build.
/// @return A complete, validate()-clean ScriptGraph.
ScriptGraph buildGameplayTemplate(GameplayTemplate which);

/// @brief Returns the canonical template name as shown in the editor.
const char* gameplayTemplateDisplayName(GameplayTemplate which);

/// @brief Returns a short description of what the template does.
const char* gameplayTemplateDescription(GameplayTemplate which);

} // namespace Vestige
