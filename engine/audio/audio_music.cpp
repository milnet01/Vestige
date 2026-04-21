// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_music.cpp
/// @brief Dynamic music primitives — layer slew + intensity →
///        weights + stinger queue.
#include "audio/audio_music.h"

#include <algorithm>

namespace Vestige
{

const char* musicLayerLabel(MusicLayer layer)
{
    switch (layer)
    {
        case MusicLayer::Ambient:     return "Ambient";
        case MusicLayer::Tension:     return "Tension";
        case MusicLayer::Exploration: return "Exploration";
        case MusicLayer::Combat:      return "Combat";
        case MusicLayer::Discovery:   return "Discovery";
        case MusicLayer::Danger:      return "Danger";
    }
    return "Unknown";
}

void advanceMusicLayer(MusicLayerState& state, float deltaSeconds)
{
    if (deltaSeconds <= 0.0f)
    {
        state.currentGain = std::max(0.0f, std::min(1.0f, state.currentGain));
        return;
    }
    const float maxStep = std::max(0.0f, state.fadeSpeedPerSecond) * deltaSeconds;
    const float diff    = state.targetGain - state.currentGain;
    if (std::abs(diff) <= maxStep)
    {
        state.currentGain = state.targetGain;
    }
    else
    {
        state.currentGain += (diff > 0.0f ? maxStep : -maxStep);
    }
    state.currentGain = std::max(0.0f, std::min(1.0f, state.currentGain));
}

namespace
{
// Triangle weight over intensity anchors: peak at `peak`, zero at
// `peak ± halfWidth`, linear in between. Used to drive each layer.
float triWeight(float intensity, float peak, float halfWidth)
{
    const float delta = std::abs(intensity - peak);
    if (delta >= halfWidth) return 0.0f;
    return 1.0f - delta / halfWidth;
}
}

MusicLayerWeights intensityToLayerWeights(float intensity, float silence)
{
    const float i = std::max(0.0f, std::min(1.0f, intensity));
    const float s = std::max(0.0f, std::min(1.0f, silence));

    // Anchors (matching header doc):
    //   Ambient      peak 0.00 (one-sided — clamp to max at 0)
    //   Exploration  peak 0.25
    //   Tension      peak 0.50
    //   Discovery    peak 0.50 (secondary)
    //   Combat       peak 0.75
    //   Danger       peak 1.00 (one-sided — clamp to max at 1)
    MusicLayerWeights w;
    w.weightOf(MusicLayer::Ambient)     = triWeight(i, 0.00f, 0.25f);
    w.weightOf(MusicLayer::Exploration) = triWeight(i, 0.25f, 0.25f);
    w.weightOf(MusicLayer::Tension)     = triWeight(i, 0.50f, 0.25f);
    w.weightOf(MusicLayer::Discovery)   = triWeight(i, 0.50f, 0.25f) * 0.5f;  // subtler bed
    w.weightOf(MusicLayer::Combat)      = triWeight(i, 0.75f, 0.25f);
    w.weightOf(MusicLayer::Danger)      = triWeight(i, 1.00f, 0.25f);

    // Silence pulls every weight toward zero multiplicatively.
    const float keep = 1.0f - s;
    for (auto& v : w.values)
    {
        v *= keep;
    }
    return w;
}

// ----- MusicStingerQueue -----------------------------------------

void MusicStingerQueue::setCapacity(std::size_t capacity)
{
    m_capacity = capacity;
    if (m_pending.size() > m_capacity)
    {
        // Drop the oldest entries until we fit.
        const auto surplus =
            static_cast<std::ptrdiff_t>(m_pending.size() - m_capacity);
        m_pending.erase(m_pending.begin(), m_pending.begin() + surplus);
    }
}

void MusicStingerQueue::enqueue(const MusicStinger& stinger)
{
    if (m_capacity == 0)
    {
        return;
    }
    if (m_pending.size() >= m_capacity)
    {
        m_pending.erase(m_pending.begin());
    }
    m_pending.push_back(stinger);
}

std::vector<MusicStinger> MusicStingerQueue::advance(float deltaSeconds)
{
    std::vector<MusicStinger> fired;
    if (m_pending.empty())
    {
        return fired;
    }
    if (deltaSeconds < 0.0f)
    {
        deltaSeconds = 0.0f;
    }

    for (auto& s : m_pending)
    {
        s.delaySeconds -= deltaSeconds;
    }

    // Partition in FIFO order: everything whose delay has expired
    // moves into `fired`, everything still waiting stays behind.
    std::vector<MusicStinger> remaining;
    remaining.reserve(m_pending.size());
    for (auto& s : m_pending)
    {
        if (s.delaySeconds <= 0.0f)
        {
            fired.push_back(s);
        }
        else
        {
            remaining.push_back(s);
        }
    }
    m_pending.swap(remaining);
    return fired;
}

void MusicStingerQueue::clear()
{
    m_pending.clear();
}

} // namespace Vestige
