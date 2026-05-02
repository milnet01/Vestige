// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_event_bus.cpp
/// @brief Unit tests for the EventBus.
#include "core/event_bus.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(EventBusTest, SubscribeAndPublish)
{
    EventBus bus;
    bool wasCalled = false;
    int receivedWidth = 0;
    int receivedHeight = 0;

    bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent& event)
    {
        wasCalled = true;
        receivedWidth = event.width;
        receivedHeight = event.height;
    });

    bus.publish(WindowResizeEvent(1920, 1080));

    EXPECT_TRUE(wasCalled);
    EXPECT_EQ(receivedWidth, 1920);
    EXPECT_EQ(receivedHeight, 1080);
}

TEST(EventBusTest, MultipleSubscribers)
{
    EventBus bus;
    int callCount = 0;

    bus.subscribe<KeyPressedEvent>([&](const KeyPressedEvent&)
    {
        callCount++;
    });

    bus.subscribe<KeyPressedEvent>([&](const KeyPressedEvent&)
    {
        callCount++;
    });

    bus.publish(KeyPressedEvent(65, false));

    EXPECT_EQ(callCount, 2);
}

TEST(EventBusTest, DifferentEventTypes)
{
    EventBus bus;
    bool resizeCalled = false;
    bool keyCalled = false;

    bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
    {
        resizeCalled = true;
    });

    bus.subscribe<KeyPressedEvent>([&](const KeyPressedEvent&)
    {
        keyCalled = true;
    });

    // Only publish resize — key should not be called
    bus.publish(WindowResizeEvent(800, 600));

    EXPECT_TRUE(resizeCalled);
    EXPECT_FALSE(keyCalled);
}

TEST(EventBusTest, NoSubscribers)
{
    EventBus bus;
    // Should not crash when publishing with no subscribers
    EXPECT_NO_THROW(bus.publish(WindowCloseEvent()));
}

TEST(EventBusTest, ClearAll)
{
    EventBus bus;
    bool wasCalled = false;

    bus.subscribe<WindowCloseEvent>([&](const WindowCloseEvent&)
    {
        wasCalled = true;
    });

    EXPECT_GT(bus.getListenerCount(), 0u);

    bus.clearAll();

    EXPECT_EQ(bus.getListenerCount(), 0u);

    bus.publish(WindowCloseEvent());
    EXPECT_FALSE(wasCalled);
}

TEST(EventBusTest, GetListenerCount)
{
    EventBus bus;
    EXPECT_EQ(bus.getListenerCount(), 0u);

    bus.subscribe<WindowResizeEvent>([](const WindowResizeEvent&) {});
    EXPECT_EQ(bus.getListenerCount(), 1u);

    bus.subscribe<KeyPressedEvent>([](const KeyPressedEvent&) {});
    EXPECT_EQ(bus.getListenerCount(), 2u);
}

// =============================================================================
// Phase 10.9 Pe4 — reentrancy sentinel + deferred-add queue
//
// The previous publish path copied the listener vector at every dispatch to
// avoid iterator invalidation if a callback subscribed/unsubscribed mid-call.
// Pe4 replaces that with a depth counter + tombstone-and-compact + pending-
// adds queue: by-ref iteration, no per-event heap allocation. The contracts
// the copy enforced still hold; the tests below pin them.
// =============================================================================

TEST(EventBusPe4, SubscribeDuringDispatchDoesNotFireThisTurn_Pe4)
{
    EventBus bus;
    int outerCalls = 0;
    int newSubCalls = 0;

    bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
    {
        ++outerCalls;
        // Subscribe a new listener mid-dispatch; it must NOT fire this turn.
        bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
        {
            ++newSubCalls;
        });
    });

    bus.publish(WindowResizeEvent(800, 600));
    EXPECT_EQ(outerCalls, 1);
    EXPECT_EQ(newSubCalls, 0);  // pre-Pe4 also held; explicit guarantee post-Pe4

    // Next publish: drain has folded the new listener into m_listeners,
    // so both fire.
    bus.publish(WindowResizeEvent(1024, 768));
    EXPECT_EQ(outerCalls, 2);
    EXPECT_EQ(newSubCalls, 1);
}

