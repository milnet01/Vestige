// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_mute_solo.h
/// @brief The editor's per-entity Mute and Solo switches for audio sources.
#pragma once

#include <cstdint>
#include <iterator>
#include <unordered_set>

namespace Vestige
{

/// @brief Which entities' audio sources are muted or soloed.
///
/// `AudioSystem` owns one and multiplies each auto-played source's output
/// gain by `gainMultiplier`; the Audio panel's Mute and Solo checkboxes write
/// to it. Like the bus solo (`busSoloMultiplier`), it applies only where the
/// gain is uploaded to OpenAL, so eviction and occlusion stay unaware of it.
///
/// Kept free of OpenAL and the scene so it can be tested without an audio
/// device.
class AudioSourceMuteSolo
{
public:
    void setMuted(std::uint32_t entityId, bool muted)
    {
        if (muted) m_muted.insert(entityId);
        else       m_muted.erase(entityId);
    }

    bool isMuted(std::uint32_t entityId) const { return m_muted.count(entityId) != 0; }

    void setSoloed(std::uint32_t entityId, bool soloed)
    {
        if (soloed) m_soloed.insert(entityId);
        else        m_soloed.erase(entityId);
    }

    bool isSoloed(std::uint32_t entityId) const { return m_soloed.count(entityId) != 0; }

    bool anySoloed() const { return !m_soloed.empty(); }

    /// @brief 0 when the source is muted, or when another source is soloed
    ///        and this one is not; 1 otherwise. Mute beats solo.
    float gainMultiplier(std::uint32_t entityId) const
    {
        if (isMuted(entityId)) return 0.0f;
        if (anySoloed() && !isSoloed(entityId)) return 0.0f;
        return 1.0f;
    }

    /// @brief Drops every entity for which `isGone(id)` is true. A soloed
    ///        entity that was deleted would otherwise silence every other
    ///        source, with no checkbox left to clear it. Entity ids are never
    ///        reused, so a dropped id cannot come back as another entity.
    template <typename IsGone>
    void forgetGone(IsGone isGone)
    {
        eraseIf(m_muted, isGone);
        eraseIf(m_soloed, isGone);
    }

private:
    template <typename IsGone>
    static void eraseIf(std::unordered_set<std::uint32_t>& ids, IsGone& isGone)
    {
        for (auto it = ids.begin(); it != ids.end(); )
        {
            it = isGone(*it) ? ids.erase(it) : std::next(it);
        }
    }

    std::unordered_set<std::uint32_t> m_muted;
    std::unordered_set<std::uint32_t> m_soloed;
};

}  // namespace Vestige
