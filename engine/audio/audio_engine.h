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

#include <chrono>
#include <functional>
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
    ///
    /// Lookup order:
    ///  1. First unused slot.
    ///  2. Reclaim finished sources, then re-scan.
    ///  3. Phase 10.9 P7 — priority-gated eviction: build the live
    ///     playbacks into `VoiceCandidate`s and ask
    ///     `chooseVoiceToEvictForIncoming` for a victim. If one
    ///     qualifies (strictly lower priority than `incomingPriority`),
    ///     release it and return that slot. If not, the incoming
    ///     drops — return 0.
    ///
    /// @param incomingPriority Priority of the sound about to start —
    ///        drives the eviction admission gate. Defaults to Normal
    ///        so fire-and-forget `playSound` calls from the editor /
    ///        script graph pick the same tier as generic gameplay SFX.
    /// @return OpenAL source ID, or 0 when the pool is full and no
    ///         existing voice is low-enough-priority to evict.
    unsigned int acquireSource(
        SoundPriority incomingPriority = SoundPriority::Normal);

    /// @brief Returns a source to the pool (stops it first).
    /// @param source The OpenAL source ID to release.
    void releaseSource(unsigned int source);

    /// @brief Plays a sound at a 3D position.
    /// @param filePath Path to the audio file.
    /// @param position World position of the sound.
    /// @param volume Volume (0.0 to 1.0).
    /// @param loop Whether the sound loops.
    /// @param bus Mixer bus the source is routed through. The effective
    ///            uploaded gain is `resolveSourceGain(mixer, bus, volume,
    ///            duckingSnapshot)` whenever a mixer has been published
    ///            via `setMixerSnapshot` (defaults to the neutral all-1
    ///            mixer otherwise).
    /// @param priority Phase 10.9 P7 — tier used by the pool-exhaustion
    ///            eviction gate. When the 32-source pool is full, a
    ///            strictly-higher-priority incoming sound can kick out
    ///            an existing lower-tier voice; equal or lower drops.
    /// @returns The acquired OpenAL source ID, or 0 on failure (no
    ///          audio hardware, buffer load failure, pool exhausted
    ///          with no evictable voice).
    ///          Fire-and-forget callers can discard the return;
    ///          per-frame trackers (Phase 10.9 P2 AudioSystem) store it
    ///          to keep pushing state each frame via `applySourceState`.
    unsigned int playSound(const std::string& filePath, const glm::vec3& position,
                           float volume = 1.0f, bool loop = false,
                           AudioBus bus = AudioBus::Sfx,
                           SoundPriority priority = SoundPriority::Normal);

    /// @brief Plays a spatial sound with explicit attenuation parameters.
    ///
    /// The engine-wide distance model (`setDistanceModel`) determines
    /// which curve OpenAL evaluates; per-source `referenceDistance`
    /// / `maxDistance` / `rolloffFactor` tune that curve.
    /// @param priority Phase 10.9 P7 eviction tier — see `playSound`.
    /// @returns Acquired OpenAL source ID, or 0 on failure.
    unsigned int playSoundSpatial(const std::string& filePath,
                                  const glm::vec3& position,
                                  const AttenuationParams& params,
                                  float volume = 1.0f,
                                  bool loop = false,
                                  AudioBus bus = AudioBus::Sfx,
                                  SoundPriority priority = SoundPriority::Normal);

    /// @brief Plays a spatial sound with attenuation + per-source
    ///        velocity for Doppler shift.
    ///
    /// Velocity is in engine meters per second. Combined with the
    /// listener velocity (`setListenerVelocity`) and the engine-wide
    /// Doppler parameters (`setDopplerFactor`, `setSpeedOfSound`),
    /// OpenAL will apply the pitch shift from the formula in
    /// `audio_doppler.h`.
    /// @param priority Phase 10.9 P7 eviction tier — see `playSound`.
    /// @returns Acquired OpenAL source ID, or 0 on failure.
    unsigned int playSoundSpatial(const std::string& filePath,
                                  const glm::vec3& position,
                                  const glm::vec3& velocity,
                                  const AttenuationParams& params,
                                  float volume = 1.0f,
                                  bool loop = false,
                                  AudioBus bus = AudioBus::Sfx,
                                  SoundPriority priority = SoundPriority::Normal);

    /// @brief Plays a non-spatial (2D) sound.
    /// @param filePath Path to the audio file.
    /// @param volume Volume (0.0 to 1.0).
    /// @param bus Mixer bus (defaults to `Ui` — 2D sounds are most
    ///            commonly UI clicks / menu accents).
    /// @param priority Phase 10.9 P7 eviction tier — see `playSound`.
    /// @returns Acquired OpenAL source ID, or 0 on failure.
    unsigned int playSound2D(const std::string& filePath, float volume = 1.0f,
                             AudioBus bus = AudioBus::Ui,
                             SoundPriority priority = SoundPriority::Normal);

    /// @brief Phase 10.9 P2 — polls the OpenAL state of `source` and
    ///        returns true iff it is currently in `AL_PLAYING`.
    ///        Used by AudioSystem to reap stopped entries from its
    ///        per-entity tracking map. Returns false when the engine
    ///        is unavailable or the source ID is 0.
    bool isSourcePlaying(unsigned int source) const;

    /// @brief Caption-routing callback (Phase 10.9 P4).
    ///
    /// Every `playSound*` overload invokes this at the top of the
    /// function — before the `!m_available` early-return — with the
    /// clip path the caller requested. The engine wires it to
    /// `CaptionMap::enqueueFor(clip, SubtitleQueue&)` at startup, so
    /// captions are announced at source-acquire rather than polled
    /// every frame from somewhere else.
    ///
    /// Firing BEFORE the availability check is deliberate: users with
    /// broken audio hardware, zero-volume output, or deafness /
    /// hearing loss still need the caption when game code *intends*
    /// to play a sound. Captions are the accessibility substitute for
    /// the audio itself, not a side-effect of audio actually reaching
    /// the speakers.
    using CaptionAnnouncer = std::function<void(const std::string& clipPath)>;
    void setCaptionAnnouncer(CaptionAnnouncer announcer)
    {
        m_captionAnnouncer = std::move(announcer);
    }

    /// @brief Phase 10.7 slice A2 — publishes a snapshot of the
    ///        engine-owned mixer into the audio engine. Held by value
    ///        so the audio engine can read it from any thread without
    ///        locking; push once per frame from `AudioSystem::update`.
    void setMixerSnapshot(const AudioMixer& mixer) { m_mixerSnapshot = mixer; }

    /// @brief Phase 10.9 P3 — publishes the engine-owned
    ///        `DuckingState::currentGain` snapshot so `updateGains`
    ///        folds it into every `AL_GAIN` push alongside the mixer.
    ///        Clamped to [0, 1] on ingest so `updateGains` can treat
    ///        the value as canonical.
    void setDuckingSnapshot(float duckingGain);

    /// @brief Most recently published duck snapshot (defaults 1.0).
    ///        Exposed so the editor's AudioPanel preview can mirror
    ///        what the engine is actually pushing to AL.
    float getDuckingSnapshot() const { return m_duckingSnapshot; }

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

    /// @brief Phase 10.9 P2 — pushes a pre-composed
    ///        `AudioSourceAlState` into the given OpenAL source ID.
    ///        Issues the full set of `alSource*` calls for
    ///        position / velocity / pitch / gain / attenuation /
    ///        spatial routing. Safe no-op when the engine is
    ///        unavailable or the source ID is 0.
    void applySourceState(unsigned int source,
                          const struct AudioSourceAlState& state);

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

    // Phase 10.7 slice A2 / Phase 10.9 P7 — per-source mixer metadata.
    // Keyed by OpenAL source ID, populated by every `playSound*` that
    // acquires a source and cleared by `releaseSource`. The
    // per-frame `updateGains()` sweep reads this map + the
    // published mixer snapshot to re-upload `AL_GAIN`. Priority +
    // startTime drive the voice-eviction scoring when the pool is
    // exhausted.
    struct SourceMix
    {
        AudioBus      bus          = AudioBus::Sfx;
        float         sourceVolume = 1.0f;
        SoundPriority priority     = SoundPriority::Normal;
        std::chrono::steady_clock::time_point startTime{};
    };
    std::unordered_map<unsigned int, SourceMix> m_livePlaybacks;
    AudioMixer m_mixerSnapshot{};  ///< Latest published mixer (defaults all-1).
    float      m_duckingSnapshot = 1.0f;  ///< Phase 10.9 P3 duck gain (1.0 = no duck).

    CaptionAnnouncer m_captionAnnouncer;  ///< Phase 10.9 P4 caption hook (may be empty).

    // Buffer cache (path -> OpenAL buffer ID)
    std::unordered_map<std::string, unsigned int> m_bufferCache;

    /// @brief Reclaims finished sources back to the pool.
    void reclaimFinishedSources();
};

} // namespace Vestige
