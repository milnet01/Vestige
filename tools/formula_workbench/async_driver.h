// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file async_driver.h
/// @brief W1 — run a Python driver on a worker thread and poll from
///        the render loop without blocking the GUI.
///
/// Shape:
///   1. Caller builds the same ``script + argv + stdinContents`` triple
///      it would pass to ``runDriverCaptured``.
///   2. ``AsyncDriverJob::start(...)`` spawns a worker; the call
///      returns immediately.
///   3. Each frame the caller invokes ``poll()``, which reports Idle /
///      Running / Done without blocking.
///   4. Once ``Done``, the caller invokes ``takeResult()`` once to
///      retrieve the captured output, which also resets the job to
///      ``Idle`` so a fresh ``start()`` can be issued.
///
/// Scope boundaries (intentional, W1-only):
///   - **No cancellation.** The only consumer today (``Suggestions``
///     panel) finishes in 1-2 s. PySR (W2) needs process-level cancel
///     via SIGTERM on the child PID, which requires exposing the PID
///     out of ``runDriverCaptured``. That change belongs in W2
///     alongside the Cancel-button UX.
///   - **No incremental stdout.** ``runDriverCaptured`` buffers the
///     full stdout then returns. Streaming output (useful for PySR's
///     minutes-long runs) is a W2 concern.
#pragma once

#include "benchmark.h"  // CapturedDriverOutput

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Wraps a single async invocation of ``runDriverCaptured``.
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
    ~AsyncDriverJob() = default;

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
    /// the first time the future becomes ready. Safe to call when
    /// ``Idle`` — returns ``Idle``.
    State poll();

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

private:
    std::atomic<State> m_state{State::Idle};
    std::future<CapturedDriverOutput> m_future;
    std::chrono::steady_clock::time_point m_startedAt{};
};

} // namespace Vestige
