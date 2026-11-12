// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tween.cpp
/// @brief Tween and TweenManager implementation.
#include "animation/tween.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Tween factory methods
// ---------------------------------------------------------------------------

Tween Tween::floatTween(float* target, float from, float to,
                        float duration, EaseType ease)
{
    Tween t;
    t.m_targetType = TargetType::FLOAT;
    t.m_target = target;
    t.m_from[0] = from;
    t.m_to[0] = to;
    t.m_duration = duration;
    t.m_easeType = ease;
    return t;
}

Tween Tween::vec3Tween(glm::vec3* target, const glm::vec3& from, const glm::vec3& to,
                       float duration, EaseType ease)
{
    Tween t;
    t.m_targetType = TargetType::VEC3;
    t.m_target = target;
    t.m_from[0] = from.x; t.m_from[1] = from.y; t.m_from[2] = from.z;
    t.m_to[0] = to.x;     t.m_to[1] = to.y;     t.m_to[2] = to.z;
    t.m_duration = duration;
    t.m_easeType = ease;
    return t;
}

Tween Tween::vec4Tween(glm::vec4* target, const glm::vec4& from, const glm::vec4& to,
                       float duration, EaseType ease)
{
    Tween t;
    t.m_targetType = TargetType::VEC4;
    t.m_target = target;
    t.m_from[0] = from.x; t.m_from[1] = from.y; t.m_from[2] = from.z; t.m_from[3] = from.w;
    t.m_to[0] = to.x;     t.m_to[1] = to.y;     t.m_to[2] = to.z;     t.m_to[3] = to.w;
    t.m_duration = duration;
    t.m_easeType = ease;
    return t;
}

Tween Tween::quatTween(glm::quat* target, const glm::quat& from, const glm::quat& to,
                       float duration, EaseType ease)
{
    Tween t;
    t.m_targetType = TargetType::QUAT;
    t.m_target = target;
    // Store as x,y,z,w
    t.m_from[0] = from.x; t.m_from[1] = from.y; t.m_from[2] = from.z; t.m_from[3] = from.w;
    t.m_to[0] = to.x;     t.m_to[1] = to.y;     t.m_to[2] = to.z;     t.m_to[3] = to.w;
    t.m_duration = duration;
    t.m_easeType = ease;
    return t;
}

// ---------------------------------------------------------------------------
// Builder methods
// ---------------------------------------------------------------------------

Tween& Tween::setPlayback(TweenPlayback mode)
{
    m_playback = mode;
    return *this;
}

Tween& Tween::setDelay(float seconds)
{
    m_delay = seconds;
    return *this;
}

Tween& Tween::setEase(EaseType ease)
{
    m_easeType = ease;
    m_customEase.reset();
    return *this;
}

Tween& Tween::setCustomEase(float x1, float y1, float x2, float y2)
{
    m_customEase = std::make_unique<CubicBezierEase>(x1, y1, x2, y2);
    return *this;
}

Tween& Tween::onComplete(std::function<void()> callback)
{
    m_onComplete = std::move(callback);
    return *this;
}

Tween& Tween::onLoop(std::function<void()> callback)
{
    m_onLoop = std::move(callback);
    return *this;
}

