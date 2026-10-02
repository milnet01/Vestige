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

/// @brief Maps entity id → the playback AudioSystem auto-played for it, and
///        decides when to start one.
///
/// `autoPlay` means "plays automatically on scene load" — once. A stopped
/// source is reaped, so the tracker also remembers which entities have
/// fired; without that, a finished one-shot would be untracked on the next
/// frame and started again (3D_E-0738). Unticking `autoPlay` re-arms an
/// entity. Entity ids are never reused (`Entity::s_nextId`), so a reloaded
/// scene's entities start unfired.
///
/// A source is identified by its OpenAL name AND the playback ticket
/// `AudioEngine::playbackTicket` gave it, because eviction hands the
/// victim's name to the new sound (3D_E-0739). A looping playback that is
/// lost that way is re-armed, so it restarts once a source is free; a lost
/// one-shot is not.
///
/// Kept free of OpenAL and the scene so the start / reap cycle can be
/// tested frame by frame without an audio device.
class AudioSourceTracker
{
public:
    /// @brief One tracked playback. `source` 0 marks a failed acquire.
    struct TrackedSource
    {
        unsigned int  source = 0;
        std::uint64_t ticket = 0;
        bool          loop   = false;
    };

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

    /// @brief Records the playback started for `entityId`. A 0 source marks
    ///        "attempted"; the next reap drops it so a later frame retries.
    void started(std::uint32_t entityId, unsigned int source,
                 std::uint64_t ticket, bool loop)
    {
        m_active[entityId] = TrackedSource{source, ticket, loop};
        if (source != 0)
        {
            m_fired.insert(entityId);
        }
    }

    /// @brief Playback tracked for `entityId`, or nullptr when untracked.
    const TrackedSource* find(std::uint32_t entityId) const
    {
        auto it = m_active.find(entityId);
        return it == m_active.end() ? nullptr : &it->second;
    }

    /// @brief Drops entries whose entity is gone or whose playback is no
    ///        longer live. `isLive(const TrackedSource&)` must be false once
    ///        the source has stopped or now carries another ticket.
    template <typename IsGone, typename IsLive>
    void reap(IsGone isGone, IsLive isLive)
    {
        for (auto it = m_active.begin(); it != m_active.end(); )
        {
            const TrackedSource& t = it->second;
            const bool gone = isGone(it->first);
            const bool lost = t.source != 0 && !gone && !isLive(t);
            if (gone || t.source == 0 || lost)
            {
                if (lost && t.loop)
                {
                    m_fired.erase(it->first);  // a loop never ends on its own
                }
                it = m_active.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = m_fired.begin(); it != m_fired.end(); )
        {
            it = isGone(*it) ? m_fired.erase(it) : std::next(it);
        }
    }

    const std::unordered_map<std::uint32_t, TrackedSource>& active() const
    {
        return m_active;
    }

private:
    std::unordered_map<std::uint32_t, TrackedSource> m_active;
    std::unordered_set<std::uint32_t> m_fired;
};

} // namespace Vestige
