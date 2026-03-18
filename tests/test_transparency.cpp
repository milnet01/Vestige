/// @file test_transparency.cpp
/// @brief Unit tests for transparency / alpha blending (alpha mode, cutoff, sorting).
#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include "renderer/material.h"

#include <algorithm>
#include <vector>

// --- Back-to-front sort helper (mirroring renderer implementation) ---

struct TransparentItem
{
    glm::vec3 position;
    float distanceToCamera;
};

static void sortBackToFront(std::vector<TransparentItem>& items, const glm::vec3& cameraPos)
{
    for (auto& item : items)
    {
        item.distanceToCamera = glm::length(item.position - cameraPos);
    }
    std::sort(items.begin(), items.end(), [](const TransparentItem& a, const TransparentItem& b)
    {
        return a.distanceToCamera > b.distanceToCamera;  // Farthest first
    });
}

// --- Alpha discard logic (mirroring shader implementation) ---

static bool shouldDiscard(float alpha, float cutoff)
{
    return alpha < cutoff;
}

// =============================================================================
// AlphaMode default tests
// =============================================================================

TEST(TransparencyTest, DefaultAlphaModeIsOpaque)
{
    Vestige::Material mat;
    EXPECT_EQ(mat.getAlphaMode(), Vestige::AlphaMode::OPAQUE);
}

TEST(TransparencyTest, SetGetAlphaModeMask)
{
    Vestige::Material mat;
    mat.setAlphaMode(Vestige::AlphaMode::MASK);
    EXPECT_EQ(mat.getAlphaMode(), Vestige::AlphaMode::MASK);
}

TEST(TransparencyTest, SetGetAlphaModeBlend)
{
    Vestige::Material mat;
    mat.setAlphaMode(Vestige::AlphaMode::BLEND);
    EXPECT_EQ(mat.getAlphaMode(), Vestige::AlphaMode::BLEND);
}

// =============================================================================
// Alpha cutoff tests
// =============================================================================

TEST(TransparencyTest, DefaultAlphaCutoff)
{
    Vestige::Material mat;
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 0.5f);
}

TEST(TransparencyTest, AlphaCutoffClampLow)
{
    Vestige::Material mat;
    mat.setAlphaCutoff(-0.5f);
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 0.0f);
}

TEST(TransparencyTest, AlphaCutoffClampHigh)
{
    Vestige::Material mat;
    mat.setAlphaCutoff(2.0f);
    EXPECT_FLOAT_EQ(mat.getAlphaCutoff(), 1.0f);
}

// =============================================================================
// Double-sided tests
// =============================================================================

TEST(TransparencyTest, DefaultDoubleSidedIsFalse)
{
    Vestige::Material mat;
    EXPECT_FALSE(mat.isDoubleSided());
}

TEST(TransparencyTest, SetDoubleSided)
{
    Vestige::Material mat;
    mat.setDoubleSided(true);
    EXPECT_TRUE(mat.isDoubleSided());
    mat.setDoubleSided(false);
    EXPECT_FALSE(mat.isDoubleSided());
}

// =============================================================================
// Base color alpha tests
// =============================================================================

TEST(TransparencyTest, DefaultBaseColorAlpha)
{
    Vestige::Material mat;
    EXPECT_FLOAT_EQ(mat.getBaseColorAlpha(), 1.0f);
}

TEST(TransparencyTest, BaseColorAlphaClampLow)
{
    Vestige::Material mat;
    mat.setBaseColorAlpha(-0.5f);
    EXPECT_FLOAT_EQ(mat.getBaseColorAlpha(), 0.0f);
}

TEST(TransparencyTest, BaseColorAlphaClampHigh)
{
    Vestige::Material mat;
    mat.setBaseColorAlpha(2.0f);
    EXPECT_FLOAT_EQ(mat.getBaseColorAlpha(), 1.0f);
}

// =============================================================================
// Back-to-front sort tests
// =============================================================================

TEST(TransparencyTest, BackToFrontSortFarthestFirst)
{
    glm::vec3 cameraPos(0.0f, 0.0f, 0.0f);
    std::vector<TransparentItem> items = {
        {glm::vec3(0.0f, 0.0f, -1.0f), 0.0f},   // Near
        {glm::vec3(0.0f, 0.0f, -10.0f), 0.0f},  // Far
        {glm::vec3(0.0f, 0.0f, -5.0f), 0.0f},   // Mid
    };

    sortBackToFront(items, cameraPos);

    // Farthest should be first
    EXPECT_FLOAT_EQ(items[0].distanceToCamera, 10.0f);
    EXPECT_FLOAT_EQ(items[1].distanceToCamera, 5.0f);
    EXPECT_FLOAT_EQ(items[2].distanceToCamera, 1.0f);
}

// =============================================================================
// Alpha discard tests
// =============================================================================

TEST(TransparencyTest, AlphaDiscardBelowCutoff)
{
    EXPECT_TRUE(shouldDiscard(0.3f, 0.5f));
}

TEST(TransparencyTest, AlphaDiscardAtCutoffKeeps)
{
    // At cutoff: alpha (0.5) is NOT less than cutoff (0.5), so keep
    EXPECT_FALSE(shouldDiscard(0.5f, 0.5f));
}

TEST(TransparencyTest, AlphaDiscardAboveCutoffKeeps)
{
    EXPECT_FALSE(shouldDiscard(0.8f, 0.5f));
}