Tween& Tween::addEvent(float normalizedTime, std::function<void()> callback)
{
    m_events.push_back({normalizedTime, std::move(callback)});
    return *this;
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

void Tween::pause() { m_paused = true; }
void Tween::resume() { m_paused = false; }

void Tween::stop()
{
    m_finished = true;
    m_paused = false;
}

void Tween::restart()
{
    m_elapsed = 0.0f;
    m_progress = 0.0f;
    m_direction = 1;
    m_finished = false;
    m_paused = false;
    m_started = false;
}

bool Tween::isFinished() const { return m_finished; }
bool Tween::isPaused() const { return m_paused; }
float Tween::getProgress() const { return m_progress; }
Tween::TargetType Tween::getTargetType() const { return m_targetType; }
void* Tween::getTarget() const { return m_target; }

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void Tween::update(float deltaTime)
{
    if (m_finished || m_paused || m_duration <= 0.0f)
    {
        return;
    }

    m_elapsed += deltaTime;

    // Delay phase
    if (m_elapsed < m_delay)
    {
        return;
    }

    float activeTime = m_elapsed - m_delay;
    float oldProgress = m_progress;
    float newProgress = 0.0f;
    bool loopWrapped = false;
    float pingPongFirstHalf = 0.0f;
    float pingPongLastHalf = 0.0f;

    switch (m_playback)
    {
    case TweenPlayback::ONCE:
    {
        newProgress = std::min(activeTime / m_duration, 1.0f);
        if (newProgress >= 1.0f)
        {
            m_finished = true;
        }
        break;
    }
    case TweenPlayback::LOOP:
    {
        // A wrap is a change of whole-cycle count since the last update.
        // (activeTime only grows, so testing activeTime >= m_duration would
        // report a wrap on every update after the first cycle.)
        float prevActive = std::max(0.0f, activeTime - deltaTime);
        loopWrapped = m_started
            && std::floor(activeTime / m_duration) > std::floor(prevActive / m_duration);
        if (loopWrapped && m_onLoop)
        {
            m_onLoop();
        }
        newProgress = std::fmod(activeTime, m_duration) / m_duration;
        break;
    }
    case TweenPlayback::PING_PONG:
    {
        // Half-cycle k (each m_duration long) runs forward when k is even
        // and backward when k is odd. One update can span several halves,
        // so count them rather than compare directions: two turnarounds
        // in one update leave the direction unchanged.
        float prevActive = std::max(0.0f, activeTime - deltaTime);
        pingPongFirstHalf = std::floor(prevActive / m_duration);
        pingPongLastHalf = std::floor(activeTime / m_duration);

        float local = std::clamp(
            (activeTime - pingPongLastHalf * m_duration) / m_duration, 0.0f, 1.0f);
        newProgress = isForwardHalf(pingPongLastHalf) ? local : 1.0f - local;
        break;
    }
    }

    m_started = true;
    m_progress = newProgress;

    // Apply easing
    float easedProgress;
    if (m_customEase)
    {
        easedProgress = m_customEase->evaluate(newProgress);
    }
    else
    {
        easedProgress = evaluateEasing(m_easeType, newProgress);
    }

    // Write to target
    applyValue(easedProgress);

    // Fire events
    if (m_playback == TweenPlayback::PING_PONG)
    {
        firePingPongLegs(oldProgress, newProgress, pingPongFirstHalf, pingPongLastHalf);
    }
    else
    {
        fireEvents(oldProgress, newProgress, loopWrapped);
    }

    // Fire onComplete for ONCE mode
    if (m_finished && m_onComplete)
    {
        m_onComplete();
    }
}

// ---------------------------------------------------------------------------
// Apply interpolated value to target
// ---------------------------------------------------------------------------

void Tween::applyValue(float easedProgress)
{
    if (!m_target) return;

    switch (m_targetType)
    {
    case TargetType::FLOAT:
    {
        float* p = static_cast<float*>(m_target);
        *p = m_from[0] + (m_to[0] - m_from[0]) * easedProgress;
        break;
    }
    case TargetType::VEC3:
    {
        glm::vec3* p = static_cast<glm::vec3*>(m_target);
        p->x = m_from[0] + (m_to[0] - m_from[0]) * easedProgress;
        p->y = m_from[1] + (m_to[1] - m_from[1]) * easedProgress;
        p->z = m_from[2] + (m_to[2] - m_from[2]) * easedProgress;
        break;
    }
    case TargetType::VEC4:
    {
        glm::vec4* p = static_cast<glm::vec4*>(m_target);
        p->x = m_from[0] + (m_to[0] - m_from[0]) * easedProgress;
        p->y = m_from[1] + (m_to[1] - m_from[1]) * easedProgress;
        p->z = m_from[2] + (m_to[2] - m_from[2]) * easedProgress;
        p->w = m_from[3] + (m_to[3] - m_from[3]) * easedProgress;
        break;
    }
    case TargetType::QUAT:
    {
        glm::quat* p = static_cast<glm::quat*>(m_target);
        glm::quat from(m_from[3], m_from[0], m_from[1], m_from[2]); // w,x,y,z
        glm::quat to(m_to[3], m_to[0], m_to[1], m_to[2]);
        *p = glm::slerp(from, to, easedProgress);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Fire events that were crossed between oldProgress and newProgress
// ---------------------------------------------------------------------------

void Tween::fireEvents(float oldProgress, float newProgress, bool loopWrapped)
{
    if (!loopWrapped)
    {
        fireLeg(oldProgress, newProgress);
        return;
    }

    // LOOP wrap: progress ran to the end and restarted, so the crossed
    // span is (oldProgress, 1] plus [0, newProgress].
    for (auto& evt : m_events)
    {
        bool crossed = (evt.normalizedTime > oldProgress || evt.normalizedTime <= newProgress);
        if (crossed && evt.callback)
        {
            evt.callback();
        }
    }
}

void Tween::fireLeg(float from, float to)
{
    for (auto& evt : m_events)
    {
        bool crossed = false;
        if (to >= from)
        {
            // Forward: fire if event time is in (from, to]
            crossed = (evt.normalizedTime > from && evt.normalizedTime <= to);
        }
        else
        {
            // Backward: fire if event time is in [to, from)
            crossed = (evt.normalizedTime >= to && evt.normalizedTime < from);
        }

        if (crossed && evt.callback)
        {
            evt.callback();
        }
    }
}

void Tween::firePingPongLegs(float oldProgress, float newProgress,
                             float firstHalf, float lastHalf)
{
    if (lastHalf <= firstHalf)
    {
        fireLeg(oldProgress, newProgress);
        return;
    }

    // The leg from oldProgress to the end of its half, then onLoop at each
    // turnaround, every whole half in between, and the start of the last
    // half up to newProgress.
    fireLeg(oldProgress, isForwardHalf(firstHalf) ? 1.0f : 0.0f);
    for (float half = firstHalf + 1.0f; half <= lastHalf; half += 1.0f)
    {
        if (m_onLoop)
        {
            m_onLoop();
        }
        bool forward = isForwardHalf(half);
        float start = forward ? 0.0f : 1.0f;
        float end = (half < lastHalf) ? (forward ? 1.0f : 0.0f) : newProgress;
        fireLeg(start, end);
    }
}

bool Tween::isForwardHalf(float halfIndex)
{
    return std::fmod(halfIndex, 2.0f) < 1.0f;
}

// ---------------------------------------------------------------------------
// TweenManager
// ---------------------------------------------------------------------------

Tween& TweenManager::add(Tween tween)
{
    // A callback run by update() may add tweens. Growing m_tweens there would
    // move the tween that is mid-update, so those wait in m_addedDuringUpdate.
    auto& list = m_updating ? m_addedDuringUpdate : m_tweens;
    list.push_back(std::move(tween));
    return list.back();
}

void TweenManager::cancelTarget(void* target)
{
    m_tweens.erase(
        std::remove_if(m_tweens.begin(), m_tweens.end(),
            [target](const Tween& t) { return t.getTarget() == target; }),
        m_tweens.end());
}

void TweenManager::cancelAll()
{
    m_tweens.clear();
}

size_t TweenManager::activeTweenCount() const
{
    size_t count = 0;
    for (const auto* list : {&m_tweens, &m_addedDuringUpdate})
    {
        for (const auto& t : *list)
        {
            if (!t.isFinished()) ++count;
        }
    }
    return count;
}

void TweenManager::update(float deltaTime)
{
    // Tick all tweens
    m_updating = true;
    for (auto& t : m_tweens)
    {
        t.update(deltaTime);
    }
    m_updating = false;

    // Tweens added by callbacks join now and first tick on the next update.
    for (auto& t : m_addedDuringUpdate)
    {
        m_tweens.push_back(std::move(t));
    }
    m_addedDuringUpdate.clear();

    // Remove finished tweens (swap-and-pop)
    m_tweens.erase(
        std::remove_if(m_tweens.begin(), m_tweens.end(),
            [](const Tween& t) { return t.isFinished(); }),
        m_tweens.end());
}

std::unique_ptr<Component> TweenManager::clone() const
{
    // Tweens reference external pointers — cloning doesn't make sense
    // Return an empty manager
    return std::make_unique<TweenManager>();
}

} // namespace Vestige
