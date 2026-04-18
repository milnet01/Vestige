// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_async_driver.cpp
/// @brief Unit tests for AsyncDriverJob — W1 of the self-learning
///        roadmap. Drives small Python one-liners through the
///        run-on-worker-thread / poll-from-render-loop path.
///
/// The tests write short throwaway scripts under
/// ``std::filesystem::temp_directory_path()`` and hand their paths
/// to ``AsyncDriverJob::start``. No Anthropic API or other network
/// access is involved.

#include <gtest/gtest.h>

#include "async_driver.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace Vestige;

namespace
{

// Writes a tiny Python script to a unique temp path and returns it.
// Caller doesn't need to clean up — /tmp turnover handles it on boot
// and the scripts are a few dozen bytes each.
std::filesystem::path writeTempScript(const std::string& label,
                                      const std::string& body)
{
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path()
                    / ("vestige_async_driver_"
                       + label + "_"
                       + std::to_string(::getpid()) + ".py");
    std::ofstream out(path);
    out << body;
    out.close();
    return path;
}

// Poll the job with a short sleep until Done, bounded by a timeout
// so a stuck child doesn't hang the suite. Returns the final state.
AsyncDriverJob::State pollUntilDone(AsyncDriverJob& job,
                                    std::chrono::milliseconds timeout)
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    while (clock::now() < deadline)
    {
        const auto state = job.poll();
        if (state == AsyncDriverJob::State::Done)
            return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return job.poll();
}

} // namespace

// ---------------------------------------------------------------------------
// Happy path — stdout captured, exit code 0.
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, FinishesWithCapturedStdout)
{
    const auto script = writeTempScript("stdout",
        "import sys\nsys.stdout.write('hello-async')\nsys.exit(0)\n");

    AsyncDriverJob job;
    ASSERT_TRUE(job.start(script.string(), {}, {}));

    const auto state = pollUntilDone(job, std::chrono::seconds(5));
    ASSERT_EQ(state, AsyncDriverJob::State::Done);

    const auto result = job.takeResult();
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "hello-async");
    EXPECT_TRUE(result.error.empty());
}

// ---------------------------------------------------------------------------
// Non-zero exit — propagates to the CapturedDriverOutput.
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, NonZeroExitCodePropagates)
{
    const auto script = writeTempScript("exit42",
        "import sys\nsys.exit(42)\n");

    AsyncDriverJob job;
    ASSERT_TRUE(job.start(script.string(), {}, {}));

    const auto state = pollUntilDone(job, std::chrono::seconds(5));
    ASSERT_EQ(state, AsyncDriverJob::State::Done);

    const auto result = job.takeResult();
    EXPECT_EQ(result.exit_code, 42);
}

// ---------------------------------------------------------------------------
// Second start() while Running is rejected; UI relies on this when
// the user double-clicks the button within a frame.
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, DoubleStartRejectedWhileRunning)
{
    const auto script = writeTempScript("sleep",
        "import time\ntime.sleep(0.3)\n");

    AsyncDriverJob job;
    ASSERT_TRUE(job.start(script.string(), {}, {}));
    // Immediately — before the worker has any chance to complete —
    // the second start must be refused.
    EXPECT_FALSE(job.start(script.string(), {}, {}));
    EXPECT_TRUE(job.isRunning());

    const auto state = pollUntilDone(job, std::chrono::seconds(5));
    ASSERT_EQ(state, AsyncDriverJob::State::Done);
    (void)job.takeResult();
}

// ---------------------------------------------------------------------------
// takeResult() before poll() observed Done returns an "empty" result
// with a documented error string — protects the UI against draining
// too early (which would otherwise block in future::get).
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, TakeResultBeforeDoneReturnsError)
{
    AsyncDriverJob job;  // Idle.
    const auto result = job.takeResult();
    EXPECT_EQ(result.exit_code, -1);
    EXPECT_EQ(result.error, "no result pending");
}

// ---------------------------------------------------------------------------
// After takeResult() on a Done job, the job is Idle again and accepts
// a fresh start(). This matches the UI flow: Run → Done → user clicks
// Run again.
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, ResetsToIdleAfterTakeResult)
{
    const auto script = writeTempScript("quick",
        "import sys\nsys.stdout.write('one')\nsys.exit(0)\n");

    AsyncDriverJob job;
    ASSERT_TRUE(job.start(script.string(), {}, {}));
    ASSERT_EQ(pollUntilDone(job, std::chrono::seconds(5)),
              AsyncDriverJob::State::Done);
    const auto first = job.takeResult();
    EXPECT_EQ(first.exit_code, 0);

    // Second run reuses the same AsyncDriverJob instance.
    ASSERT_TRUE(job.start(script.string(), {}, {}));
    ASSERT_EQ(pollUntilDone(job, std::chrono::seconds(5)),
              AsyncDriverJob::State::Done);
    const auto second = job.takeResult();
    EXPECT_EQ(second.exit_code, 0);
    EXPECT_EQ(second.stdout_text, "one");
}

// ---------------------------------------------------------------------------
// Stdin payload reaches the child process (matches Suggestions panel
// flow: library JSON piped to llm_rank.py).
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, StdinPayloadDeliveredToChild)
{
    const auto script = writeTempScript("stdin",
        "import sys\nsys.stdout.write(sys.stdin.read())\nsys.exit(0)\n");

    AsyncDriverJob job;
    const std::string payload = "piped-via-stdin";
    ASSERT_TRUE(job.start(script.string(), {}, payload));

    ASSERT_EQ(pollUntilDone(job, std::chrono::seconds(5)),
              AsyncDriverJob::State::Done);
    const auto result = job.takeResult();
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, payload);
}

// ---------------------------------------------------------------------------
// elapsedSeconds() is 0 when idle and strictly > 0 once a job starts.
// Loose bound — the exact value is timing-dependent.
// ---------------------------------------------------------------------------

TEST(AsyncDriverJob, ElapsedSecondsZeroWhenIdleNonZeroWhenRunning)
{
    AsyncDriverJob job;
    EXPECT_EQ(job.elapsedSeconds(), 0.0f);

    const auto script = writeTempScript("timer",
        "import time\ntime.sleep(0.1)\n");
    ASSERT_TRUE(job.start(script.string(), {}, {}));

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_GT(job.elapsedSeconds(), 0.0f);

    ASSERT_EQ(pollUntilDone(job, std::chrono::seconds(5)),
              AsyncDriverJob::State::Done);
    (void)job.takeResult();
}
