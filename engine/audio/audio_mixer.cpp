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

void AudioMixer::setBusGain(AudioBus bus, float gain)
{
    const float clamped = std::max(0.0f, std::min(1.0f, gain));
    busGain[static_cast<std::size_t>(bus)] = clamped;
}

float AudioMixer::getBusGain(AudioBus bus) const
{
    return busGain[static_cast<std::size_t>(bus)];
}

float effectiveBusGain(const AudioMixer& mixer, AudioBus bus)
{
    const float master = mixer.busGain[static_cast<std::size_t>(AudioBus::Master)];
    const float self   = mixer.busGain[static_cast<std::size_t>(bus)];
    const float product = (bus == AudioBus::Master) ? master : master * self;
    return std::max(0.0f, std::min(1.0f, product));
}

float resolveSourceGain(const AudioMixer& mixer,
                        AudioBus bus,
                        float sourceVolume)
{
    // Guard against negative input. Clamp the per-source volume to
    // [0, 1] before multiplication so an authoring bug (e.g.
    // `volume = 1.5f`) does not push the composed gain above 1.0,
    // which the downstream `effectiveBusGain` clamp would *not*
    // re-clamp (it clamps the bus product, not the final product).
    const float vol = std::max(0.0f, std::min(1.0f, sourceVolume));
    const float busGain = effectiveBusGain(mixer, bus);
    return std::max(0.0f, std::min(1.0f, busGain * vol));
}

float resolveSourceGain(const AudioMixer& mixer,
                        AudioBus bus,
                        float sourceVolume,
                        float duckingGain)
{
    // Phase 10.9 P3: fold the DuckingState::currentGain snapshot into
    // the composed gain. clamp01(duckingGain) before multiplying so
    // a state that overshoots its floor / ceiling during attack or
    // release slew can't push AL_GAIN outside unit range.
    const float duck = std::max(0.0f, std::min(1.0f, duckingGain));
    const float base = resolveSourceGain(mixer, bus, sourceVolume);
    return std::max(0.0f, std::min(1.0f, base * duck));
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

std::size_t chooseVoiceToEvictForIncoming(
    const std::vector<VoiceCandidate>& /*voices*/,
    SoundPriority /*incomingPriority*/)
{
    // Phase 10.9 P7 RED: deliberately always refuses eviction so the
    // "incoming High wins over Normal" test fails. Green replaces with
    // `chooseVoiceToEvict` + strict priority-tier admission gate.
    return static_cast<std::size_t>(-1);
}

} // namespace Vestige
