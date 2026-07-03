// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_reverb_ir_pool.cpp
/// @brief AX2 R2 — LRU / byte-limit / pinning invariants of the convolution
///        IR pool. Device-free (the pool holds opaque buffer IDs), so these
///        exercise the eviction logic the headless AudioEngine suite cannot.

#include <gtest/gtest.h>

#include "audio/reverb_ir_pool.h"

#include <vector>

using namespace Vestige;

namespace
{
// Each fake IR is 100 bytes; buffer IDs are arbitrary non-zero handles.
constexpr std::size_t kIr = 100;
}

TEST(ReverbIrPool, FindMissReturnsZero)
{
    ReverbIrPool pool;
    EXPECT_EQ(pool.find("nope.wav"), 0u);
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_EQ(pool.bytes(), 0u);
}

TEST(ReverbIrPool, InsertThenFindReturnsBufferAndAccounts)
{
    ReverbIrPool pool;
    const auto evicted = pool.insert("a.wav", 1u, kIr);
    EXPECT_TRUE(evicted.empty());
    EXPECT_EQ(pool.find("a.wav"), 1u);
    EXPECT_EQ(pool.count(), 1u);
    EXPECT_EQ(pool.bytes(), kIr);
}

TEST(ReverbIrPool, ReinsertSamePathReplacesAndReturnsOldBuffer)
{
    ReverbIrPool pool;
    pool.insert("a.wav", 1u, kIr);
    // Same path, different buffer ID → old buffer handed back for deletion,
    // byte total unchanged (still one entry).
    const auto evicted = pool.insert("a.wav", 2u, kIr);
    ASSERT_EQ(evicted.size(), 1u);
    EXPECT_EQ(evicted[0], 1u);
    EXPECT_EQ(pool.find("a.wav"), 2u);
    EXPECT_EQ(pool.count(), 1u);
    EXPECT_EQ(pool.bytes(), kIr);
}

TEST(ReverbIrPool, EvictsLruTailWhenOverLimit)
{
    ReverbIrPool pool;
    pool.setLimit(2 * kIr + 50);   // fits two IRs, not three
    pool.insert("a.wav", 1u, kIr);
    pool.insert("b.wav", 2u, kIr);
    const auto evicted = pool.insert("c.wav", 3u, kIr);  // A is oldest

    ASSERT_EQ(evicted.size(), 1u);
    EXPECT_EQ(evicted[0], 1u);           // A evicted
    EXPECT_EQ(pool.find("a.wav"), 0u);
    EXPECT_EQ(pool.find("b.wav"), 2u);
    EXPECT_EQ(pool.find("c.wav"), 3u);
    EXPECT_EQ(pool.count(), 2u);
    EXPECT_EQ(pool.bytes(), 2 * kIr);
}

TEST(ReverbIrPool, FindPromotesToMruSoEvictionSkipsIt)
{
    ReverbIrPool pool;
    pool.setLimit(3 * kIr + 50);       // fits three
    pool.insert("a.wav", 1u, kIr);
    pool.insert("b.wav", 2u, kIr);
    pool.insert("c.wav", 3u, kIr);
    EXPECT_EQ(pool.find("a.wav"), 1u); // A is now MRU; B is the LRU tail

    const auto evicted = pool.insert("d.wav", 4u, kIr);
    ASSERT_EQ(evicted.size(), 1u);
    EXPECT_EQ(evicted[0], 2u);          // B evicted, not the just-touched A
    EXPECT_EQ(pool.find("a.wav"), 1u);
    EXPECT_EQ(pool.find("b.wav"), 0u);
}

TEST(ReverbIrPool, PinnedIrIsNeverEvicted)
{
    ReverbIrPool pool;
    pool.setLimit(2 * kIr + 50);
    pool.insert("a.wav", 1u, kIr);
    pool.setPinned(1u);                // pin the oldest (the attached IR)
    pool.insert("b.wav", 2u, kIr);
    const auto evicted = pool.insert("c.wav", 3u, kIr);

    ASSERT_EQ(evicted.size(), 1u);
    EXPECT_EQ(evicted[0], 2u);         // B evicted; pinned A survives at the tail
    EXPECT_EQ(pool.find("a.wav"), 1u);
    EXPECT_EQ(pool.pinned(), 1u);
}

TEST(ReverbIrPool, MruFrontSurvivesEvenWhenItAloneExceedsLimit)
{
    ReverbIrPool pool;
    pool.setLimit(kIr / 2);            // smaller than a single IR
    const auto evicted = pool.insert("a.wav", 1u, kIr);
    EXPECT_TRUE(evicted.empty());      // nothing to evict but itself → kept
    EXPECT_EQ(pool.find("a.wav"), 1u);
    EXPECT_EQ(pool.count(), 1u);
    EXPECT_GT(pool.bytes(), pool.limitBytes());  // documented overflow
}

TEST(ReverbIrPool, PinnedPlusMruOverflowIsNotEvicted)
{
    ReverbIrPool pool;
    pool.setLimit(kIr + 50);           // fits only one IR
    pool.insert("a.wav", 1u, kIr);
    pool.setPinned(1u);
    const auto evicted = pool.insert("b.wav", 2u, kIr);  // B=MRU front, A=pinned tail

    EXPECT_TRUE(evicted.empty());      // both protected → overflow tolerated
    EXPECT_EQ(pool.count(), 2u);
    EXPECT_GT(pool.bytes(), pool.limitBytes());
}

TEST(ReverbIrPool, SetLimitEvictsDownImmediately)
{
    ReverbIrPool pool;
    pool.insert("a.wav", 1u, kIr);
    pool.insert("b.wav", 2u, kIr);
    pool.insert("c.wav", 3u, kIr);     // order: C(MRU) B A(LRU)

    const auto evicted = pool.setLimit(kIr + 50);   // fit ~one
    // A then B evicted from the tail; C (MRU front) is protected.
    ASSERT_EQ(evicted.size(), 2u);
    EXPECT_EQ(evicted[0], 1u);         // A first (oldest)
    EXPECT_EQ(evicted[1], 2u);         // then B
    EXPECT_EQ(pool.count(), 1u);
    EXPECT_EQ(pool.find("c.wav"), 3u);
}

TEST(ReverbIrPool, ClearReturnsAllAndResets)
{
    ReverbIrPool pool;
    pool.insert("a.wav", 1u, kIr);
    pool.insert("b.wav", 2u, kIr);
    pool.setPinned(2u);

    const auto all = pool.clear();
    EXPECT_EQ(all.size(), 2u);         // both IDs returned for deletion
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_EQ(pool.bytes(), 0u);
    EXPECT_EQ(pool.pinned(), 0u);      // unpinned
    EXPECT_EQ(pool.find("a.wav"), 0u);
}

TEST(ReverbIrPool, DefaultLimitIs64MiB)
{
    ReverbIrPool pool;
    EXPECT_EQ(pool.limitBytes(), 64ull * 1024 * 1024);
    EXPECT_EQ(ReverbIrPool::kDefaultLimitBytes, 64ull * 1024 * 1024);
}
