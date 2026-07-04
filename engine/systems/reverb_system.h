// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reverb_system.h
/// @brief AX2 R3 — drives the engine reverb aux slot from scene reverb zones.
#pragma once

#include "core/i_system.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Engine;
class AudioEngine;

/// @brief Selects the winning `ReverbZoneComponent` each frame and drives the
///        engine's single reverb aux slot from it.
///
/// Mirrors `AudioOcclusionSystem`'s shape: `UpdatePhase::PostCamera`, registered
/// **before** `AudioSystem` so the stable-sort runs it first — it reads the
/// settled listener (camera already stepped), picks the highest-weighted zone,
/// pushes the blended parametric character (or swaps the convolution IR), slews
/// the slot wet gain, and writes each spatial source's `reverbSend`, all of
/// which `AudioSystem`'s compose loop consumes the same frame.
///
/// **Ownership differs from AX1 on purpose.** `getOwnedComponentTypes()` returns
/// `{ ReverbZoneComponent }`, so with the default `isForceActive() == false` the
/// system activates only when a scene actually has reverb zones — a scene with
/// sources but no zone stays dry, which is the intended no-reverb path.
///
/// Design of record: docs/phases/phase_10_audio_reverb_design.md § 5.
class ReverbSystem : public ISystem
{
public:
    ReverbSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    std::vector<uint32_t> getOwnedComponentTypes() const override;

    /// @brief PostCamera — same phase as AudioSystem, registered before it.
    UpdatePhase getUpdatePhase() const override { return UpdatePhase::PostCamera; }

    // -- Debug read-outs (AX2 R4 audio-panel Debug tab) --

    /// @brief Name of the zone entity currently driving the slot this frame,
    ///        or empty when the listener is dry / reverb is disabled.
    const std::string& winningZoneName() const { return m_winningZoneName; }

    /// @brief The current slewed slot wet gain [0, 1] (0 = dry).
    float currentWetGain() const { return m_slotWetGain; }

private:
    static inline const std::string m_name = "Reverb";

    Engine* m_engine = nullptr;

    /// @brief Cached at init from AudioSystem — the slot the reverb lives on.
    ///        Null in test harnesses with no AudioSystem; then update no-ops.
    AudioEngine* m_audioEngine = nullptr;

    /// @brief Slewed slot wet gain, carried across frames so entering / leaving
    ///        a zone fades rather than steps (§5.2 step 5).
    float m_slotWetGain = 0.0f;

    /// @brief Path of the IR currently attached to the convolution slot, so a
    ///        winning-zone change triggers exactly one load + swap + wet dip.
    std::string m_attachedIrPath;

    /// @brief Name of the winning zone entity this frame (for the Debug tab).
    std::string m_winningZoneName;
};

} // namespace Vestige
