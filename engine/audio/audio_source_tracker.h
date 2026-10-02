// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_tracker.h
/// @brief AudioSystem's per-entity bookkeeping for auto-played sources:
///        which entity owns which OpenAL source, and when to start one.
#pragma once

#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace Vestige
{

/// @brief Maps entity id → OpenAL source for every `AudioSourceComponent`
///        AudioSystem has auto-played, and decides when to start one.
///
/// `autoPlay` means "plays automatically on scene load" — once. A stopped
/// source is reaped, so the tracker also remembers which entities have
/// fired; without that, a finished one-shot would be untracked on the next
/// frame and started again (3D_E-0738). Unticking `autoPlay` re-arms an
/// entity. Entity ids are never reused (`Entity::s_nextId`), so a reloaded
/// scene's entities start unfired.
///
/// Kept free of OpenAL and the scene so the start / reap cycle can be
/// tested frame by frame without an audio device.
class AudioSourceTracker
{
public:
    /// @brief Records this frame's `autoPlay` value. False re-arms the entity.
    void observe(std::uint32_t entityId, bool autoPlay)
    {
        if (!autoPlay)
        {
            m_fired.erase(entityId);
        }
    }

    /// @brief True when an untracked component should be started this frame.
    bool shouldStart(std::uint32_t entityId, bool autoPlay, bool hasClip) const
    {
        return autoPlay && hasClip && m_active.count(entityId) == 0
            && m_fired.count(entityId) == 0;
    }

    /// @brief Records the source acquired for `entityId`. A 0 source marks
    ///        "attempted"; the next reap drops it so a later frame retries.
    void started(std::uint32_t entityId, unsigned int source)
    {
        m_active[entityId] = source;
        if (source != 0)
        {
            m_fired.insert(entityId);
        }
    }

    /// @brief Source tracked for `entityId`, or nullptr when untracked.
    const unsigned int* find(std::uint32_t entityId) const
    {
        auto it = m_active.find(entityId);
        return it == m_active.end() ? nullptr : &it->second;
    }

    /// @brief Drops entries whose entity is gone or whose source has stopped.
    template <typename IsGone, typename IsPlaying>
    void reap(IsGone isGone, IsPlaying isPlaying)
    {
        for (auto it = m_active.begin(); it != m_active.end(); )
        {
            const bool dead = it->second == 0 || !isPlaying(it->second);
            it = (isGone(it->first) || dead) ? m_active.erase(it) : std::next(it);
        }
        for (auto it = m_fired.begin(); it != m_fired.end(); )
        {
            it = isGone(*it) ? m_fired.erase(it) : std::next(it);
        }
    }

    const std::unordered_map<std::uint32_t, unsigned int>& active() const
    {
        return m_active;
    }

private:
    std::unordered_map<std::uint32_t, unsigned int> m_active;
    std::unordered_set<std::uint32_t> m_fired;
};

} // namespace Vestige
