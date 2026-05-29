// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_node_type_registry.cpp
/// @brief Unit tests for the scripting NodeTypeRegistry.
#include "scripting/node_type_registry.h"
#include "scripting/core_nodes.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(NodeTypeRegistry, RegisterAndFind)
{
    NodeTypeRegistry registry;
    registry.registerNode({
        "TestNode", "Test Node", "Testing", "A test node",
        {{PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}}},
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "", false, false, nullptr
    });

    EXPECT_TRUE(registry.hasNode("TestNode"));
    EXPECT_FALSE(registry.hasNode("NonExistent"));

    auto* desc = registry.findNode("TestNode");
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->displayName, "Test Node");
    EXPECT_EQ(desc->category, "Testing");
}

TEST(NodeTypeRegistry, GetCategories)
{
    NodeTypeRegistry registry;
    registry.registerNode({"A", "A", "Cat1", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"B", "B", "Cat2", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"C", "C", "Cat1", "", {}, {}, "", false, false, nullptr});

    auto categories = registry.getCategories();
    ASSERT_EQ(categories.size(), 2u);
    EXPECT_EQ(categories[0], "Cat1");
    EXPECT_EQ(categories[1], "Cat2");
}

TEST(NodeTypeRegistry, GetByCategory)
{
    NodeTypeRegistry registry;
    registry.registerNode({"B", "Beta", "Cat1", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"A", "Alpha", "Cat1", "", {}, {}, "", false, false, nullptr});
    registry.registerNode({"C", "Gamma", "Cat2", "", {}, {}, "", false, false, nullptr});

    auto cat1 = registry.getByCategory("Cat1");
    ASSERT_EQ(cat1.size(), 2u);
    EXPECT_EQ(cat1[0]->displayName, "Alpha"); // Sorted by display name
    EXPECT_EQ(cat1[1]->displayName, "Beta");
}

TEST(NodeTypeRegistry, CoreNodesRegistered)
{
    NodeTypeRegistry registry;
    registerCoreNodeTypes(registry);

    EXPECT_TRUE(registry.hasNode("OnStart"));
    EXPECT_TRUE(registry.hasNode("OnUpdate"));
    EXPECT_TRUE(registry.hasNode("OnDestroy"));
    EXPECT_TRUE(registry.hasNode("Branch"));
    EXPECT_TRUE(registry.hasNode("Sequence"));
    EXPECT_TRUE(registry.hasNode("Delay"));
    EXPECT_TRUE(registry.hasNode("SetVariable"));
    EXPECT_TRUE(registry.hasNode("GetVariable"));
    EXPECT_TRUE(registry.hasNode("PrintToScreen"));
    EXPECT_TRUE(registry.hasNode("LogMessage"));
    EXPECT_EQ(registry.nodeCount(), 10u);
}
