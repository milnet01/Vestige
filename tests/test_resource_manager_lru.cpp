// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_resource_manager_lru.cpp
/// @brief Phase 10.9 Slice 13 Pe5 — pin LRU eviction + cache-limit contract.
///
/// The LRU helpers (`touchCache`, `insertAndEnforceCache`,
/// `enforceCacheLimit`) live in `engine/resource/lru_cache.h` so that
/// the eviction logic can be exercised against a non-GL value type
/// (the tests use `int` as a stand-in for `Mesh` / `Texture` /
/// `Model`, which all need a live OpenGL context for `upload()`).
///
/// Also covers the public ResourceManager setters/getters (limits and
/// `clearAll`) that don't touch the asset payload.

#include <gtest/gtest.h>
#include "resource/resource_manager.h"
#include "resource/lru_cache.h"

namespace Vestige::ResourceManagerLru::Test
{

// -- Public ResourceManager surface (no GL needed) --------------------------

// Slice 18 Ts4: dropped `DefaultLimitsAreSetFromConstants` — it
// asserted a thin accessor returned its named-constant initialiser;
// no failure mode short of someone changing the constant itself.
// Numeric-value pin (catching drift in the constants) lives in
// `DefaultLimitsAreReasonable` below.

TEST(ResourceManagerLruTest, DefaultLimitsAreReasonable)
{
    ResourceManager rm;
    // Pin the actual numeric defaults so a constant drift is visible
    // in the test diff, not just deferred to the next observer.
    EXPECT_EQ(rm.getTextureCacheLimit(), 1024u);
    EXPECT_EQ(rm.getMeshCacheLimit(),     512u);
    EXPECT_EQ(rm.getModelCacheLimit(),    128u);
}

TEST(ResourceManagerLruTest, SetCacheLimitRoundTrips)
{
    ResourceManager rm;
    rm.setTextureCacheLimit(7);
    rm.setMeshCacheLimit(11);
    rm.setModelCacheLimit(13);
    EXPECT_EQ(rm.getTextureCacheLimit(), 7u);
    EXPECT_EQ(rm.getMeshCacheLimit(),    11u);
    EXPECT_EQ(rm.getModelCacheLimit(),   13u);
}

// -- LRU helper template (Cache<int> as stand-in for Mesh/Texture/Model) ---

namespace
{
struct LruFixture
{
    Cache<int> cache;
    std::list<std::string> order;
};

// Convenience: insert key=value with limit, return the shared_ptr stored.
std::shared_ptr<int> insert(LruFixture& f, const std::string& key, int v, size_t limit)
{
    auto p = std::make_shared<int>(v);
    insertAndEnforceCache(f.cache, f.order, key, p, limit);
    return p;
}
}  // namespace

TEST(LruCacheHelpers, BelowLimitNoEviction)
{
    LruFixture f;
    auto a = insert(f, "a", 1, 8);
    auto b = insert(f, "b", 2, 8);
    auto c = insert(f, "c", 3, 8);
    EXPECT_EQ(f.cache.size(), 3u);
    EXPECT_TRUE(f.cache.count("a"));
    EXPECT_TRUE(f.cache.count("b"));
    EXPECT_TRUE(f.cache.count("c"));
}

TEST(LruCacheHelpers, OverLimitEvictsLeastRecentlyUsed)
{
    LruFixture f;
    // Insert two; cap = 2.
    insert(f, "a", 1, 2);   // dropped immediately → use_count goes to 1.
    insert(f, "b", 2, 2);
    EXPECT_EQ(f.cache.size(), 2u);

    // Inserting "c" pushes size to 3 → eviction loop fires → "a" (LRU)
    // is evictable (use_count == 1) → cache size returns to 2.
    insert(f, "c", 3, 2);
    EXPECT_EQ(f.cache.size(), 2u);
    EXPECT_FALSE(f.cache.count("a"));  // Evicted.
    EXPECT_TRUE (f.cache.count("b"));
    EXPECT_TRUE (f.cache.count("c"));
}

TEST(LruCacheHelpers, EvictionSkipsHeldEntries_SoftCap)
{
    LruFixture f;
    auto a = insert(f, "a", 1, 1);  // Held externally.
    auto b = insert(f, "b", 2, 1);  // Held externally.
    auto c = insert(f, "c", 3, 1);  // Held externally.

    // Cap is 1 but every entry has use_count > 1 → soft-cap exceeded.
    EXPECT_EQ(f.cache.size(), 3u);
    EXPECT_TRUE(f.cache.count("a"));
    EXPECT_TRUE(f.cache.count("b"));
    EXPECT_TRUE(f.cache.count("c"));
}

TEST(LruCacheHelpers, CacheHitTouchBumpsRecency)
{
    LruFixture f;
    insert(f, "a", 1, 2);  // dropped — refcount 1.
    insert(f, "b", 2, 2);  // dropped.

    // Touch "a" → moves to MRU. Now "b" is LRU.
    {
        auto it = f.cache.find("a");
        ASSERT_NE(it, f.cache.end());
        touchCache(f.cache, f.order, it);
    }

    // Insert "c" → eviction kicks in. LRU is now "b" (because we touched a).
    insert(f, "c", 3, 2);

    EXPECT_EQ(f.cache.size(), 2u);
    EXPECT_TRUE (f.cache.count("a"));   // Saved by the touch.
    EXPECT_FALSE(f.cache.count("b"));   // Evicted.
    EXPECT_TRUE (f.cache.count("c"));
}

TEST(LruCacheHelpers, EnforceCacheLimitTightensRetroactively)
{
    LruFixture f;
    insert(f, "a", 1, 8);
    insert(f, "b", 2, 8);
    insert(f, "c", 3, 8);
    EXPECT_EQ(f.cache.size(), 3u);

    enforceCacheLimit(f.cache, f.order, 1);
    EXPECT_EQ(f.cache.size(), 1u);
    // The MRU entry survives ("c" was the last inserted).
    EXPECT_TRUE(f.cache.count("c"));
}

TEST(LruCacheHelpers, ReinsertExistingKeyDoesNotEvict)
{
    LruFixture f;
    auto a1 = insert(f, "a", 1, 2);  // cache holds value once we drop a1.
    auto b1 = insert(f, "b", 2, 2);

    // Re-insert "a" with a new value. The previous entry is overwritten;
    // the new entry should claim a fresh order-list slot. Net cache size
    // stays at 2 (no eviction needed).
    auto a2 = insert(f, "a", 99, 2);
    EXPECT_EQ(f.cache.size(), 2u);
    EXPECT_TRUE(f.cache.count("a"));
    EXPECT_TRUE(f.cache.count("b"));
    EXPECT_EQ(*f.cache["a"].value, 99);
}

// -- ResourceManager setter retroactive eviction (no GL — empty cache) ----

// Slice 18 Ts1 cleanup: renamed from `*IsNoOp` — without a GL context
// these tests can't populate the cache, so the *retroactive-eviction*
// half of the setter contract is exercised at engine launch. Here we
// pin only that the setters / clearAll don't crash on a fresh manager.
TEST(ResourceManagerLruTest, SetterOnEmptyCacheDoesNotCrash)
{
    ResourceManager rm;
    rm.setTextureCacheLimit(0);
    rm.setMeshCacheLimit(0);
    rm.setModelCacheLimit(0);
    EXPECT_EQ(rm.getTextureCount(), 0u);
    EXPECT_EQ(rm.getMeshCount(), 0u);
}

TEST(ResourceManagerLruTest, ClearAllOnEmptyCacheDoesNotCrash)
{
    ResourceManager rm;
    rm.clearAll();
    EXPECT_EQ(rm.getTextureCount(), 0u);
    EXPECT_EQ(rm.getMeshCount(), 0u);
}

}  // namespace Vestige::ResourceManagerLru::Test
