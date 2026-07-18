// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_foliage_texture_load.cpp
/// @brief 3D_E-0038 B1 — FoliageRenderer::setTypeTexture fallback/adopt decision.
///
/// setTypeTexture's live path needs a GL context (the upload), but its decision —
/// ADOPT the freshly-loaded texture or KEEP the current (procedural) one — is
/// factored into the pure predicate `foliageAdoptLoadedTexture(pathEmpty,
/// decodeNull, uploadedHandle)` so the fallback + upload-failure ordering contract
/// is testable WITHOUT a GL context (design §4.1/§7). The live GL path is covered
/// by the meadow `--visual-test` frame rendering GL-error-free.

#include <gtest/gtest.h>

#include "renderer/foliage_renderer.h"

using namespace Vestige;

// Adopt only when the path was non-empty AND the decode produced pixels AND the
// GL upload produced a non-zero handle.
TEST(FoliageTextureLoad, AdoptsOnValidDecodeAndUpload_B1)
{
    EXPECT_TRUE(foliageAdoptLoadedTexture(/*pathEmpty=*/false, /*decodeNull=*/false,
                                          /*uploadedHandle=*/42u));
}

TEST(FoliageTextureLoad, KeepsCurrentOnEmptyPath_B1)
{
    // Empty path = "no override" — never adopt, even if a handle is passed.
    EXPECT_FALSE(foliageAdoptLoadedTexture(/*pathEmpty=*/true, /*decodeNull=*/false, 42u));
}

TEST(FoliageTextureLoad, KeepsCurrentOnDecodeFailure_B1)
{
    // stbi_load returned null (missing / corrupt file) — keep the procedural default.
    EXPECT_FALSE(foliageAdoptLoadedTexture(/*pathEmpty=*/false, /*decodeNull=*/true, 0u));
}

TEST(FoliageTextureLoad, KeepsCurrentOnUploadFailure_B1)
{
    // Decode succeeded but the GL upload returned 0 — the highest-risk branch: the
    // type must fall back rather than adopt a null texture (design §4.1, loop-2 H2).
    EXPECT_FALSE(foliageAdoptLoadedTexture(/*pathEmpty=*/false, /*decodeNull=*/false,
                                           /*uploadedHandle=*/0u));
}
