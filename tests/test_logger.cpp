// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_logger.cpp
/// @brief Unit tests for the Logger class.
#include "core/logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

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

// F9: Logger thread-safety (Phase 10.9 Slice 1)
//
// AsyncTextureLoader already logs from a worker thread, so Logger::log() runs
// concurrently with itself today. Without synchronisation around s_entries
// (a std::deque) and s_logFile (an ofstream), concurrent push_back/pop_front
// and interleaved stream writes are data races / UB: lost entries, torn log
// lines, or outright crashes.
//
// Contract pinned by these tests: N threads logging M messages each produce
// exactly N*M ring-buffer entries (when total < MAX_ENTRIES) and the ring
// buffer keeps exactly MAX_ENTRIES when the total overflows. Any lock-free
// deque would fail these counts; the fix is a std::mutex around the mutable
// state inside Logger::log().
TEST(LoggerTest, ConcurrentLoggingPreservesAllEntries_F9)
{
    Logger::clearEntries();
    Logger::setLevel(LogLevel::Trace);

    constexpr int NUM_THREADS = 8;
    constexpr int MSGS_PER_THREAD = 100; // total 800, well under MAX_ENTRIES (1000)

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    std::atomic<bool> start{false};
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back([t, &start]() {
            while (!start.load(std::memory_order_acquire))
            {
                // Spin until all threads are ready, to maximise overlap.
            }
            for (int i = 0; i < MSGS_PER_THREAD; ++i)
            {
                Logger::info("t" + std::to_string(t) + "_m" + std::to_string(i));
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& th : threads)
    {
        th.join();
    }

    EXPECT_EQ(Logger::getEntries().size(),
              static_cast<size_t>(NUM_THREADS * MSGS_PER_THREAD));
}

TEST(LoggerTest, ConcurrentLoggingRespectsRingBufferCap_F9)
{
    Logger::clearEntries();
    Logger::setLevel(LogLevel::Trace);

    // Force overflow: 8 threads * 500 = 4000 messages, MAX_ENTRIES = 1000.
    // Ring-buffer trim (pop_front + push_back) is the hottest race path.
    constexpr int NUM_THREADS = 8;
    constexpr int MSGS_PER_THREAD = 500;
    constexpr size_t MAX_ENTRIES = 1000;

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    std::atomic<bool> start{false};
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back([t, &start]() {
            while (!start.load(std::memory_order_acquire))
            {
            }
            for (int i = 0; i < MSGS_PER_THREAD; ++i)
            {
                Logger::info("t" + std::to_string(t) + "_m" + std::to_string(i));
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& th : threads)
    {
        th.join();
    }

    EXPECT_EQ(Logger::getEntries().size(), MAX_ENTRIES);
}
