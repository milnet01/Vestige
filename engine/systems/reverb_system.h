// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reverb_system.h
/// @brief AX2 R3 — drives the engine reverb aux slot from scene reverb zones.
#pragma once

#include "core/i_system.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Engine;
class AudioEngine;
class Scene;

/// @brief One probe as loaded from a scene's baked `acoustics_index.json` (B4),
///        with its IR path resolved against the sidecar directory.
struct LoadedAcousticProbe
{
    glm::vec3     position{ 0.0f };
    float         influenceRadius = 0.0f;
    std::string   irPath;  ///< Full path to the probe's IR .wav (sidecar dir + filename).
    std::uint32_t id = 0;  ///< Source probe id (for the Debug read-out label).
};

/// @brief Reads `<scene-dir>/<scene-stem>_acoustics/acoustics_index.json` and
///        returns its probes with IR paths resolved against the sidecar dir.
///
/// Pure file read — no engine or physics state. Returns empty when
/// `sceneSourcePath` is empty, no sidecar exists, or the index is malformed. An
/// index `ir` entry containing a path separator is rejected (it must be a bare
/// filename inside the sidecar dir, so a hand-edited index cannot escape it).
std::vector<LoadedAcousticProbe> loadAcousticsIndex(const std::string& sceneSourcePath);

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
/// `{ ReverbZoneComponent, AcousticProbeComponent }`, so with the default
/// `isForceActive() == false` the system activates when a scene has authored
/// reverb zones **or** baked acoustic probes (B4) — a scene with sources but
/// neither stays dry, which is the intended no-reverb path.
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

    /// @brief Forces a baked-probe reload on the next `update`. Called after a
    ///        re-bake so the runtime picks up fresh IRs without a scene reload
    ///        (AX3 B5 editor "Bake Acoustics" button).
    void invalidateBakedProbes() { m_bakedScene = nullptr; }

private:
    /// @brief Drives the slot from the nearest baked probe's IR when the active
    ///        scene has a bake, taking precedence over authored zones (§6.3).
    ///        Convolution-only (probes are IR, never parametric params); returns
    ///        false — deferring to the zone path — when reverb is off, the
    ///        backend is parametric, or the scene has no baked probes.
    bool driveFromBakedProbes(const glm::vec3& listenerPos, float deltaTime, Scene& scene);

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

    // -- AX3 B4: baked acoustic probes for the active scene --

    /// @brief Identity of the scene whose bake is currently loaded, so a scene
    ///        switch (or reload) re-reads the index. Compared, never dereferenced.
    Scene* m_bakedScene = nullptr;

    /// @brief Source path of `m_bakedScene`, so an in-place reload (same Scene
    ///        object, new file) also refreshes the bake.
    std::string m_bakedScenePath;

    /// @brief Probes loaded from the active scene's `acoustics_index.json`.
    std::vector<LoadedAcousticProbe> m_bakedProbes;

    /// @brief Probe positions, parallel to `m_bakedProbes`, for the nearest-probe
    ///        query (`nearestAcousticProbeIndex`).
    std::vector<glm::vec3> m_bakedProbePositions;
};

} // namespace Vestige
