// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_engine.h
/// @brief OpenAL wrapper for device management, source pooling, and listener control.
#pragma once

#include "audio/audio_attenuation.h"
#include "audio/audio_clip.h"
#include "audio/audio_doppler.h"
#include "audio/audio_hrtf.h"
#include "audio/audio_mixer.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations for OpenAL types (avoid including AL headers in engine headers)
typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;

namespace Vestige
{

/// @brief Wraps OpenAL for audio playback with 3D spatial positioning.
///
/// Manages the audio device, context, a preallocated source pool, and a
/// buffer cache. The listener position is synced to the camera each frame.
/// Fails gracefully if no audio hardware is available.
class AudioEngine
{
public:
    /// @brief Maximum simultaneous audio sources.
    static constexpr int MAX_SOURCES = 32;

    AudioEngine();
    ~AudioEngine();

    // Non-copyable
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// @brief Opens the default audio device and creates an OpenAL context.
    /// @return True if audio is available; false if no device found (non-fatal).
    bool initialize();

    /// @brief Releases all sources, buffers, and destroys the OpenAL context.
    void shutdown();

    /// @brief Updates the listener position and orientation (call once per frame).
    ///
    /// The listener's velocity is read from `getListenerVelocity()`
    /// and uploaded as `AL_VELOCITY` so OpenAL's native Doppler
    /// calculation matches the engine's. Update the velocity via
    /// `setListenerVelocity` before calling this.
    ///
    /// @param position Listener world position (typically camera position).
    /// @param forward Listener forward direction.
    /// @param up Listener up direction.
    void updateListener(const glm::vec3& position, const glm::vec3& forward,
                        const glm::vec3& up);

    /// @brief Sets the listener velocity (m/s) for Doppler-shift
    ///        calculations. The stored value is uploaded to OpenAL
    ///        on the next `updateListener` call.
    void setListenerVelocity(const glm::vec3& velocity) { m_listenerVelocity = velocity; }

    /// @brief Returns the most recently set listener velocity.
    const glm::vec3& getListenerVelocity() const { return m_listenerVelocity; }

    /// @brief Loads an audio file into an OpenAL buffer (cached by path).
    /// @param filePath Path to the audio file.
    /// @return OpenAL buffer ID, or 0 on failure.
    unsigned int loadBuffer(const std::string& filePath);

    /// @brief Acquires a source from the pool.
    /// @return OpenAL source ID, or 0 if pool is exhausted.
    unsigned int acquireSource();

    /// @brief Returns a source to the pool (stops it first).
    /// @param source The OpenAL source ID to release.
    void releaseSource(unsigned int source);

    /// @brief Plays a sound at a 3D position (fire-and-forget).
    /// @param filePath Path to the audio file.
    /// @param position World position of the sound.
    /// @param volume Volume (0.0 to 1.0).
    /// @param loop Whether the sound loops.
    /// @param bus Mixer bus the source is routed through. The effective
    ///            uploaded gain is `resolveSourceGain(mixer, bus, volume)`
    ///            whenever a mixer has been published via
    ///            `setMixerSnapshot` (defaults to the neutral all-1
    ///            mixer otherwise).
    void playSound(const std::string& filePath, const glm::vec3& position,
                   float volume = 1.0f, bool loop = false,
                   AudioBus bus = AudioBus::Sfx);

    /// @brief Plays a spatial sound with explicit attenuation parameters.
    ///
    /// The engine-wide distance model (`setDistanceModel`) determines
    /// which curve OpenAL evaluates; per-source `referenceDistance`
    /// / `maxDistance` / `rolloffFactor` tune that curve.
    void playSoundSpatial(const std::string& filePath,
                          const glm::vec3& position,
                          const AttenuationParams& params,
                          float volume = 1.0f,
                          bool loop = false,
                          AudioBus bus = AudioBus::Sfx);

    /// @brief Plays a spatial sound with attenuation + per-source
    ///        velocity for Doppler shift.
    ///
    /// Velocity is in engine meters per second. Combined with the
    /// listener velocity (`setListenerVelocity`) and the engine-wide
    /// Doppler parameters (`setDopplerFactor`, `setSpeedOfSound`),
    /// OpenAL will apply the pitch shift from the formula in
    /// `audio_doppler.h`.
    void playSoundSpatial(const std::string& filePath,
                          const glm::vec3& position,
                          const glm::vec3& velocity,
                          const AttenuationParams& params,
                          float volume = 1.0f,
                          bool loop = false,
                          AudioBus bus = AudioBus::Sfx);

    /// @brief Plays a non-spatial (2D) sound (fire-and-forget).
    /// @param filePath Path to the audio file.
    /// @param volume Volume (0.0 to 1.0).
    /// @param bus Mixer bus (defaults to `Ui` — 2D sounds are most
    ///            commonly UI clicks / menu accents).
    void playSound2D(const std::string& filePath, float volume = 1.0f,
                     AudioBus bus = AudioBus::Ui);

    /// @brief Phase 10.7 slice A2 — publishes a snapshot of the
    ///        engine-owned mixer into the audio engine. Held by value
    ///        so the audio engine can read it from any thread without
    ///        locking; push once per frame from `AudioSystem::update`.
    void setMixerSnapshot(const AudioMixer& mixer) { m_mixerSnapshot = mixer; }

