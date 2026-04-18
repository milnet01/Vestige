// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file async_driver.h
/// @brief W1 / W2 — run a Python driver on a worker thread and poll
///        from the render loop without blocking the GUI.
///
/// Shape:
///   1. Caller builds the same ``script + argv + stdinContents`` triple
///      it would pass to ``runDriverCaptured``.
///   2. ``AsyncDriverJob::start(...)`` spawns a worker; the call
///      returns immediately.
///   3. Each frame the caller invokes ``poll()``, which reports Idle /
///      Running / Done without blocking.
///   4. For long-running drivers (W2 PySR), the caller can call
///      ``drainStdoutChunk()`` each frame to pull new output as the
///      child prints it, and ``cancel()`` to send SIGTERM to the
///      child process (auto-escalates to SIGKILL after a grace
///      period if the child ignores the polite signal).
///   5. Once ``Done``, the caller invokes ``takeResult()`` once to
///      retrieve the captured output (including any chunks not yet
///      drained), which also resets the job to ``Idle`` so a fresh
///      ``start()`` can be issued.
///
/// Scope boundaries:
///   - One job per instance. ``start()`` rejects overlapping runs.
#pragma once

#include "benchmark.h"  // CapturedDriverOutput, DriverProcess

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Vestige
{

/// @brief Wraps a single async invocation of a Python driver.
///
/// One job at a time. ``start()`` rejects new work while a previous
/// run is still in-flight, which matches the UI (the button is
/// disabled while running) and avoids having to reason about
/// concurrent ownership of the result strings.
class AsyncDriverJob
{
public:
    enum class State
    {
        Idle,      ///< No run in flight; ``start()`` accepted.
        Running,   ///< Worker thread active; ``poll()`` reports Running.
        Done       ///< Worker finished; ``takeResult()`` ready.
    };

    AsyncDriverJob() = default;
    ~AsyncDriverJob();

    AsyncDriverJob(const AsyncDriverJob&) = delete;
    AsyncDriverJob& operator=(const AsyncDriverJob&) = delete;

    /// @brief Launch the driver on a worker thread.
    ///
    /// @param script Absolute or relative path to the Python script.
    ///               Must already be resolved by the caller —
    ///               ``AsyncDriverJob`` does not locate scripts.
    /// @param argv   Arguments appended after ``script`` on the
    ///               python3 command line.
    /// @param stdinContents Optional stdin payload piped into the
    ///                      child process.
    /// @return ``true`` if the worker was launched; ``false`` if a
    ///         previous run is still ``Running`` (caller should poll
    ///         first) or in the ``Done`` state pending ``takeResult``.
    bool start(const std::string& script,
               const std::vector<std::string>& argv,
               std::string stdinContents = {});

    /// @brief Non-blocking state check. Call every frame.
    ///
    /// Transitions the internal state from ``Running`` → ``Done``
    /// the first time the worker signals completion. Safe to call
    /// when ``Idle`` — returns ``Idle``.
    ///
    /// Also drives the cancellation escalator: if ``cancel()`` was
    /// called more than ``CANCEL_SIGKILL_GRACE_SECONDS`` ago and the
    /// worker is still Running, sends SIGKILL once. PySR's embedded
    /// Julia runtime has been observed to ignore SIGTERM mid-sweep,
    /// so the escalation is load-bearing for the "Cancel" button UX.
    State poll();

    /// @brief Pull any stdout bytes that have arrived since the last
    ///        call, without waiting for the child to finish. Safe to
    ///        call from the main thread every frame.
    ///
    /// Returns an empty string when the worker hasn't produced new
    /// output yet (common for short idle frames during a PySR run).
    /// Bytes drained by this call are NOT included in the stdout_text
    /// of ``takeResult()``, so callers who mix streaming with the
    /// final result must concatenate themselves.
    std::string drainStdoutChunk();

    /// @brief Send SIGTERM to the child process. Non-blocking.
    ///
    /// Returns ``false`` if the job isn't Running or the PID is no
    /// longer valid; ``true`` if the signal was delivered. ``poll()``
    /// will auto-escalate to SIGKILL if the child hasn't exited
    /// within the documented grace window.
    ///
    /// After cancel(), the job still transitions through Done (the
    /// worker thread needs to reap the child) and ``takeResult()``
    /// must still be called to recycle the instance. The result's
    /// ``error`` field is populated with a "cancelled by user"
    /// marker for UI display.
    bool cancel();

    /// @brief Retrieve the captured output. Only valid once ``poll()``
    ///        has returned ``Done``; calling outside that window
    ///        returns an empty result with ``exit_code = -1`` and an
    ///        ``error`` of "no result pending".
    ///
    /// Resets the job to ``Idle`` on success, so a fresh ``start()``
    /// can follow immediately.
    CapturedDriverOutput takeResult();

    /// @brief Convenience check equivalent to ``poll() == Running``
    ///        but without the transition side-effect. Useful for
    ///        disabling a UI button without churning state.
    bool isRunning() const noexcept
    {
        return m_state.load(std::memory_order_acquire) == State::Running;
    }

    /// @brief Seconds since ``start()`` was called for the currently
    ///        in-flight or just-finished run. Returns 0.0f when idle.
    ///        Used by the UI for a "running 2.3s…" label.
    float elapsedSeconds() const noexcept;

    /// @brief Grace window (seconds) between SIGTERM and SIGKILL.
    ///        Exposed as a constant so tests can reason about it.
    static constexpr float CANCEL_SIGKILL_GRACE_SECONDS = 3.0f;

private:
    std::atomic<State> m_state{State::Idle};

    // PID of the currently running child process, or -1 when idle.
    // Stored atomically so cancel() can read it from the main
    // thread while the worker thread is blocked on read().
    std::atomic<int> m_childPid{-1};

    // Guarded by m_streamMutex — chunks read by the worker thread
    // but not yet drained by the main thread. drainStdoutChunk
    // swaps this out under the lock to keep the critical section
    // bounded to a pointer swap.
    std::mutex  m_streamMutex;
    std::string m_pendingStdout;

    // Result captured once the worker finishes (stdout_text is the
    // full concatenation; the main thread may have already drained
    // part of it via drainStdoutChunk). Only valid after Done.
    CapturedDriverOutput m_result;

    // Worker thread. Joined in takeResult() (success) or dtor
    // (safety net when the caller drops the job mid-run).
    std::thread m_worker;

    // Cancellation bookkeeping. m_cancelRequestedAt is written only
    // by the main thread (cancel / poll); the worker thread doesn't
    // touch it. m_sigkillSent guards against repeated SIGKILLs on
    // successive poll() frames.
    std::chrono::steady_clock::time_point m_cancelRequestedAt{};
    bool m_cancelRequested = false;
    bool m_sigkillSent     = false;

    std::chrono::steady_clock::time_point m_startedAt{};

    // Worker body. Pumps stdin into the child, reads stdout into
    // m_pendingStdout + m_result.stdout_text, waitpids, and stores
    // m_result + flips m_state to Done.
    void workerMain(DriverProcess proc, std::string stdinContents);

    // Join the worker thread if joinable. Safe to call multiple times.
    void joinWorker();
};

} // namespace Vestige
