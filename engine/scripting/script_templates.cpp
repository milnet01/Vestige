// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_templates.cpp
/// @brief Factory implementations for pre-built gameplay script templates.
#include "scripting/script_templates.h"

#include "scripting/script_value.h"

namespace Vestige
{

namespace
{

/// @brief Helper: add a property override on the most recently added node.
///
/// Template graphs configure sensible defaults via per-instance property
/// overrides (e.g. Delay's ``Duration``, SetVariable's ``Name``) so the
/// graph works out of the box without relying on the node type's pin
/// default alone — that default is a sentinel; the template needs a real
/// value that makes the pattern self-explanatory.
void setNodeProperty(ScriptGraph& graph, uint32_t nodeId,
                     const std::string& propertyName, ScriptValue value)
{
    if (ScriptNodeDef* node = graph.findNode(nodeId))
    {
        node->properties[propertyName] = std::move(value);
    }
}

// ---------------------------------------------------------------------------
// Template: Door Opens
// ---------------------------------------------------------------------------
//
//     OnTriggerEnter ─exec─▶ DoOnce ─exec─▶ PlayAnimation("DoorOpen")
//                                      └──▶ PlaySound("door_open.ogg")
//
// Pattern: a physics trigger volume attached to a door opens the door once
// when the player walks in. DoOnce guards against repeated triggers while
// the player stands inside the volume.
ScriptGraph buildDoorTemplate()
{
    ScriptGraph graph;
    graph.name = "DoorOpens";

    uint32_t onEnter = graph.addNode("OnTriggerEnter", 0.0f, 0.0f);
    uint32_t doOnce  = graph.addNode("DoOnce", 220.0f, 0.0f);
    uint32_t playAnim = graph.addNode("PlayAnimation", 440.0f, -80.0f);
    uint32_t playSound = graph.addNode("PlaySound", 440.0f, 80.0f);

    graph.addConnection(onEnter, "Entered", doOnce, "Exec");
    graph.addConnection(doOnce,  "Then",   playAnim,  "Exec");
    graph.addConnection(doOnce,  "Then",   playSound, "Exec");

    setNodeProperty(graph, playAnim,  "clipName", ScriptValue(std::string("DoorOpen")));
    setNodeProperty(graph, playAnim,  "blendTime", ScriptValue(0.2f));
    setNodeProperty(graph, playSound, "clipPath", ScriptValue(std::string("assets/sounds/door_open.ogg")));
    setNodeProperty(graph, playSound, "volume",   ScriptValue(0.8f));

    return graph;
}

// ---------------------------------------------------------------------------
// Template: Collectible Item
// ---------------------------------------------------------------------------
//
//     OnTriggerEnter ─▶ PlaySound("pickup.ogg") ─▶ SetVariable(score += 1) ─▶ DestroyEntity(self)
//
// Pattern: an overlap volume on a pickup plays a jingle, increments a
// scoped "score" variable, then removes the pickup from the scene. The
// SetVariable uses its Any-type ``Value`` pin with a literal 1 — live
// games typically chain a GetVariable + MathAdd for "+= 1", but the
// simpler form here keeps the starter graph readable.
ScriptGraph buildCollectibleTemplate()
{
    ScriptGraph graph;
    graph.name = "CollectibleItem";

    uint32_t onEnter = graph.addNode("OnTriggerEnter", 0.0f, 0.0f);
    uint32_t playSound = graph.addNode("PlaySound", 220.0f, 0.0f);
    uint32_t setScore  = graph.addNode("SetVariable", 440.0f, 0.0f);
    uint32_t destroy   = graph.addNode("DestroyEntity", 660.0f, 0.0f);

    graph.addConnection(onEnter,   "Entered", playSound, "Exec");
    graph.addConnection(playSound, "Then",    setScore,  "Exec");
    graph.addConnection(setScore,  "Then",    destroy,   "Exec");

    setNodeProperty(graph, playSound, "clipPath", ScriptValue(std::string("assets/sounds/pickup.ogg")));
    setNodeProperty(graph, playSound, "volume",   ScriptValue(1.0f));
    setNodeProperty(graph, setScore,  "Name",     ScriptValue(std::string("score")));
    setNodeProperty(graph, setScore,  "Value",    ScriptValue(1.0f));

    return graph;
}

// ---------------------------------------------------------------------------
// Template: Damage Zone
// ---------------------------------------------------------------------------
//
//     OnTriggerEnter ─▶ PublishEvent("damage", amount)
//
// Pattern: a volume that publishes a ``damage`` custom event when an
// entity enters. Gameplay subscribers pick up the event and apply their
// own damage logic (no health system yet, so the template stays at the
// event-publishing boundary).
ScriptGraph buildDamageZoneTemplate()
{
    ScriptGraph graph;
    graph.name = "DamageZone";

    uint32_t onEnter = graph.addNode("OnTriggerEnter", 0.0f, 0.0f);
    uint32_t publish = graph.addNode("PublishEvent", 220.0f, 0.0f);

    graph.addConnection(onEnter, "Entered", publish, "Exec");

    setNodeProperty(graph, publish, "name",    ScriptValue(std::string("damage")));
    setNodeProperty(graph, publish, "payload", ScriptValue(10.0f));

    return graph;
}

// ---------------------------------------------------------------------------
// Template: Checkpoint
// ---------------------------------------------------------------------------
//
//     OnTriggerEnter ─▶ DoOnce ─▶ SetVariable(lastCheckpoint = entity) ─▶ PrintToScreen
//
// Pattern: on first entry, record the checkpoint entity ID in a Saved-
// scoped variable. PrintToScreen gives the designer visual confirmation
// while iterating; they can delete it once the checkpoint UI exists.
ScriptGraph buildCheckpointTemplate()
{
    ScriptGraph graph;
    graph.name = "Checkpoint";

    uint32_t onEnter = graph.addNode("OnTriggerEnter", 0.0f, 0.0f);
    uint32_t doOnce  = graph.addNode("DoOnce", 220.0f, 0.0f);
    uint32_t setVar  = graph.addNode("SetVariable", 440.0f, 0.0f);
    uint32_t print   = graph.addNode("PrintToScreen", 660.0f, 0.0f);

    graph.addConnection(onEnter, "Entered", doOnce, "Exec");
    graph.addConnection(doOnce,  "Then",    setVar, "Exec");
    graph.addConnection(setVar,  "Then",    print,  "Exec");
    // Pipe the Any-typed otherEntity through the SetVariable data pin.
    graph.addConnection(onEnter, "otherEntity", setVar, "Value");

    setNodeProperty(graph, setVar, "Name",    ScriptValue(std::string("lastCheckpoint")));
    setNodeProperty(graph, print,  "Message", ScriptValue(std::string("Checkpoint reached.")));

    return graph;
}

// ---------------------------------------------------------------------------
// Template: Dialogue Trigger
// ---------------------------------------------------------------------------
//
//     OnTriggerEnter ─▶ DoOnce ─▶ PublishEvent("dialogue_started", id) ─▶ PrintToScreen
//
// Pattern: first-entry publishes a ``dialogue_started`` event so the UI
// layer can show the prompt, then PrintToScreen leaves an editor-side
// breadcrumb for iteration.
ScriptGraph buildDialogueTemplate()
{
    ScriptGraph graph;
    graph.name = "DialogueTrigger";

    uint32_t onEnter = graph.addNode("OnTriggerEnter", 0.0f, 0.0f);
    uint32_t doOnce  = graph.addNode("DoOnce", 220.0f, 0.0f);
    uint32_t publish = graph.addNode("PublishEvent", 440.0f, 0.0f);
    uint32_t print   = graph.addNode("PrintToScreen", 660.0f, 0.0f);

    graph.addConnection(onEnter, "Entered", doOnce, "Exec");
    graph.addConnection(doOnce,  "Then",    publish, "Exec");
    graph.addConnection(publish, "Then",    print,  "Exec");

    setNodeProperty(graph, publish, "name",    ScriptValue(std::string("dialogue_started")));
    setNodeProperty(graph, publish, "payload", ScriptValue(std::string("greeting")));
    setNodeProperty(graph, print,   "Message", ScriptValue(std::string("[dialogue] greeting")));

    return graph;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ScriptGraph buildGameplayTemplate(GameplayTemplate which)
{
    switch (which)
    {
    case GameplayTemplate::DOOR_OPENS:        return buildDoorTemplate();
    case GameplayTemplate::COLLECTIBLE_ITEM:  return buildCollectibleTemplate();
    case GameplayTemplate::DAMAGE_ZONE:       return buildDamageZoneTemplate();
    case GameplayTemplate::CHECKPOINT:        return buildCheckpointTemplate();
    case GameplayTemplate::DIALOGUE_TRIGGER:  return buildDialogueTemplate();
    }
    return {};
}

const char* gameplayTemplateDisplayName(GameplayTemplate which)
{
    switch (which)
    {
    case GameplayTemplate::DOOR_OPENS:        return "Door Opens";
    case GameplayTemplate::COLLECTIBLE_ITEM:  return "Collectible Item";
    case GameplayTemplate::DAMAGE_ZONE:       return "Damage Zone";
    case GameplayTemplate::CHECKPOINT:        return "Checkpoint";
    case GameplayTemplate::DIALOGUE_TRIGGER:  return "Dialogue Trigger";
    }
    return "";
}

const char* gameplayTemplateDescription(GameplayTemplate which)
{
    switch (which)
    {
    case GameplayTemplate::DOOR_OPENS:
        return "On trigger overlap, plays a door-open animation and sound (once).";
    case GameplayTemplate::COLLECTIBLE_ITEM:
        return "On overlap, plays a pickup sound, awards score, then destroys self.";
    case GameplayTemplate::DAMAGE_ZONE:
        return "On overlap, publishes a 'damage' event with a configurable amount.";
    case GameplayTemplate::CHECKPOINT:
        return "On first overlap, records the player entity as the last checkpoint.";
    case GameplayTemplate::DIALOGUE_TRIGGER:
        return "On first overlap, publishes a 'dialogue_started' event with a line ID.";
    }
    return "";
}

} // namespace Vestige
