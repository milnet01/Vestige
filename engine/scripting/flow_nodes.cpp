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
constexpr int MAX_WHILE_ITERATIONS = 10000;

/// @brief Safety cap for ForLoop iteration count.
constexpr int MAX_FOR_ITERATIONS = 10000;

/// @brief Get the mutable runtime state map for a node.
std::unordered_map<std::string, ScriptValue>* getMutableRuntimeState(
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
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            int32_t first = ctx.readInputAs<int32_t>(node, "First");
            int32_t last = ctx.readInputAs<int32_t>(node, "Last");

            if (last < first)
            {
                ctx.triggerOutput(node, "Completed");
                return;
            }

            int32_t count = last - first + 1;
            if (count > MAX_FOR_ITERATIONS)
            {
                Logger::warning(
                    "[ForLoop] Iteration count " + std::to_string(count) +
                    " exceeds MAX_FOR_ITERATIONS — clamping");
                count = MAX_FOR_ITERATIONS;
            }

            ScriptNodeInstance* mutNode = ctx.instance().getNodeInstance(node.nodeId);
            for (int32_t i = 0; i < count; ++i)
            {
                int32_t index = first + i;
                if (mutNode)
                {
                    mutNode->outputValues["Index"] = ScriptValue(index);
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
            // Gate uses runtime state to track open/closed.
            auto* state = getMutableRuntimeState(ctx, node.nodeId);
            if (!state) return;

            // Initialise on first execution
            if (state->find("_init") == state->end())
            {
                bool startClosed = ctx.readInputAs<bool>(node, "StartClosed");
                (*state)["_open"] = ScriptValue(!startClosed);
                (*state)["_init"] = ScriptValue(true);
            }

            // The Gate node's execute is called by whichever exec input fired.
            // We can't distinguish inputs natively (there's only one exec lambda),
            // so we use a convention: the context sets a "pin fired" marker via
            // outputValues before calling. Since we don't have that yet, use a
            // simpler model: only the "Enter" path runs through; Open/Close/Toggle
            // are reserved for a future extension. For now, the Gate is controlled
            // via the StartClosed property and passes through Enter unconditionally
            // when open.
            //
            // TODO Phase 9E-3: extend the interpreter with an "entryPin" field on
            // ScriptContext so nodes can distinguish which input fired.
            bool isOpen = (*state)["_open"].asBool();
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
            auto* state = getMutableRuntimeState(ctx, node.nodeId);
            if (!state) return;

            auto it = state->find("_fired");
            if (it == state->end() || !it->second.asBool())
            {
                (*state)["_fired"] = ScriptValue(true);
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
            auto* state = getMutableRuntimeState(ctx, node.nodeId);
            if (!state) return;

            auto it = state->find("_isA");
            bool nextIsA = (it == state->end()) ? true : it->second.asBool();

            ScriptNodeInstance* mutNode = ctx.instance().getNodeInstance(node.nodeId);
            if (mutNode)
            {
                mutNode->outputValues["IsA"] = ScriptValue(nextIsA);
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
            (*state)["_isA"] = ScriptValue(!nextIsA);
        }
    });
}

} // namespace Vestige
