// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_engine_paths.cpp
/// @brief Phase 10.9 Slice 1 F1 — spec-driven tests for engine
///        path-composition helpers.
///
/// These tests are authored from `docs/phases/phase_10_7_design.md` §4.2 and the
/// existing engine convention of `<assetPath>/<sub>` (used
/// throughout `engine.cpp` for fonts, scenes, shaders, brush
/// preview, debug-draw). They must fail against the pre-fix helper
/// implementation that extracted the historical double-concat
/// behaviour verbatim, and pass after the F1 green commit corrects
/// the join.

#include <gtest/gtest.h>

#include "core/engine_paths.h"

using namespace Vestige;

// --- docs/phases/phase_10_7_design.md §4.2: caption map lives at
//     `<assetPath>/captions.json`.

TEST(EnginePaths, CaptionMapPath_DefaultAssetRoot_JoinsWithSingleSlash)
{
    // Default config uses `assetPath = "assets"`. The resolved
    // caption-file path must be `assets/captions.json`, NOT
    // `assetsassets/captions.json` (the double-concat bug this
    // test exists to pin).
    EXPECT_EQ(captionMapPath("assets"), "assets/captions.json");
}

TEST(EnginePaths, CaptionMapPath_AbsoluteAssetRoot_JoinsWithSingleSlash)
{
    // A user shipping an installed game typically configures an
    // absolute asset root. The join must be a single slash, not
    // "assetsassets" prepended to the filename.
    EXPECT_EQ(captionMapPath("/opt/vestige-game/assets"),
              "/opt/vestige-game/assets/captions.json");
}

TEST(EnginePaths, CaptionMapPath_TrailingSlashAssetRoot_DoesNotDoubleSlash)
{
    // `assetPath = "assets/"` is a plausible user configuration;
    // it must not produce `assets//captions.json` (which most
    // OSes tolerate but which breaks path-equality in any cache
    // keyed by the string).
    EXPECT_EQ(captionMapPath("assets/"), "assets/captions.json");
    EXPECT_EQ(captionMapPath("/opt/vestige-game/assets/"),
              "/opt/vestige-game/assets/captions.json");
}

TEST(EnginePaths, CaptionMapPath_EmptyAssetRoot_ProducesBarefilename)
{
    // Edge case: a consumer that has not yet configured an asset
    // root (rare but reachable in tests) should resolve to just
    // `captions.json`, not `assetscaptions.json`.
    EXPECT_EQ(captionMapPath(""), "captions.json");
}
