// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_profile_log.cpp
/// @brief Headless unit tests for the profiler CSV formatter (design §8.1/§11).
///        `formatSampleRows` is a pure function — no GL context, no getters — so
///        the row format is pinned independently of a running engine.

#include <gtest/gtest.h>

#include "profiler/profile_log.h"

#include <string>
#include <vector>

using namespace Vestige;

namespace
{

// A representative sample mirroring the §8.1 example: one synthetic `total` per
// category plus real per-pass/per-scope rows, a nested CPU scope, and a mem row.
ProfileSample makeSample()
{
    ProfileSample s;
    s.timeSec = 12.0;
    s.fps = 62.9;
    s.entries = {
        {ProfileCategory::Frame, "total", 0, 15.9},
        {ProfileCategory::Gpu, "total", 0, 9.8},
        {ProfileCategory::Gpu, "ShadowPass", 0, 2.1},
        {ProfileCategory::Cpu, "total", 0, 4.2},
        {ProfileCategory::Cpu, "SceneUpdate", 0, 2.6},
        {ProfileCategory::Cpu, "Culling", 1, 0.8},
        {ProfileCategory::Mem, "gpu_mb", 0, 512.0},
    };
    return s;
}

} // namespace

TEST(ProfileLog, RowPerEntryInOrder)
{
    const auto rows = formatSampleRows(makeSample());
    ASSERT_EQ(rows.size(), 7u);
    // First-seen order is preserved (frame, gpu…, cpu…, mem).
    EXPECT_EQ(rows[0], "12.00,frame,total,0,15.900,62.9");
    EXPECT_EQ(rows[6], "12.00,mem,gpu_mb,0,512,");
}

TEST(ProfileLog, FpsOnlyOnFrameTotalRow)
{
    const auto rows = formatSampleRows(makeSample());
    // frame,total carries fps in the last column.
    EXPECT_EQ(rows[0], "12.00,frame,total,0,15.900,62.9");
    // Every other row ends with an empty fps column (a trailing comma).
    for (size_t i = 1; i < rows.size(); ++i)
    {
        EXPECT_EQ(rows[i].back(), ',') << "row " << i << ": " << rows[i];
    }
    // The gpu `total` row must NOT get fps even though its name is "total".
    EXPECT_EQ(rows[1], "12.00,gpu,total,0,9.800,");
}

TEST(ProfileLog, CpuDepthIsCarriedThrough)
{
    const auto rows = formatSampleRows(makeSample());
    // SceneUpdate is depth 0, its child Culling is depth 1.
    EXPECT_EQ(rows[4], "12.00,cpu,SceneUpdate,0,2.600,");
    EXPECT_EQ(rows[5], "12.00,cpu,Culling,1,0.800,");
}

TEST(ProfileLog, MemValueIsIntegerMbInMsColumn)
{
    const auto rows = formatSampleRows(makeSample());
    // mem carries MB as an integer in the `ms` column (no decimals), fps blank.
    EXPECT_EQ(rows[6], "12.00,mem,gpu_mb,0,512,");
}

TEST(ProfileLog, EmptySampleYieldsNoRows)
{
    ProfileSample s;
    s.timeSec = 3.0;
    s.fps = 60.0;
    EXPECT_TRUE(formatSampleRows(s).empty());
}
