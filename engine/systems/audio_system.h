// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_system.h
/// @brief Domain system for audio playback and spatial sound.
#pragma once

#include "core/i_system.h"
#include "audio/audio_engine.h"
#include "audio/audio_ducking.h"
#include "audio/audio_lod.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Manages audio playback, spatial sound, and listener positioning.
///
/// Owns the AudioEngine subsystem that wraps OpenAL for 3D audio. Syncs
/// the listener to the camera each frame. Fails gracefully if no audio
/// hardware is available — the engine continues without sound.
class AudioSystem : public ISystem
{
public:
    AudioSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    /// @brief Audio runs in the PostCamera phase so the listener-sync at
    ///        the top of `update()` reads the camera transform after the
    ///        camera has been stepped this frame (Phase 10.9 Slice 11 Sy1
    ///        — closes the W6 listener-after-camera dependency).
    UpdatePhase getUpdatePhase() const override { return UpdatePhase::PostCamera; }

    /// @brief Phase 10.9 Slice 8 W5 — AudioSystem owns OpenAL device + listener
    ///        + buffer cache as global state, not as per-entity components,
    ///        so the "scene has no owned components → deactivate" heuristic
    ///        is the wrong policy. The system must keep ticking even with
    ///        no AudioSourceComponents in the scene (ducking decay, listener
    ///        sync, caption queue), so it forces itself active.
    bool isForceActive() const override { return true; }

    // -- Accessors --
    AudioEngine& getAudioEngine() { return m_audioEngine; }
    const AudioEngine& getAudioEngine() const { return m_audioEngine; }

    /// @brief Whether audio hardware is available.
    bool isAvailable() const { return m_audioEngine.isAvailable(); }

    /// @brief AX13 — installs the side-chain duck routes (normally loaded
    ///        from `assets/audio/mix_graph.json` at init; also used by
    ///        gameplay/tests). Resets the per-route state.
    void setDuckingRoutes(std::vector<DuckingRoute> routes)
    {
        m_duckingRouter.setRoutes(std::move(routes));
    }

    /// @brief AX13 — read-only access to the router (tests / debug panel).
    const DuckingRouter& duckingRouter() const { return m_duckingRouter; }

    /// @brief Phase 10.9 P2 — read-only access to the per-entity
    ///        active-source map. Tests use this to verify the
    ///        auto-play + source-tracking pipeline without touching AL.
    const std::unordered_map<std::uint32_t, unsigned int>&
    activeSources() const { return m_activeSources; }

private:
    static inline const std::string m_name = "Audio";
    AudioEngine m_audioEngine;
    Engine* m_engine = nullptr;

    /// @brief Phase 10.9 P2 — maps entity ID → OpenAL source ID for
    ///        every `AudioSourceComponent` that has been acquired by
    ///        this system (auto-play or explicit trigger). Cleared
    ///        when the component is removed or the source stops
    ///        (reaped in the per-frame update).
    std::unordered_map<std::uint32_t, unsigned int> m_activeSources;

    /// @brief AX5 — per-entity LOD tier from the previous frame, the
    ///        `previousTier` input to `audioLodTier` (hysteresis). Reaped
    ///        alongside `m_activeSources`. The tuning config stays at its
    ///        defaults; only the master enable is user-facing (read each
    ///        frame from the AudioEngine).
    std::unordered_map<std::uint32_t, AudioLodTier> m_lodTiers;
    AudioLodConfig m_lodConfig;

    /// @brief AX13 — side-chain duck routes (additive to the global manual
    ///        duck). Empty by default → manual-duck-only (pre-AX13).
    DuckingRouter m_duckingRouter;

    /// @brief AX13 — last frame's per-bus activity, fed to the router this
    ///        frame (one-frame latency; imperceptible against the ~80 ms
    ///        duck attack and feedback-free since it reads source volume,
    ///        not the already-ducked gain).
    std::array<bool, AudioBusCount> m_busActivePrev{};
};

} // namespace Vestige
