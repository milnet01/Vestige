// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_stills_file.cpp
/// @brief Unit tests for the --stills camera-spot parser.
#include <gtest/gtest.h>

#include "testing/stills_file.h"

using namespace Vestige;

TEST(StillsFileTest, ReadsSpotsAndSkipsCommentsAndBlankLines)
{
    const auto ok = parseStills("# rocks by the pond\n"
                                "\n"
                                "rock_a  10 2.5 -4  -90 -15\n"
                                "\tlog-1\t3\t~1.7\t-8\t45\t-30\n");
    ASSERT_TRUE(ok.error.empty()) << ok.error;
    ASSERT_EQ(ok.spots.size(), 2u);

    EXPECT_EQ(ok.spots[0].name, "rock_a");
    EXPECT_FALSE(ok.spots[0].aboveGround);
    EXPECT_FLOAT_EQ(ok.spots[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(ok.spots[0].position.y, 2.5f);
    EXPECT_FLOAT_EQ(ok.spots[0].position.z, -4.0f);
    EXPECT_FLOAT_EQ(ok.spots[0].yaw, -90.0f);
    EXPECT_FLOAT_EQ(ok.spots[0].pitch, -15.0f);

    EXPECT_EQ(ok.spots[1].name, "log-1");
    EXPECT_TRUE(ok.spots[1].aboveGround);
    EXPECT_FLOAT_EQ(ok.spots[1].position.y, 1.7f);
    EXPECT_FLOAT_EQ(ok.spots[1].pitch, -30.0f);
}

TEST(StillsFileTest, WrongFieldCountNamesTheLine)
{
    const auto r = parseStills("a 1 2 3 4 5\nb 1 2 3 4\n");
    EXPECT_NE(r.error.find("line 2"), std::string::npos) << r.error;
}

TEST(StillsFileTest, NonNumbersAreRejected)
{
    for (const char* text : {"a 1 2 3 4 x\n", "a 1 2 3 4 5x\n", "a 1 ~ 3 4 5\n",
                             "a 1 nan 3 4 5\n", "a 1 inf 3 4 5\n"})
    {
        const auto r = parseStills(text);
        EXPECT_FALSE(r.error.empty()) << text;
        EXPECT_TRUE(r.spots.empty()) << text;
    }
}

TEST(StillsFileTest, NameMustBeSafeForAFilename)
{
    EXPECT_FALSE(parseStills("../up 1 2 3 4 5\n").error.empty());
    EXPECT_FALSE(parseStills("a/b 1 2 3 4 5\n").error.empty());
    EXPECT_TRUE(parseStills("Rock_2-b 1 2 3 4 5\n").error.empty());
}

TEST(StillsFileTest, NoSpotsIsAnError)
{
    EXPECT_FALSE(parseStills("").error.empty());
    EXPECT_FALSE(parseStills("# only a comment\n\n").error.empty());
}

TEST(StillsFileTest, MissingFileIsAnError)
{
    const auto r = loadStillsFile("no/such/stills_file.txt");
    EXPECT_NE(r.error.find("cannot open"), std::string::npos) << r.error;
}
