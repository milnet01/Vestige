// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file async_driver.cpp
/// @brief Implementation of the W1 / W2 async-worker pattern. See
///        the header for the full design rationale.

#include "async_driver.h"

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace Vestige
{

AsyncDriverJob::~AsyncDriverJob()
{
    // Safety net: if the caller dropped the job while a worker was
    // in-flight, make sure we don't leave a zombie thread behind.
    // The worker will naturally exit when its child process is
    // reaped (by its own waitpid call), so we just join.
    if (m_state.load(std::memory_order_acquire) == State::Running)
    {
        // Send SIGKILL to speed the child's exit — otherwise we'd
        // block here for however long the driver takes (PySR can be
        // minutes). Accept the data loss; the caller threw away the
        // job, so the result wasn't being consumed anyway.
        const int pid = m_childPid.load(std::memory_order_acquire);
        if (pid > 0)
            ::kill(static_cast<pid_t>(pid), SIGKILL);
    }
    joinWorker();
}

bool AsyncDriverJob::start(const std::string& script,
                           const std::vector<std::string>& argv,
                           std::string stdinContents)
{
    // Reject overlapping starts. Caller must drain ``Done`` via
    // ``takeResult()`` before the next run — the explicit handoff
    // prevents clobbering a result the UI hasn't rendered yet.
    const State cur = m_state.load(std::memory_order_acquire);
    if (cur != State::Idle)
        return false;

    // Paranoia: a previous Done→Idle transition via takeResult()
    // already joined the worker, but guard in case a subclass or
    // future edit leaves a stale thread handle.
    joinWorker();

    const bool wantStdin = !stdinContents.empty();
    DriverProcess proc = spawnDriverProcess(script, argv, wantStdin);
    if (proc.pid < 0)
    {
        // Spawn failure: produce a Done state with the error so
        // renderers can surface it the same way they'd surface a
        // non-zero exit. No worker thread gets launched.
        m_result = {};
        m_result.error = proc.error;
        m_startedAt = std::chrono::steady_clock::now();
        m_state.store(State::Done, std::memory_order_release);
        return true;
    }

    m_childPid.store(proc.pid, std::memory_order_release);
    m_cancelRequested = false;
    m_sigkillSent     = false;
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_pendingStdout.clear();
    }
    m_result = {};
    m_startedAt = std::chrono::steady_clock::now();
    m_state.store(State::Running, std::memory_order_release);

    m_worker = std::thread(&AsyncDriverJob::workerMain,
                           this, std::move(proc),
                           std::move(stdinContents));
    return true;
}

void AsyncDriverJob::workerMain(DriverProcess proc, std::string stdinContents)
{
    // Pump stdin first so the child isn't blocked on read() while
    // we're trying to drain its stdout. Mirrors the synchronous
    // runDriverCaptured path — the child sees exactly the same byte
    // stream either way.
    if (proc.stdin_fd >= 0)
    {
        ssize_t total = 0;
        const char* buf = stdinContents.data();
        const ssize_t need = static_cast<ssize_t>(stdinContents.size());
        while (total < need)
        {
            ssize_t n = ::write(proc.stdin_fd, buf + total, need - total);
            if (n <= 0) break;
            total += n;
        }
        ::close(proc.stdin_fd);
    }

    // Drain stdout to EOF. Each chunk is appended to both the
    // streaming buffer (drainStdoutChunk consumers) and the final
    // stdout_text (takeResult consumers). The two views are
    // deliberately non-overlapping: what the main thread drains via
    // drainStdoutChunk is NOT later re-delivered via takeResult.
    char buf[4096];
    while (true)
    {
        ssize_t n = ::read(proc.stdout_fd, buf, sizeof(buf));
        if (n <= 0) break;
        {
            std::lock_guard<std::mutex> lock(m_streamMutex);
            m_pendingStdout.append(buf, buf + n);
        }
        m_result.stdout_text.append(buf, buf + n);
    }
    ::close(proc.stdout_fd);

    int status = 0;
    ::waitpid(static_cast<pid_t>(proc.pid), &status, 0);
    if (WIFEXITED(status))
    {
        m_result.exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        const int sig = WTERMSIG(status);
        m_result.exit_code = -1;
        // Distinguish user-cancel (SIGTERM/SIGKILL we sent) from a
        // driver crash. Either way the string is human-readable for
        // the UI; the exit_code stays -1 so programmatic callers
        // know it didn't exit cleanly.
        if (sig == SIGTERM || sig == SIGKILL)
            m_result.error = "cancelled by user";
        else
            m_result.error = "terminated by signal " + std::to_string(sig);
    }
    else
    {
        m_result.exit_code = -1;
        m_result.error = "child exited abnormally";
    }

    m_childPid.store(-1, std::memory_order_release);
    m_state.store(State::Done, std::memory_order_release);
}

