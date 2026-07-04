// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_engine.h
/// @brief OpenAL wrapper for device management, source pooling, and listener control.
#pragma once

#include "audio/audio_attenuation.h"
#include "audio/audio_clip.h"
#include "audio/audio_device_hotswap.h"
#include "audio/audio_doppler.h"
#include "audio/audio_hrtf.h"
#include "audio/audio_loudness.h"
#include "audio/audio_mixer.h"
#include "audio/audio_output_mode.h"
#include "audio/audio_reverb.h"                     // AX2 ReverbParams
#include "audio/reverb_ir_pool.h"                   // AX2 R2 convolution IR pool
#include "audio/procedural/material_sound_bank.h"  // AX4 S5 synth bank
#include "physics/surface_material.h"              // AX4 S5 SurfaceMaterial

#include <glm/glm.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <list>
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
    /// @param filePath Path to the audio file. Subject to the
    ///                 path-sandbox set by `setSandboxRoots` (Phase 10.9
    ///                 Slice 5 D11) — paths that escape every configured
    ///                 root are rejected before any decoder reads them.
    /// @return OpenAL buffer ID, or 0 on failure.
    unsigned int loadBuffer(const std::string& filePath);

    /// @brief Phase 10.9 Slice 5 D11 — installs the path-sandbox roots
    ///        used by `loadBuffer`. Mirrors `ResourceManager::setSandboxRoots`.
    ///
    /// Empty `roots` (the default) means "no sandbox active" — any
    /// path the caller supplies is forwarded to the decoder. Production
    /// wires the install + project + asset-library roots once at startup;
    /// tests typically leave it empty so fixture paths work as-is.
    void setSandboxRoots(std::vector<std::filesystem::path> roots);

    /// @brief Returns the currently configured sandbox roots.
    const std::vector<std::filesystem::path>& getSandboxRoots() const
    {
        return m_sandboxRoots;
    }

    /// @brief Phase 10.9 W8 — public sandbox check for streaming
    ///        consumers (`AudioMusicPlayer`) that open decoders
    ///        directly instead of going through `loadBuffer`.
    ///
    /// Returns the canonical in-sandbox path, or an empty string when
    /// the path escapes every configured root. With no sandbox active
    /// (empty roots) the path is returned unchanged. Mirrors the gate
    /// `loadBuffer` applies internally — reuses the same `validatePath`.
    std::string resolveSandboxedPath(const std::string& filePath) const
    {
        return validatePath(filePath);
    }

    // -- Buffer cache management (Phase 10.9 Slice 8 W8) -------------------
    //
    // The cache used to be unbounded — every `loadBuffer` insertion lived
    // in `m_bufferCache` for the lifetime of the AudioEngine. W8 adds an
    // LRU recency list + soft entry cap so streaming sound libraries
    // (footstep variants, ambient one-shots) don't grow VRAM/CPU memory
    // forever. Per-scene flush lets a SceneManager drop everything on
    // unload without tearing down the engine.

    /// @brief Sets the buffer-cache entry limit. Default
    ///        `kDefaultBufferCacheLimit`. Tightening retroactively
    ///        evicts from the LRU tail until size ≤ limit.
    void setBufferCacheLimit(size_t maxEntries);

    /// @brief Returns the configured buffer-cache entry limit.
    size_t getBufferCacheLimit() const { return m_bufferCacheLimit; }

    /// @brief Returns the current number of cached buffers.
    size_t getBufferCacheSize() const { return m_bufferCache.size(); }

    /// @brief Releases every cached buffer (`alDeleteBuffers` per entry)
    ///        and clears the recency list. Intended for per-scene
    ///        unload — currently-playing voices keep their already-bound
    ///        buffer handles, so eviction here does NOT cut them off
    ///        mid-playback (OpenAL retains the buffer data for active
    ///        sources). New `loadBuffer` calls re-decode from disk.
    void flushBufferCache();

    /// @brief Default buffer-cache cap. 256 entries comfortably covers
    ///        a level's worth of unique sound files (footstep banks +
    ///        ambient one-shots + UI clips); streaming projects that
    ///        load thousands of unique clips can raise via
    ///        `setBufferCacheLimit`.
    static constexpr size_t kDefaultBufferCacheLimit = 256;

    // -- Loudness normalisation (Phase 10 AX9) ----------------------------
    //
    // EBU R128 / ITU-R BS.1770 per-clip loudness. `loadBuffer` measures
    // each decoded clip's integrated loudness (LUFS) once and caches it
    // parallel to the buffer cache; `loudnessMakeupForPath` turns that into
    // a linear makeup gain toward the current target, which the play paths
    // fold into the per-source volume. Measurement always runs at decode
    // (off the frame path); the enabled flag only gates application, so a
    // runtime toggle takes effect on already-cached clips with no re-decode.

    /// @brief Enables/disables loudness-makeup application. Default on.
    ///        Does not affect measurement — toggling on applies the cached
    ///        per-clip loudness immediately.
    void setLoudnessEnabled(bool enabled) { m_loudnessEnabled = enabled; }

    /// @brief Whether loudness-makeup application is active.
    bool isLoudnessEnabled() const { return m_loudnessEnabled; }

    /// @brief Sets the reference loudness target (LUFS). −16 (game norm,
    ///        the default) … −23 (EBU R128 broadcast / streamer preset).
    void setLoudnessTargetLufs(float lufs) { m_loudnessTargetLufs = lufs; }

    /// @brief Returns the reference loudness target (LUFS).
    float getLoudnessTargetLufs() const { return m_loudnessTargetLufs; }

    /// @brief Linear makeup gain for the clip at @a path: 1.0 when loudness
    ///        is disabled or the path has no cached measurement, else the
    ///        gain that moves the clip's measured loudness toward the
    ///        target (clamped, silence-gated). The play paths multiply this
    ///        into the per-source volume before `resolveSourceGain`.
    float loudnessMakeupForPath(const std::string& path) const;

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

    /// @brief Phase 10.9 Slice 15 Au1 — stops a previously-acquired source
    ///        and returns it to the pool.
    ///
    /// **Looping callers must hold the source ID returned from any
    /// `playSound*` overload and call `stopSound(handle)` to terminate
    /// playback.** A looping sound played without storing the handle
    /// holds a pool slot for the lifetime of the OpenAL context — call
    /// 32 of those and the pool is exhausted, the mixer effectively
    /// frozen until shutdown. The fire-and-forget shape is appropriate
    /// for one-shots; `loop = true` is not a one-shot.
    ///
    /// Passing 0 (the failure return from `playSound*`) is a no-op so
    /// the call is safe even if the caller did not check the return.
    /// Passing a stale or already-released handle is a no-op (`alIsSource`
    /// gate inside the implementation drops it silently).
    void stopSound(unsigned int source) { releaseSource(source); }

    // -- Procedural material-aware audio (AX4 S5) -------------------------
    //
    // The one place synthesis meets the source pool (design §8). `playSynth`
    // turns a (material, approach-speed) request into a fresh 16-bit mono PCM
    // one-shot via the material bank (`material_sound_bank.*`), uploads it into
    // a per-source synth buffer, and plays it as an ordinary positional voice —
    // so LOD / occlusion / air-absorption all apply unchanged. No AX9 LUFS
    // makeup: synth buffers have no path key, and amplitude is already the
    // `impactLoudnessGain` curve baked into the samples.

    /// @brief Loads the procedural-audio material bank from @a path (design §6
    ///        schema). Returns false (built-in fallback retained) on a missing /
    ///        malformed file. Wired once at startup by `Engine`.
    bool loadSynthBank(const std::string& path) { return m_synthBank.loadFromFile(path); }

    /// @brief Read access to the loaded material bank (editor preview / tests).
    const Procedural::MaterialSoundBank& synthBank() const { return m_synthBank; }

    /// @brief Synthesises and plays a material-aware one-shot at @a position.
    /// @param material      Surface struck (selects the bank entry).
    /// @param approachSpeed Contact speed (m/s) — drives the FW loudness / pitch
    ///                      / grain-rate curves. Raw m/s, one energy domain.
    /// @param position      World position of the strike.
    /// @param envelopeScale 1.0 = footstep (bank `durSec`); >1 = longer impact
    ///                      ring (clamped to the synth duration cap).
    /// @param bus           Mixer bus (defaults to `Sfx`).
    /// @param priority      Pool-exhaustion eviction tier — see `playSound`.
    /// @returns The acquired OpenAL source ID, or 0 on failure (no hardware,
    ///          silent strike, pool exhausted, or buffer-upload error).
    unsigned int playSynth(SurfaceMaterial material, float approachSpeed,
                           const glm::vec3& position, float envelopeScale = 1.0f,
                           AudioBus bus = AudioBus::Sfx,
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

    /// @brief Phase 10.7 slice A2 / Phase 10.9 W7 — publishes the
    ///        engine-owned mixer into the audio engine.
    ///
    /// **Lifetime contract**: the pointer must remain valid for the
    /// AudioEngine's lifetime — typically `&Engine::m_audioMixer`, which
    /// outlives the AudioEngine in the destruction order. Pass `nullptr`
    /// to revert to the default all-1 mixer (e.g. in test fixtures or on
    /// teardown).
    ///
    /// W7 replaced the previous per-frame full-struct value-copy: the
    /// mixer is read on the same thread that publishes it (the
    /// `AudioSystem::update` path), so the pointer suffices and the
    /// "snapshot copy keeps it thread-safe" comment that justified the
    /// copy was never actually true — single-threaded today, and a value
    /// copy of `std::array<float, 6>` was not atomic anyway.
    void setMixerSnapshot(const AudioMixer* mixer) { m_mixerSnapshot = mixer; }

    /// @brief Accessor for the published mixer (Phase 10.9 W7).
    /// Returns the snapshot pointer; `nullptr` if the AudioSystem hasn't
    /// pushed one yet. Reads inside `AudioEngine` use the private
    /// `currentMixer()` helper that falls back to a default all-1 mixer.
    const AudioMixer* getMixerSnapshot() const { return m_mixerSnapshot; }

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

    /// @brief AX13 — publishes the per-target-bus duck gains from the
    ///        side-chain router. Each is multiplied on top of the global
    ///        `setDuckingSnapshot` value for sources on that bus, so the
    ///        effective duck is `manualGlobal × routerBus[bus]`. Default
    ///        (all 1.0) reproduces the pre-AX13 single-duck behaviour.
    ///        Values clamped to [0, 1] on ingest.
    void setBusDuckSnapshot(const std::array<float, AudioBusCount>& duck);

    /// @brief The per-bus router-duck snapshot (defaults all 1.0).
    const std::array<float, AudioBusCount>& getBusDuckSnapshot() const
    {
        return m_busDuckSnapshot;
    }

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

    /// @brief AX8 — sets the requested speaker layout (mono / stereo /
    ///        5.1 / 7.1 / auto).
    ///
    /// Applied through the **same** `alcResetDeviceSOFT` path as HRTF
    /// (one reset rebuilds both the HRTF and `ALC_OUTPUT_MODE_SOFT`
    /// attributes). Safe to call before `initialize()` — the value is
    /// stored and applied once the context is live. A no-op when the
    /// layout is unchanged. When the layout cannot be honoured (device
    /// lacks `ALC_SOFT_output_mode`, or HRTF is enabled), OpenAL Soft
    /// falls back to the nearest layout — see `resolveOutputMode`.
    void setOutputLayout(AudioOutputLayout layout);

    /// @brief Returns the currently requested speaker layout.
    AudioOutputLayout getOutputLayout() const { return m_outputLayout; }

    /// @brief AX8 — true iff the live device advertises
    ///        `ALC_SOFT_output_mode` (i.e. 5.1/7.1 can be requested).
    ///        False before `initialize()` or when the extension is
    ///        absent — the settings UI greys the surround options.
    bool isSurroundOutputSupported() const;

    /// @brief AX6 — master toggle for distance-driven air absorption.
    ///
    /// Stored only; read each frame by `AudioSystem` when it fills the
    /// per-frame `AirAbsorptionParams`. Off restores the pre-AX6
    /// gain-only attenuation (no per-source HF rolloff) for users who
    /// find the muffling unwelcome. Does **not** touch the device.
    void setAirAbsorptionEnabled(bool enabled) { m_airAbsorptionEnabled = enabled; }

    /// @brief Returns whether air absorption is enabled (default true).
    bool isAirAbsorptionEnabled() const { return m_airAbsorptionEnabled; }

    /// @brief AX5 — master toggle for the audio level-of-detail ladder.
    ///
    /// Stored only; read each frame by `AudioSystem` to gate the per-source
    /// tier decision. Off keeps every source at `Full` (the pre-AX5
    /// behaviour). Does **not** touch the device.
    void setLodEnabled(bool enabled) { m_lodEnabled = enabled; }

    /// @brief Returns whether the audio LOD ladder is enabled (default true).
    bool isLodEnabled() const { return m_lodEnabled; }

    /// @brief AX1 — geometric audio occlusion settings. Stored only; read each
    ///        frame by `AudioOcclusionSystem` (which owns no settings of its
    ///        own). Off casts no rays and releases every source to unoccluded.
    ///        Do **not** touch the device.
    void setOcclusionEnabled(bool enabled)   { m_occlusionEnabled = enabled; }
    bool isOcclusionEnabled() const          { return m_occlusionEnabled; }
    void setOcclusionRayCount(int count)     { m_occlusionRayCount = count; }
    int  occlusionRayCount() const           { return m_occlusionRayCount; }
    void setOcclusionMaxDistance(float m)    { m_occlusionMaxDistance = m; }
    float occlusionMaxDistance() const       { return m_occlusionMaxDistance; }
    void setOcclusionSourceRadius(float r)   { m_occlusionSourceRadius = r; }
    float occlusionSourceRadius() const      { return m_occlusionSourceRadius; }

    /// @brief AX2 R2 — which OpenAL backend drives the reverb aux slot.
    ///        `Convolution` when the driver advertises the experimental
    ///        `AL_SOFTX_convolution_effect` (IRs loaded via `loadReverbIr` +
    ///        `attachReverbIr`); else `Parametric` (R1's `AL_EFFECT_REVERB`
    ///        driven by `setReverbParams`). Selected once at `initialize()`.
    enum class ReverbBackend { Parametric, Convolution };

    /// @name AX2 reverb (R1 aux-slot + parametric; R2 IR convolution)
    /// @{
    /// @brief Push a `ReverbParams` set onto the parametric `AL_EFFECT_REVERB`
    ///        object and re-attach it to the aux slot. Silent no-op when the
    ///        reverb slot/effect are unavailable (EFX absent or driver-refused).
    ///        R1 drives one engine-wide reverb; the zone-selection layer (R3)
    ///        calls this with the blended per-frame params.
    void setReverbParams(const ReverbParams& params);
    /// @brief Set the reverb aux-slot wet gain in [0, 1] (clamped). 0 (the
    ///        default) is fully dry — the pre-AX2 behaviour. Stored even when
    ///        the slot is unavailable so a getter reflects the request.
    void setReverbWetGain(float gain);
    float reverbWetGain() const   { return m_reverbWetGain; }
    /// @brief True once an EFX aux-effect slot + reverb effect exist and the
    ///        device advertises ≥ 1 auxiliary send. False ⟹ reverb degrades to
    ///        dry and `applySourceState` never sets an aux send.
    bool  isReverbAvailable() const { return m_reverbSlot != 0 && m_maxAuxSends > 0; }
    /// @brief AX2 R2 — the backend chosen at init. Convolution ⟹ IRs;
    ///        Parametric ⟹ `ReverbParams`. Defaults Parametric before init.
    ReverbBackend reverbBackend() const { return m_reverbBackend; }

    /// @brief AX2 R4 — reverb master toggle (default on). When off, the
    ///        `ReverbSystem` gathers no zones, so the slot fades to dry and no
    ///        source carries a reverb send. A stored flag read each frame.
    void setReverbEnabled(bool enabled) { m_reverbEnabled = enabled; }
    bool isReverbEnabled() const        { return m_reverbEnabled; }

    /// @brief AX2 R4 — accessibility ceiling in [0, 1] on any zone's wet gain
    ///        (default 0.5, taming loud convolution IRs). `ReverbSystem` clamps
    ///        its target slot gain to this before slewing. Stored raw here;
    ///        `Settings::validate()` guarantees the [0, 1] range on load.
    void setReverbWetCap(float cap) { m_reverbWetCap = cap; }
    float reverbWetCap() const      { return m_reverbWetCap; }

    /// @brief AX2 R4 — allow the experimental convolution backend (default on).
    ///        Read once at `initialize()` to gate the `AL_SOFTX_convolution_effect`
    ///        probe: off forces the parametric backend. Because the backend is
    ///        selected once at init (a live effect-type swap is out of scope), a
    ///        runtime flip takes effect at the next launch — the boot-time
    ///        `forceLiveApply()` pushes the persisted value before init, so a
    ///        saved preference is always honoured.
    void setReverbConvolutionAllowed(bool allowed) { m_reverbConvolutionAllowed = allowed; }
    bool isReverbConvolutionAllowed() const         { return m_reverbConvolutionAllowed; }
    /// @brief AX2 R2 — decode an impulse-response WAV at @a path into a
    ///        dedicated AL buffer for the convolution backend, cached by path
    ///        in a 64 MB LRU pool (`ReverbIrPool`). Reuses the
    ///        `AudioClip` decoder + the `validatePath` sandbox; deliberately
    ///        skips the clip LRU cache and AX9 loudness (an IR is not a played
    ///        clip). A cache hit returns the existing buffer (promoted to MRU).
    ///        Returns 0 on sandbox rejection, decode failure, upload error, or
    ///        when the engine is unavailable. The returned buffer is NOT
    ///        attached — call `attachReverbIr` (R3 drives that per winning zone).
    unsigned int loadReverbIr(const std::string& path);
    /// @brief AX2 R2 — attach an IR buffer (from `loadReverbIr`) to the aux
    ///        slot as the active convolution response. No-op unless the
    ///        convolution backend is active and the slot is live. The attached
    ///        IR is pinned — exempt from LRU eviction (the extension forbids
    ///        deleting an attached buffer). Pass 0 to detach.
    void attachReverbIr(unsigned int irBuffer);
    /// @brief AX2 R2 — the IR currently attached to the slot (0 = none).
    unsigned int attachedReverbIr() const { return m_reverbIrPool.pinned(); }
    /// @brief AX2 R2 — resident IR-buffer count / their total PCM bytes.
    size_t reverbIrCount() const     { return m_reverbIrPool.count(); }
    size_t reverbIrPoolBytes() const { return m_reverbIrPool.bytes(); }
    /// @brief AX2 R2 — set the IR-pool byte ceiling; tightening evicts LRU-tail
    ///        IRs immediately (never the attached one). Default 64 MB
    ///        (`ReverbIrPool::kDefaultLimitBytes`).
    void   setReverbIrPoolLimitBytes(size_t bytes);
    size_t reverbIrPoolLimitBytes() const { return m_reverbIrPool.limitBytes(); }
    /// @}

    /// @brief AX4 S9 — master toggle for procedural (synthesised) audio:
    ///        footsteps + collision impacts. When off, `playSynth` early-returns
    ///        without synthesising or acquiring a source, muting all procedural
    ///        emission while leaving sample-based sound (and the Sfx bus gain)
    ///        untouched. Stored only — read at the top of `playSynth`.
    void setProceduralAudioEnabled(bool enabled) { m_proceduralAudioEnabled = enabled; }

    /// @brief Returns whether procedural audio is enabled (default true).
    bool isProceduralAudioEnabled() const { return m_proceduralAudioEnabled; }

    /// @brief AX4 S9 — force-enable impact audio for Default↔Default (untagged)
    ///        collisions, off by default so an unauthored scene of untagged
    ///        boxes stays quiet (design §8). Stored only — read by
    ///        `ImpactAudioSystem` when it decides whether to synthesise.
    void setEmitUntaggedCollisions(bool enabled) { m_emitUntaggedCollisions = enabled; }

    /// @brief Returns whether untagged-collision impacts are emitted (default false).
    bool emitUntaggedCollisions() const { return m_emitUntaggedCollisions; }

    /// @brief AX11 — device hot-swap policy: what to do when the OS default
    ///        playback device changes mid-session.
    ///
    /// `Notify` (default) fires the device-change listener so the UI can
    /// offer a "switch?" toast; `Auto` swaps silently; `Off` ignores the
    /// change. Stored only — `pollAndHandleDeviceChange` reads it each frame.
    void setDeviceHotSwapMode(DeviceHotSwapMode mode) { m_deviceHotSwapMode = mode; }

    /// @brief Returns the current device hot-swap policy (default Notify).
    DeviceHotSwapMode getDeviceHotSwapMode() const { return m_deviceHotSwapMode; }

    /// @brief AX11 — listener fired on the main thread (from
    ///        `pollAndHandleDeviceChange`) when the default device changed
    ///        and the policy is `Notify`. The argument is the new default
    ///        device name. The UI shows a toast and, on confirm, calls
    ///        `reopenToDefaultDevice(name)`. May be null (no-op).
    using DeviceChangeListener = std::function<void(const std::string& newDeviceName)>;
    void setDeviceChangeListener(DeviceChangeListener listener)
    {
        m_deviceChangeListener = std::move(listener);
    }

    /// @brief AX11 — main-thread poll for a pending device change.
    ///
    /// Called once per frame at the top of `AudioSystem::update`. If the
    /// OpenAL event thread flagged a default-device change, this clears the
    /// flag and dispatches per `getDeviceHotSwapMode()`: `Off` → ignore;
    /// `Notify` → fire the device-change listener; `Auto` → reopen onto the
    /// new device + re-evaluate HRTF. One relaxed atomic load when idle.
    ///
    /// @return true iff a device swap actually occurred this call.
    bool pollAndHandleDeviceChange();

    /// @brief AX11 — true while a device change detected by the event
    ///        thread is still awaiting a poll. Lets the UI badge a pending
    ///        change; tests assert the callback→main hand-off contract.
    bool isDeviceChangePending() const
    {
        return m_deviceChangePending.load(std::memory_order_acquire);
    }

    /// @brief AX11 — reopen the live device onto @a deviceName (empty =
    ///        driver default) via `alcReopenDeviceSOFT`, keeping the
    ///        context, sources and buffers alive, then re-evaluate HRTF so
    ///        `Auto` HRTF re-detects headphones. The `Notify` confirm path
    ///        calls this directly. A no-op returning false when the engine
    ///        is unavailable or the reopen extension is absent — but HRTF
    ///        re-eval (and its status listener) still fires so the Settings
    ///        UI reflects the change.
    bool reopenToDefaultDevice(const std::string& deviceName);

    /// @brief AX11 — records a default-device change. Normally invoked on
    ///        OpenAL's internal event thread via the engine's event
    ///        trampoline; public for headless testing. Does the minimum
    ///        safe on a foreign thread: stash @a newDeviceName under a mutex
    ///        and set the atomic pending flag. No ALC calls, no engine
    ///        mutation, no allocation beyond the string copy.
    void onDeviceChanged(const std::string& newDeviceName);

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

    /// @brief Phase 10.9 Slice 2 P8 — hook invoked every time the
    ///        engine applies HRTF settings to the driver, i.e. from
    ///        `setHrtfMode`, `setHrtfDataset`, and from `initialize()`
    ///        once the context is live.
    ///
    /// The payload carries the engine's stored request alongside the
    /// driver's resolved status, so a listener can render
    /// "Requested: Forced / Actual: Denied (UnsupportedFormat)" from
    /// a single call — no extra `getHrtfStatus()` round-trip needed.
    ///
    /// Mirrors the `CaptionAnnouncer` pattern: fires even on an
    /// uninitialized engine (`actualStatus = Unknown`) so the
    /// Settings UI can reflect a user's pre-init mode choice the
    /// same way as a post-init device-reset outcome.
    using HrtfStatusListener = std::function<void(const HrtfStatusEvent&)>;
    void setHrtfStatusListener(HrtfStatusListener listener)
    {
        m_hrtfStatusListener = std::move(listener);
    }

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
    AudioOutputLayout m_outputLayout = AudioOutputLayout::Auto;  ///< AX8 speaker layout.

    // ALC_SOFT_HRTF extension function pointers, loaded via
    // alcGetProcAddress after the device is open. Null when the
    // extension is unavailable — every HRTF call short-circuits.
    void* m_alcResetDeviceSOFT = nullptr;
    void* m_alcGetStringiSOFT  = nullptr;

    // AX6 — ALC_EXT_EFX low-pass filter. Function pointers loaded via
    // alGetProcAddress in initialize(); `m_lowPassFilter` is a single
    // reusable AL_FILTER_LOWPASS object whose AL_LOWPASS_GAINHF is
    // rewritten and re-bound (AL_DIRECT_FILTER) per spatial source each
    // frame in applySourceState. Null pointers / a 0 filter mean the
    // extension is absent — the low-pass path is a silent no-op and the
    // engine degrades to gain-only attenuation (pre-AX6 behaviour).
    void*        m_alGenFilters    = nullptr;
    void*        m_alDeleteFilters = nullptr;
    void*        m_alFilteri       = nullptr;
    void*        m_alFilterf       = nullptr;
    unsigned int m_lowPassFilter   = 0;

    // AX2 R1 — ALC_EXT_EFX auxiliary effect slot + one AL_EFFECT_REVERB
    // effect object for engine-wide room reverb. Entry points load via
    // alGetProcAddress inside the same ALC_EXT_EFX gate as the AX6 filter.
    // A null proc / 0 slot / 0 aux-sends means reverb is unavailable — the
    // AL_AUXILIARY_SEND_FILTER path in applySourceState is a silent no-op and
    // every source stays dry (pre-AX2 behaviour). R2 swaps the effect type to
    // the native convolution effect when the experimental extension is present.
    void*        m_alGenAuxiliaryEffectSlots    = nullptr;
    void*        m_alDeleteAuxiliaryEffectSlots = nullptr;
    void*        m_alAuxiliaryEffectSloti       = nullptr;
    void*        m_alAuxiliaryEffectSlotf       = nullptr;
    void*        m_alGenEffects                 = nullptr;
    void*        m_alDeleteEffects              = nullptr;
    void*        m_alEffecti                    = nullptr;
    void*        m_alEffectf                    = nullptr;
    unsigned int m_reverbSlot   = 0;
    unsigned int m_reverbEffect = 0;
    float        m_reverbWetGain = 0.0f;  ///< Slot gain [0,1]; 0 = dry (default).
    int          m_maxAuxSends   = 0;     ///< ALC_MAX_AUXILIARY_SENDS probe.
    ReverbBackend m_reverbBackend = ReverbBackend::Parametric;  ///< AX2 R2 backend.
    bool          m_reverbEnabled  = true;   ///< AX2 R4 master toggle (read by ReverbSystem).
    float         m_reverbWetCap   = 0.5f;   ///< AX2 R4 wet-gain ceiling [0,1].
    bool          m_reverbConvolutionAllowed = true;  ///< AX2 R4 init-time backend gate.

    // AX2 R2 — convolution IR pool: an LRU, byte-bounded store of decoded IR
    // buffers (device-free bookkeeping in `ReverbIrPool`; this class performs
    // the alGenBuffers/alDeleteBuffers around it and pins the attached IR so the
    // extension's "don't delete an attached buffer" rule holds — design §4.1/§7).
    ReverbIrPool m_reverbIrPool;
    bool         m_airAbsorptionEnabled = true;  ///< AX6 master toggle (read by AudioSystem).
    bool         m_lodEnabled           = true;  ///< AX5 LOD-ladder toggle (read by AudioSystem).
    bool         m_proceduralAudioEnabled = true;  ///< AX4 S9 master procedural-audio toggle (gates playSynth).
    bool         m_emitUntaggedCollisions = false; ///< AX4 S9 Default↔Default impact gate (read by ImpactAudioSystem).
    bool         m_occlusionEnabled       = true;   ///< AX1 occlusion master toggle (read by AudioOcclusionSystem).
    int          m_occlusionRayCount      = 8;      ///< AX1 rays/source [1,16].
    float        m_occlusionMaxDistance   = 40.0f;  ///< AX1 cull radius (m).
    float        m_occlusionSourceRadius  = 0.5f;   ///< AX1 sample-sphere radius (m).

    // AX11 — audio device hot-swap. `m_alcReopenDeviceSOFT` is the per-device
    // `ALC_SOFT_reopen_device` entry point; the two event pointers are the
    // device-independent `ALC_SOFT_system_events` controls. Null pointers ⟹
    // the OpenAL Soft build lacks the extension and hot-swap silently
    // disables (logged once at init). `m_deviceEventsActive` records whether
    // the global event callback was registered, so `shutdown` deregisters it
    // before the engine dies (the callback runs on OpenAL's own thread).
    void* m_alcReopenDeviceSOFT  = nullptr;
    void* m_alcEventControlSOFT  = nullptr;
    void* m_alcEventCallbackSOFT = nullptr;
    bool  m_deviceEventsActive   = false;
    DeviceHotSwapMode    m_deviceHotSwapMode = DeviceHotSwapMode::Notify;
    DeviceChangeListener m_deviceChangeListener;  ///< Notify-path UI hook (may be empty).
    std::atomic<bool>    m_deviceChangePending{false};  ///< set by event thread, cleared by poll.
    std::mutex           m_deviceChangeMutex;     ///< guards m_pendingDeviceName (event ↔ main).
    std::string          m_pendingDeviceName;     ///< new default name stashed by the event.

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
    /// Phase 10.9 W7 — pointer to the engine-owned mixer, not a per-frame
    /// value copy. `nullptr` means "no snapshot published yet" and the
    /// reader uses a function-local default-constructed `AudioMixer`.
    const AudioMixer* m_mixerSnapshot = nullptr;

    /// @brief Returns the current mixer (the snapshot pointer if set, or
    /// a default all-1 mixer if `setMixerSnapshot(nullptr)` was the most
    /// recent call). Phase 10.9 W7 helper.
    const AudioMixer& currentMixer() const
    {
        static const AudioMixer kDefault{};
        return m_mixerSnapshot ? *m_mixerSnapshot : kDefault;
    }
    float      m_duckingSnapshot = 1.0f;  ///< Phase 10.9 P3 global manual duck (1.0 = no duck).

    /// @brief AX13 — per-target-bus duck from the side-chain router.
    ///        Multiplied on top of `m_duckingSnapshot` per source bus.
    std::array<float, AudioBusCount> m_busDuckSnapshot{
        {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};

    /// @brief AX13 — effective per-bus duck: global manual × router bus.
    float effectiveDuck(AudioBus bus) const
    {
        return m_duckingSnapshot
             * m_busDuckSnapshot[static_cast<std::size_t>(bus)];
    }

    CaptionAnnouncer m_captionAnnouncer;  ///< Phase 10.9 P4 caption hook (may be empty).

    /// @brief Phase 10.9 Slice 2 P8 — HRTF status-change listener.
    ///        Fired from `applyHrtfSettings()` once per invocation;
    ///        may be null (no-op) when no UI is attached.
    HrtfStatusListener m_hrtfStatusListener;

    // Buffer cache (path -> OpenAL buffer ID).
    // Phase 10.9 Slice 8 W8: parallel `m_bufferOrder` list maintains
    // LRU-recency (front = MRU). `loadBuffer` splices on hit and
    // push_fronts + evicts on insert.
    std::unordered_map<std::string, unsigned int> m_bufferCache;
    std::list<std::string> m_bufferOrder;
    size_t m_bufferCacheLimit = kDefaultBufferCacheLimit;

    /// @brief AX9 — per-clip integrated loudness (LUFS), keyed by the same
    ///        path as `m_bufferCache` and evicted alongside it. Stores the
    ///        intrinsic measured loudness (not a makeup gain) so a target
    ///        change recomputes makeup with no re-measure. Default target
    ///        −16 LUFS (game norm); makeup application enabled by default.
    std::unordered_map<std::string, float> m_loudnessLufs;
    bool  m_loudnessEnabled    = true;
    float m_loudnessTargetLufs = -16.0f;

    /// @brief Phase 10.9 Slice 5 D11 — sandbox roots; populated paths
    ///        force `loadBuffer` to reject paths that don't lie inside
    ///        any of them. Empty = sandbox disabled.
    std::vector<std::filesystem::path> m_sandboxRoots;

    /// @brief Validates @a filePath against `m_sandboxRoots`; returns
    ///        the canonical path (or @a filePath unchanged when the
    ///        sandbox is disabled), or empty string on rejection.
    std::string validatePath(const std::string& filePath) const;

    // -- Procedural material-aware audio (AX4 S5) -------------------------
    //
    // `m_synthBank` holds the per-material synthesis params (loaded from JSON,
    // built-in thud fallback). `m_synthRng` is the single impact-emission
    // generator — fixed seed so a session replays identically (§5c); footstep
    // emitters (S6) own their own generators. `m_synthBuffers` maps an OpenAL
    // source ID to a dedicated synth buffer, generated once on that slot's first
    // synth use and refilled per strike — never per-strike `alGenBuffers`, and
    // never overwritten while attached to a playing source (acquireSource only
    // hands back a slot whose prior buffer was already detached via AL_BUFFER,0).
    // This realises the design's "32-buffer ring, one per source slot" as a
    // source-keyed association. `m_synthScratch` is the reused float→int16 PCM
    // staging buffer (no per-strike heap churn).
    Procedural::MaterialSoundBank m_synthBank;
    std::mt19937 m_synthRng{1337u};
    std::unordered_map<unsigned int, unsigned int> m_synthBuffers;
    std::vector<std::int16_t> m_synthScratch;

    /// @brief Reclaims finished sources back to the pool.
    void reclaimFinishedSources();

    /// @brief Phase 10.9 Slice 8 W8 — pops LRU-tail entries from
    ///        `m_bufferCache` (with `alDeleteBuffers`) until size ≤
    ///        `m_bufferCacheLimit`. Called automatically after every
    ///        `loadBuffer` insert and from `setBufferCacheLimit`.
    void enforceBufferCacheLimit();

    /// @brief AX2 R2 — deletes the evicted IR buffer IDs a pool operation
    ///        returned, then logs if the pool still exceeds its cap because the
    ///        only survivors are the pinned + MRU-front IRs (Rule 5 — no silent
    ///        caps). Central so both load and limit-change paths behave alike.
    void reapEvictedReverbIrs(const std::vector<unsigned int>& evicted);
};

} // namespace Vestige
