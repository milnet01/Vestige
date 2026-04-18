// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file async_driver.cpp
/// @brief Implementation of the W1 async-worker pattern. See the
///        header for the full design rationale and scope boundaries.

#include "async_driver.h"

namespace Vestige
{

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

    m_state.store(State::Running, std::memory_order_release);
    m_startedAt = std::chrono::steady_clock::now();

    // std::async with launch::async guarantees a genuine worker
    // thread (not a deferred lazy run on get()), which is what the
    // render-loop polling model requires. The lambda copies its
    // inputs — ``script`` / ``argv`` / ``stdinContents`` are all
    // self-contained, no dangling references back to the caller.
    m_future = std::async(std::launch::async,
        [script, argv, stdin_text = std::move(stdinContents)]()
        {
            return runDriverCaptured(script, argv, stdin_text);
        });

    return true;
}

AsyncDriverJob::State AsyncDriverJob::poll()
{
    State cur = m_state.load(std::memory_order_acquire);
    if (cur != State::Running)
        return cur;

    // ``wait_for(0)`` returns ``ready`` once the worker has stored
    // its result; until then the call is genuinely non-blocking.
    if (m_future.valid()
        && m_future.wait_for(std::chrono::seconds(0))
               == std::future_status::ready)
    {
        m_state.store(State::Done, std::memory_order_release);
        return State::Done;
    }
    return State::Running;
}

CapturedDriverOutput AsyncDriverJob::takeResult()
{
    // Refuse to touch the future unless we've observed Done —
    // ``future::get()`` would block otherwise, defeating the point
    // of the async pattern. Matches the "poll then take" contract
    // documented in the header.
    if (m_state.load(std::memory_order_acquire) != State::Done
        || !m_future.valid())
    {
        CapturedDriverOutput empty;
        empty.error = "no result pending";
        return empty;
    }

    CapturedDriverOutput result = m_future.get();
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

} // namespace Vestige
