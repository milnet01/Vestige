// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_memory_tracker.cpp
/// @brief Unit tests for the MemoryTracker.
#include "profiler/memory_tracker.h"

#include <gtest/gtest.h>

using namespace Vestige;

// =============================================================================
// MemoryTrackerTest — CPU heap tracking via recordAlloc / recordFree
// =============================================================================
//
// NOTE: MemoryTracker uses static atomics shared across all tests in this
// binary. These tests use explicit recordAlloc/recordFree pairs with known
// sizes to verify relative changes rather than relying on absolute values.

/// @brief Fixture that snapshots baseline counters before each test.
class MemoryTrackerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_baseBytes = MemoryTracker::getCpuAllocatedBytes();
        m_baseCount = MemoryTracker::getCpuAllocationCount();
        m_basePeak = MemoryTracker::getCpuPeakBytes();
    }

    size_t m_baseBytes = 0;
    size_t m_baseCount = 0;
    size_t m_basePeak = 0;
};

TEST_F(MemoryTrackerTest, RecordAllocIncreasesBytesAndCount)
{
    MemoryTracker::recordAlloc(256);

    EXPECT_EQ(MemoryTracker::getCpuAllocatedBytes(), m_baseBytes + 256);
    EXPECT_EQ(MemoryTracker::getCpuAllocationCount(), m_baseCount + 1);

    // Clean up so later tests start from a known delta
    MemoryTracker::recordFree(256);
}

TEST_F(MemoryTrackerTest, RecordFreeDecreasesBytesAndCount)
{
    MemoryTracker::recordAlloc(512);
    MemoryTracker::recordFree(512);

    EXPECT_EQ(MemoryTracker::getCpuAllocatedBytes(), m_baseBytes);
    EXPECT_EQ(MemoryTracker::getCpuAllocationCount(), m_baseCount);
}

TEST_F(MemoryTrackerTest, MultipleAllocationsAccumulate)
{
    MemoryTracker::recordAlloc(100);
    MemoryTracker::recordAlloc(200);
    MemoryTracker::recordAlloc(300);

    EXPECT_EQ(MemoryTracker::getCpuAllocatedBytes(), m_baseBytes + 600);
    EXPECT_EQ(MemoryTracker::getCpuAllocationCount(), m_baseCount + 3);

    // Clean up
    MemoryTracker::recordFree(100);
    MemoryTracker::recordFree(200);
    MemoryTracker::recordFree(300);
}

TEST_F(MemoryTrackerTest, PeakBytesTracksMaximum)
{
    // Allocate a large block that should set a new peak
    constexpr size_t BIG_ALLOC = 64 * 1024 * 1024;  // 64 MB
    MemoryTracker::recordAlloc(BIG_ALLOC);

    size_t peakAfterAlloc = MemoryTracker::getCpuPeakBytes();
    EXPECT_GE(peakAfterAlloc, m_baseBytes + BIG_ALLOC);

    // Free it — peak should NOT decrease
    MemoryTracker::recordFree(BIG_ALLOC);

    EXPECT_EQ(MemoryTracker::getCpuPeakBytes(), peakAfterAlloc);
}

TEST_F(MemoryTrackerTest, PeakDoesNotDecreaseAfterFree)
{
    MemoryTracker::recordAlloc(1024);
    size_t peakA = MemoryTracker::getCpuPeakBytes();

    MemoryTracker::recordFree(1024);
    size_t peakB = MemoryTracker::getCpuPeakBytes();

    // Peak must be monotonically non-decreasing
    EXPECT_GE(peakB, peakA);
}

TEST_F(MemoryTrackerTest, GpuStatsDefaultToZero)
{
    MemoryTracker tracker;

    // Before any update(), GPU stats should be zero
    EXPECT_EQ(tracker.getGpuUsedMB(), 0u);
    EXPECT_EQ(tracker.getGpuTotalMB(), 0u);
}

TEST_F(MemoryTrackerTest, UpdateDoesNotCrash)
{
    MemoryTracker tracker;

    // update() reads from sysfs (may or may not exist). It should never crash.
    for (int i = 0; i < 120; i++)
    {
        EXPECT_NO_THROW(tracker.update());
    }
}
