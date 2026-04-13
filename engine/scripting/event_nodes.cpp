/// @file event_nodes.cpp
/// @brief Implementation of event-driven script node types.
///
/// Event nodes are graph entry points. They have no execution input pin and
/// are fired by the ScriptingSystem when a matching EventBus event is
/// published. See ScriptingSystem::subscribeEventNodes() for wiring.
///
/// The execute() function here is typically a no-op, because execution is
/// driven by the EventBus callback setting output data pins and triggering
/// the exec output. Some events still have an execute() for consistency
/// (e.g. so they could be fired manually for testing).
#include "scripting/event_nodes.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_context.h"

namespace Vestige
{

void registerEventNodeTypes(NodeTypeRegistry& registry)
{
    // -----------------------------------------------------------------------
    // OnKeyPressed — fires when a keyboard key is pressed.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnKeyPressed",
        "On Key Pressed",
        "Events",
        "Fires when any keyboard key is pressed",
        {},
        {
            {PinKind::EXECUTION, "Pressed", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "keyCode", ScriptDataType::INT, ScriptValue(0)},
            {PinKind::DATA, "isRepeat", ScriptDataType::BOOL, ScriptValue(false)},
        },
        "KeyPressedEvent",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Pressed");
        }
    });

    // -----------------------------------------------------------------------
    // OnKeyReleased — fires when a keyboard key is released.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnKeyReleased",
        "On Key Released",
        "Events",
        "Fires when any keyboard key is released",
        {},
        {
            {PinKind::EXECUTION, "Released", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "keyCode", ScriptDataType::INT, ScriptValue(0)},
        },
        "KeyReleasedEvent",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Released");
        }
    });

    // -----------------------------------------------------------------------
    // OnMouseButton — fires when a mouse button is pressed.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnMouseButton",
        "On Mouse Button",
        "Events",
        "Fires when a mouse button is pressed",
        {},
        {
            {PinKind::EXECUTION, "Pressed", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "button", ScriptDataType::INT, ScriptValue(0)},
        },
        "MouseButtonPressedEvent",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Pressed");
        }
    });

    // -----------------------------------------------------------------------
    // OnSceneLoaded — fires when a scene finishes loading.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnSceneLoaded",
        "On Scene Loaded",
        "Events",
        "Fires when a scene finishes loading",
        {},
        {{PinKind::EXECUTION, "Loaded", ScriptDataType::BOOL, {}}},
        "SceneLoadedEvent",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Loaded");
        }
    });

    // -----------------------------------------------------------------------
    // OnWeatherChanged — fires when environmental weather parameters change.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnWeatherChanged",
        "On Weather Changed",
        "Events",
        "Fires when weather parameters (temperature, humidity, wind) change",
        {},
        {
            {PinKind::EXECUTION, "Changed", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "temperature", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "humidity", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "precipitation", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "windStrength", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        "WeatherChangedEvent",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Changed");
        }
    });

    // -----------------------------------------------------------------------
    // OnCustomEvent — fires when a user-named ScriptCustomEvent is published.
    // Filters by the "Name" property (blank name matches all).
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnCustomEvent",
        "On Custom Event",
        "Events",
        "Fires when a user-defined event (via PublishEvent) matches the given name",
        {},
        {
            {PinKind::EXECUTION, "Fired", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "name", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "payload", ScriptDataType::ANY, ScriptValue(0.0f)},
        },
        "ScriptCustomEvent",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            // Filtering is applied by the EventBus callback via an internal
            // "_filtered" flag written to the node's output values.
            auto it = node.outputValues.find("_filtered");
            if (it != node.outputValues.end() && it->second.asBool())
            {
                return;
            }
            ctx.triggerOutput(node, "Fired");
        }
    });

    // -----------------------------------------------------------------------
    // OnTriggerEnter / OnTriggerExit (stubs — awaiting trigger event in engine)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnTriggerEnter",
        "On Trigger Enter",
        "Events",
        "(Stub) Fires when an entity enters a trigger zone — trigger events pending",
        {},
        {
            {PinKind::EXECUTION, "Entered", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "otherEntity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
        },
        "", // Not yet wired
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Entered");
        }
    });

    registry.registerNode({
        "OnTriggerExit",
        "On Trigger Exit",
        "Events",
        "(Stub) Fires when an entity exits a trigger zone — trigger events pending",
        {},
        {
            {PinKind::EXECUTION, "Exited", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "otherEntity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Exited");
        }
    });

    // -----------------------------------------------------------------------
    // OnCollisionEnter / OnCollisionExit (stubs — awaiting collision events)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnCollisionEnter",
        "On Collision Enter",
        "Events",
        "(Stub) Fires when a physics collision begins — collision events pending",
        {},
        {
            {PinKind::EXECUTION, "Hit", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "otherEntity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "contactPoint", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "normal", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Hit");
        }
    });

    registry.registerNode({
        "OnCollisionExit",
        "On Collision Exit",
        "Events",
        "(Stub) Fires when a physics collision ends — collision events pending",
        {},
        {
            {PinKind::EXECUTION, "Separated", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "otherEntity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Separated");
        }
    });

    // -----------------------------------------------------------------------
    // OnAudioFinished / OnVariableChanged (stubs — no event support yet)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnAudioFinished",
        "On Audio Finished",
        "Events",
        "(Stub) Fires when an audio clip finishes playing — audio finished events pending",
        {},
        {
            {PinKind::EXECUTION, "Finished", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "clipPath", ScriptDataType::STRING, ScriptValue(std::string(""))},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Finished");
        }
    });

    registry.registerNode({
        "OnVariableChanged",
        "On Variable Changed",
        "Events",
        "(Stub) Fires when a blackboard variable changes — change tracking pending",
        {},
        {
            {PinKind::EXECUTION, "Changed", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "oldValue", ScriptDataType::ANY, ScriptValue(0.0f)},
            {PinKind::DATA, "newValue", ScriptDataType::ANY, ScriptValue(0.0f)},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Changed");
        }
    });
}

} // namespace Vestige
