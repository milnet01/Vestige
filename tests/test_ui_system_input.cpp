// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_system_input.cpp
/// @brief Phase 9C input-routing tests for UISystem (CPU-side flag logic).

#include <gtest/gtest.h>

#include "systems/ui_system.h"

using namespace Vestige;

TEST(UISystemInput, DefaultDoesNotCapture)
{
    UISystem sys;
    EXPECT_FALSE(sys.wantsCaptureInput());
    EXPECT_FALSE(sys.isModalCapture());
}

TEST(UISystemInput, ModalCaptureFlagDrivesWantsCaptureInput)
{
    UISystem sys;
    sys.setModalCapture(true);
    EXPECT_TRUE(sys.isModalCapture());
    EXPECT_TRUE(sys.wantsCaptureInput());

    sys.setModalCapture(false);
    EXPECT_FALSE(sys.wantsCaptureInput());
}

TEST(UISystemInput, ThemeAccessorReturnsMutableReference)
{
    UISystem sys;
    sys.getTheme().crosshairLength = 99.0f;
    EXPECT_FLOAT_EQ(sys.getTheme().crosshairLength, 99.0f);
}

TEST(UISystemInput, UpdateMouseHitOnEmptyCanvasDoesNotCapture)
{
    // No interactive elements in the canvas → cursor hit reports false →
    // wantsCaptureInput stays false (assuming modal is also off).
    UISystem sys;
    sys.updateMouseHit({100.0f, 100.0f}, 1920, 1080);
    EXPECT_FALSE(sys.wantsCaptureInput());
}
