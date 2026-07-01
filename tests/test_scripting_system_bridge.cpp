// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scripting_system_bridge.cpp
/// @brief Unit tests for the ScriptingSystem event bridge.
#include "scripting/scripting_system.h"
#include "scripting/script_events.h"
#include "scripting/script_graph.h"
#include "scripting/script_instance.h"
#include "scripting/script_context.h"
#include "scripting/script_value.h"
#include "scripting/node_type_registry.h"
#include "scripting/core_nodes.h"
#include "scripting/event_nodes.h"
#include "scripting/action_nodes.h"
#include "scripting/latent_nodes.h"
#include "core/engine.h"
#include "core/event.h"
#include "physics/collision_event.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

/// Build a CollisionEvent between entity ids a and b for the S8 bridge tests.
CollisionEvent collisionEvent(uint32_t a, uint32_t b, bool isEnter)
{
    CollisionEvent e;
    e.entityA = a;
    e.entityB = b;
    e.isEnter = isEnter;
    e.point = glm::vec3(4.0f, 5.0f, 6.0f);
    e.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    e.approachSpeed = 3.0f;
    e.matA = SurfaceMaterial::Stone;
    e.matB = SurfaceMaterial::Metal;
    return e;
}

}  // namespace

TEST(ScriptingSystemBridge, KeyPressedEventTriggersOnKeyPressedNode)
{
    Engine engine;  // default-constructed: EventBus is usable, other subsystems are not
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    // Build a graph: OnKeyPressed -> PrintToScreen
    ScriptGraph graph;
    graph.name = "BridgeTest";
    uint32_t onKey = graph.addNode("OnKeyPressed");
    uint32_t printer = graph.addNode("PrintToScreen");
    graph.addConnection(onKey, "Pressed", printer, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Publish a KeyPressedEvent — the bridge should fire onKey's execute,
    // which triggers the PrintToScreen node.
    KeyPressedEvent evt(42, false);
    engine.getEventBus().publish(evt);

    // The onKey node's keyCode output should be populated by the bridge.
    auto* nodeInst = instance.getNodeInstance(onKey);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("keyCode")].asInt(), 42);
    EXPECT_FALSE(nodeInst->outputValues[internPin("isRepeat")].asBool());

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, UnregisterCleansUpSubscriptions)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    graph.addNode("OnKeyPressed");
    graph.addNode("OnMouseButton");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Should have subscribed to at least two event types
    EXPECT_GE(instance.subscriptions().size(), 2u);

    sys.unregisterInstance(instance);
    EXPECT_EQ(instance.subscriptions().size(), 0u);
    EXPECT_FALSE(instance.isActive());

    sys.shutdown();
}

