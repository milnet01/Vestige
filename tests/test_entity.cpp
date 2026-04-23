// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_entity.cpp
/// @brief Unit tests for the Entity and Component system.
#include "scene/entity.h"
#include "scene/component.h"

#include <gtest/gtest.h>

#include <set>

using namespace Vestige;

// Simple test component
class TestComponent : public Component
{
public:
    int value = 0;

    void update(float deltaTime) override
    {
        value += static_cast<int>(deltaTime * 10.0f);
    }

    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<TestComponent>(*this);
    }
};

// Another test component to verify distinct type IDs
class OtherComponent : public Component
{
public:
    std::string label = "default";

    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<OtherComponent>(*this);
    }
};

TEST(EntityTest, CreateWithName)
{
    Entity entity("TestEntity");
    EXPECT_EQ(entity.getName(), "TestEntity");
    EXPECT_TRUE(entity.isActive());
}

TEST(EntityTest, AddAndGetComponent)
{
    Entity entity("Test");
    auto* comp = entity.addComponent<TestComponent>();
    EXPECT_NE(comp, nullptr);
    EXPECT_EQ(comp->value, 0);
    EXPECT_EQ(comp->getOwner(), &entity);

    auto* retrieved = entity.getComponent<TestComponent>();
    EXPECT_EQ(retrieved, comp);
}

TEST(EntityTest, HasComponent)
{
    Entity entity("Test");
    EXPECT_FALSE(entity.hasComponent<TestComponent>());
    entity.addComponent<TestComponent>();
    EXPECT_TRUE(entity.hasComponent<TestComponent>());
}

TEST(EntityTest, RemoveComponent)
{
    Entity entity("Test");
    entity.addComponent<TestComponent>();
    EXPECT_TRUE(entity.hasComponent<TestComponent>());
    entity.removeComponent<TestComponent>();
    EXPECT_FALSE(entity.hasComponent<TestComponent>());
    EXPECT_EQ(entity.getComponent<TestComponent>(), nullptr);
}

TEST(EntityTest, MultipleComponentTypes)
{
    Entity entity("Test");
    auto* test = entity.addComponent<TestComponent>();
    auto* other = entity.addComponent<OtherComponent>();

    EXPECT_TRUE(entity.hasComponent<TestComponent>());
    EXPECT_TRUE(entity.hasComponent<OtherComponent>());
    EXPECT_EQ(entity.getComponent<TestComponent>(), test);
    EXPECT_EQ(entity.getComponent<OtherComponent>(), other);
}

TEST(EntityTest, SetActive)
{
    Entity entity("Test");
    EXPECT_TRUE(entity.isActive());
    entity.setActive(false);
    EXPECT_FALSE(entity.isActive());
}

TEST(EntityTest, AddChild)
{
    Entity parent("Parent");
    auto child = std::make_unique<Entity>("Child");
    Entity* childPtr = parent.addChild(std::move(child));

    EXPECT_NE(childPtr, nullptr);
    EXPECT_EQ(childPtr->getName(), "Child");
    EXPECT_EQ(childPtr->getParent(), &parent);
    EXPECT_EQ(parent.getChildren().size(), 1u);
}

TEST(EntityTest, FindChild)
{
    Entity parent("Parent");
    parent.addChild(std::make_unique<Entity>("Alpha"));
    parent.addChild(std::make_unique<Entity>("Beta"));

    EXPECT_NE(parent.findChild("Alpha"), nullptr);
    EXPECT_NE(parent.findChild("Beta"), nullptr);
    EXPECT_EQ(parent.findChild("Gamma"), nullptr);
}

TEST(EntityTest, FindDescendant)
{
    Entity root("Root");
    auto child = std::make_unique<Entity>("Child");
    auto grandchild = std::make_unique<Entity>("Grandchild");
    child->addChild(std::move(grandchild));
    root.addChild(std::move(child));

    EXPECT_NE(root.findDescendant("Grandchild"), nullptr);
    EXPECT_EQ(root.findChild("Grandchild"), nullptr);  // Not a direct child
}

TEST(EntityTest, TransformDefaultValues)
{
    Entity entity("Test");
    EXPECT_FLOAT_EQ(entity.transform.position.x, 0.0f);
    EXPECT_FLOAT_EQ(entity.transform.scale.x, 1.0f);
    EXPECT_FLOAT_EQ(entity.transform.rotation.y, 0.0f);
}

TEST(EntityTest, UpdateComputesWorldMatrix)
{
    Entity entity("Test");
    entity.transform.position = glm::vec3(5.0f, 0.0f, 0.0f);
    entity.update(0.0f);

    glm::vec3 worldPos = entity.getWorldPosition();
    EXPECT_NEAR(worldPos.x, 5.0f, 0.001f);
    EXPECT_NEAR(worldPos.y, 0.0f, 0.001f);
    EXPECT_NEAR(worldPos.z, 0.0f, 0.001f);
}

TEST(EntityTest, ComponentUpdatedDuringEntityUpdate)
{
    Entity entity("Test");
    auto* comp = entity.addComponent<TestComponent>();
    entity.update(1.0f);  // deltaTime = 1.0 -> value += 10
    EXPECT_EQ(comp->value, 10);
}

TEST(EntityTest, IdsAreUnique)
{
    constexpr int COUNT = 100;
    std::vector<std::unique_ptr<Entity>> entities;
    entities.reserve(COUNT);

    for (int i = 0; i < COUNT; ++i)
    {
        entities.push_back(std::make_unique<Entity>("Entity_" + std::to_string(i)));
    }

    std::set<uint32_t> ids;
    for (const auto& e : entities)
    {
        ids.insert(e->getId());
    }

    // All IDs must be distinct
    EXPECT_EQ(ids.size(), static_cast<size_t>(COUNT));
}

TEST(EntityTest, IdsAreNonZero)
{
    constexpr int COUNT = 50;
    for (int i = 0; i < COUNT; ++i)
    {
        Entity entity("Test_" + std::to_string(i));
        EXPECT_NE(entity.getId(), 0u) << "Entity ID must never be 0 (sentinel value)";
    }
}

TEST(EntityTest, ClonedEntityGetsNewId)
{
    Entity original("Original");
    auto cloned = original.clone();

    EXPECT_NE(cloned->getId(), 0u);
    EXPECT_NE(cloned->getId(), original.getId());
}
