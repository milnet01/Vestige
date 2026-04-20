// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scripting_system.cpp
/// @brief ScriptingSystem implementation — visual script lifecycle and execution.
#include "scripting/scripting_system.h"
#include "scripting/script_compiler.h"
#include "scripting/script_context.h"
#include "scripting/script_events.h"
#include "core/engine.h"
#include "core/system_events.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vestige
{

namespace
{

/// @brief Helper that subscribes one event node to an EventBus event type.
/// The populate lambda copies event fields into the node's output data pins,
/// then the trigger pin is fired to continue execution.
///
/// Lifetime model: the lambda captures *pointers* and performs a liveness
/// check via ScriptingSystem::isInstanceActive before dereferencing. This
/// prevents use-after-free if an instance is destroyed without a prior
/// unregisterInstance() call.
///
/// A populate lambda that returns `false` (via the overload taking a
/// predicate-shaped callable) signals "filter mismatch" — the trigger pin is
/// NOT fired in that case (audit M1).
template <typename EventT>
SubscriptionId subscribeOneEventNode(
    ScriptingSystem* sys,
    ScriptInstance* instance,
    uint32_t nodeId,
    std::string triggerPin,
    std::function<void(const EventT&, ScriptNodeInstance&)> populate)
{
    return sys->engine()->getEventBus().subscribe<EventT>(
        [sys, instance, nodeId, triggerPin = std::move(triggerPin),
         populate = std::move(populate)]
        (const EventT& event)
        {
            // Liveness guard: only dereference if the system still has this
            // instance registered. An unregistered (or destroyed) instance
            // drops the event silently.
            if (!sys || !sys->isInstanceActive(instance))
            {
                return;
            }
            // Re-entrancy guard: ScriptContext::MAX_CALL_DEPTH resets on
            // every fresh EventBus dispatch, so a PublishEvent node that
            // re-publishes the event type its OnCustomEvent is listening for
            // would infinitely recurse without this check.
            if (instance->eventDispatchDepth() >=
                ScriptInstance::MAX_EVENT_REENTRY_DEPTH)
            {
                Logger::warning(
                    "[ScriptingSystem] Event re-entrancy cap hit in graph '" +
                    instance->graph().name + "' — event dropped");
                return;
            }
            ScriptNodeInstance* nodeInst = instance->getNodeInstance(nodeId);
            if (!nodeInst)
            {
                return;
            }
            populate(event, *nodeInst);
            instance->incrementEventDispatchDepth();
            {
                ScriptContext ctx(*instance, sys->nodeRegistry(), sys->engine());
                ctx.triggerOutput(*nodeInst, triggerPin);
            }
            instance->decrementEventDispatchDepth();
        });
}

/// @brief Variant of subscribeOneEventNode that honors a populate function
/// returning bool: true = fire the trigger, false = filter out (skip).
/// Used for OnCustomEvent where the node has a per-event name filter and
/// should NOT fire downstream execution on filter mismatch.
template <typename EventT>
SubscriptionId subscribeFilteredEventNode(
    ScriptingSystem* sys,
    ScriptInstance* instance,
    uint32_t nodeId,
    std::string triggerPin,
    std::function<bool(const EventT&, ScriptNodeInstance&)> populate)
{
    return sys->engine()->getEventBus().subscribe<EventT>(
        [sys, instance, nodeId, triggerPin = std::move(triggerPin),
         populate = std::move(populate)]
        (const EventT& event)
        {
            if (!sys || !sys->isInstanceActive(instance))
            {
                return;
            }
            if (instance->eventDispatchDepth() >=
                ScriptInstance::MAX_EVENT_REENTRY_DEPTH)
            {
                Logger::warning(
                    "[ScriptingSystem] Event re-entrancy cap hit in graph '" +
                    instance->graph().name + "' — event dropped");
                return;
            }
            ScriptNodeInstance* nodeInst = instance->getNodeInstance(nodeId);
            if (!nodeInst)
            {
                return;
            }
            if (!populate(event, *nodeInst))
            {
                // Filter rejected this event — do not trigger downstream.
                return;
            }
            instance->incrementEventDispatchDepth();
            {
                ScriptContext ctx(*instance, sys->nodeRegistry(), sys->engine());
                ctx.triggerOutput(*nodeInst, triggerPin);
            }
            instance->decrementEventDispatchDepth();
        });
}

} // namespace

const std::string ScriptingSystem::SYSTEM_NAME = "Scripting";

// ---------------------------------------------------------------------------
// ISystem interface
// ---------------------------------------------------------------------------

const std::string& ScriptingSystem::getSystemName() const
{
    return SYSTEM_NAME;
}

bool ScriptingSystem::initialize(Engine& engine)
{
    m_engine = &engine;

    // Register all built-in node types
    registerCoreNodes();

    Logger::info("[ScriptingSystem] Initialized with " +
                 std::to_string(m_nodeRegistry.nodeCount()) + " node types");
    return true;
}

void ScriptingSystem::shutdown()
{
    m_activeInstances.clear();
    m_sceneBlackboard.clear();
    // Note: appBlackboard persists across scenes but is cleared on shutdown
    m_appBlackboard.clear();
    m_engine = nullptr;

    Logger::info("[ScriptingSystem] Shut down");
}

void ScriptingSystem::update(float deltaTime)
{
    // 1. Tick OnUpdate event nodes
    tickUpdateNodes(deltaTime);

    // 2. Tick pending latent actions (timers, conditions)
    tickLatentActions(deltaTime);
}

void ScriptingSystem::onSceneLoad(Scene& /*scene*/)
{
    // In the future, this will:
    // 1. Find all entities with ScriptComponent
    // 2. Load their graph assets
    // 3. Create ScriptInstance objects + call registerInstance() on each
    // 5. Fire OnStart for all scripts
    //
    // For now, instances are managed manually (via registerInstance) or by
    // the editor test-play workflow.

    Logger::info("[ScriptingSystem] Scene loaded — " +
                 std::to_string(m_activeInstances.size()) + " active scripts");
}

void ScriptingSystem::onSceneUnload(Scene& /*scene*/)
{
    // Unsubscribe all event nodes from EventBus
    if (m_engine)
    {
        for (auto* instance : m_activeInstances)
        {
            for (uint32_t subId : instance->subscriptions())
            {
                m_engine->getEventBus().unsubscribe(subId);
            }
            instance->clearSubscriptions();
            instance->pendingActions().clear();
            instance->setActive(false);
        }
    }

    m_activeInstances.clear();
    m_sceneBlackboard.clear();

    Logger::info("[ScriptingSystem] Scene unloaded — all scripts deactivated");
}

// ---------------------------------------------------------------------------
// Instance registration
// ---------------------------------------------------------------------------

void ScriptingSystem::registerInstance(ScriptInstance& instance)
{
    if (!m_engine)
    {
        Logger::warning("[ScriptingSystem] registerInstance called before initialize");
        return;
    }

    // Prevent double-registration
    auto it = std::find(m_activeInstances.begin(), m_activeInstances.end(),
                        &instance);
    if (it != m_activeInstances.end())
    {
        return;
    }

    // Validate the graph against the current node registry. Errors (unknown
    // types, dangling connections, pin-type mismatches, pure-data cycles)
    // would otherwise surface inside ScriptContext with a partial trace —
    // at registration we still have the full graph in scope, so log it all
    // and refuse to activate. Warnings are recorded but don't block.
    const CompilationResult compileResult =
        ScriptGraphCompiler::compile(instance.graph(), m_nodeRegistry);
    for (const auto& diag : compileResult.diagnostics)
    {
        const std::string prefix =
            "[ScriptingSystem] graph '" + instance.graph().name + "': ";
        switch (diag.severity)
        {
        case CompileSeverity::ERROR:
            Logger::error(prefix + diag.message);
            break;
        case CompileSeverity::WARNING:
            Logger::warning(prefix + diag.message);
            break;
        case CompileSeverity::INFO:
            Logger::info(prefix + diag.message);
            break;
        }
    }
    if (!compileResult.success)
    {
        Logger::error("[ScriptingSystem] Refusing to activate graph '" +
                      instance.graph().name +
                      "' — compile produced " +
                      std::to_string(
                          compileResult.countAt(CompileSeverity::ERROR)) +
                      " error(s)");
        return;
    }

    subscribeEventNodes(instance);
    instance.setActive(true);
    m_activeInstances.push_back(&instance);
}

void ScriptingSystem::unregisterInstance(ScriptInstance& instance)
{
    if (m_engine)
    {
        for (uint32_t subId : instance.subscriptions())
        {
            m_engine->getEventBus().unsubscribe(subId);
        }
    }
    instance.clearSubscriptions();
    instance.pendingActions().clear();
    instance.setActive(false);

    m_activeInstances.erase(
        std::remove(m_activeInstances.begin(), m_activeInstances.end(),
                    &instance),
        m_activeInstances.end());
}

bool ScriptingSystem::isInstanceActive(const ScriptInstance* instance) const
{
    if (!instance)
    {
        return false;
    }
    // Liveness check: pointer must still be in the active list. A destroyed
    // instance was either unregistered first (removing it from the list) or
    // the registration invariant was violated (caller bug).
    return std::find(m_activeInstances.begin(), m_activeInstances.end(),
                     instance) != m_activeInstances.end();
}

// ---------------------------------------------------------------------------
// Event node bridge — subscribe graph event nodes to EventBus
// ---------------------------------------------------------------------------

void ScriptingSystem::subscribeEventNodes(ScriptInstance& instance)
{
    if (!m_engine)
    {
        return;
    }

    const ScriptGraph& graph = instance.graph();
    for (const auto& nodeDef : graph.nodes)
    {
        const NodeTypeDescriptor* desc = m_nodeRegistry.findNode(nodeDef.typeName);
        if (!desc || desc->eventTypeName.empty())
        {
            continue;
        }

        SubscriptionId subId = 0;
        const std::string& eventTypeName = desc->eventTypeName;

        if (eventTypeName == "KeyPressedEvent")
        {
            subId = subscribeOneEventNode<KeyPressedEvent>(
                this, &instance, nodeDef.id, "Pressed",
                [](const KeyPressedEvent& e, ScriptNodeInstance& n)
                {
                    static const PinId pinKeyCode = internPin("keyCode");
                    static const PinId pinIsRepeat = internPin("isRepeat");
                    n.outputValues[pinKeyCode] = ScriptValue(static_cast<int32_t>(e.keyCode));
                    n.outputValues[pinIsRepeat] = ScriptValue(e.isRepeat);
                });
        }
        else if (eventTypeName == "KeyReleasedEvent")
        {
            subId = subscribeOneEventNode<KeyReleasedEvent>(
                this, &instance, nodeDef.id, "Released",
                [](const KeyReleasedEvent& e, ScriptNodeInstance& n)
                {
                    static const PinId pinKeyCode = internPin("keyCode");
                    n.outputValues[pinKeyCode] = ScriptValue(static_cast<int32_t>(e.keyCode));
                });
        }
        else if (eventTypeName == "MouseButtonPressedEvent")
        {
            subId = subscribeOneEventNode<MouseButtonPressedEvent>(
                this, &instance, nodeDef.id, "Pressed",
                [](const MouseButtonPressedEvent& e, ScriptNodeInstance& n)
                {
                    static const PinId pinButton = internPin("button");
                    n.outputValues[pinButton] = ScriptValue(static_cast<int32_t>(e.button));
                });
        }
        else if (eventTypeName == "SceneLoadedEvent")
        {
            subId = subscribeOneEventNode<SceneLoadedEvent>(
                this, &instance, nodeDef.id, "Loaded",
                [](const SceneLoadedEvent&, ScriptNodeInstance&) {});
        }
        else if (eventTypeName == "WeatherChangedEvent")
        {
            subId = subscribeOneEventNode<WeatherChangedEvent>(
                this, &instance, nodeDef.id, "Changed",
                [](const WeatherChangedEvent& e, ScriptNodeInstance& n)
                {
                    static const PinId pinTemperature   = internPin("temperature");
                    static const PinId pinHumidity      = internPin("humidity");
                    static const PinId pinPrecipitation = internPin("precipitation");
                    static const PinId pinWindStrength  = internPin("windStrength");
                    n.outputValues[pinTemperature]   = ScriptValue(e.temperature);
                    n.outputValues[pinHumidity]      = ScriptValue(e.humidity);
                    n.outputValues[pinPrecipitation] = ScriptValue(e.precipitation);
                    n.outputValues[pinWindStrength]  = ScriptValue(e.windStrength);
                });
        }
        else if (eventTypeName == "ScriptCustomEvent")
        {
            // OnCustomEvent: filter by name property, expose payload as data.
            // Filter mismatches are reported through the populate lambda's
            // bool return — subscribeFilteredEventNode suppresses the trigger
            // on false (audit M1).
            std::string filterName;
            auto propIt = nodeDef.properties.find("Name");
            if (propIt != nodeDef.properties.end())
            {
                filterName = propIt->second.asString();
            }
            subId = subscribeFilteredEventNode<ScriptCustomEvent>(
                this, &instance, nodeDef.id, "Fired",
                [filterName](const ScriptCustomEvent& e, ScriptNodeInstance& n) -> bool
                {
                    // Empty filter = pass through every event.
                    if (!filterName.empty() && e.name != filterName)
                    {
                        return false;
                    }
                    static const PinId pinName = internPin("name");
                    static const PinId pinPayload = internPin("payload");
                    n.outputValues[pinName] = ScriptValue(e.name);
                    n.outputValues[pinPayload] = e.payload;
                    return true;
                });
        }
        else if (eventTypeName == "EntityDestroyedEvent")
        {
            // OnDestroy listens to EntityDestroyedEvent but filters by entity id.
            // Capture a ScriptingSystem* + ScriptInstance* pair and perform a
            // liveness lookup before dereferencing — see subscribeOneEventNode.
            uint32_t ownerEntity = instance.entityId();
            ScriptInstance* instancePtr = &instance;
            ScriptingSystem* sys = this;
            subId = m_engine->getEventBus().subscribe<EntityDestroyedEvent>(
                [sys, instancePtr, nodeId = nodeDef.id, ownerEntity]
                (const EntityDestroyedEvent& e)
                {
                    if (!sys || !sys->isInstanceActive(instancePtr)) return;
                    if (ownerEntity != 0 && e.entityId != ownerEntity) return;
                    ScriptNodeInstance* ni = instancePtr->getNodeInstance(nodeId);
                    if (!ni) return;
                    ScriptContext ctx(*instancePtr, sys->nodeRegistry(),
                                      sys->engine());
                    ctx.triggerOutput(*ni, "Destroying");
                });
        }
        // Other event types (OnTriggerEnter/Exit, OnCollisionEnter/Exit,
        // OnAudioFinished, OnVariableChanged) are registered as descriptors
        // so they appear in the palette but are not wired yet — they require
        // new engine events that do not exist yet.
        else
        {
            // AUDIT.md §M7 / FIXPLAN: warn on unknown eventTypeName so that
            // typos don't silently produce non-firing nodes. The known-not-
            // yet-wired set is explicitly ignored to avoid noise.
            static const std::unordered_set<std::string> kKnownUnwired = {
                "TriggerEnterEvent", "TriggerExitEvent",
                "CollisionEnterEvent", "CollisionExitEvent",
                "AudioFinishedEvent", "VariableChangedEvent"
            };
            if (kKnownUnwired.count(eventTypeName) == 0)
            {
                Logger::warning(std::string("[ScriptingSystem] No "
                    "subscription branch for eventTypeName '") +
                    eventTypeName + "' on node id " +
                    std::to_string(nodeDef.id) + " (type '" +
                    nodeDef.typeName + "'). Node will never fire — likely "
                    "a typo. See AUDIT.md §M7.");
            }
        }

        if (subId != 0)
        {
            instance.addSubscription(subId);
        }
    }
}

// ---------------------------------------------------------------------------
// Event firing
// ---------------------------------------------------------------------------

void ScriptingSystem::fireEvent(ScriptInstance& instance, uint32_t nodeId)
{
    if (!m_engine)
    {
        return;
    }

    ScriptContext ctx(instance, m_nodeRegistry, m_engine);
    ctx.executeNode(nodeId);
}

// ---------------------------------------------------------------------------
// Per-frame updates
// ---------------------------------------------------------------------------

void ScriptingSystem::tickUpdateNodes(float deltaTime)
{
    if (!m_engine)
    {
        return;
    }

    for (auto* instance : m_activeInstances)
    {
        if (!instance->isActive())
        {
            continue;
        }

        // Use the cached OnUpdate node list built at initialize() time. This
        // collapses a per-frame O(nodes) scan into O(OnUpdates) (audit H3).
        for (uint32_t nodeId : instance->updateNodes())
        {
            ScriptNodeInstance* nodeInst = instance->getNodeInstance(nodeId);
            if (!nodeInst)
            {
                continue;
            }

            // Set deltaTime as the node's output before triggering
            static const PinId pinDeltaTime = internPin("deltaTime");
            nodeInst->outputValues[pinDeltaTime] = ScriptValue(deltaTime);

            ScriptContext ctx(*instance, m_nodeRegistry, m_engine);
            ctx.triggerOutput(*nodeInst, "Tick");
        }
    }
}

void ScriptingSystem::tickLatentActions(float deltaTime)
{
    if (!m_engine)
    {
        return;
    }

    for (auto* instance : m_activeInstances)
    {
        if (!instance->isActive())
        {
            continue;
        }

        auto& actions = instance->pendingActions();
        const size_t countAtStart = actions.size();

        // Single pass: update each action's state, classify as done/not-done,
        // and partition in place. O(N) vs. the prior O(N^2) that used
        // find_if+erase per completed action (audit H5).
        std::vector<PendingLatentAction> completed;
        std::vector<PendingLatentAction> pending;
        pending.reserve(countAtStart);

        for (size_t i = 0; i < countAtStart; ++i)
        {
            auto& action = actions[i];
            bool done = false;

            if (action.condition)
            {
                // Condition-based: check each frame
                done = action.condition();
            }
            else if (action.remainingTime >= 0.0f)
            {
                // Time-based: count down
                action.remainingTime -= deltaTime;
                done = action.remainingTime <= 0.0f;
            }

            // Per-frame tick callback (Timeline, MoveTo) — fires with progress
            // [0,1] while the action is still pending. Guard against non-finite
            // inputs that would produce NaN progress.
            if (action.onTick && std::isfinite(action.totalDuration) &&
                action.totalDuration > 0.0f)
            {
                float progress = 1.0f - (action.remainingTime / action.totalDuration);
                if (!std::isfinite(progress)) progress = 0.0f;
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;
                action.onTick(progress);
            }

            if (done)
            {
                completed.push_back(std::move(action));
            }
            else
            {
                pending.push_back(std::move(action));
            }
        }

        // Preserve any actions that were scheduled during an onTick callback
        // (rare — none of the built-in nodes currently do this, but keep the
        // semantics correct).
        for (size_t i = countAtStart; i < actions.size(); ++i)
        {
            pending.push_back(std::move(actions[i]));
        }
        actions = std::move(pending);

        // Fire continuations. triggerOutput may itself schedule new latent
        // actions via scheduleDelay — those are appended to `actions` for
        // the next frame.
        for (const auto& action : completed)
        {
            ScriptNodeInstance* nodeInst = instance->getNodeInstance(action.nodeId);
            if (nodeInst)
            {
                ScriptContext ctx(*instance, m_nodeRegistry, m_engine);
                ctx.triggerOutput(*nodeInst, action.outputPin);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Node registration (forward declarations — implemented in category files)
// ---------------------------------------------------------------------------

void registerCoreNodeTypes(NodeTypeRegistry& registry);
void registerEventNodeTypes(NodeTypeRegistry& registry);
void registerActionNodeTypes(NodeTypeRegistry& registry);
void registerPureNodeTypes(NodeTypeRegistry& registry);
void registerFlowNodeTypes(NodeTypeRegistry& registry);
void registerLatentNodeTypes(NodeTypeRegistry& registry);

void ScriptingSystem::registerCoreNodes()
{
    registerCoreNodeTypes(m_nodeRegistry);
    registerEventNodeTypes(m_nodeRegistry);
    registerActionNodeTypes(m_nodeRegistry);
    registerPureNodeTypes(m_nodeRegistry);
    registerFlowNodeTypes(m_nodeRegistry);
    registerLatentNodeTypes(m_nodeRegistry);
}

} // namespace Vestige
