// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_memory_tracker.cpp
/// @brief Unit tests for the MemoryTracker.
#include "profiler/memory_tracker.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

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

// Regression: a free without a matching alloc (or a double-free) must clamp
// at zero, not wrap the underlying atomic to SIZE_MAX.
//
// Policy (recordFree clamps the two counters independently):
//   * bytes — over-freeing subtracts down to zero, never below; the 1024-byte
//     free against a 64-byte alloc lands the byte counter back at baseline.
//   * count — decrements by exactly one per free call (also floored at zero),
//     regardless of the byte amount; the single matching alloc's count is
//     undone, returning the count to baseline as well.
TEST_F(MemoryTrackerTest, RecordFreeUnderflowClampsAtZero)
{
    MemoryTracker::recordAlloc(64);
    // Free more than we allocated.
    MemoryTracker::recordFree(1024);

    EXPECT_EQ(MemoryTracker::getCpuAllocatedBytes(), m_baseBytes);
    EXPECT_EQ(MemoryTracker::getCpuAllocationCount(), m_baseCount);
}

TEST_F(MemoryTrackerTest, RecordFreeWithoutAllocIsNoop)
{
    MemoryTracker::recordFree(512);

    EXPECT_EQ(MemoryTracker::getCpuAllocatedBytes(), m_baseBytes);
    EXPECT_EQ(MemoryTracker::getCpuAllocationCount(), m_baseCount);
}

// Slice 18 Ts1 clarification: the two tests below operate on a local
// `MemoryTracker tracker;` instance, not the static recordAlloc state
// the rest of the suite shares. Moved out of the `MemoryTrackerTest`
// fixture (which captures static baseline) to a plain `TEST` so the
// reader can't confuse instance-state and static-state observation.
TEST(MemoryTrackerGpu, GpuStatsDefaultToZero)
{
    MemoryTracker tracker;
    EXPECT_EQ(tracker.getGpuUsedMB(), 0u);
    EXPECT_EQ(tracker.getGpuTotalMB(), 0u);
}

TEST(MemoryTrackerGpu, UpdateDoesNotCrash)
{
    MemoryTracker tracker;
    // update() reads from sysfs (may or may not exist). The contract under
    // test is observability — sysfs is read once or not at all on a given
    // host. Three samples (first read, repeat read, idempotency check) cover
    // it; 120 reads was just sysfs spam.
    EXPECT_NO_THROW(tracker.update());
    EXPECT_NO_THROW(tracker.update());
    EXPECT_NO_THROW(tracker.update());
}

// =============================================================================
// DRM fdinfo — this process's own VRAM (3D_E-0820)
// =============================================================================
//
// amdgpu's mem_info_vram_used is card-wide, so the profiler's gpu_mb counted
// every other program's VRAM too. The process's own figure comes from its DRM
// clients' fdinfo records (kernel Documentation/gpu/drm-usage-stats.rst).

namespace
{

/// A real amdgpu record from this machine (2026-11-05), trimmed.
std::string amdgpuRecord(const std::string& clientId, const std::string& vram)
{
    return "pos:\t0\nflags:\t02100002\ndrm-driver:\tamdgpu\n"
           "drm-client-id:\t" + clientId + "\ndrm-pdev:\t0000:30:00.0\n"
           "drm-total-gtt:\t10252 KiB\ndrm-resident-gtt:\t10252 KiB\n"
           "drm-total-vram:\t99999 KiB\ndrm-resident-vram:\t" + vram + "\n";
}

}  // namespace

TEST(DrmFdinfo, ReadsResidentVramOfOneClient)
{
    const auto c = MemoryTracker::parseDrmFdinfo(amdgpuRecord("20", "14764 KiB"));
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->clientId, "20");
    EXPECT_EQ(c->device, "0000:30:00.0");
    EXPECT_EQ(c->vramBytes, 14764ULL * 1024ULL) << "resident, not total";
}

TEST(DrmFdinfo, UnitsAreBytesKibOrMib)
{
    EXPECT_EQ(MemoryTracker::parseDrmFdinfo(amdgpuRecord("1", "4096"))->vramBytes, 4096ULL);
    EXPECT_EQ(MemoryTracker::parseDrmFdinfo(amdgpuRecord("1", "3 MiB"))->vramBytes,
              3ULL * 1024ULL * 1024ULL);
}

TEST(DrmFdinfo, OlderKernelKeyIsUsedWhenResidentIsAbsent)
{
    const std::string older =
        "drm-driver:\tamdgpu\ndrm-client-id:\t7\ndrm-pdev:\t0000:30:00.0\n"
        "drm-memory-vram:\t2048 KiB\n";
    EXPECT_EQ(MemoryTracker::parseDrmFdinfo(older)->vramBytes, 2048ULL * 1024ULL);

    // Either order, resident wins over the older key.
    const std::string both = older + "drm-resident-vram:\t1024 KiB\n";
    EXPECT_EQ(MemoryTracker::parseDrmFdinfo(both)->vramBytes, 1024ULL * 1024ULL);
    const std::string bothFirst =
        "drm-client-id:\t7\ndrm-resident-vram:\t1024 KiB\ndrm-memory-vram:\t2048 KiB\n";
    EXPECT_EQ(MemoryTracker::parseDrmFdinfo(bothFirst)->vramBytes, 1024ULL * 1024ULL);
}

TEST(DrmFdinfo, NonDrmFdOrNoVramKeyIsNotAClient)
{
    EXPECT_FALSE(MemoryTracker::parseDrmFdinfo("pos:\t0\nflags:\t02\nmnt_id:\t41\n").has_value());
    EXPECT_FALSE(MemoryTracker::parseDrmFdinfo(
        "drm-driver:\ti915\ndrm-client-id:\t3\ndrm-total-system0:\t4 KiB\n").has_value());
}

TEST(DrmFdinfo, EachClientCountsOnceAndClientsSum)
{
    const std::vector<std::string> records = {
        amdgpuRecord("20", "1000 KiB"),
        amdgpuRecord("20", "1000 KiB"),  // a dup'd fd of the same client
        amdgpuRecord("21", "500 KiB"),
        "pos:\t0\nflags:\t0\n",            // not a DRM fd
    };
    const auto sum = MemoryTracker::processVramBytes(records);
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(*sum, 1500ULL * 1024ULL);
}

TEST(DrmFdinfo, NoClientMeansUnknownNotZero)
{
    EXPECT_FALSE(MemoryTracker::processVramBytes({"pos:\t0\n"}).has_value());
    EXPECT_FALSE(MemoryTracker::processVramBytes({}).has_value());
}
