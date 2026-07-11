// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cli_args.cpp
/// @brief Unit tests for the Vestige CLI parser (app/cli_args.cpp) — verifies
///        the flag→EngineConfig mapping headlessly (no engine bring-up), with
///        focus on the meadow-scene flags --material-demo and --profile-log
///        (phase_10_meadow_benchmark_scene_design.md §11).

#include "../app/cli_args.h"
#include "core/engine.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

// Runs parseArgs over a fake argv (argv[0] is a program name), returning the
// resulting config + success flag + exit code so tests can assert on them.
struct ParseResult
{
    Vestige::EngineConfig config;
    bool                  ok = false;
    int                   exitCode = -1;
};

ParseResult run(std::vector<std::string> args)
{
    std::vector<std::string> owned;
    owned.reserve(args.size() + 1);
    owned.emplace_back("vestige");  // argv[0]
    for (auto& a : args) owned.push_back(a);

    std::vector<char*> argv;
    argv.reserve(owned.size());
    for (auto& s : owned) argv.push_back(s.data());

    ParseResult r;
    r.exitCode = 0;
    r.ok = Vestige::parseArgs(static_cast<int>(argv.size()), argv.data(),
                              r.config, r.exitCode);
    return r;
}

} // namespace

// --- Defaults ---------------------------------------------------------------

TEST(CliArgs, NoArgsLeavesSceneFlagsOff)
{
    auto r = run({});
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.config.materialDemo);
    EXPECT_FALSE(r.config.biblicalDemo);
    EXPECT_TRUE(r.config.profileLogPath.empty());
    EXPECT_FALSE(r.config.startInPlayMode);
}

// --- --material-demo --------------------------------------------------------

TEST(CliArgs, MaterialDemoSetsFlag)
{
    auto r = run({"--material-demo"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.config.materialDemo);
    EXPECT_FALSE(r.config.biblicalDemo);
}

TEST(CliArgs, MaterialAndBiblicalAreIndependentFlags)
{
    // Both may be set on the command line; precedence (biblical wins) is
    // resolved in Engine::initialize(), not in the parser.
    auto r = run({"--material-demo", "--biblical-demo"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.config.materialDemo);
    EXPECT_TRUE(r.config.biblicalDemo);
}

// --- --profile-log ----------------------------------------------------------

TEST(CliArgs, ProfileLogBareUsesNonEmptySentinel)
{
    auto r = run({"--profile-log"});
    EXPECT_TRUE(r.ok);
    // Bare form yields a non-empty sentinel (ProfileLog resolves it to a
    // timestamped default at open time); logging is therefore "on".
    EXPECT_FALSE(r.config.profileLogPath.empty());
}

TEST(CliArgs, ProfileLogWithPathTakesThePath)
{
    auto r = run({"--profile-log=/tmp/run42.csv"});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.config.profileLogPath, "/tmp/run42.csv");
}

TEST(CliArgs, ProfileLogWithEmptyPathIsAnError)
{
    auto r = run({"--profile-log="});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, 2);
}

// --- Existing flags still parse (regression) --------------------------------

TEST(CliArgs, PlayFlagSetsPlayMode)
{
    auto r = run({"--play"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.config.startInPlayMode);
}

TEST(CliArgs, SceneFlagTakesPathValue)
{
    auto r = run({"--scene", "my.scene"});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.config.startupScene, "my.scene");
}

TEST(CliArgs, UnknownFlagFailsWithExit2)
{
    auto r = run({"--nope"});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, 2);
}

TEST(CliArgs, HelpReturnsFalseWithExit0)
{
    auto r = run({"--help"});
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, 0);
}
