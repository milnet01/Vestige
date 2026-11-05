// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_visual_test_runner.cpp
/// @brief VisualTestRunner's capture file names (no GL).

#include "testing/visual_test_runner.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(VisualTestRunner, CaptureNameShowsTheRotationInWholeDegrees)
{
    EXPECT_EQ(VisualTestRunner::captureBasename(0, "gate_exterior", 1, 45.0f),
              "01_gate_exterior_rot045");
    // lane-23 lead visual_test_runner.cpp:127: the angle was truncated before
    // multiplying, so 22.5-degree steps read 022 and 044 for 22.5 and 45.
    EXPECT_EQ(VisualTestRunner::captureBasename(2, "pond", 1, 22.5f), "03_pond_rot023");
    EXPECT_EQ(VisualTestRunner::captureBasename(2, "pond", 2, 22.5f), "03_pond_rot045");
}
