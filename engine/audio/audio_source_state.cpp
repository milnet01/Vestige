// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_source_state.cpp
/// @brief Phase 10.9 Slice 2 P2 — pure compose implementation.
#include "audio/audio_source_state.h"

#include "audio/audio_occlusion.h"
#include "audio/audio_source_component.h"

namespace Vestige
{

AudioSourceAlState composeAudioSourceAlState(
    const AudioSourceComponent& comp,
    const glm::vec3&            entityPosition,
    const AudioMixer&           mixer,
    float                       duckingGain)
{
    // Phase 10.9 P2 (red): stub returns defaults + position, ignoring
    // every other component field so the full component→AL contract
    // fails at runtime. Green populates every field through the
    // occlusion / mixer / duck composition pipeline.
    (void)comp;
    (void)mixer;
    (void)duckingGain;

    AudioSourceAlState state;
    state.position = entityPosition;
    return state;
}

} // namespace Vestige
