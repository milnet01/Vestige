// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// MT2-S1 — JobSystem submit / wait / synchronous mode + fire-and-forget
// lifetime. GL-free, deterministic. See docs/phases/phase_10_6_design.md §5.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "core/job_system.h"

using Vestige::JobHandle;
using Vestige::JobSystem;

TEST(JobSystem, SubmitAndWaitRunsWorkOnce)
{
    JobSystem js({2});
    std::atomic<int> n{0};
    JobHandle h = js.submit([&n] { n.fetch_add(1, std::memory_order_relaxed); });
    js.wait(h);
    EXPECT_EQ(n.load(), 1);
    EXPECT_TRUE(h.isComplete());
}

TEST(JobSystem, ManyJobsAllExecute)
{
    JobSystem js({4});
    std::atomic<int> counter{0};
    std::vector<JobHandle> handles;
    for (int i = 0; i < 500; ++i)
    {
        handles.push_back(js.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
    }
    for (const JobHandle& h : handles)
    {
        js.wait(h);
    }
    EXPECT_EQ(counter.load(), 500);
}

// A discarded handle must still run — and the dtor's WaitforAllAndShutdown must
// drain every in-flight task before the JobSystem is destroyed.
TEST(JobSystem, FireAndForgetAllRunBeforeShutdown)
{
    std::atomic<int> counter{0};
    {
        JobSystem js({4});
        for (int i = 0; i < 1000; ++i)
        {
            js.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        // JobSystem destroyed here — dtor waits for all outstanding work.
    }
    EXPECT_EQ(counter.load(), 1000);
}

// numWorkers==0 → wrapper-owned synchronous mode: work runs inline AT SUBMIT,
// so a never-waited job has already executed by the time submit returns.
TEST(JobSystem, SynchronousModeRunsInlineAtSubmit)
{
    JobSystem js({0});
    EXPECT_TRUE(js.isSynchronous());
    EXPECT_EQ(js.workerCount(), 0);

    bool ran = false;
    JobHandle h = js.submit([&ran] { ran = true; });
    EXPECT_TRUE(ran) << "sync submit must run inline before returning";
    EXPECT_FALSE(h.isValid()) << "sync jobs return an empty (already-complete) handle";
    EXPECT_TRUE(h.isComplete());

    js.wait(h);   // no-op; must not hang
}

TEST(JobSystem, WorkerCountHonoursConfig)
{
    {
        JobSystem js({3});
        EXPECT_EQ(js.workerCount(), 3);
        EXPECT_FALSE(js.isSynchronous());
    }
    {
        JobSystem js({});   // auto → max(1, cores-1), never 0
        EXPECT_GE(js.workerCount(), 1);
        EXPECT_FALSE(js.isSynchronous());
    }
}

TEST(JobSystem, DefaultHandleIsCompleteAndInvalid)
{
    JobHandle h;
    EXPECT_FALSE(h.isValid());
    EXPECT_TRUE(h.isComplete());

    JobSystem js({1});
    js.wait(h);   // no-op on an empty handle; must not crash or hang
}

// parallelFor must cover [0, N) with disjoint ranges, each element exactly once.
// sum of [0, N) == N(N-1)/2 proves both coverage and no double-visit.
TEST(JobSystem, ParallelForCoversRangeExactlyOnce)
{
    JobSystem js({4});
    constexpr uint32_t kN = 10000;
    std::atomic<uint64_t> sum{0};
    std::atomic<uint32_t> visits{0};

    JobHandle h = js.parallelFor(kN, [&](uint32_t begin, uint32_t end)
    {
        uint64_t local = 0;
        for (uint32_t i = begin; i < end; ++i)
        {
            local += i;
            visits.fetch_add(1, std::memory_order_relaxed);
        }
        sum.fetch_add(local, std::memory_order_relaxed);
    });
    js.wait(h);

    EXPECT_EQ(sum.load(), static_cast<uint64_t>(kN) * (kN - 1) / 2);
    EXPECT_EQ(visits.load(), kN);
}

TEST(JobSystem, ParallelForZeroCountIsNoop)
{
    JobSystem js({2});
    bool called = false;
    JobHandle h = js.parallelFor(0, [&](uint32_t, uint32_t) { called = true; });
    js.wait(h);
    EXPECT_FALSE(called);
    EXPECT_TRUE(h.isComplete());
}

TEST(JobSystem, ParallelForSynchronousRunsOneInlineRange)
{
    JobSystem js({0});
    uint32_t seenBegin = 99;
    uint32_t seenEnd = 99;
    js.parallelFor(50, [&](uint32_t b, uint32_t e) { seenBegin = b; seenEnd = e; });
    EXPECT_EQ(seenBegin, 0u);
    EXPECT_EQ(seenEnd, 50u);   // one inline range covering everything, at submit
}

// Stress: heavy concurrent submit + parallelFor + wait across many rounds,
// hammering the shared reduction and the m_inFlight reap-on-submit. This is the
// TSAN down-payment target (design D6) — run it under -DENGINE_TSAN=ON as well
// as the default ASan build.
TEST(JobSystem, StressConcurrentSubmitAndParallelFor)
{
    JobSystem js({4});
    std::atomic<uint64_t> counter{0};
    constexpr int kRounds = 50;
    constexpr int kSubmitsPerRound = 20;
    constexpr uint32_t kRangePerRound = 1000;

    for (int round = 0; round < kRounds; ++round)
    {
        std::vector<JobHandle> handles;
        for (int i = 0; i < kSubmitsPerRound; ++i)
        {
            handles.push_back(js.submit([&] { counter.fetch_add(1, std::memory_order_relaxed); }));
        }
        handles.push_back(js.parallelFor(kRangePerRound, [&](uint32_t b, uint32_t e)
        {
            counter.fetch_add(e - b, std::memory_order_relaxed);
        }));
        for (const JobHandle& h : handles)
        {
            js.wait(h);
        }
    }

    EXPECT_EQ(counter.load(),
              static_cast<uint64_t>(kRounds) * (kSubmitsPerRound + kRangePerRound));
}
