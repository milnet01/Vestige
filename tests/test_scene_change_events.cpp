// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_scene_change_events.cpp
/// @brief 3D_E-0730: scene-loaded and scene-unloaded events, published by
///        Scene::Replacement and SceneManager (INV-1 to INV-4 of
///        docs/specs/3D_E-0730-scene-change-events.md).
#include "core/event_bus.h"
#include "core/system_events.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{

/// @brief Records every scene event on a bus, in order.
struct EventLog
{
    std::vector<std::string> events;
    std::vector<Scene*> scenes;
};

void recordSceneEvents(EventBus& bus, EventLog& log)
{
    bus.subscribe<SceneUnloadedEvent>([&log](const SceneUnloadedEvent& e)
    {
        log.events.emplace_back("unloaded");
        log.scenes.push_back(e.scene);
    });
    bus.subscribe<SceneLoadedEvent>([&log](const SceneLoadedEvent& e)
    {
        log.events.emplace_back("loaded");
        log.scenes.push_back(e.scene);
    });
}

} // namespace

// INV-1: one unload while the old contents exist, then one load once the new
// contents are in place.
TEST(SceneChangeEvents, ReplacementPublishesUnloadThenLoad_INV1)
{
    Scene scene("Replaced");
    const uint32_t oldId = scene.createEntity("Old")->getId();

    EventBus bus;
    scene.attachEventBus(&bus);

    std::vector<std::string> order;
    bool oldFoundOnUnload = false;
    bool newFoundOnLoad = false;
    uint32_t newId = 0;
    bus.subscribe<SceneUnloadedEvent>([&](const SceneUnloadedEvent& e)
    {
        order.emplace_back("unloaded");
        oldFoundOnUnload = e.scene->findEntityById(oldId) != nullptr;
    });
    bus.subscribe<SceneLoadedEvent>([&](const SceneLoadedEvent& e)
    {
        order.emplace_back("loaded");
        newFoundOnLoad = e.scene->findEntityById(newId) != nullptr;
    });

    {
        Scene::Replacement replacement(scene);
        EXPECT_EQ(scene.findEntityById(oldId), nullptr);
        newId = scene.createEntity("New")->getId();
        EXPECT_EQ(order, std::vector<std::string>{"unloaded"});
    }

    EXPECT_EQ(order, (std::vector<std::string>{"unloaded", "loaded"}));
    EXPECT_TRUE(oldFoundOnUnload);
    EXPECT_TRUE(newFoundOnLoad);
}

// INV-2: only the active scene has the bus.
TEST(SceneChangeEvents, OnlyActiveSceneAnnounces_INV2)
{
    EventBus bus;
    SceneManager manager;
    manager.attachEventBus(bus);

    Scene* first = manager.createScene("First");
    Scene* second = manager.createScene("Second");
    ASSERT_EQ(manager.getActiveScene(), first);

    EventLog log;
    recordSceneEvents(bus, log);

    {
        Scene::Replacement replacement(*second);
        second->createEntity("Filler");
    }
    EXPECT_TRUE(log.events.empty());

    {
        Scene::Replacement replacement(*first);
    }
    EXPECT_EQ(log.events, (std::vector<std::string>{"unloaded", "loaded"}));
}

// INV-3: attaching the bus announces the active scene once, and nothing when
// there is none.
TEST(SceneChangeEvents, AttachAnnouncesActiveScene_INV3)
{
    EventBus bus;
    EventLog log;
    recordSceneEvents(bus, log);

    SceneManager empty;
    empty.attachEventBus(bus);
    EXPECT_TRUE(log.events.empty());

    SceneManager manager;
    Scene* scene = manager.createScene("Startup");
    EXPECT_TRUE(log.events.empty());  // no bus yet, nothing announced

    manager.attachEventBus(bus);
    ASSERT_EQ(log.events, std::vector<std::string>{"loaded"});
    EXPECT_EQ(log.scenes[0], scene);
}

// INV-4: a nested replacement announces nothing; the outer one still announces
// one pair.
TEST(SceneChangeEvents, NestedReplacementAnnouncesOnce_INV4)
{
    Scene scene("Nested");
    EventBus bus;
    scene.attachEventBus(&bus);
    EventLog log;
    recordSceneEvents(bus, log);

    {
        Scene::Replacement outer(scene);
        {
            Scene::Replacement inner(scene);
            scene.createEntity("Inner");
        }
        EXPECT_EQ(log.events, std::vector<std::string>{"unloaded"});
    }

    EXPECT_EQ(log.events, (std::vector<std::string>{"unloaded", "loaded"}));
}

// §4.2: switching the active scene announces the old one's unload and the new
// one's load, and moves the bus with it.
TEST(SceneChangeEvents, SwitchingActiveSceneMovesTheBus)
{
    EventBus bus;
    SceneManager manager;
    Scene* first = manager.createScene("First");
    Scene* second = manager.createScene("Second");
    manager.attachEventBus(bus);

    EventLog log;
    recordSceneEvents(bus, log);

    ASSERT_TRUE(manager.setActiveScene("Second"));
    ASSERT_EQ(log.events, (std::vector<std::string>{"unloaded", "loaded"}));
    EXPECT_EQ(log.scenes[0], first);
    EXPECT_EQ(log.scenes[1], second);

    log.events.clear();
    {
        Scene::Replacement replacement(*first);  // no longer active
    }
    EXPECT_TRUE(log.events.empty());

    manager.removeScene("Second");
    EXPECT_EQ(log.events, std::vector<std::string>{"unloaded"});
}
