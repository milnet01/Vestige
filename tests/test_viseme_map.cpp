// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_viseme_map.cpp
/// @brief Unit tests for the VisemeMap phoneme-to-blend-shape table.
#include "experimental/animation/viseme_map.h"

#include <gtest/gtest.h>

using namespace Vestige;

class VisemeMapTest : public ::testing::Test {};

TEST_F(VisemeMapTest, AllVisemesExist)
{
    for (int i = 0; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        const auto& shape = VisemeMap::get(viseme);
        EXPECT_EQ(shape.viseme, viseme);
    }
}

TEST_F(VisemeMapTest, AllVisemesHaveNames)
{
    for (int i = 0; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        const char* name = visemeName(viseme);
        EXPECT_NE(name, nullptr);
        EXPECT_GT(strlen(name), 0u);
    }
}

TEST_F(VisemeMapTest, RestVisemeHasNoEntries)
{
    const auto& rest = VisemeMap::get(Viseme::X);
    EXPECT_TRUE(rest.entries.empty());
}

TEST_F(VisemeMapTest, NonRestVisemesHaveEntries)
{
    for (int i = 1; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        const auto& shape = VisemeMap::get(viseme);
        EXPECT_FALSE(shape.entries.empty())
            << "Viseme " << visemeName(viseme) << " has no entries";
    }
}

TEST_F(VisemeMapTest, AllWeightsInValidRange)
{
    for (int i = 0; i < static_cast<int>(Viseme::COUNT); ++i)
    {
        auto viseme = static_cast<Viseme>(i);
        for (const auto& entry : VisemeMap::get(viseme).entries)
        {
            EXPECT_GE(entry.weight, 0.0f);
            EXPECT_LE(entry.weight, 1.0f);
        }
    }
}

TEST_F(VisemeMapTest, RhubarbCharRoundtrip)
{
    const char chars[] = {'X', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    for (char c : chars)
    {
        Viseme v = VisemeMap::fromRhubarbChar(c);
        char result = VisemeMap::toRhubarbChar(v);
        EXPECT_EQ(result, c) << "Roundtrip failed for char '" << c << "'";
    }
}

TEST_F(VisemeMapTest, RhubarbCharCaseInsensitive)
{
    EXPECT_EQ(VisemeMap::fromRhubarbChar('a'), Viseme::A);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('A'), Viseme::A);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('x'), Viseme::X);
}

TEST_F(VisemeMapTest, UnknownCharDefaultsToRest)
{
    EXPECT_EQ(VisemeMap::fromRhubarbChar('Z'), Viseme::X);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('1'), Viseme::X);
    EXPECT_EQ(VisemeMap::fromRhubarbChar('\0'), Viseme::X);
}

TEST_F(VisemeMapTest, OutOfRangeVisemeFallsBackToRest)
{
    const auto& shape = VisemeMap::get(static_cast<Viseme>(255));
    EXPECT_EQ(shape.viseme, Viseme::X);
}

TEST_F(VisemeMapTest, BlendWeightsAtZero)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::A, Viseme::D, 0.0f, out);

    // Should be pure A
    const auto& shapeA = VisemeMap::get(Viseme::A);
    for (const auto& entry : shapeA.entries)
    {
        EXPECT_NEAR(out[entry.shapeName], entry.weight, 0.001f);
    }
}

TEST_F(VisemeMapTest, BlendWeightsAtOne)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::A, Viseme::D, 1.0f, out);

    // Should be pure D
    const auto& shapeD = VisemeMap::get(Viseme::D);
    for (const auto& entry : shapeD.entries)
    {
        EXPECT_NEAR(out[entry.shapeName], entry.weight, 0.001f);
    }
}

TEST_F(VisemeMapTest, BlendWeightsAtHalf)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::A, Viseme::D, 0.5f, out);

    // jawOpen should be 0.5 * (A's jawOpen) + 0.5 * (D's jawOpen)
    // A has no jawOpen, D has jawOpen = 0.7
    EXPECT_NEAR(out["jawOpen"], 0.35f, 0.001f);
}

TEST_F(VisemeMapTest, BlendWithRestProducesScaledWeights)
{
    std::unordered_map<std::string, float> out;
    VisemeMap::blendWeights(Viseme::D, Viseme::X, 0.5f, out);

    // D has jawOpen = 0.7, blended 50% with rest (0) = 0.35
    EXPECT_NEAR(out["jawOpen"], 0.35f, 0.001f);
}

TEST_F(VisemeMapTest, GetCountMatchesEnumSize)
{
    EXPECT_EQ(VisemeMap::getCount(), static_cast<int>(Viseme::COUNT));
}

TEST_F(VisemeMapTest, ClosedVisemeHasMouthClose)
{
    // Viseme A (P/B/M) should have mouthClose
    const auto& shapeA = VisemeMap::get(Viseme::A);
    bool found = false;
    for (const auto& entry : shapeA.entries)
    {
        if (std::string(entry.shapeName) == "mouthClose")
        {
            found = true;
            EXPECT_GT(entry.weight, 0.5f);
        }
    }
    EXPECT_TRUE(found) << "Viseme A should have mouthClose";
}

TEST_F(VisemeMapTest, WideOpenVisemeHasLargeJawOpen)
{
    // Viseme D (AA) should have jawOpen > 0.5
    const auto& shapeD = VisemeMap::get(Viseme::D);
    bool found = false;
    for (const auto& entry : shapeD.entries)
    {
        if (std::string(entry.shapeName) == "jawOpen")
        {
            found = true;
            EXPECT_GT(entry.weight, 0.5f);
        }
    }
    EXPECT_TRUE(found) << "Viseme D should have jawOpen > 0.5";
}

TEST_F(VisemeMapTest, PuckerVisemeHasMouthPucker)
{
    // Viseme F (UW/OW/W) should have mouthPucker
    const auto& shapeF = VisemeMap::get(Viseme::F);
    bool found = false;
    for (const auto& entry : shapeF.entries)
    {
        if (std::string(entry.shapeName) == "mouthPucker")
        {
            found = true;
            EXPECT_GT(entry.weight, 0.5f);
        }
    }
    EXPECT_TRUE(found) << "Viseme F should have mouthPucker";
}
