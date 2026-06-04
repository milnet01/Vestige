// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file music_system.h
/// @brief Phase 10.9 Slice 8 W8 (part 2/2) — MusicSystem ISystem wrapper.
///
/// Thin `ISystem` that gives streaming music a canonical phase slot in
/// the dispatch loop and a public surface for gameplay to push the
/// intensity / silence mix at, without coupling gameplay to
/// `AudioMusicPlayer`. Parallel to `AudioSystem` owning `AudioEngine` —
/// here the player is owned by `Engine` and this system borrows it.
///
/// Spec: `docs/engine/audio/spec.md` §2/§3/§5 names `MusicSystem` as one
/// of the three audio `ISystem` implementations; §5 puts it in the
/// default Update phase (no camera dependence, unlike `AudioSystem`).
#pragma once

#include <string>

#include "core/i_system.h"

namespace Vestige
{

class AudioMusicPlayer;

class MusicSystem final : public ISystem
{
public:
    explicit MusicSystem(AudioMusicPlayer& player);

    // ---- ISystem ----------------------------------------------------

    const std::string& getSystemName() const override { return m_name; }

    bool initialize(Engine& /*engine*/) override
    {
        // The player is constructed + owned by Engine; this system only
        // holds the reference. Nothing to set up here.
        return true;
    }

    void shutdown() override
    {
        // Player teardown is the owner's (Engine's) job — calling
        // clearAllLayers here would race the AudioEngine shutdown order.
    }

    void update(float deltaSeconds) override;

    // getUpdatePhase() not overridden — default UpdatePhase::Update
    // matches spec.md §5 (separate ISystem, Update phase).

    // ---- Gameplay surface -------------------------------------------

    /// Gameplay-driven mix inputs. Last write within a frame wins; the
    /// next `update(dt)` applies them via `intensityToLayerWeights`.
    void setIntensity(float intensity) { m_intensity = intensity; }
    void setSilence(float silence) { m_silence = silence; }

    float getIntensity() const { return m_intensity; }
    float getSilence() const { return m_silence; }

private:
    AudioMusicPlayer& m_player;
    const std::string m_name = "MusicSystem";
    float m_intensity = 0.0f;
    float m_silence   = 0.0f;
};

} // namespace Vestige