TEST(EventBusPe4, UnsubscribeDuringDispatchTombstonesNotErases_Pe4)
{
    EventBus bus;
    int aCalls = 0;
    int bCalls = 0;

    SubscriptionId aId = 0;
    SubscriptionId bId = 0;

    aId = bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
    {
        ++aCalls;
        // A unsubscribes B during dispatch. B must not fire this turn — the
        // pre-Pe4 vector-copy semantics also held, but Pe4 implements it via
        // a tombstone instead of an erase so the by-ref loop stays valid.
        bus.unsubscribe(bId);
    });
    bId = bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
    {
        ++bCalls;
    });

    bus.publish(WindowResizeEvent(1, 1));
    EXPECT_EQ(aCalls, 1);
    EXPECT_EQ(bCalls, 0);  // tombstoned during this dispatch

    // After dispatch unwinds, the tombstone is compacted out.
    EXPECT_EQ(bus.getListenerCount(), 1u);

    bus.publish(WindowResizeEvent(2, 2));
    EXPECT_EQ(aCalls, 2);
    EXPECT_EQ(bCalls, 0);
}

TEST(EventBusPe4, NestedPublishDoesNotPrematurelyDrain_Pe4)
{
    EventBus bus;
    int outerCalls = 0;
    int innerCalls = 0;
    int afterNestedSubCalls = 0;

    bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
    {
        ++outerCalls;
        // Reentrant publish + subscribe-during-dispatch. The pending add
        // must NOT fold into m_listeners until the OUTER publish unwinds —
        // otherwise the nested publish's loop snapshot would see it and
        // fire it twice.
        bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
        {
            ++afterNestedSubCalls;
        });
        bus.publish(KeyPressedEvent(0, false, 0));  // different type — nested but unrelated
    });

    bus.subscribe<KeyPressedEvent>([&](const KeyPressedEvent&)
    {
        ++innerCalls;
    });

    bus.publish(WindowResizeEvent(1, 1));
    EXPECT_EQ(outerCalls, 1);
    EXPECT_EQ(innerCalls, 1);
    EXPECT_EQ(afterNestedSubCalls, 0);  // pending — not drained mid-nest

    bus.publish(WindowResizeEvent(2, 2));
    EXPECT_EQ(afterNestedSubCalls, 1);  // drained after first publish unwound
}

TEST(EventBusPe4, SubscribeThenUnsubscribeDuringDispatchNetsToZero_Pe4)
{
    EventBus bus;
    int extraCalls = 0;

    bus.subscribe<WindowResizeEvent>([&](const WindowResizeEvent&)
    {
        // Add a new listener and immediately unsubscribe it within the
        // same dispatch — should never fire and not leak.
        SubscriptionId tmp = bus.subscribe<WindowResizeEvent>(
            [&](const WindowResizeEvent&) { ++extraCalls; });
        EXPECT_TRUE(bus.unsubscribe(tmp));
    });

    bus.publish(WindowResizeEvent(1, 1));
    EXPECT_EQ(extraCalls, 0);

    bus.publish(WindowResizeEvent(2, 2));
    EXPECT_EQ(extraCalls, 0);  // never folded into m_listeners — gone

    EXPECT_EQ(bus.getListenerCount(), 1u);  // only the original
}

TEST(EventBusPe4, ListenerCountReflectsTombstonedAndPendingAccurately_Pe4)
{
    EventBus bus;

    SubscriptionId id1 = bus.subscribe<WindowResizeEvent>([](const WindowResizeEvent&) {});
    bus.subscribe<WindowResizeEvent>([](const WindowResizeEvent&) {});
    EXPECT_EQ(bus.getListenerCount(), 2u);

    // Mid-dispatch: tombstone id1, add a new one. Count should be:
    //   2 live - 1 tombstoned + 1 pending = 2.
    bus.subscribe<KeyPressedEvent>([&](const KeyPressedEvent&)
    {
        EXPECT_TRUE(bus.unsubscribe(id1));
        bus.subscribe<WindowResizeEvent>([](const WindowResizeEvent&) {});
        EXPECT_EQ(bus.getListenerCount(), 2u + 1u /* the KeyPressedEvent listener */);
    });

    bus.publish(KeyPressedEvent(0, false, 0));

    // Post-drain: 1 surviving WindowResize + 1 newly-added WindowResize + 1 KeyPressed = 3.
    EXPECT_EQ(bus.getListenerCount(), 3u);
}
