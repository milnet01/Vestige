// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_texture_filter.cpp
/// @brief Unit tests for TextureFilterMode enum and Texture filter mode tracking.
#include "renderer/texture.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(TextureFilterTest, DefaultFilterModeIsTrilinear)
{
    Texture texture;
    EXPECT_EQ(texture.getFilterMode(), TextureFilterMode::TRILINEAR);
}

TEST(TextureFilterTest, FilterModeEnumValues)
{
    // Verify enum values are sequential for combo box indexing
    EXPECT_EQ(static_cast<int>(TextureFilterMode::NEAREST), 0);
    EXPECT_EQ(static_cast<int>(TextureFilterMode::LINEAR), 1);
    EXPECT_EQ(static_cast<int>(TextureFilterMode::TRILINEAR), 2);
    EXPECT_EQ(static_cast<int>(TextureFilterMode::ANISOTROPIC_4X), 3);
    EXPECT_EQ(static_cast<int>(TextureFilterMode::ANISOTROPIC_8X), 4);
    EXPECT_EQ(static_cast<int>(TextureFilterMode::ANISOTROPIC_16X), 5);
}

TEST(TextureFilterTest, SetFilterModeWithoutGlContextDoesNotCrash)
{
    // setFilterMode on an unloaded texture (m_textureId == 0) should be a no-op
    Texture texture;
    EXPECT_FALSE(texture.isLoaded());
    EXPECT_NO_THROW(texture.setFilterMode(TextureFilterMode::NEAREST));
    // Filter mode stored even without GL context
    // Note: actual GL state change skipped when m_textureId == 0
}

TEST(TextureFilterTest, MoveConstructorPreservesFilterMode)
{
    Texture a;
    // Can't actually set filter mode without GL context, but verify move semantics
    Texture b(std::move(a));
    EXPECT_EQ(b.getFilterMode(), TextureFilterMode::TRILINEAR);
}
