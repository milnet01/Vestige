// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_animation.cpp
/// @brief SpriteAnimation implementation.
#include "animation/sprite_animation.h"

namespace Vestige
{

void SpriteAnimation::addClip(SpriteAnimationClip clip)
{
    // Cache the key up-front — the subsequent `std::move(clip)` leaves
    // `clip.name` in a moved-from state, so using it after the move is
    // undefined (and on libstdc++ lands in an empty string).
    const std::string key = clip.name;
    const bool existed = m_clips.find(key) != m_clips.end();
    m_clips[key] = std::move(clip);
    if (!existed)
    {
        m_clipOrder.push_back(key);
    }
}

std::vector<std::string> SpriteAnimation::clipNames() const
{
    return m_clipOrder;
}

const SpriteAnimationClip* SpriteAnimation::findClip(const std::string& name) const
{
    auto it = m_clips.find(name);
    if (it == m_clips.end())
    {
        return nullptr;
    }
    return &it->second;
}

void SpriteAnimation::play(const std::string& clipName)
{
    const auto it = m_clips.find(clipName);
    if (it == m_clips.end() || it->second.frames.empty())
    {
        return;
    }
    const auto& clip = it->second;

    m_currentClipName = clip.name;
    m_playing = true;
    m_frameElapsedMs = 0.0f;
    m_advancedLastTick = false;
    m_pingPongStep = 1;

    if (clip.direction == SpriteAnimationDirection::Reverse)
    {
        m_frameIndex = static_cast<int>(clip.frames.size()) - 1;
    }
    else
    {
        m_frameIndex = 0;
    }
    m_currentFrameName = clip.frames[static_cast<std::size_t>(m_frameIndex)].name;
}

void SpriteAnimation::stop()
{
    m_playing = false;
    m_advancedLastTick = false;
}

void SpriteAnimation::tick(float deltaTimeSeconds)
{
    m_advancedLastTick = false;

    if (!m_playing || m_frameIndex < 0)
    {
        return;
    }
    const auto* clip = findClip(m_currentClipName);
    if (!clip || clip->frames.empty())
    {
        return;
    }

    m_frameElapsedMs += deltaTimeSeconds * 1000.0f;

    // Tight loop: a single `tick(dt)` may cross multiple short frames when
    // the delta is large (e.g. a hitched frame or a testing-time scrub).
    // Advance until the remaining elapsed fits into the current frame.
    while (true)
    {
        const auto idx = static_cast<std::size_t>(m_frameIndex);
        const float duration = clip->frames[idx].durationMs;
        if (duration <= 0.0f)
        {
            // Degenerate frame — treat as instantaneous and advance.
            advanceFrame(*clip);
            m_advancedLastTick = true;
            if (!m_playing)
            {
                return;
            }
            continue;
        }

        if (m_frameElapsedMs < duration)
        {
            break;
        }

        m_frameElapsedMs -= duration;
        advanceFrame(*clip);
        m_advancedLastTick = true;
        if (!m_playing)
        {
            return;
        }
    }
}

void SpriteAnimation::advanceFrame(const SpriteAnimationClip& clip)
{
    const int count = static_cast<int>(clip.frames.size());
    if (count <= 0)
    {
        m_playing = false;
        return;
    }

    switch (clip.direction)
    {
        case SpriteAnimationDirection::Forward:
        {
            if (m_frameIndex + 1 < count)
            {
                ++m_frameIndex;
            }
            else if (clip.loop)
            {
                m_frameIndex = 0;
            }
            else
            {
                m_playing = false;
                m_frameElapsedMs = 0.0f;
            }
            break;
        }
        case SpriteAnimationDirection::Reverse:
        {
            if (m_frameIndex - 1 >= 0)
            {
                --m_frameIndex;
            }
            else if (clip.loop)
            {
                m_frameIndex = count - 1;
            }
            else
            {
                m_playing = false;
                m_frameElapsedMs = 0.0f;
            }
            break;
        }
        case SpriteAnimationDirection::PingPong:
        {
            const int next = m_frameIndex + m_pingPongStep;
            if (next >= 0 && next < count)
            {
                m_frameIndex = next;
            }
            else
            {
                // Hit an endpoint — flip direction.
                m_pingPongStep = -m_pingPongStep;
                const int reflected = m_frameIndex + m_pingPongStep;
                // Safe because ping-pong requires at least one frame of
                // travel room; for single-frame clips we stop below.
                if (count == 1)
                {
                    m_playing = clip.loop;
                    m_frameElapsedMs = 0.0f;
                    break;
                }
                if (reflected >= 0 && reflected < count)
                {
                    m_frameIndex = reflected;
                }
                if (!clip.loop &&
                    ((m_pingPongStep == 1 && m_frameIndex == 0) ||
                     (m_pingPongStep == -1 && m_frameIndex == count - 1)))
                {
                    // Completed a full ping-pong cycle without loop — stop
                    // on the endpoint we just returned to.
                    m_playing = false;
                    m_frameElapsedMs = 0.0f;
                }
            }
            break;
        }
    }

    if (m_frameIndex >= 0 && m_frameIndex < count)
    {
        m_currentFrameName = clip.frames[static_cast<std::size_t>(m_frameIndex)].name;
    }
}

} // namespace Vestige