    /// @brief Per-frame sweep that (a) releases sources whose
    ///        OpenAL state has drifted to `AL_STOPPED` and (b)
    ///        re-uploads the composed `master × bus × sourceVolume`
    ///        gain for every still-playing registered source. Uses
    ///        the most-recent `setMixerSnapshot` value.
    ///
    /// Safe to call when the engine is unavailable — short-circuits
    /// to a no-op. Safe to call when no sources are live — iterates
    /// an empty map.
    void updateGains();

    /// @brief Sets the engine-wide distance-attenuation model.
    ///
    /// Every playing source follows this curve; per-source
    /// `AttenuationParams` tune it. Defaults to `InverseDistance`
    /// (matches OpenAL's own default + the engine's Phase 9C
    /// behaviour, so adopting this API is non-breaking).
    void setDistanceModel(AttenuationModel model);

    /// @brief Returns the engine-wide distance-attenuation model.
    AttenuationModel getDistanceModel() const { return m_distanceModel; }

    /// @brief Sets the Doppler-factor multiplier (`AL_DOPPLER_FACTOR`).
    ///
    /// 0.0 disables Doppler shift; 1.0 matches the canonical formula;
    /// >1.0 exaggerates. Stored in `DopplerParams` for CPU-side
    /// `computeDopplerPitchRatio` and pushed to OpenAL.
    void setDopplerFactor(float factor);

    /// @brief Sets the speed of sound (`AL_SPEED_OF_SOUND`, m/s).
    /// Defaults to 343.3 (dry air, 20 °C).
    void setSpeedOfSound(float speed);

    /// @brief Returns the current Doppler-shift parameters.
    const DopplerParams& getDopplerParams() const { return m_doppler; }

    /// @brief Sets the HRTF activation policy.
    ///
    /// Calls `alcResetDeviceSOFT` to apply the change mid-session
    /// when the ALC_SOFT_HRTF extension is present. Safe to call
    /// before `initialize()` — the value is stored and applied once
    /// the context is live.
    void setHrtfMode(HrtfMode mode);

    /// @brief Sets the preferred HRTF dataset by name.
    ///
    /// Empty string means "driver default" (typically index 0).
    /// The value is validated against `getAvailableHrtfDatasets()`
    /// when applied; an unknown name is stored but ignored at
    /// reset time.
    void setHrtfDataset(const std::string& name);

    /// @brief Returns the current HRTF configuration.
    const HrtfSettings& getHrtfSettings() const { return m_hrtf; }

    /// @brief Queries the driver's current HRTF state.
    ///
    /// Reports what the driver actually decided after the last
    /// device reset. A `Forced` mode may still end up `Denied` if
    /// the output format doesn't support HRTF.
    HrtfStatus getHrtfStatus() const;

    /// @brief Enumerates the HRTF datasets the driver can offer.
    ///
    /// Empty if the extension is unavailable or the driver ships no
    /// datasets. Order is driver-defined; index 0 is the default
    /// `preferredDataset` target when the user hasn't picked one.
    std::vector<std::string> getAvailableHrtfDatasets() const;

    /// @brief Stops all playing sources.
    void stopAll();

    /// @brief Checks if audio is available (device opened successfully).
    bool isAvailable() const { return m_available; }

private:
    ALCdevice* m_device = nullptr;
    ALCcontext* m_context = nullptr;
    bool m_available = false;
    AttenuationModel m_distanceModel = AttenuationModel::InverseDistance;
    DopplerParams m_doppler{};
    glm::vec3 m_listenerVelocity{0.0f};
    HrtfSettings m_hrtf{};

    // ALC_SOFT_HRTF extension function pointers, loaded via
    // alcGetProcAddress after the device is open. Null when the
    // extension is unavailable — every HRTF call short-circuits.
    void* m_alcResetDeviceSOFT = nullptr;
    void* m_alcGetStringiSOFT  = nullptr;

    /// @brief Applies `m_hrtf` to the device via `alcResetDeviceSOFT`.
    ///        Called from `initialize()` and whenever the settings
    ///        change. Silent no-op if the extension is unavailable.
    void applyHrtfSettings();

    // Source pool. Using uint8_t rather than bool because std::vector<bool>
    // is a specialized proxy-reference container (not a true std::vector)
    // and on GCC 15 its resize() triggers a -Warray-bounds false positive
    // in libstdc++ stl_bvector.h.
    std::vector<unsigned int> m_sourcePool;
    std::vector<uint8_t> m_sourceInUse;

    // Phase 10.7 slice A2 — per-source mixer metadata. Keyed by
    // OpenAL source ID, populated by every `playSound*` that
    // acquires a source and cleared by `releaseSource`. The
    // per-frame `updateGains()` sweep reads this map + the
    // published mixer snapshot to re-upload `AL_GAIN`.
    struct SourceMix
    {
        AudioBus bus          = AudioBus::Sfx;
        float    sourceVolume = 1.0f;
    };
    std::unordered_map<unsigned int, SourceMix> m_livePlaybacks;
    AudioMixer m_mixerSnapshot{};  ///< Latest published mixer (defaults all-1).

    // Buffer cache (path -> OpenAL buffer ID)
    std::unordered_map<std::string, unsigned int> m_bufferCache;

    /// @brief Reclaims finished sources back to the pool.
    void reclaimFinishedSources();
};

} // namespace Vestige
