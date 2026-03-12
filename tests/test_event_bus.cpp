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
