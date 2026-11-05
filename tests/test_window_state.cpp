// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_window_state.cpp
/// @brief Saved window state: parsing never throws, negative positions
///        restore (lane-02 leads window.cpp:258 and :274). No GLFW.

#include "core/window_state.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(WindowState, ReadsAWellFormedFile)
{
    const auto s = parseWindowState(R"({"width":1600,"height":900,"x":-1820,"y":40,"maximized":true})");
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->width, 1600);
    EXPECT_EQ(s->height, 900);
    EXPECT_TRUE(s->hasPosition);
    EXPECT_EQ(s->x, -1820);
    EXPECT_TRUE(s->maximized);
}

// window.cpp:258: value() threw type_error on these, which ended start-up.
TEST(WindowState, MalformedFilesAreIgnoredNotThrown)
{
    for (const char* text : {R"({"width":"x","height":900})", "[]", "5", "null",
                             "{not json", R"({"width":1600,"height":900,"maximized":"yes"})"})
    {
        EXPECT_NO_THROW({
            const auto s = parseWindowState(text);
            if (s) EXPECT_FALSE(s->maximized) << text;
        }) << text;
    }
    EXPECT_FALSE(parseWindowState(R"({"width":"x","height":900})").has_value());
    EXPECT_FALSE(parseWindowState("[]").has_value());
}

TEST(WindowState, SizeOutsideTheSaneRangeIsRefused)
{
    EXPECT_FALSE(parseWindowState(R"({"width":100,"height":900})").has_value());
    EXPECT_FALSE(parseWindowState(R"({"width":1600,"height":9000})").has_value());
}

TEST(WindowState, MissingPositionIsNotAPosition)
{
    const auto s = parseWindowState(R"({"width":1600,"height":900})");
    ASSERT_TRUE(s.has_value());
    EXPECT_FALSE(s->hasPosition);
}

// window.cpp:274: "x >= 0 && y >= 0" dropped a window saved on a monitor to
// the left of the primary one.
TEST(WindowState, NegativePositionOnALeftMonitorIsKept)
{
    const std::vector<MonitorRect> monitors = {{-1920, 0, 1920, 1080}, {0, 0, 2560, 1440}};
    WindowState onLeft;
    onLeft.width = 1600;
    onLeft.height = 900;
    onLeft.x = -1820;
    onLeft.y = 40;
    EXPECT_TRUE(titleBarOnAMonitor(onLeft, monitors));

    WindowState offEverything = onLeft;
    offEverything.x = -5000;
    EXPECT_FALSE(titleBarOnAMonitor(offEverything, monitors));
}
