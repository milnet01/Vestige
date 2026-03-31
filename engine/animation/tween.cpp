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
        float rawProgress = activeTime / m_duration;
        // Detect loop wrap
        if (m_started && rawProgress >= 1.0f)
        {
            if (m_onLoop) m_onLoop();
        }
        newProgress = std::fmod(activeTime, m_duration) / m_duration;
        break;
    }
    case TweenPlayback::PING_PONG:
    {
        float cycleDuration = m_duration * 2.0f;
        float cycle = std::fmod(activeTime, cycleDuration);

        // Detect direction change
        float prevCycle = std::fmod(std::max(0.0f, activeTime - deltaTime), cycleDuration);
        bool wasForward = prevCycle < m_duration;
        bool isForward = cycle < m_duration;
        if (m_started && wasForward != isForward && m_onLoop)
        {
            m_onLoop();
        }

        if (cycle < m_duration)
        {
            newProgress = cycle / m_duration;
        }
        else
        {
            newProgress = 1.0f - (cycle - m_duration) / m_duration;
        }
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
    fireEvents(oldProgress, newProgress);

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

void Tween::fireEvents(float oldProgress, float newProgress)
{
    for (auto& evt : m_events)
    {
        bool crossed = false;
        if (newProgress >= oldProgress)
        {
            // Forward: fire if event time is in (oldProgress, newProgress]
            crossed = (evt.normalizedTime > oldProgress && evt.normalizedTime <= newProgress);
        }
        else
        {
            // Backward (ping-pong reverse): fire if event time is in [newProgress, oldProgress)
            crossed = (evt.normalizedTime >= newProgress && evt.normalizedTime < oldProgress);
        }

        if (crossed && evt.callback)
        {
            evt.callback();
        }
    }
}

// ---------------------------------------------------------------------------
// TweenManager
// ---------------------------------------------------------------------------

Tween& TweenManager::add(Tween tween)
{
    m_tweens.push_back(std::move(tween));
    return m_tweens.back();
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
    for (const auto& t : m_tweens)
    {
        if (!t.isFinished()) ++count;
    }
    return count;
}

void TweenManager::update(float deltaTime)
{
    // Tick all tweens
    for (auto& t : m_tweens)
    {
        t.update(deltaTime);
    }

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
