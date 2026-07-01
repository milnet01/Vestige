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

/// One submitted job. enkiTS runs it via ExecuteRange on a worker thread (or,
/// for a size-1 job, once). The shared_ptr owning this outlives execution:
/// held by the JobHandle and by JobSystem::m_inFlight until it completes.
class JobTask final : public enki::ITaskSet
{
public:
    explicit JobTask(std::function<void()> fn)
        : m_fn(std::move(fn))
    {
        m_SetSize = 1;
    }

    void ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/) override
    {
        if (m_fn)
        {
            m_fn();
        }
    }

private:
    std::function<void()> m_fn;
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

    reapCompleted();   // bound m_inFlight until S3's frame-top drain exists.

    auto task = std::make_shared<detail::JobTask>(std::move(work));
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
