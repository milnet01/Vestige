// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// MT2 — CPU job system (Phase 10.6). A thin wrapper over enkiTS that hides all
// enki types behind the project's own surface. Owned by Engine; the first
// consumer is AX1 geometric audio occlusion.
//
// Design of record: docs/phases/phase_10_6_design.md. Threading contract:
// submit / wait / workerCount are MAIN-THREAD-ONLY in v1 (asserted in debug).
// This slice (MT2-S1) ships submit + wait + a wrapper-owned synchronous mode;
// parallelFor (S2) and runOnMainThread + drain (S3) follow.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace enki
{
class TaskScheduler;
}

namespace Vestige
{

namespace detail
{
class JobTask;   ///< enkiTS ITaskSet wrapper; defined in the .cpp.
}

/// Opaque completion token returned by every submit. Copyable and cheap
/// (shared-ownership of the in-flight task). Safe to discard for
/// fire-and-forget — the JobSystem keeps the task alive until it completes.
class JobHandle
{
public:
    JobHandle() = default;

    /// False for a default-constructed handle and for synchronous-mode jobs
    /// (which have already run by the time submit returns).
    bool isValid() const { return m_task != nullptr; }

    /// Non-blocking completion poll. An empty handle (no pending task) is
    /// reported complete. Defined in the .cpp where JobTask is complete.
    bool isComplete() const;

private:
    friend class JobSystem;
    explicit JobHandle(std::shared_ptr<detail::JobTask> task)
        : m_task(std::move(task))
    {
    }

    std::shared_ptr<detail::JobTask> m_task;   ///< null for empty / sync jobs.
};

struct JobSystemConfig
{
    /// Worker threads to spawn.
    ///  -1 (default) → max(1, hardware_concurrency()-1); enkiTS never gets 0.
    ///   0 → wrapper-owned SYNCHRONOUS mode: no enkiTS scheduler is created,
    ///       submit() runs the job inline and returns an already-complete
    ///       handle, wait() is a no-op. Deterministic; used by tests and the
    ///       determinism-sensitive replay set.
    ///  >=1 → that many enkiTS worker threads.
    int numWorkers = -1;
};

/// Thin wrapper over enkiTS. Single owner (Engine). See the header banner and
/// docs/phases/phase_10_6_design.md for the threading contract.
class JobSystem
{
public:
    explicit JobSystem(const JobSystemConfig& config = {});
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Submit one unit of work. Returns immediately (async) unless in
    /// synchronous mode, where the job runs inline before returning.
    /// Main-thread-only.
    JobHandle submit(std::function<void()> work);

    /// Data-parallel loop: `work(begin, end)` is called on worker threads over
    /// disjoint sub-ranges that together cover `[0, count)` (each element
    /// visited exactly once; `end` is EXCLUSIVE). This is AX1's primitive.
    /// `count == 0` is a no-op returning a complete handle. Async unless in
    /// synchronous mode (then `work(0, count)` runs inline). Main-thread-only.
    JobHandle parallelFor(uint32_t count,
                          std::function<void(uint32_t begin, uint32_t end)> work);

    /// Block the calling thread until `handle` completes. While waiting the
    /// calling thread helps run other queued work (enkiTS WaitforTask). No-op
    /// on an empty/complete handle and in synchronous mode. Main-thread-only.
    void wait(const JobHandle& handle);

    /// Enqueue `work` to run on the MAIN thread. This is the ONLY verb that is
    /// safe to call from a worker thread — its point is to marshal a result back
    /// to the main thread. The work runs when drainMainThreadQueue() is next
    /// called (frame top). No-op if `work` is empty.
    void runOnMainThread(std::function<void()> work);

    /// Run all queued runOnMainThread work in FIFO order, then reap completed
    /// fire-and-forget tasks. MAIN-THREAD-ONLY (asserted). The Engine calls this
    /// once at the top of each frame. Work re-posted by a drained callback lands
    /// in the next drain (no reentrant deadlock).
    void drainMainThreadQueue();

    /// Actual worker-thread count (0 in synchronous mode).
    int workerCount() const { return m_workerCount; }

    /// True when running wrapper-owned synchronous mode (no enkiTS scheduler).
    bool isSynchronous() const { return m_scheduler == nullptr; }

private:
    /// Debug-only: trips if called off the thread that constructed the
    /// JobSystem (the true main thread, per the Engine ctor). No-op in release.
    /// The engine-wide VESTIGE_ASSERT_MAIN_THREAD sweep is MT14.
    void assertMainThread() const;

    /// Drop already-complete tasks from m_inFlight (main-thread-only). Bounds
    /// the fire-and-forget registry before S3's drainMainThreadQueue exists.
    void reapCompleted();

    /// Async task creation shared by submit + parallelFor: register in
    /// m_inFlight (keeps a dropped handle alive), pipe to enkiTS, return the
    /// handle. Caller has already handled synchronous mode. Main-thread-only.
    JobHandle enqueue(uint32_t setSize,
                      std::function<void(uint32_t, uint32_t)> work);

    std::unique_ptr<enki::TaskScheduler> m_scheduler;   ///< null in sync mode.
    int m_workerCount = 0;
    std::thread::id m_mainThreadId;

    /// Keeps fire-and-forget tasks alive until they complete (a dropped
    /// JobHandle must still run). Mutated only on the main thread in v1, so the
    /// mutex only guards against a future relaxation of that rule.
    std::mutex m_inFlightMutex;
    std::vector<std::shared_ptr<detail::JobTask>> m_inFlight;

    /// runOnMainThread queue: pushed from any thread, drained on the main
    /// thread. The one piece of genuinely cross-thread mutable state, guarded by
    /// its own mutex (never held while a queued callback runs).
    std::mutex m_mainQueueMutex;
    std::vector<std::function<void()>> m_mainQueue;
};

}   // namespace Vestige
