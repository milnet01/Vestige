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

// /test-audit 2026-05-17: Logger level + ring buffer are process-global.
// Snapshot in SetUp and restore in TearDown so a mid-test failure can't
// leak the level into another test in the binary.
class LoggerTest : public ::testing::Test
{
protected:
    LogLevel m_savedLevel = LogLevel::Trace;

    void SetUp() override
    {
        m_savedLevel = Logger::getLevel();
        Logger::clearEntries();
    }

    void TearDown() override
    {
        Logger::setLevel(m_savedLevel);
        Logger::clearEntries();
    }
};

// Spawn `nThreads` threads that all wait on an atomic start barrier and then
// run `body(t)` in parallel. Joins before returning. Used by F9 concurrency
// tests to share the start-barrier scaffolding without duplicating it.
template <typename Body>
static void runConcurrent(int nThreads, Body body)
{
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(nThreads));
    std::atomic<bool> start{false};
    for (int t = 0; t < nThreads; ++t)
    {
        threads.emplace_back([t, &start, &body]() {
            // yield (instead of bare spin) so a single-vCPU CI runner doesn't
            // starve the main thread between thread creation and start.store.
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            body(t);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& th : threads)
    {
        th.join();
    }
}

TEST_F(LoggerTest, SetAndGetLevel)
{
    Logger::setLevel(LogLevel::Warning);
    EXPECT_EQ(Logger::getLevel(), LogLevel::Warning);

    Logger::setLevel(LogLevel::Trace);
    EXPECT_EQ(Logger::getLevel(), LogLevel::Trace);
}

TEST_F(LoggerTest, AllLevelsDoNotCrash)
{
    Logger::setLevel(LogLevel::Trace);

    EXPECT_NO_THROW(Logger::trace("test trace"));
    EXPECT_NO_THROW(Logger::debug("test debug"));
    EXPECT_NO_THROW(Logger::info("test info"));
    EXPECT_NO_THROW(Logger::warning("test warning"));
    EXPECT_NO_THROW(Logger::error("test error"));
    EXPECT_NO_THROW(Logger::fatal("test fatal"));
}

TEST_F(LoggerTest, LevelFilteringDoesNotCrash)
{
    Logger::setLevel(LogLevel::Error);

    EXPECT_NO_THROW(Logger::trace("should be filtered"));
    EXPECT_NO_THROW(Logger::debug("should be filtered"));
    EXPECT_NO_THROW(Logger::info("should be filtered"));
    EXPECT_NO_THROW(Logger::warning("should be filtered"));
    EXPECT_NO_THROW(Logger::error("should appear"));
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
TEST_F(LoggerTest, ConcurrentLoggingPreservesAllEntries_F9)
{
    Logger::setLevel(LogLevel::Trace);

    constexpr int NUM_THREADS = 8;
    constexpr int MSGS_PER_THREAD = 100; // total 800, well under Logger::MAX_ENTRIES
    static_assert(NUM_THREADS * MSGS_PER_THREAD < Logger::MAX_ENTRIES,
                  "below-cap test must stay below MAX_ENTRIES");

    runConcurrent(NUM_THREADS, [](int t) {
        for (int i = 0; i < MSGS_PER_THREAD; ++i)
        {
            Logger::info("t" + std::to_string(t) + "_m" + std::to_string(i));
        }
    });

    EXPECT_EQ(Logger::getEntries().size(),
              static_cast<size_t>(NUM_THREADS * MSGS_PER_THREAD));
}

TEST_F(LoggerTest, ConcurrentLoggingRespectsRingBufferCap_F9)
{
    Logger::setLevel(LogLevel::Trace);

    // Force overflow: 8 threads * 500 = 4000 messages, well over Logger::MAX_ENTRIES.
    // Ring-buffer trim (pop_front + push_back) is the hottest race path.
    constexpr int NUM_THREADS = 8;
    constexpr int MSGS_PER_THREAD = 500;
    static_assert(NUM_THREADS * MSGS_PER_THREAD > Logger::MAX_ENTRIES,
                  "overflow test must exceed MAX_ENTRIES");

    runConcurrent(NUM_THREADS, [](int t) {
        for (int i = 0; i < MSGS_PER_THREAD; ++i)
        {
            Logger::info("t" + std::to_string(t) + "_m" + std::to_string(i));
        }
    });

    EXPECT_EQ(Logger::getEntries().size(), Logger::MAX_ENTRIES);
}
