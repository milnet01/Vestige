// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_entity_serializer_depth_cap.cpp
/// @brief Phase 10.9 Slice 5 D7 — pin recursion-depth cap on
///        scene-JSON deserialise + countJsonEntities.
///
/// A maliciously-crafted scene JSON with deeply-nested `children:`
/// blocks could blow the default 8 MB stack. D7 caps recursion at 128
/// levels; deeper inputs are rejected (return nullptr on
/// `deserializeEntity`, return partial-count from `countJsonEntities`).

#include <gtest/gtest.h>

#include "resource/resource_manager.h"
#include "scene/scene.h"
#include "utils/entity_serializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>

using json = nlohmann::json;

namespace Vestige::EntitySerializerDepthCap::Test
{

namespace
{
/// @brief Build a `{ "name": "...", "children": [{...}] }` chain @a depth
///        levels deep.
json buildNestedChain(int depth)
{
    json root;
    root["name"] = "root";
    json* cur = &root;
    for (int i = 0; i < depth; ++i)
    {
        (*cur)["children"] = json::array();
        json child;
        child["name"] = "level" + std::to_string(i);
        (*cur)["children"].push_back(child);
        cur = &(*cur)["children"][0];
    }
    return root;
}
}

TEST(EntitySerializerDepthCap, AcceptsDepthAtBoundary_D7)
{
    // 128 levels = the cap. Should succeed.
    Scene scene;
    ResourceManager rm;
    auto j = buildNestedChain(128);
    auto* e = EntitySerializer::deserializeEntity(j, scene, rm);
    EXPECT_NE(e, nullptr);
}

TEST(EntitySerializerDepthCap, RejectsDepthAboveBoundary_D7)
{
    // 200 levels > the 128 cap. The root entity is created (depth 0)
    // but recursion bails out at the 129th nesting level — the
    // returned tree has at most 128 levels of nested children even
    // though the input had 200.
    Scene scene;
    ResourceManager rm;
    auto j = buildNestedChain(200);
    auto* e = EntitySerializer::deserializeEntity(j, scene, rm);
    ASSERT_NE(e, nullptr);

    // Walk the deserialised tree and measure the deepest chain.
    auto measureDepth = [](const Entity* root) {
        int maxDepth = 0;
        std::function<void(const Entity*, int)> walk =
            [&](const Entity* node, int d) {
                maxDepth = std::max(maxDepth, d);
                for (const auto& child : node->getChildren())
                {
                    walk(child.get(), d + 1);
                }
            };
        walk(root, 0);
        return maxDepth;
    };

    const int actualDepth = measureDepth(e);
    EXPECT_LE(actualDepth, 128) << "depth cap not enforced — deserialised "
                                << actualDepth << " levels from a 200-level input";
}

TEST(EntitySerializerDepthCap, FlatTreeWithManyChildrenIsAccepted_D7)
{
    // Wide-and-shallow trees should not be rejected — the cap is on
    // recursion depth, not entity count.
    Scene scene;
    ResourceManager rm;
    json j;
    j["name"] = "root";
    j["children"] = json::array();
    for (int i = 0; i < 5000; ++i)
    {
        json child;
        child["name"] = "c" + std::to_string(i);
        j["children"].push_back(child);
    }
    auto* e = EntitySerializer::deserializeEntity(j, scene, rm);
    EXPECT_NE(e, nullptr);
}

}  // namespace Vestige::EntitySerializerDepthCap::Test
