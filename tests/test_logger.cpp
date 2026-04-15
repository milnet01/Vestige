// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_logger.cpp
/// @brief Unit tests for the Logger class.
#include "core/logger.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(LoggerTest, SetAndGetLevel)
{
    Logger::setLevel(LogLevel::Warning);
    EXPECT_EQ(Logger::getLevel(), LogLevel::Warning);

    Logger::setLevel(LogLevel::Trace);
    EXPECT_EQ(Logger::getLevel(), LogLevel::Trace);
}

TEST(LoggerTest, AllLevelsDoNotCrash)
{
    Logger::setLevel(LogLevel::Trace);

    EXPECT_NO_THROW(Logger::trace("test trace"));
    EXPECT_NO_THROW(Logger::debug("test debug"));
    EXPECT_NO_THROW(Logger::info("test info"));
    EXPECT_NO_THROW(Logger::warning("test warning"));
    EXPECT_NO_THROW(Logger::error("test error"));
    EXPECT_NO_THROW(Logger::fatal("test fatal"));
}

TEST(LoggerTest, LevelFilteringDoesNotCrash)
{
    // Set level high — trace/debug should be silently skipped
    Logger::setLevel(LogLevel::Error);

    EXPECT_NO_THROW(Logger::trace("should be filtered"));
    EXPECT_NO_THROW(Logger::debug("should be filtered"));
    EXPECT_NO_THROW(Logger::info("should be filtered"));
    EXPECT_NO_THROW(Logger::warning("should be filtered"));
    EXPECT_NO_THROW(Logger::error("should appear"));

    // Reset to default
    Logger::setLevel(LogLevel::Trace);
}
