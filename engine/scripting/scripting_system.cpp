/// @file scripting_system.cpp
/// @brief ScriptingSystem implementation — visual script lifecycle and execution.
#include "scripting/scripting_system.h"
#include "scripting/script_context.h"
#include "core/engine.h"
#include "core/logger.h"

#include <algorithm>

namespace Vestige
{

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
    // 3. Create ScriptInstance objects
    // 4. Subscribe event nodes to EventBus
    // 5. Fire OnStart for all scripts
    //
    // For now, instances are managed manually (for testing) or via the editor.

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
            instance->setActive(false);
        }
    }

    m_activeInstances.clear();
    m_sceneBlackboard.clear();

    Logger::info("[ScriptingSystem] Scene unloaded — all scripts deactivated");
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

            if (done)
            {
                completed.push_back(action);
            }
        }

        // Remove completed actions and fire their continuations
        for (const auto& action : completed)
        {
            // Remove from pending list
            actions.erase(
                std::remove_if(actions.begin(), actions.end(),
                    [&action](const PendingLatentAction& a)
                    {
                        return a.nodeId == action.nodeId &&
                               a.outputPin == action.outputPin;
                    }),
                actions.end());

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
// Node registration (forward declaration — implemented in core_nodes.cpp)
// ---------------------------------------------------------------------------

void registerCoreNodeTypes(NodeTypeRegistry& registry);

void ScriptingSystem::registerCoreNodes()
{
    registerCoreNodeTypes(m_nodeRegistry);
}

} // namespace Vestige
