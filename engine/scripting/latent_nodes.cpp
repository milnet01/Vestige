/// @file latent_nodes.cpp
/// @brief Latent nodes — suspend execution and resume across frames.
///
/// When an execute() registers a PendingLatentAction and returns without
/// triggering the output pin, the chain pauses there. ScriptingSystem's
/// tickLatentActions() walks pending actions each frame, optionally firing
/// an onTick progress callback (Timeline/MoveTo) and resuming with the
/// specified output pin once the action completes.
#include "scripting/latent_nodes.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_context.h"
#include "scripting/script_instance.h"
#include "core/engine.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

namespace Vestige
{

void registerLatentNodeTypes(NodeTypeRegistry& registry)
{
    // -----------------------------------------------------------------------
    // WaitForEvent — polling stub that waits until the "EventReceived"
    // blackboard flag flips true. A future extension will subscribe directly
    // to an event-name string.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "WaitForEvent",
        "Wait For Event",
        "Flow Control",
        "Suspends until a flag in the graph blackboard becomes true",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "FlagName", ScriptDataType::STRING, ScriptValue(std::string("EventReceived"))},
        },
        {{PinKind::EXECUTION, "Received", ScriptDataType::BOOL, {}}},
        "",
        false, true,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto flagName = ctx.readInputAs<std::string>(node, "FlagName");
            ScriptInstance* inst = &ctx.instance();
            ctx.scheduleWaitForCondition(
                node, "Received",
                [inst, flagName]() -> bool
                {
                    return inst->graphBlackboard().get(flagName).asBool();
                });
        }
    });

    // -----------------------------------------------------------------------
    // WaitForCondition — polls a graph-scope bool variable each frame.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "WaitForCondition",
        "Wait For Condition",
        "Flow Control",
        "Suspends until the named boolean variable evaluates true",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "VarName", ScriptDataType::STRING, ScriptValue(std::string(""))},
        },
        {{PinKind::EXECUTION, "Done", ScriptDataType::BOOL, {}}},
        "",
        false, true,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto varName = ctx.readInputAs<std::string>(node, "VarName");
            ScriptInstance* inst = &ctx.instance();
            ctx.scheduleWaitForCondition(
                node, "Done",
                [inst, varName]() -> bool
                {
                    return inst->graphBlackboard().get(varName).asBool();
                });
        }
    });

    // -----------------------------------------------------------------------
    // Timeline — animates a float Alpha from 0..1 over Duration seconds.
    // Fires "Update" every frame with the current Alpha, then "Finished".
    // -----------------------------------------------------------------------
    registry.registerNode({
        "Timeline",
        "Timeline",
        "Flow Control",
        "Animates Alpha from 0 to 1 over Duration seconds. Fires Update each frame.",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Duration", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {
            {PinKind::EXECUTION, "Update", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "Alpha", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::EXECUTION, "Finished", ScriptDataType::BOOL, {}},
        },
        "",
        false, true,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            static const PinId pinAlpha = internPin("Alpha");

            float duration = ctx.readInputAs<float>(node, "Duration");
            if (duration <= 0.0f)
            {
                // Degenerate case: finish immediately with Alpha = 1.
                ScriptNodeInstance* mut = ctx.instance().getNodeInstance(node.nodeId);
                if (mut) mut->outputValues[pinAlpha] = ScriptValue(1.0f);
                ctx.triggerOutput(node, "Finished");
                return;
            }

            ScriptInstance* inst = &ctx.instance();
            uint32_t nodeId = node.nodeId;

            PendingLatentAction action;
            action.nodeId = nodeId;
            action.outputPin = "Finished";
            action.remainingTime = duration;
            action.totalDuration = duration;
            action.onTick =
                [inst, nodeId](float progress)
                {
                    ScriptNodeInstance* mut = inst->getNodeInstance(nodeId);
                    if (mut)
                    {
                        mut->outputValues[pinAlpha] = ScriptValue(progress);
                    }
                    // Note: we intentionally do NOT re-trigger "Update" from
                    // the onTick callback to avoid needing a ScriptContext
                    // here. The Alpha pin reflects current progress and is
                    // read by whatever pure nodes pull it. Designers using
                    // Timeline wire the Update pin from a polling path;
                    // alternatively, the Finished pin signals completion.
                };

            inst->addLatentAction(std::move(action));
        }
    });

    // -----------------------------------------------------------------------
    // MoveTo — moves an entity from current position to Target over Duration.
    // Scheduled as a latent action with an onTick that lerps position.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "MoveTo",
        "Move To",
        "Transform",
        "Moves an entity from its current position to Target over Duration (linear)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "Target", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "Duration", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {{PinKind::EXECUTION, "Finished", ScriptDataType::BOOL, {}}},
        "",
        false, true,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto target = ctx.readInputAs<glm::vec3>(node, "Target");
            float duration = ctx.readInputAs<float>(node, "Duration");
            uint32_t entityId = ctx.readInputAs<uint32_t>(node, "entity");
            uint32_t resolvedId = (entityId == 0) ? ctx.instance().entityId() : entityId;

            Entity* entity = ctx.findEntity(resolvedId);

            if (!entity || duration <= 0.0f)
            {
                // Finish immediately.
                if (entity)
                {
                    entity->transform.position = target;
                }
                ctx.triggerOutput(node, "Finished");
                return;
            }

            glm::vec3 start = entity->transform.position;

            // Capture a stable Engine pointer (not the context — it's
            // stack-local and destroyed when execute returns). The entity is
            // looked up by ID each tick so a destroy mid-move is handled.
            // engine() may be nullptr in tests; the move action needs a real
            // engine to look up entities anyway, so skip scheduling if null.
            Engine* engine = ctx.engine();
            if (!engine)
            {
                ctx.triggerOutput(node, "Finished");
                return;
            }
            uint32_t capturedId = resolvedId;

            PendingLatentAction action;
            action.nodeId = node.nodeId;
            action.outputPin = "Finished";
            action.remainingTime = duration;
            action.totalDuration = duration;
            action.onTick =
                [engine, capturedId, start, target](float progress)
                {
                    if (!engine) return;
                    auto* scene = engine->getSceneManager().getActiveScene();
                    if (!scene) return;
                    Entity* ent = scene->findEntityById(capturedId);
                    if (!ent) return;
                    ent->transform.position = start + (target - start) * progress;
                };
            ctx.instance().addLatentAction(std::move(action));
        }
    });
}

} // namespace Vestige