TEST(ScriptingSystemBridge, PublishEventNodeDeliversToOnCustomEvent)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    // Two graphs: publisher and subscriber
    ScriptGraph pubGraph;
    uint32_t pubNode = pubGraph.addNode("PublishEvent");
    pubGraph.findNode(pubNode)->properties["name"] =
        ScriptValue(std::string("HelloEvent"));
    pubGraph.findNode(pubNode)->properties["payload"] = ScriptValue(7.0f);
    ScriptInstance publisher;
    publisher.initialize(pubGraph, 1);

    ScriptGraph subGraph;
    uint32_t onCustom = subGraph.addNode("OnCustomEvent");
    subGraph.findNode(onCustom)->properties["Name"] =
        ScriptValue(std::string("HelloEvent"));
    ScriptInstance subscriber;
    subscriber.initialize(subGraph, 2);

    sys.registerInstance(publisher);
    sys.registerInstance(subscriber);

    // Fire the publisher's PublishEvent node manually
    sys.fireEvent(publisher, pubNode);

    // The subscriber's OnCustomEvent node should have been populated
    auto* subNode = subscriber.getNodeInstance(onCustom);
    ASSERT_NE(subNode, nullptr);
    EXPECT_EQ(subNode->outputValues[internPin("name")].asString(), "HelloEvent");
    EXPECT_FLOAT_EQ(subNode->outputValues[internPin("payload")].asFloat(), 7.0f);

    sys.unregisterInstance(publisher);
    sys.unregisterInstance(subscriber);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, LatentActionOnTickFiresDuringTickLatentActions)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(1.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Manually fire the Timeline's execute to schedule the latent action
    sys.fireEvent(instance, tlId);
    ASSERT_EQ(instance.pendingActions().size(), 1u);

    // Simulate half a second elapsing — onTick should have set Alpha ~0.5
    sys.update(0.5f);
    auto* nodeInst = instance.getNodeInstance(tlId);
    ASSERT_NE(nodeInst, nullptr);
    float alpha = nodeInst->outputValues[internPin("Alpha")].asFloat();
    EXPECT_GE(alpha, 0.45f);
    EXPECT_LE(alpha, 0.55f);

    // Finish the timeline
    sys.update(0.6f);
    EXPECT_EQ(instance.pendingActions().size(), 0u);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, CustomEventReEntrancyHitsDepthLimit)
{
    // One OnCustomEvent wired to a PublishEvent that re-publishes the same
    // event type. Without the depth guard this would recurse forever.
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onEvt = graph.addNode("OnCustomEvent");
    graph.findNode(onEvt)->properties["Name"] =
        ScriptValue(std::string("loop"));
    uint32_t pub = graph.addNode("PublishEvent");
    graph.findNode(pub)->properties["name"] =
        ScriptValue(std::string("loop"));
    graph.addConnection(onEvt, "Fired", pub, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    ScriptCustomEvent evt("loop", ScriptValue(1.0f));
    // Should complete without stack overflow thanks to the node-count /
    // call-depth limits in ScriptContext.
    engine.getEventBus().publish(evt);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, OnCustomEventFilterMismatchDoesNotFire)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onEvt = graph.addNode("OnCustomEvent");
    graph.findNode(onEvt)->properties["Name"] =
        ScriptValue(std::string("expected"));
    uint32_t sink = graph.addNode("PrintToScreen");
    graph.addConnection(onEvt, "Fired", sink, "Exec");

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    // Publish an event whose name does NOT match the filter.
    ScriptCustomEvent evt("different", ScriptValue(0.0f));
    engine.getEventBus().publish(evt);

    // The OnCustomEvent node's `name` output should NOT have been populated,
    // because the filter rejected the event upstream.
    auto* nodeInst = instance.getNodeInstance(onEvt);
    ASSERT_NE(nodeInst, nullptr);
    auto it = nodeInst->outputValues.find(internPin("name"));
    EXPECT_TRUE(it == nodeInst->outputValues.end())
        << "name output should be absent on filter mismatch";

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, KeyReleasedEventPopulatesOutputs)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onKey = graph.addNode("OnKeyReleased");
    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    KeyReleasedEvent evt(101);
    engine.getEventBus().publish(evt);

    auto* nodeInst = instance.getNodeInstance(onKey);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("keyCode")].asInt(), 101);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, MouseButtonEventPopulatesOutputs)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onMouse = graph.addNode("OnMouseButton");
    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    MouseButtonPressedEvent evt(2);
    engine.getEventBus().publish(evt);

    auto* nodeInst = instance.getNodeInstance(onMouse);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("button")].asInt(), 2);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, IsInstanceActiveReflectsRegistration)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    graph.addNode("OnKeyPressed");
    ScriptInstance instance;
    instance.initialize(graph, 1);

    EXPECT_FALSE(sys.isInstanceActive(&instance));
    sys.registerInstance(instance);
    EXPECT_TRUE(sys.isInstanceActive(&instance));
    sys.unregisterInstance(instance);
    EXPECT_FALSE(sys.isInstanceActive(&instance));
    EXPECT_FALSE(sys.isInstanceActive(nullptr));

    sys.shutdown();
}

// Phase 10.9 Sc6: liveness must include a generation-tag check, not just
// pointer identity. ScriptInstance::initialize bumps `m_generation`; an
// EventBus subscription captured under generation N must drop dispatches
// after the instance has been re-initialized to generation N+1.
TEST(ScriptingSystemBridge, IsInstanceActiveGenerationGate_Sc6)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    graph.addNode("OnKeyPressed");
    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    const uint32_t gen0 = instance.generation();
    EXPECT_TRUE(sys.isInstanceActive(&instance, gen0));

    // Re-initialize bumps the generation. The captured gen0 must now
    // fail the gate while the up-to-date generation passes.
    instance.initialize(graph, 1);
    EXPECT_NE(gen0, instance.generation());
    EXPECT_FALSE(sys.isInstanceActive(&instance, gen0))
        << "stale-generation subscription must be gated out (ABA hazard)";
    EXPECT_TRUE(sys.isInstanceActive(&instance, instance.generation()));

    // nullptr fails regardless of expected generation.
    EXPECT_FALSE(sys.isInstanceActive(nullptr, 0));
    EXPECT_FALSE(sys.isInstanceActive(nullptr, gen0));

    sys.unregisterInstance(instance);
    sys.shutdown();
}

