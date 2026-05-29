// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_blackboard.cpp
/// @brief Unit tests for the scripting Blackboard key/value store.
#include "scripting/blackboard.h"
#include "scripting/script_value.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(Blackboard, SetAndGet)
{
    Blackboard bb;
    bb.set("health", ScriptValue(100.0f));
    EXPECT_TRUE(bb.has("health"));
    EXPECT_FLOAT_EQ(bb.get("health").asFloat(), 100.0f);
}

TEST(Blackboard, GetMissing)
{
    Blackboard bb;
    auto val = bb.get("nonexistent");
    EXPECT_FLOAT_EQ(val.asFloat(), 0.0f); // Default
}

TEST(Blackboard, Remove)
{
    Blackboard bb;
    bb.set("x", ScriptValue(1.0f));
    EXPECT_TRUE(bb.has("x"));
    EXPECT_TRUE(bb.remove("x"));
    EXPECT_FALSE(bb.has("x"));
    EXPECT_FALSE(bb.remove("x")); // Already removed
}

TEST(Blackboard, Clear)
{
    Blackboard bb;
    bb.set("a", ScriptValue(1));
    bb.set("b", ScriptValue(2));
    bb.set("c", ScriptValue(3));
    EXPECT_EQ(bb.size(), 3u);
    bb.clear();
    EXPECT_EQ(bb.size(), 0u);
}

TEST(Blackboard, Overwrite)
{
    Blackboard bb;
    bb.set("x", ScriptValue(1.0f));
    bb.set("x", ScriptValue(2.0f));
    EXPECT_FLOAT_EQ(bb.get("x").asFloat(), 2.0f);
    EXPECT_EQ(bb.size(), 1u);
}

TEST(Blackboard, JsonRoundTrip)
{
    Blackboard original;
    original.set("name", ScriptValue(std::string("Player")));
    original.set("health", ScriptValue(100.0f));
    original.set("alive", ScriptValue(true));

    auto json = original.toJson();
    auto restored = Blackboard::fromJson(json);

    EXPECT_EQ(restored.size(), 3u);
    EXPECT_EQ(restored.get("name").asString(), "Player");
    EXPECT_FLOAT_EQ(restored.get("health").asFloat(), 100.0f);
    EXPECT_TRUE(restored.get("alive").asBool());
}

TEST(BlackboardCap, InsertionRefusedAtMaxKeys)
{
    Blackboard bb;
    for (size_t i = 0; i < Blackboard::MAX_KEYS; ++i)
    {
        bb.set("k" + std::to_string(i), ScriptValue(static_cast<int32_t>(i)));
    }
    EXPECT_EQ(bb.size(), Blackboard::MAX_KEYS);
    bb.set("overflow", ScriptValue(999));
    EXPECT_EQ(bb.size(), Blackboard::MAX_KEYS);
    EXPECT_FALSE(bb.has("overflow"));
}

TEST(BlackboardCap, UpdatesToExistingKeysAlwaysSucceed)
{
    Blackboard bb;
    for (size_t i = 0; i < Blackboard::MAX_KEYS; ++i)
    {
        bb.set("k" + std::to_string(i), ScriptValue(0));
    }
    // Updating an existing key past the cap still works.
    bb.set("k0", ScriptValue(42));
    EXPECT_EQ(bb.get("k0").asInt(), 42);
}

// Regression test for AUDIT.md §H6 / FIXPLAN D2: crafted JSON must not
// bypass the MAX_KEYS cap. Prior to the fix, fromJson wrote directly into
// m_values, allowing an attacker to create a blackboard with millions of
// keys via a malicious save file.
TEST(BlackboardCap, FromJsonHonoursMaxKeys)
{
    nlohmann::json j = nlohmann::json::object();
    // Try to insert 2× MAX_KEYS via the load path.
    for (size_t i = 0; i < Blackboard::MAX_KEYS * 2; ++i)
    {
        j["overflow_" + std::to_string(i)] = nlohmann::json{
            {"type", "int"}, {"value", static_cast<int>(i)}};
    }
    auto bb = Blackboard::fromJson(j);
    EXPECT_LE(bb.size(), Blackboard::MAX_KEYS);
}

// Regression test for AUDIT.md §H6 / FIXPLAN D2: crafted JSON with long
// key names must be clamped on load, matching ScriptGraph::loadFromFile's
// handling of user-supplied strings.
TEST(BlackboardCap, FromJsonClampsLongKeys)
{
    const std::string longKey(1024, 'K');  // far exceeds the 256-byte cap
    nlohmann::json j = nlohmann::json::object();
    j[longKey] = nlohmann::json{{"type", "int"}, {"value", 1}};
    auto bb = Blackboard::fromJson(j);
    // Key must exist but be length-clamped to 256 bytes.
    EXPECT_EQ(bb.size(), 1u);
    EXPECT_FALSE(bb.has(longKey));
    EXPECT_TRUE(bb.has(std::string(256, 'K')));
}
