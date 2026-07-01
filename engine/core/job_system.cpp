// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "core/job_system.h"

#include <algorithm>
#include <cassert>

#include "TaskScheduler.h"   // enkiTS (SYSTEM include; marked in engine/CMakeLists.txt)

namespace Vestige
{

namespace detail
{

/// One submitted job. enkiTS partitions [0, m_SetSize) across worker threads and
/// calls ExecuteRange once per partition with a half-open [start, end) range. A
/// single submit() sets m_SetSize=1 (one call); parallelFor sets it to `count`.
/// The shared_ptr owning this outlives execution: held by the JobHandle and by
/// JobSystem::m_inFlight until it completes.
class JobTask final : public enki::ITaskSet
{
public:
    JobTask(uint32_t setSize, std::function<void(uint32_t, uint32_t)> fn)
        : m_fn(std::move(fn))
    {
        m_SetSize = setSize;
    }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t /*threadnum*/) override
    {
        if (m_fn)
        {
            m_fn(range.start, range.end);
        }
    }

private:
    std::function<void(uint32_t, uint32_t)> m_fn;
};

}   // namespace detail

bool JobHandle::isComplete() const
{
    // An empty handle has nothing pending → complete. Otherwise defer to
    // enkiTS's own lock-free acquire-load on the task's running count.
    return m_task == nullptr || m_task->GetIsComplete();
}

JobSystem::JobSystem(const JobSystemConfig& config)
    : m_mainThreadId(std::this_thread::get_id())
{
    int resolved = config.numWorkers;
    if (resolved < 0)
    {
        // Auto: leave one core for the OS + OpenAL's mixer thread. Clamp to >=1
        // so enkiTS never receives 0 (it rejects a zero-thread init).
        const int hw = static_cast<int>(std::thread::hardware_concurrency());
        resolved = std::max(1, hw - 1);
    }

    m_workerCount = resolved;

    if (resolved > 0)
    {
        enki::TaskSchedulerConfig cfg;
        cfg.numTaskThreadsToCreate = static_cast<uint32_t>(resolved);
        m_scheduler = std::make_unique<enki::TaskScheduler>();
        m_scheduler->Initialize(cfg);
    }
    // resolved == 0 → wrapper-owned synchronous mode: m_scheduler stays null.
}

JobSystem::~JobSystem()
{
    // Retire every in-flight task before members are destroyed, so no worker is
    // still touching a JobTask when m_inFlight frees it. In synchronous mode
    // there is no scheduler and nothing was ever in flight.
    if (m_scheduler)
    {
        m_scheduler->WaitforAllAndShutdown();
    }
}

JobHandle JobSystem::submit(std::function<void()> work)
{
    assertMainThread();

    if (isSynchronous())
    {
        // Run inline at submit; return an empty (already-complete) handle.
        if (work)
        {
            work();
        }
        return JobHandle();
    }

    // A single job is a size-1 parallel range whose callback ignores the range.
    return enqueue(1u, [w = std::move(work)](uint32_t, uint32_t)
                   {
                       if (w)
                       {
                           w();
                       }
                   });
}

JobHandle JobSystem::parallelFor(uint32_t count,
                                 std::function<void(uint32_t, uint32_t)> work)
{
    assertMainThread();

    if (count == 0u)
    {
        return JobHandle();   // nothing to iterate → already complete.
    }

    if (isSynchronous())
    {
        if (work)
        {
            work(0u, count);   // one inline range covering everything.
        }
        return JobHandle();
    }

    return enqueue(count, std::move(work));
}

JobHandle JobSystem::enqueue(uint32_t setSize,
                             std::function<void(uint32_t, uint32_t)> work)
{
    reapCompleted();   // bound m_inFlight until S3's frame-top drain exists.

    auto task = std::make_shared<detail::JobTask>(setSize, std::move(work));
    {
        std::lock_guard<std::mutex> lock(m_inFlightMutex);
        m_inFlight.push_back(task);
    }
    m_scheduler->AddTaskSetToPipe(task.get());
    return JobHandle(task);
}

void JobSystem::wait(const JobHandle& handle)
{
    assertMainThread();

    // Empty handle (or synchronous mode) → nothing to wait on. The handle keeps
    // its task alive for the duration of the wait, so WaitforTask is safe even
    // if m_inFlight already reaped it.
    if (!handle.m_task || !m_scheduler)
    {
        return;
    }
    m_scheduler->WaitforTask(handle.m_task.get());
}

void JobSystem::runOnMainThread(std::function<void()> work)
{
    // Deliberately NOT assertMainThread() — this is the one worker-callable verb.
    if (!work)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mainQueueMutex);
    m_mainQueue.push_back(std::move(work));
}

void JobSystem::drainMainThreadQueue()
{
    assertMainThread();

    // Swap the queue out under the lock, then run the callbacks WITHOUT holding
    // it — a callback may post more main-thread work (that lands in the next
    // drain) and must not deadlock on the queue mutex.
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(m_mainQueueMutex);
        local.swap(m_mainQueue);
    }
    for (std::function<void()>& fn : local)   // FIFO — insertion order preserved.
    {
        if (fn)
        {
            fn();
        }
    }

    reapCompleted();   // frame-top reclamation of completed fire-and-forget tasks.
}

void JobSystem::reapCompleted()
{
    std::lock_guard<std::mutex> lock(m_inFlightMutex);
    m_inFlight.erase(
        std::remove_if(m_inFlight.begin(), m_inFlight.end(),
                       [](const std::shared_ptr<detail::JobTask>& t)
                       {
                           return t->GetIsComplete();
                       }),
        m_inFlight.end());
}

void JobSystem::assertMainThread() const
{
#ifndef NDEBUG
    assert(std::this_thread::get_id() == m_mainThreadId &&
           "JobSystem method called off the main thread (v1 is main-thread-only)");
#else
    (void)m_mainThreadId;
#endif
}

}   // namespace Vestige