AsyncDriverJob::State AsyncDriverJob::poll()
{
    const State cur = m_state.load(std::memory_order_acquire);

    // Cancellation escalator: if we sent SIGTERM and the child is
    // still running past the grace window, send SIGKILL once.
    // Checked from poll() (rather than a dedicated watchdog thread)
    // so the escalation cadence naturally matches the caller's
    // render loop — no extra scheduling, no sleeping threads.
    if (cur == State::Running && m_cancelRequested && !m_sigkillSent)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto dt  = now - m_cancelRequestedAt;
        const auto secs = std::chrono::duration_cast<
            std::chrono::duration<float>>(dt).count();
        if (secs >= CANCEL_SIGKILL_GRACE_SECONDS)
        {
            const int pid = m_childPid.load(std::memory_order_acquire);
            if (pid > 0)
                ::kill(static_cast<pid_t>(pid), SIGKILL);
            m_sigkillSent = true;
        }
    }

    return cur;
}

std::string AsyncDriverJob::drainStdoutChunk()
{
    std::string chunk;
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        chunk.swap(m_pendingStdout);
    }
    return chunk;
}

bool AsyncDriverJob::cancel()
{
    if (m_state.load(std::memory_order_acquire) != State::Running)
        return false;

    const int pid = m_childPid.load(std::memory_order_acquire);
    if (pid <= 0)
        return false;

    if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0)
        return false;

    // Record the cancellation time so poll() can escalate. Written
    // only from the main thread (cancel / poll / takeResult), so no
    // atomicity needed — the worker thread doesn't read this.
    if (!m_cancelRequested)
    {
        m_cancelRequested   = true;
        m_cancelRequestedAt = std::chrono::steady_clock::now();
    }
    return true;
}

CapturedDriverOutput AsyncDriverJob::takeResult()
{
    // Refuse to touch the result unless we've observed Done — the
    // worker thread may still be mid-write otherwise. Matches the
    // "poll then take" contract documented in the header.
    if (m_state.load(std::memory_order_acquire) != State::Done)
    {
        CapturedDriverOutput empty;
        empty.error = "no result pending";
        return empty;
    }

    // Worker has already stored m_result and transitioned to Done;
    // joining just releases the thread handle.
    joinWorker();

    CapturedDriverOutput result = std::move(m_result);
    m_result = {};
    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_pendingStdout.clear();
    }
    m_cancelRequested = false;
    m_sigkillSent     = false;
    m_state.store(State::Idle, std::memory_order_release);
    return result;
}

float AsyncDriverJob::elapsedSeconds() const noexcept
{
    if (m_state.load(std::memory_order_acquire) == State::Idle)
        return 0.0f;

    const auto now  = std::chrono::steady_clock::now();
    const auto dt   = now - m_startedAt;
    const auto secs = std::chrono::duration_cast<
        std::chrono::duration<float>>(dt);
    return secs.count();
}

void AsyncDriverJob::joinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}

} // namespace Vestige
