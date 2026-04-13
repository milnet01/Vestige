/// @file scripting_system.cpp
/// @brief ScriptingSystem implementation — visual script lifecycle and execution.
#include "scripting/scripting_system.h"
#include "scripting/script_context.h"
#include "scripting/script_events.h"
#include "core/engine.h"
#include "core/system_events.h"
#include "core/logger.h"

#include <algorithm>

namespace Vestige
{

namespace
{

/// @brief Helper that subscribes one event node to an EventBus event type.
/// The populate lambda copies event fields into the node's output data pins,
/// then the trigger pin is fired to continue execution.
template <typename EventT>
SubscriptionId subscribeOneEventNode(
    Engine& engine,
    const NodeTypeRegistry& registry,
    ScriptInstance& instance,
    uint32_t nodeId,
    const std::string& triggerPin,
    std::function<void(const EventT&, ScriptNodeInstance&)> populate)
{
    return engine.getEventBus().subscribe<EventT>(
        [&engine, &registry, &instance, nodeId, triggerPin, populate]
        (const EventT& event)
        {
            if (!instance.isActive())
            {
                return;
            }
            ScriptNodeInstance* nodeInst = instance.getNodeInstance(nodeId);
            if (!nodeInst)
            {
                return;
            }
            populate(event, *nodeInst);
            ScriptContext ctx(instance, registry, engine);
            ctx.triggerOutput(*nodeInst, triggerPin);
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
                *m_engine, m_nodeRegistry, instance, nodeDef.id, "Pressed",
                [](const KeyPressedEvent& e, ScriptNodeInstance& n)
                {
                    n.outputValues["keyCode"] = ScriptValue(static_cast<int32_t>(e.keyCode));
                    n.outputValues["isRepeat"] = ScriptValue(e.isRepeat);
                });
        }
        else if (eventTypeName == "KeyReleasedEvent")
        {
            subId = subscribeOneEventNode<KeyReleasedEvent>(
                *m_engine, m_nodeRegistry, instance, nodeDef.id, "Released",
                [](const KeyReleasedEvent& e, ScriptNodeInstance& n)
                {
                    n.outputValues["keyCode"] = ScriptValue(static_cast<int32_t>(e.keyCode));
                });
        }
        else if (eventTypeName == "MouseButtonPressedEvent")
        {
            subId = subscribeOneEventNode<MouseButtonPressedEvent>(
                *m_engine, m_nodeRegistry, instance, nodeDef.id, "Pressed",
                [](const MouseButtonPressedEvent& e, ScriptNodeInstance& n)
                {
                    n.outputValues["button"] = ScriptValue(static_cast<int32_t>(e.button));
                });
        }
        else if (eventTypeName == "SceneLoadedEvent")
        {
            subId = subscribeOneEventNode<SceneLoadedEvent>(
                *m_engine, m_nodeRegistry, instance, nodeDef.id, "Loaded",
                [](const SceneLoadedEvent&, ScriptNodeInstance&) {});
        }
        else if (eventTypeName == "WeatherChangedEvent")
        {
            subId = subscribeOneEventNode<WeatherChangedEvent>(
                *m_engine, m_nodeRegistry, instance, nodeDef.id, "Changed",
                [](const WeatherChangedEvent& e, ScriptNodeInstance& n)
                {
                    n.outputValues["temperature"] = ScriptValue(e.temperature);
                    n.outputValues["humidity"] = ScriptValue(e.humidity);
                    n.outputValues["precipitation"] = ScriptValue(e.precipitation);
                    n.outputValues["windStrength"] = ScriptValue(e.windStrength);
                });
        }
        else if (eventTypeName == "ScriptCustomEvent")
        {
            // OnCustomEvent: filter by name property, expose payload as data
            std::string filterName;
            auto propIt = nodeDef.properties.find("Name");
            if (propIt != nodeDef.properties.end())
            {
                filterName = propIt->second.asString();
            }
            subId = subscribeOneEventNode<ScriptCustomEvent>(
                *m_engine, m_nodeRegistry, instance, nodeDef.id, "Fired",
                [filterName](const ScriptCustomEvent& e, ScriptNodeInstance& n)
                {
                    // If no filter given, pass through every event
                    if (!filterName.empty() && e.name != filterName)
                    {
                        n.outputValues["_filtered"] = ScriptValue(true);
                        return;
                    }
                    n.outputValues["name"] = ScriptValue(e.name);
                    n.outputValues["payload"] = e.payload;
                    n.outputValues["_filtered"] = ScriptValue(false);
                });
            // We still trigger the output unconditionally, but filtered events
            // won't carry useful data. Cleaner: handle filter inside lambda by
            // not triggering at all. Switch to the filter-aware helper below.
        }
        else if (eventTypeName == "EntityDestroyedEvent")
        {
            // OnDestroy listens to EntityDestroyedEvent but filters by entity id
            uint32_t ownerEntity = instance.entityId();
            subId = m_engine->getEventBus().subscribe<EntityDestroyedEvent>(
                [this, &instance, nodeId = nodeDef.id, ownerEntity]
                (const EntityDestroyedEvent& e)
                {
                    if (!instance.isActive()) return;
                    if (ownerEntity != 0 && e.entityId != ownerEntity) return;
                    ScriptNodeInstance* ni = instance.getNodeInstance(nodeId);
                    if (!ni) return;
                    ScriptContext ctx(instance, m_nodeRegistry, *m_engine);
                    ctx.triggerOutput(*ni, "Destroying");
                });
        }
        // Other event types (OnTriggerEnter/Exit, OnCollisionEnter/Exit,
        // OnAudioFinished, OnVariableChanged) are registered as descriptors
        // so they appear in the palette but are not wired yet — they require
        // new engine events that do not exist yet.

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

    ScriptContext ctx(instance, m_nodeRegistry, *m_engine);
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

        // Find all OnUpdate nodes in this instance
        auto updateNodes = instance->findNodesByType("OnUpdate");
        for (uint32_t nodeId : updateNodes)
        {
            ScriptNodeInstance* nodeInst = instance->getNodeInstance(nodeId);
            if (!nodeInst)
            {
                continue;
            }

            // Set deltaTime as the node's output before triggering
            nodeInst->outputValues["deltaTime"] = ScriptValue(deltaTime);

            ScriptContext ctx(*instance, m_nodeRegistry, *m_engine);
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
        std::vector<PendingLatentAction> completed;

        for (auto& action : actions)
        {
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
            // [0,1] while the action is still pending.
            if (action.onTick && action.totalDuration > 0.0f)
            {
                float progress = 1.0f - (action.remainingTime / action.totalDuration);
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;
                action.onTick(progress);
            }

            if (done)
            {
                completed.push_back(action);
            }
        }

        // Remove completed actions and fire their continuations.
        // Note: use a snapshot (matching nodeId+outputPin) to avoid O(N^2)
        // misses when multiple pending actions share a node.
        for (const auto& action : completed)
        {
            // Remove the first match from pending list
            auto matchIt = std::find_if(actions.begin(), actions.end(),
                [&action](const PendingLatentAction& a)
                {
                    return a.nodeId == action.nodeId &&
                           a.outputPin == action.outputPin;
                });
            if (matchIt != actions.end())
            {
                actions.erase(matchIt);
            }

            // Fire the continuation
            ScriptNodeInstance* nodeInst = instance->getNodeInstance(action.nodeId);
            if (nodeInst)
            {
                ScriptContext ctx(*instance, m_nodeRegistry, *m_engine);
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
