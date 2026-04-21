// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_mixer.cpp
/// @brief Mixer bus gains + ducking state + priority-based eviction.
#include "audio/audio_mixer.h"

#include <algorithm>
#include <limits>

namespace Vestige
{

const char* soundPriorityLabel(SoundPriority p)
{
    switch (p)
    {
        case SoundPriority::Low:      return "Low";
        case SoundPriority::Normal:   return "Normal";
        case SoundPriority::High:     return "High";
        case SoundPriority::Critical: return "Critical";
    }
    return "Unknown";
}

int soundPriorityRank(SoundPriority p)
{
    switch (p)
    {
        case SoundPriority::Low:      return 0;
        case SoundPriority::Normal:   return 1;
        case SoundPriority::High:     return 2;
        case SoundPriority::Critical: return 3;
    }
    return 0;
}

const char* audioBusLabel(AudioBus bus)
{
    switch (bus)
    {
        case AudioBus::Master:  return "Master";
        case AudioBus::Music:   return "Music";
        case AudioBus::Voice:   return "Voice";
        case AudioBus::Sfx:     return "Sfx";
        case AudioBus::Ambient: return "Ambient";
        case AudioBus::Ui:      return "Ui";
    }
    return "Unknown";
}

float effectiveBusGain(const AudioMixer& mixer, AudioBus bus)
{
    const float master = mixer.busGain[static_cast<std::size_t>(AudioBus::Master)];
    const float self   = mixer.busGain[static_cast<std::size_t>(bus)];
    const float product = (bus == AudioBus::Master) ? master : master * self;
    return std::max(0.0f, std::min(1.0f, product));
}

void updateDucking(DuckingState& state,
                    const DuckingParams& params,
                    float deltaSeconds)
{
    if (deltaSeconds < 0.0f)
    {
        deltaSeconds = 0.0f;
    }

    const float floor = std::max(0.0f, std::min(1.0f, params.duckFactor));
    const float target = state.triggered ? floor : 1.0f;

    const float duration = state.triggered
        ? std::max(1e-6f, params.attackSeconds)
        : std::max(1e-6f, params.releaseSeconds);

    // Slew speed = (full swing) / duration, scaled by dt.
    // `full swing` is 1 − floor, so attack = releases travel the
    // same dB distance at the configured times.
    const float fullSwing = 1.0f - floor;
    const float maxStep   = (fullSwing / duration) * deltaSeconds;
    const float diff      = target - state.currentGain;

    if (std::abs(diff) <= maxStep)
    {
        state.currentGain = target;
    }
    else
    {
        state.currentGain += (diff > 0.0f ? maxStep : -maxStep);
    }
    state.currentGain = std::max(floor, std::min(1.0f, state.currentGain));
}

float voiceKeepScore(const VoiceCandidate& v)
{
    const int   rank = soundPriorityRank(v.priority);
    const float gain = std::max(0.0f, std::min(1.0f, v.effectiveGain));
    const float age  = std::max(0.0f, v.ageSeconds);
    return static_cast<float>(rank) * 1000.0f + gain * 10.0f - age;
}

std::size_t chooseVoiceToEvict(const std::vector<VoiceCandidate>& voices)
{
    if (voices.empty())
    {
        return static_cast<std::size_t>(-1);
    }
    std::size_t worstIdx = 0;
    float       worstScore = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        const float score = voiceKeepScore(voices[i]);
        if (score < worstScore)
        {
            worstScore = score;
            worstIdx = i;
        }
    }
    return worstIdx;
}

} // namespace Vestige