// --- AX4 S8: OnCollisionEnter / OnCollisionExit wire-up --------------------

TEST(ScriptingSystemBridge, CollisionEnterPopulatesOtherEntityPointAndNormal)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onHit = graph.addNode("OnCollisionEnter");
    ScriptInstance instance;
    instance.initialize(graph, /*entityId*/ 1);   // owner = entity 1
    sys.registerInstance(instance);

    // Entity 1 (owner) strikes entity 2 — the node's `otherEntity` is 2.
    engine.getEventBus().publish(collisionEvent(1, 2, /*isEnter*/ true));

    auto* nodeInst = instance.getNodeInstance(onHit);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("otherEntity")].asEntityId(), 2u);
    EXPECT_EQ(nodeInst->outputValues[internPin("contactPoint")].asVec3(),
              glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(nodeInst->outputValues[internPin("normal")].asVec3(),
              glm::vec3(0.0f, 1.0f, 0.0f));

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, CollisionExitPopulatesOtherEntityWhenOwnerIsBodyB)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onSep = graph.addNode("OnCollisionExit");
    ScriptInstance instance;
    instance.initialize(graph, /*entityId*/ 1);
    sys.registerInstance(instance);

    // Owner is the *B* side of the pair here — otherEntity must still resolve
    // to the far body (2), exercising the ternary's other branch.
    engine.getEventBus().publish(collisionEvent(2, 1, /*isEnter*/ false));

    auto* nodeInst = instance.getNodeInstance(onSep);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_EQ(nodeInst->outputValues[internPin("otherEntity")].asEntityId(), 2u);

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, CollisionEnterNodeIgnoresExitEvent)
{
    // An OnCollisionEnter node must not fire on an Exit event (phase filter);
    // its outputs stay unpopulated.
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onHit = graph.addNode("OnCollisionEnter");
    ScriptInstance instance;
    instance.initialize(graph, /*entityId*/ 1);
    sys.registerInstance(instance);

    engine.getEventBus().publish(collisionEvent(1, 2, /*isEnter*/ false));

    auto* nodeInst = instance.getNodeInstance(onHit);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_TRUE(nodeInst->outputValues.find(internPin("otherEntity"))
                == nodeInst->outputValues.end())
        << "Enter node must not populate outputs from an Exit event";

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, CollisionForUninvolvedEntityDoesNotFire)
{
    // A collision between two other entities must not reach an entity-scoped
    // graph (owner-entity filter, mirroring OnDestroy).
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t onHit = graph.addNode("OnCollisionEnter");
    ScriptInstance instance;
    instance.initialize(graph, /*entityId*/ 1);
    sys.registerInstance(instance);

    engine.getEventBus().publish(collisionEvent(5, 6, /*isEnter*/ true));

    auto* nodeInst = instance.getNodeInstance(onHit);
    ASSERT_NE(nodeInst, nullptr);
    EXPECT_TRUE(nodeInst->outputValues.find(internPin("otherEntity"))
                == nodeInst->outputValues.end())
        << "a collision not involving the owner must not fire the node";

    sys.unregisterInstance(instance);
    sys.shutdown();
}

TEST(ScriptingSystemBridge, TimelineHandlesRemainingTimeNaNGracefully)
{
    Engine engine;
    ScriptingSystem sys;
    ASSERT_TRUE(sys.initialize(engine));

    ScriptGraph graph;
    uint32_t tlId = graph.addNode("Timeline");
    graph.findNode(tlId)->properties["Duration"] = ScriptValue(1.0f);

    ScriptInstance instance;
    instance.initialize(graph, 1);
    sys.registerInstance(instance);

    sys.fireEvent(instance, tlId);
    ASSERT_EQ(instance.pendingActions().size(), 1u);

    // Inject NaN into remainingTime — ticker should not crash or emit NaN.
    instance.pendingActions()[0].remainingTime =
        std::numeric_limits<float>::quiet_NaN();
    sys.update(0.1f);

    auto* nodeInst = instance.getNodeInstance(tlId);
    ASSERT_NE(nodeInst, nullptr);
    float alpha = nodeInst->outputValues[internPin("Alpha")].asFloat();
    EXPECT_TRUE(std::isfinite(alpha));

    sys.unregisterInstance(instance);
    sys.shutdown();
}
