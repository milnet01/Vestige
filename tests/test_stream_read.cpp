// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_stream_read.cpp
/// @brief 3D_E-0874 — a read that stops early leaves the old data whole.
///
/// Terrain::loadHeightmap and loadSplatmap read through readWholeOrKeep. A
/// short read used to return false with the live heightmap half replaced.

#include "utils/stream_read.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace Vestige::StreamRead::Test
{

std::string bytesOf(const std::vector<float>& values)
{
    return std::string(reinterpret_cast<const char*>(values.data()),
                       values.size() * sizeof(float));
}

TEST(StreamRead, ShortReadKeepsTheOldData_0874)
{
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    // Three of the four floats the destination needs.
    std::istringstream in(bytesOf({9.0f, 9.0f, 9.0f}), std::ios::binary);

    std::streamsize got = -1;
    EXPECT_FALSE(readWholeOrKeep(in, data, &got));
    EXPECT_EQ(got, static_cast<std::streamsize>(3 * sizeof(float)));
    EXPECT_EQ(data, (std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}));
}

TEST(StreamRead, FullReadReplacesTheData_0874)
{
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    std::istringstream in(bytesOf({5.0f, 6.0f, 7.0f, 8.0f}), std::ios::binary);

    std::streamsize got = -1;
    EXPECT_TRUE(readWholeOrKeep(in, data, &got));
    EXPECT_EQ(got, static_cast<std::streamsize>(4 * sizeof(float)));
    EXPECT_EQ(data, (std::vector<float>{5.0f, 6.0f, 7.0f, 8.0f}));
}

}  // namespace Vestige::StreamRead::Test
