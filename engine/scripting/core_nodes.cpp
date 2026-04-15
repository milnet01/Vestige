// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file core_nodes.cpp
/// @brief Registration of the 10 core visual scripting node types.
#include "scripting/core_nodes.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_context.h"
#include "core/logger.h"

namespace Vestige
{

void registerCoreNodeTypes(NodeTypeRegistry& registry)
{
    // -----------------------------------------------------------------------
    // OnStart — fires once when the script is first activated
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnStart",                          // typeName
        "On Start",                         // displayName
        "Events",                           // category
        "Fires once when the script starts", // tooltip
        {},                                 // inputDefs (no inputs)
        {{PinKind::EXECUTION, "Started", ScriptDataType::BOOL, {}}}, // outputDefs
        "",                                 // eventTypeName
        false,                              // isPure
        false,                              // isLatent
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Started");
        }
    });

    // -----------------------------------------------------------------------
    // OnUpdate — fires every frame (use sparingly!)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnUpdate",
        "On Update",
        "Events",
        "Fires every frame. Use sparingly — prefer event-driven nodes.",
        {},  // no inputs
        {
            {PinKind::EXECUTION, "Tick", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "deltaTime", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        "",
        false,
        false,
        // Execute is a no-op here — ScriptingSystem handles ticking OnUpdate
        // by setting deltaTime and triggering the "Tick" output directly.
        [](ScriptContext&, const ScriptNodeInstance&) {}
    });

    // -----------------------------------------------------------------------
    // OnDestroy — fires when the entity is about to be destroyed
    // -----------------------------------------------------------------------
    registry.registerNode({
        "OnDestroy",
        "On Destroy",
        "Events",
        "Fires when the entity is about to be destroyed",
        {},
        {{PinKind::EXECUTION, "Destroying", ScriptDataType::BOOL, {}}},
        "EntityDestroyedEvent",
        false,
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Destroying");
        }
    });

    // -----------------------------------------------------------------------
    // Branch — if/else: routes execution based on a boolean condition
    // -----------------------------------------------------------------------
    registry.registerNode({
        "Branch",
        "Branch",
        "Flow Control",
        "Routes execution based on a boolean condition (if/else)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Condition", ScriptDataType::BOOL, ScriptValue(false)},
        },
        {
            {PinKind::EXECUTION, "True", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "False", ScriptDataType::BOOL, {}},
        },
        "",
        false,
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            bool condition = ctx.readInputAs<bool>(node, "Condition");
            if (condition)
            {
                ctx.triggerOutput(node, "True");
            }
            else
            {
                ctx.triggerOutput(node, "False");
            }
        }
    });

    // -----------------------------------------------------------------------
    // Sequence — executes all outputs in order (Then 0, Then 1, ...)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "Sequence",
        "Sequence",
        "Flow Control",
        "Executes all outputs in order",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
        },
        {
            {PinKind::EXECUTION, "Then 0", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Then 1", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Then 2", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Then 3", ScriptDataType::BOOL, {}},
        },
        "",
        false,
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ctx.triggerOutput(node, "Then 0");
            ctx.triggerOutput(node, "Then 1");
            ctx.triggerOutput(node, "Then 2");
            ctx.triggerOutput(node, "Then 3");
        }
    });

    // -----------------------------------------------------------------------
    // Delay — waits N seconds, then continues (latent)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "Delay",
        "Delay",
        "Flow Control",
        "Waits for the specified duration, then continues execution",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Duration", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {
            {PinKind::EXECUTION, "Completed", ScriptDataType::BOOL, {}},
        },
        "",
        false,
        true, // isLatent
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float duration = ctx.readInputAs<float>(node, "Duration");
            ctx.scheduleDelay(node, "Completed", duration);
            // Execution pauses here — resumes from "Completed" when timer fires
        }
    });

    // -----------------------------------------------------------------------
    // SetVariable — write a value to the blackboard
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SetVariable",
        "Set Variable",
        "Variables",
        "Writes a value to a variable in the specified scope",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Name", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "Value", ScriptDataType::ANY, ScriptValue(0.0f)},
        },
        {
            {PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Value", ScriptDataType::ANY, ScriptValue(0.0f)},
        },
        "",
        false,
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto name = ctx.readInputAs<std::string>(node, "Name");
            auto value = ctx.readInput(node, "Value");

            // Default to graph scope
            ctx.setVariable(name, VariableScope::GRAPH, value);

            // Pass through the value for chaining
            ctx.setOutput(node, "Value", value);
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // GetVariable — read a value from the blackboard (pure)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "GetVariable",
        "Get Variable",
        "Variables",
        "Reads a variable value from the specified scope",
        {
            {PinKind::DATA, "Name", ScriptDataType::STRING, ScriptValue(std::string(""))},
        },
        {
            {PinKind::DATA, "Value", ScriptDataType::ANY, ScriptValue(0.0f)},
        },
        "",
        true, // isPure
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto name = ctx.readInputAs<std::string>(node, "Name");
            auto value = ctx.getVariable(name, VariableScope::GRAPH);
            ctx.setOutput(node, "Value", value);
        },
        false, // memoizable — NO: blackboard reads depend on call-time state (AUDIT.md §H7)
    });

    // -----------------------------------------------------------------------
    // PrintToScreen — debug text overlay
    // -----------------------------------------------------------------------
    registry.registerNode({
        "PrintToScreen",
        "Print To Screen",
        "Debug",
        "Displays a debug text message (logged to console)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Message", ScriptDataType::STRING, ScriptValue(std::string("Hello"))},
        },
        {
            {PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}},
        },
        "",
        false,
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto message = ctx.readInputAs<std::string>(node, "Message");
            Logger::info("[Script] " + message);
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // LogMessage — write to engine log with severity
    // -----------------------------------------------------------------------
    registry.registerNode({
        "LogMessage",
        "Log Message",
        "Debug",
        "Writes a message to the engine log",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Message", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "Severity", ScriptDataType::STRING, ScriptValue(std::string("info"))},
        },
        {
            {PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}},
        },
        "",
        false,
        false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto message = ctx.readInputAs<std::string>(node, "Message");
            auto severity = ctx.readInputAs<std::string>(node, "Severity");

            if (severity == "warning")
            {
                Logger::warning("[Script] " + message);
            }
            else if (severity == "error")
            {
                Logger::error("[Script] " + message);
            }
            else
            {
                Logger::info("[Script] " + message);
            }

            ctx.triggerOutput(node, "Then");
        }
    });
}

} // namespace Vestige
