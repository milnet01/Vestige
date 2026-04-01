/// @file test_scene.cpp
/// @brief Unit tests for the Scene and SceneManager systems.
#include "scene/scene.h"
#include "scene/scene_manager.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(SceneTest, CreateEntity)
{
    Scene scene("TestScene");
    Entity* entity = scene.createEntity("Box");

    EXPECT_NE(entity, nullptr);
    EXPECT_EQ(entity->getName(), "Box");
}

TEST(SceneTest, FindEntity)
{
    Scene scene("TestScene");
    scene.createEntity("Alpha");
    scene.createEntity("Beta");

    EXPECT_NE(scene.findEntity("Alpha"), nullptr);
    EXPECT_NE(scene.findEntity("Beta"), nullptr);
    EXPECT_EQ(scene.findEntity("Gamma"), nullptr);
}

TEST(SceneTest, GetNameAndRoot)
{
    Scene scene("MyScene");
    EXPECT_EQ(scene.getName(), "MyScene");
    EXPECT_NE(scene.getRoot(), nullptr);
}

TEST(SceneTest, UpdateDoesNotCrash)
{
    Scene scene("TestScene");
    scene.createEntity("A");
    scene.createEntity("B");
    EXPECT_NO_THROW(scene.update(0.016f));
}

// --- SceneManager tests ---

TEST(SceneManagerTest, CreateScene)
{
    SceneManager manager;
    Scene* scene = manager.createScene("Level1");

    EXPECT_NE(scene, nullptr);
    EXPECT_EQ(scene->getName(), "Level1");
    EXPECT_EQ(manager.getSceneCount(), 1u);
}

TEST(SceneManagerTest, SetActiveScene)
{
    SceneManager manager;
    manager.createScene("Level1");
    manager.createScene("Level2");

    EXPECT_TRUE(manager.setActiveScene("Level2"));
    EXPECT_EQ(manager.getActiveScene()->getName(), "Level2");
}

TEST(SceneManagerTest, FirstSceneBecomesActive)
{
    SceneManager manager;
    manager.createScene("First");

    EXPECT_NE(manager.getActiveScene(), nullptr);
    EXPECT_EQ(manager.getActiveScene()->getName(), "First");
}

TEST(SceneManagerTest, SetActiveSceneInvalidName)
{
    SceneManager manager;
    manager.createScene("Level1");
    EXPECT_FALSE(manager.setActiveScene("NonExistent"));
}

TEST(SceneManagerTest, RemoveScene)
{
    SceneManager manager;
    manager.createScene("Level1");
    EXPECT_EQ(manager.getSceneCount(), 1u);
    manager.removeScene("Level1");
    EXPECT_EQ(manager.getSceneCount(), 0u);
}

TEST(SceneManagerTest, UpdateDoesNotCrashWithNoActiveScene)
{
    SceneManager manager;
    EXPECT_NO_THROW(manager.update(0.016f));
}

// --- forEachEntity tests ---

TEST(SceneTest, ForEachEntityVisitsAll)
{
    Scene scene("TestScene");
    scene.createEntity("A");
    scene.createEntity("B");
    scene.createEntity("C");

    int count = 0;
    scene.forEachEntity([&](Entity& entity)
    {
        (void)entity;
        count++;
    });

    EXPECT_EQ(count, 3);
}

TEST(SceneTest, ForEachEntityVisitsChildren)
{
    Scene scene("TestScene");
    Entity* parent = scene.createEntity("Parent");
    parent->addChild(std::make_unique<Entity>("Child1"));
    parent->addChild(std::make_unique<Entity>("Child2"));
    scene.rebuildEntityIndex();

    std::vector<std::string> names;
    scene.forEachEntity([&](Entity& entity)
    {
        names.push_back(entity.getName());
    });

    EXPECT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Parent");
    EXPECT_EQ(names[1], "Child1");
    EXPECT_EQ(names[2], "Child2");
}

TEST(SceneTest, ForEachEntityEmptyScene)
{
    Scene scene("Empty");
    int count = 0;
    scene.forEachEntity([&](Entity& entity)
    {
        (void)entity;
        count++;
    });
    EXPECT_EQ(count, 0);
}

TEST(SceneTest, ForEachEntityConst)
{
    Scene scene("TestScene");
    scene.createEntity("A");
    scene.createEntity("B");

    const Scene& constScene = scene;
    int count = 0;
    constScene.forEachEntity([&](const Entity& entity)
    {
        (void)entity;
        count++;
    });

    EXPECT_EQ(count, 2);
}
