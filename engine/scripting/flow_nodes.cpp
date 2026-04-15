// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file flow_nodes.cpp
/// @brief Flow control nodes — routing execution without side effects.
///
/// Stateful nodes (Gate, DoOnce, FlipFlop, ForLoop) keep their state in
/// ScriptNodeInstance::runtimeState. Because execute() is invoked via a
/// const ScriptNodeInstance&, mutation is indirected through the context:
/// ctx.instance().getNodeInstance(node.nodeId) returns a mutable pointer.
#include "scripting/flow_nodes.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_context.h"
#include "core/logger.h"

#include <string>

namespace Vestige
{

namespace
{

/// @brief Safety cap for WhileLoop iteration to prevent runaway scripts.
/// 10,000 lets legitimate bounded loops (e.g. iterating through entities in
/// a scene) run comfortably while terminating infinite conditions in milliseconds.
/// The cap is hit silently with a warning in the logs — consider expanding to
/// a dedicated "Aborted" output pin if designers ask for in-graph visibility.
constexpr int MAX_WHILE_ITERATIONS = 10000;

/// @brief Safety cap for ForLoop iteration count. Same reasoning as
/// MAX_WHILE_ITERATIONS; surfaces through the node's "Clamped" output pin
/// so designers can branch on the truncation (audit M3).
constexpr int MAX_FOR_ITERATIONS = 10000;

/// @brief Get the mutable runtime state map for a node. Keyed by PinId
/// (audit M10). Stateful flow nodes (DoOnce, Gate, FlipFlop) write/read
/// flag values keyed by interned pin-name handles.
std::unordered_map<PinId, ScriptValue>* getMutableRuntimeState(
    ScriptContext& ctx, uint32_t nodeId)
{
    ScriptNodeInstance* mut = ctx.instance().getNodeInstance(nodeId);
    return mut ? &mut->runtimeState : nullptr;
}

} // namespace

void registerFlowNodeTypes(NodeTypeRegistry& registry)
{
    // -----------------------------------------------------------------------
    // SwitchInt — routes execution based on an integer value (Case 0..3 + Default).
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SwitchInt",
        "Switch (Int)",
        "Flow Control",
        "Routes execution based on an integer value (0..3)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Value", ScriptDataType::INT, ScriptValue(0)},
        },
        {
            {PinKind::EXECUTION, "Case 0", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Case 1", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Case 2", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Case 3", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Default", ScriptDataType::BOOL, {}},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            int32_t v = ctx.readInputAs<int32_t>(node, "Value");
            if (v == 0)      ctx.triggerOutput(node, "Case 0");
            else if (v == 1) ctx.triggerOutput(node, "Case 1");
            else if (v == 2) ctx.triggerOutput(node, "Case 2");
            else if (v == 3) ctx.triggerOutput(node, "Case 3");
            else             ctx.triggerOutput(node, "Default");
        }
    });

    // -----------------------------------------------------------------------
    // SwitchString — routes execution based on string match. Cases are given
    // via properties "Match 0", "Match 1", "Match 2", "Match 3". Value is
    // compared to each and the matching output fires. Falls back to Default.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SwitchString",
        "Switch (String)",
        "Flow Control",
        "Routes execution by string match. Configure 'Match 0..3' properties on this node.",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Value", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "Match 0", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "Match 1", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "Match 2", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "Match 3", ScriptDataType::STRING, ScriptValue(std::string(""))},
        },
        {
            {PinKind::EXECUTION, "Case 0", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Case 1", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Case 2", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Case 3", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Default", ScriptDataType::BOOL, {}},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto v = ctx.readInputAs<std::string>(node, "Value");
            for (int i = 0; i < 4; ++i)
            {
                std::string pinName = "Match " + std::to_string(i);
                auto matchStr = ctx.readInputAs<std::string>(node, pinName);
                if (!matchStr.empty() && v == matchStr)
                {
                    std::string caseName = "Case " + std::to_string(i);
                    ctx.triggerOutput(node, caseName);
                    return;
                }
            }
            ctx.triggerOutput(node, "Default");
        }
    });

    // -----------------------------------------------------------------------
    // ForLoop — iterates Index from First..Last (inclusive), firing Body each time.
    // Fires Completed after the loop finishes.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "ForLoop",
        "For Loop",
        "Flow Control",
        "Iterates from First to Last (inclusive), firing Body each iteration",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "First", ScriptDataType::INT, ScriptValue(0)},
            {PinKind::DATA, "Last", ScriptDataType::INT, ScriptValue(9)},
        },
        {
            {PinKind::EXECUTION, "Body", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Index", ScriptDataType::INT, ScriptValue(0)},
            {PinKind::EXECUTION, "Completed", ScriptDataType::BOOL, {}},
            // "Clamped" signals that the designer's requested iteration count
            // exceeded MAX_FOR_ITERATIONS and the loop was truncated (audit M3).
            // Scripts can branch on this to surface a runtime warning or to
            // fall back to a paged strategy.
            {PinKind::DATA, "Clamped", ScriptDataType::BOOL, ScriptValue(false)},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            int32_t first = ctx.readInputAs<int32_t>(node, "First");
            int32_t last = ctx.readInputAs<int32_t>(node, "Last");

            static const PinId pinClamped = internPin("Clamped");
            static const PinId pinIndex   = internPin("Index");

            ScriptNodeInstance* mutNode = ctx.instance().getNodeInstance(node.nodeId);
            if (mutNode)
            {
                mutNode->outputValues[pinClamped] = ScriptValue(false);
            }

            if (last < first)
            {
                ctx.triggerOutput(node, "Completed");
                return;
            }

            // Compute the iteration count in int64 to avoid signed overflow
            // at boundary inputs (e.g. first=INT32_MIN, last=INT32_MAX).
            const int64_t count64 =
                static_cast<int64_t>(last) - static_cast<int64_t>(first) + 1;
            const bool clamped = (count64 > MAX_FOR_ITERATIONS);
            int32_t count = clamped ? MAX_FOR_ITERATIONS
                                    : static_cast<int32_t>(count64);
            if (clamped)
            {
                Logger::warning(
                    "[ForLoop] Iteration count " + std::to_string(count64) +
                    " exceeds MAX_FOR_ITERATIONS — clamping");
                if (mutNode)
                {
                    mutNode->outputValues[pinClamped] = ScriptValue(true);
                }
            }

            for (int32_t i = 0; i < count; ++i)
            {
                int32_t index = first + i;
                if (mutNode)
                {
                    mutNode->outputValues[pinIndex] = ScriptValue(index);
                }
                ctx.triggerOutput(node, "Body");
            }
            ctx.triggerOutput(node, "Completed");
        }
    });

    // -----------------------------------------------------------------------
    // WhileLoop — repeats Body while Condition is true. Condition is
    // re-evaluated each iteration (pulled from a connected pure node).
    // Bounded by MAX_WHILE_ITERATIONS as a safety valve.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "WhileLoop",
        "While Loop",
        "Flow Control",
        "Repeats Body while Condition is true (safety cap: 10000 iterations)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Condition", ScriptDataType::BOOL, ScriptValue(false)},
        },
        {
            {PinKind::EXECUTION, "Body", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Completed", ScriptDataType::BOOL, {}},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            int32_t iterations = 0;
            while (iterations < MAX_WHILE_ITERATIONS)
            {
                bool cond = ctx.readInputAs<bool>(node, "Condition");
                if (!cond)
                {
                    break;
                }
                ctx.triggerOutput(node, "Body");
                ++iterations;
            }
            if (iterations >= MAX_WHILE_ITERATIONS)
            {
                Logger::warning(
                    "[WhileLoop] Hit iteration cap — possible runaway loop");
            }
            ctx.triggerOutput(node, "Completed");
        }
    });

    // -----------------------------------------------------------------------
    // Gate — a controllable valve. Starts closed by default (configurable).
    // -----------------------------------------------------------------------
    registry.registerNode({
        "Gate",
        "Gate",
        "Flow Control",
        "Controllable valve: Enter passes through when open. Open/Close/Toggle control state.",
        {
            {PinKind::EXECUTION, "Enter", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Open", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Close", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "Toggle", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "StartClosed", ScriptDataType::BOOL, ScriptValue(true)},
        },
        {{PinKind::EXECUTION, "Out", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            static const PinId stateInit = internPin("_init");
            static const PinId stateOpen = internPin("_open");
            static const PinId pinEnter  = internPin("Enter");
            static const PinId pinOpen   = internPin("Open");
            static const PinId pinClose  = internPin("Close");
            static const PinId pinToggle = internPin("Toggle");

            // Gate uses runtime state to track open/closed.
            auto* state = getMutableRuntimeState(ctx, node.nodeId);
            if (!state) return;

            // Initialise on first execution
            if (state->find(stateInit) == state->end())
            {
                bool startClosed = ctx.readInputAs<bool>(node, "StartClosed");
                (*state)[stateOpen] = ScriptValue(!startClosed);
                (*state)[stateInit] = ScriptValue(true);
            }

            // Dispatch on the input pin that fired this execution chain
            // (audit L6 — entryPin field on ScriptContext). Defaults to
            // Enter when called directly via executeNode (no incoming
            // connection), preserving the historical behavior.
            const PinId entry = ctx.entryPin();
            if (entry == pinOpen)
            {
                (*state)[stateOpen] = ScriptValue(true);
                return;
            }
            if (entry == pinClose)
            {
                (*state)[stateOpen] = ScriptValue(false);
                return;
            }
            if (entry == pinToggle)
            {
                (*state)[stateOpen] = ScriptValue(!(*state)[stateOpen].asBool());
                return;
            }
            // entry == pinEnter (or INVALID_PIN_ID for direct execute):
            // pass-through when open.
            (void)pinEnter;
            const bool isOpen = (*state)[stateOpen].asBool();
            if (isOpen)
            {
                ctx.triggerOutput(node, "Out");
            }
        }
    });

    // -----------------------------------------------------------------------
    // DoOnce — executes once per lifetime (or until Reset). Reset is input-only
    // (same limitation as Gate; see note above).
    // -----------------------------------------------------------------------
    registry.registerNode({
        "DoOnce",
        "Do Once",
        "Flow Control",
        "Executes the first time, then blocks until the instance is reset",
        {{PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}}},
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            static const PinId stateFired = internPin("_fired");

            auto* state = getMutableRuntimeState(ctx, node.nodeId);
            if (!state) return;

            auto it = state->find(stateFired);
            if (it == state->end() || !it->second.asBool())
            {
                (*state)[stateFired] = ScriptValue(true);
                ctx.triggerOutput(node, "Then");
            }
            // else: blocked — no output
        }
    });

    // -----------------------------------------------------------------------
    // FlipFlop — alternates between A and B output each time it fires.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "FlipFlop",
        "Flip Flop",
        "Flow Control",
        "Alternates between A and B output each time it fires",
        {{PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}}},
        {
            {PinKind::EXECUTION, "A", ScriptDataType::BOOL, {}},
            {PinKind::EXECUTION, "B", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "IsA", ScriptDataType::BOOL, ScriptValue(true)},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            static const PinId stateIsA = internPin("_isA");
            static const PinId pinIsA   = internPin("IsA");

            auto* state = getMutableRuntimeState(ctx, node.nodeId);
            if (!state) return;

            auto it = state->find(stateIsA);
            bool nextIsA = (it == state->end()) ? true : it->second.asBool();

            ScriptNodeInstance* mutNode = ctx.instance().getNodeInstance(node.nodeId);
            if (mutNode)
            {
                mutNode->outputValues[pinIsA] = ScriptValue(nextIsA);
            }

            if (nextIsA)
            {
                ctx.triggerOutput(node, "A");
            }
            else
            {
                ctx.triggerOutput(node, "B");
            }

            // Toggle for next call
            (*state)[stateIsA] = ScriptValue(!nextIsA);
        }
    });
}

} // namespace Vestige
