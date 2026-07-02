// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_engine.cpp
/// @brief AudioEngine implementation — OpenAL device, source pool, buffer cache.
#include "audio/audio_engine.h"
#include "audio/audio_source_state.h"
#include "core/logger.h"
#include "utils/path_sandbox.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include <algorithm>

namespace Vestige
{

namespace
{

/// @brief AX11 — OpenAL `ALC_SOFT_system_events` callback trampoline.
///
/// Runs on OpenAL's internal event thread. Forwards a default-*playback*-
/// device change to `AudioEngine::onDeviceChanged` via @p userParam (the
/// engine `this`). Ignores every other event type / device type. The
/// minimal-work-on-a-foreign-thread contract lives in `onDeviceChanged`.
void ALC_APIENTRY deviceEventTrampoline(ALCenum eventType, ALCenum deviceType,
                                        ALCdevice* /*device*/, ALCsizei /*length*/,
                                        const ALCchar* message, void* userParam) noexcept
{
    if (eventType != ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT
        || deviceType != ALC_PLAYBACK_DEVICE_SOFT)
    {
        return;
    }
    auto* self = static_cast<AudioEngine*>(userParam);
    if (self == nullptr)
    {
        return;
    }
    // `message` is the new default device's (null-terminated) name.
    self->onDeviceChanged(message != nullptr ? std::string(message)
                                             : std::string());
}

} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    if (m_available)
    {
        shutdown();
    }
}

bool AudioEngine::initialize()
{
    // Open default audio device
    m_device = alcOpenDevice(nullptr);
    if (!m_device)
    {
        Logger::warning("[AudioEngine] No audio device found — audio unavailable");
        m_available = false;
        return false;
    }

    // Create context
    m_context = alcCreateContext(m_device, nullptr);
    if (!m_context || !alcMakeContextCurrent(m_context))
    {
        Logger::warning("[AudioEngine] Failed to create OpenAL context");
        if (m_context)
        {
            alcDestroyContext(m_context);
            m_context = nullptr;
        }
        alcCloseDevice(m_device);
        m_device = nullptr;
        m_available = false;
        return false;
    }

    // Preallocate source pool
    m_sourcePool.resize(MAX_SOURCES);
    m_sourceInUse.assign(MAX_SOURCES, 0u);
    alGenSources(MAX_SOURCES, m_sourcePool.data());

    ALenum err = alGetError();
    if (err != AL_NO_ERROR)
    {
        Logger::warning("[AudioEngine] Failed to allocate source pool");
        m_sourcePool.clear();
        m_sourceInUse.clear();
    }

    // Set default distance model. Keep in sync with m_distanceModel so
    // setDistanceModel() + getDistanceModel() report truth even if the
    // caller never explicitly set it.
    alDistanceModel(static_cast<ALenum>(alDistanceModelFor(m_distanceModel)));

    // Push Doppler defaults so OpenAL's native shift matches the
    // engine's CPU-side computeDopplerPitchRatio from the first frame.
    //
    // AUDIT Au2 — clamp speedOfSound > 0 at the init push. OpenAL rejects
    // `alSpeedOfSound(0)` and silently keeps its internal default (343.3),
    // which would leave OpenAL and our CPU-side `computeDopplerPitchRatio`
    // disagreeing on the first frame if a caller default-constructed a
    // zero `DopplerParams` and assigned it via `setDopplerParams`. Mirror
    // the existing setter clamp here.
    if (m_doppler.speedOfSound <= 0.0f)
    {
        m_doppler.speedOfSound = 1e-3f;
    }
    alDopplerFactor(m_doppler.dopplerFactor);
    alSpeedOfSound(m_doppler.speedOfSound);

    // Load ALC_SOFT_HRTF extension entry points. `alcIsExtensionPresent`
    // gates the lookup so drivers without the extension simply leave
    // the pointers null and every HRTF method short-circuits.
    if (alcIsExtensionPresent(m_device, "ALC_SOFT_HRTF") == ALC_TRUE)
    {
        m_alcResetDeviceSOFT = reinterpret_cast<void*>(
            alcGetProcAddress(m_device, "alcResetDeviceSOFT"));
        m_alcGetStringiSOFT = reinterpret_cast<void*>(
            alcGetProcAddress(m_device, "alcGetStringiSOFT"));
    }

    // AX6 — load ALC_EXT_EFX and create one reusable low-pass filter
    // object for per-source air absorption + occlusion HF damping.
    // The EFX entry points are core-context AL functions, so they load
    // via alGetProcAddress (not the ALC variant). Absent extension or a
    // refused filter ⟹ pointers/filter stay null and the per-source
    // filter path in applySourceState is a silent no-op.
    if (alcIsExtensionPresent(m_device, "ALC_EXT_EFX") == ALC_TRUE)
    {
        m_alGenFilters = reinterpret_cast<void*>(
            alGetProcAddress("alGenFilters"));
        m_alDeleteFilters = reinterpret_cast<void*>(
            alGetProcAddress("alDeleteFilters"));
        m_alFilteri = reinterpret_cast<void*>(
            alGetProcAddress("alFilteri"));
        m_alFilterf = reinterpret_cast<void*>(
            alGetProcAddress("alFilterf"));

        if (m_alGenFilters && m_alDeleteFilters && m_alFilteri && m_alFilterf)
        {
            alGetError();  // clear any stale error before probing
            auto genFilters = reinterpret_cast<LPALGENFILTERS>(m_alGenFilters);
            auto filteri    = reinterpret_cast<LPALFILTERI>(m_alFilteri);
            genFilters(1, &m_lowPassFilter);
            filteri(m_lowPassFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
            if (alGetError() != AL_NO_ERROR || m_lowPassFilter == 0)
            {
                // Driver refused the LOWPASS filter type — fall back to
                // gain-only by dropping the (possibly partial) object.
                if (m_lowPassFilter != 0)
                {
                    auto delFilters =
                        reinterpret_cast<LPALDELETEFILTERS>(m_alDeleteFilters);
                    delFilters(1, &m_lowPassFilter);
                }
                m_lowPassFilter = 0;
            }
        }

        // AX2 R1 — auxiliary effect slot + one parametric AL_EFFECT_REVERB
        // object for engine-wide room reverb. Same graceful-fallback posture
        // as the AX6 filter: a null proc, a 0 slot/effect, or a device with 0
        // auxiliary sends leaves reverb unavailable and every source dry.
        m_alGenAuxiliaryEffectSlots = reinterpret_cast<void*>(
            alGetProcAddress("alGenAuxiliaryEffectSlots"));
        m_alDeleteAuxiliaryEffectSlots = reinterpret_cast<void*>(
            alGetProcAddress("alDeleteAuxiliaryEffectSlots"));
        m_alAuxiliaryEffectSloti = reinterpret_cast<void*>(
            alGetProcAddress("alAuxiliaryEffectSloti"));
        m_alAuxiliaryEffectSlotf = reinterpret_cast<void*>(
            alGetProcAddress("alAuxiliaryEffectSlotf"));
        m_alGenEffects    = reinterpret_cast<void*>(alGetProcAddress("alGenEffects"));
        m_alDeleteEffects = reinterpret_cast<void*>(alGetProcAddress("alDeleteEffects"));
        m_alEffecti       = reinterpret_cast<void*>(alGetProcAddress("alEffecti"));
        m_alEffectf       = reinterpret_cast<void*>(alGetProcAddress("alEffectf"));

        // Auxiliary sends the device advertises; 0 ⟹ no reverb send path.
        alcGetIntegerv(m_device, ALC_MAX_AUXILIARY_SENDS, 1, &m_maxAuxSends);

        const bool reverbProcs = m_alGenAuxiliaryEffectSlots &&
            m_alDeleteAuxiliaryEffectSlots && m_alAuxiliaryEffectSloti &&
            m_alAuxiliaryEffectSlotf && m_alGenEffects && m_alDeleteEffects &&
            m_alEffecti && m_alEffectf;
        if (reverbProcs && m_maxAuxSends > 0)
        {
            alGetError();  // clear any stale error before probing
            auto genSlots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(
                m_alGenAuxiliaryEffectSlots);
            auto genEffects = reinterpret_cast<LPALGENEFFECTS>(m_alGenEffects);
            auto effecti    = reinterpret_cast<LPALEFFECTI>(m_alEffecti);
            genSlots(1, &m_reverbSlot);
            genEffects(1, &m_reverbEffect);
            effecti(m_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
            if (alGetError() != AL_NO_ERROR ||
                m_reverbSlot == 0 || m_reverbEffect == 0)
            {
                // Driver refused the reverb effect — drop the objects and
                // degrade to dry (applySourceState skips the aux-send path).
                if (m_reverbEffect != 0)
                {
                    auto delEffects =
                        reinterpret_cast<LPALDELETEEFFECTS>(m_alDeleteEffects);
                    delEffects(1, &m_reverbEffect);
                }
                if (m_reverbSlot != 0)
                {
                    auto delSlots =
                        reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(
                            m_alDeleteAuxiliaryEffectSlots);
                    delSlots(1, &m_reverbSlot);
                }
                m_reverbSlot   = 0;
                m_reverbEffect = 0;
            }
            else
            {
                // A sane default preset + attach; wet gain starts at 0 so the
                // engine is dry until a zone/setting raises it (R3/R4).
                setReverbParams(reverbPresetParams(ReverbPreset::Generic));
                setReverbWetGain(m_reverbWetGain);
            }
        }
    }

    // AX11 — audio device hot-swap. `ALC_SOFT_reopen_device` is a
    // per-device extension (swap onto a new device without losing the
    // context / sources / buffers); `ALC_SOFT_system_events` is
    // device-independent (queried on a null device) and delivers the
    // default-device-changed event on OpenAL's own thread. Both absent on
    // older OpenAL Soft ⟹ hot-swap silently disables.
    if (alcIsExtensionPresent(m_device, "ALC_SOFT_reopen_device") == ALC_TRUE)
    {
        m_alcReopenDeviceSOFT = reinterpret_cast<void*>(
            alcGetProcAddress(m_device, "alcReopenDeviceSOFT"));
    }
    if (alcIsExtensionPresent(nullptr, "ALC_SOFT_system_events") == ALC_TRUE)
    {
        m_alcEventControlSOFT = reinterpret_cast<void*>(
            alcGetProcAddress(nullptr, "alcEventControlSOFT"));
        m_alcEventCallbackSOFT = reinterpret_cast<void*>(
            alcGetProcAddress(nullptr, "alcEventCallbackSOFT"));
        if (m_alcEventControlSOFT != nullptr && m_alcEventCallbackSOFT != nullptr)
        {
            reinterpret_cast<LPALCEVENTCALLBACKSOFT>(m_alcEventCallbackSOFT)(
                &deviceEventTrampoline, this);
            const ALCenum events[1] = {ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT};
            reinterpret_cast<LPALCEVENTCONTROLSOFT>(m_alcEventControlSOFT)(
                1, events, ALC_TRUE);
            m_deviceEventsActive = true;
        }
    }
    if (m_alcReopenDeviceSOFT == nullptr || !m_deviceEventsActive)
    {
        Logger::info("[AudioEngine] Device hot-swap unavailable "
                     "(ALC_SOFT_reopen_device / ALC_SOFT_system_events absent)");
    }

    // Phase 10.9 Slice 2 P8 — flip to available *before* applying HRTF.
    // `applyHrtfSettings()` guards on `m_available` and on the
    // extension pointer, so setting the flag first lets pre-init
    // `setHrtfMode` / `setHrtfDataset` calls actually reach the driver
    // on startup. The previous order silently short-circuited the
    // first pass.
    m_available = true;

    // Apply the stored HRTF settings so callers that configured the
    // engine before initialize() see their preference honoured.
    applyHrtfSettings();

    Logger::info("[AudioEngine] Initialized (" +
                 std::string(alcGetString(m_device, ALC_DEVICE_SPECIFIER)) +
                 ", " + std::to_string(m_sourcePool.size()) + " sources)");
    return true;
}

void AudioEngine::shutdown()
{
    if (!m_available)
    {
        return;
    }

    // Stop and delete all sources
    for (size_t i = 0; i < m_sourcePool.size(); ++i)
    {
        alSourceStop(m_sourcePool[i]);
        alSourcei(m_sourcePool[i], AL_BUFFER, 0);  // Detach buffer
    }
    if (!m_sourcePool.empty())
    {
        alDeleteSources(static_cast<int>(m_sourcePool.size()), m_sourcePool.data());
    }
    m_sourcePool.clear();
    m_sourceInUse.clear();

    // Delete all cached buffers (W8: also clears the recency list).
    for (auto& [path, buffer] : m_bufferCache)
    {
        alDeleteBuffers(1, &buffer);
    }
    m_bufferCache.clear();
    m_bufferOrder.clear();
    m_loudnessLufs.clear();  // AX9 — measurements are parallel to the cache

    // AX4 S5 — delete the per-source synth buffers. Sources were stopped and
    // had their buffers detached above, so none of these are still attached.
    for (auto& [source, buffer] : m_synthBuffers)
    {
        alDeleteBuffers(1, &buffer);
    }
    m_synthBuffers.clear();

    // AX6 — release the reusable EFX low-pass filter while the context
    // is still current.
    if (m_lowPassFilter != 0 && m_alDeleteFilters)
    {
        reinterpret_cast<LPALDELETEFILTERS>(m_alDeleteFilters)(
            1, &m_lowPassFilter);
        m_lowPassFilter = 0;
    }

    // AX2 R1 — release the reverb aux-effect slot + effect (detach the effect
    // from the slot first) while the context is still current.
    if (m_reverbSlot != 0 && m_alAuxiliaryEffectSloti && m_alDeleteAuxiliaryEffectSlots)
    {
        reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(m_alAuxiliaryEffectSloti)(
            m_reverbSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
        reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(
            m_alDeleteAuxiliaryEffectSlots)(1, &m_reverbSlot);
        m_reverbSlot = 0;
    }
    if (m_reverbEffect != 0 && m_alDeleteEffects)
    {
        reinterpret_cast<LPALDELETEEFFECTS>(m_alDeleteEffects)(
            1, &m_reverbEffect);
        m_reverbEffect = 0;
    }

    // AX11 — deregister the global system-events callback BEFORE tearing
    // the context / device down, so OpenAL's event thread cannot call back
    // into a half-destroyed AudioEngine.
    if (m_deviceEventsActive)
    {
        if (m_alcEventControlSOFT != nullptr)
        {
            const ALCenum events[1] = {ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT};
            reinterpret_cast<LPALCEVENTCONTROLSOFT>(m_alcEventControlSOFT)(
                1, events, ALC_FALSE);
        }
        if (m_alcEventCallbackSOFT != nullptr)
        {
            reinterpret_cast<LPALCEVENTCALLBACKSOFT>(m_alcEventCallbackSOFT)(
                nullptr, nullptr);
        }
        m_deviceEventsActive = false;
    }

    // Destroy context and close device
    alcMakeContextCurrent(nullptr);
    if (m_context)
    {
        alcDestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_device)
    {
        alcCloseDevice(m_device);
        m_device = nullptr;
    }

    m_available = false;
    Logger::info("[AudioEngine] Shut down");
}

void AudioEngine::updateListener(const glm::vec3& position,
                                  const glm::vec3& forward,
                                  const glm::vec3& up)
{
    if (!m_available)
    {
        return;
    }

    alListener3f(AL_POSITION, position.x, position.y, position.z);
    alListener3f(AL_VELOCITY,
                 m_listenerVelocity.x,
                 m_listenerVelocity.y,
                 m_listenerVelocity.z);

    // OpenAL orientation: forward (at) + up as 6 floats
    float orientation[6] = {
        forward.x, forward.y, forward.z,
        up.x, up.y, up.z
    };
    alListenerfv(AL_ORIENTATION, orientation);

    // Reclaim any sources that have finished playing
    reclaimFinishedSources();
}

void AudioEngine::setSandboxRoots(std::vector<std::filesystem::path> roots)
{
    m_sandboxRoots = std::move(roots);
}

std::string AudioEngine::validatePath(const std::string& filePath) const
{
    if (m_sandboxRoots.empty())
        return filePath;  // Sandbox disabled.

    auto canon = PathSandbox::validateInsideRoots(
        std::filesystem::path(filePath), m_sandboxRoots);
    if (canon.empty())
    {
        Logger::warning("[AudioEngine] path rejected (escapes sandbox): " + filePath);
    }
    return canon;
}

unsigned int AudioEngine::loadBuffer(const std::string& filePath)
{
    // Path sandbox (Slice 5 D11): reject before opening file. Run before
    // the m_available short-circuit so callers can't probe paths via the
    // audio API on machines without a device.
    std::string safePath = validatePath(filePath);
    if (safePath.empty())
    {
        return 0;
    }

    if (!m_available)
    {
        return 0;
    }

    // Check cache first
    auto it = m_bufferCache.find(filePath);
    if (it != m_bufferCache.end())
    {
        // Phase 10.9 Slice 8 W8: cache hit — splice key to MRU-front.
        // Linear scan on a list of ≤ kDefaultBufferCacheLimit (256)
        // entries is fine; promoting to a `std::list::iterator` cached
        // alongside the buffer ID would shrink this to O(1) but doubles
        // the per-entry storage for a cache that's tiny in absolute
        // terms.
        auto orderIt = std::find(m_bufferOrder.begin(), m_bufferOrder.end(), filePath);
        if (orderIt != m_bufferOrder.end())
        {
            m_bufferOrder.splice(m_bufferOrder.begin(), m_bufferOrder, orderIt);
        }
        return it->second;
    }

    // Load and decode the audio file
    auto clip = AudioClip::loadFromFile(safePath);
    if (!clip)
    {
        return 0;
    }

    // Create OpenAL buffer and upload PCM data.
    // AUDIT Au3 — initialise to 0 before alGenBuffers. If gen fails (very
    // rare, but possible under context loss / OOM), `buffer` would otherwise
    // hold an indeterminate ID and the alDeleteBuffers cleanup path below
    // could ask the driver to delete a garbage handle.
    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer,
                 static_cast<ALenum>(clip->getALFormat()),
                 clip->getSamples().data(),
                 static_cast<ALsizei>(clip->getDataSizeBytes()),
                 static_cast<ALsizei>(clip->getSampleRate()));

    ALenum err = alGetError();
    if (err != AL_NO_ERROR)
    {
        Logger::error("[AudioEngine] Failed to upload buffer: " + filePath);
        alDeleteBuffers(1, &buffer);
        return 0;
    }

    // AX9 — measure integrated loudness once per clip (off the frame path),
    // keyed by the same path as the buffer cache. Stored as the intrinsic
    // LUFS (not a makeup gain) so a runtime target-LUFS change recomputes
    // makeup with no re-measure. Runs regardless of m_loudnessEnabled so
    // toggling the feature on applies to already-cached clips immediately.
    m_loudnessLufs[filePath] = integratedLoudnessLufs(
        clip->getSamples().data(),
        static_cast<std::size_t>(clip->getFrameCount()),
        static_cast<int>(clip->getChannels()),
        static_cast<int>(clip->getSampleRate()));

    // Phase 10.9 Slice 8 W8: insert at MRU-front, then evict from the
    // LRU tail until cache size is at-or-below the configured cap.
    // Unlike ResourceManager, we don't have a use_count gate because
    // OpenAL buffer handles are unsigned ints, not shared_ptrs — but
    // dropping a cache entry while a source is still bound is safe:
    // alSourcePlay holds a reference to the buffer and the data
    // survives until the source releases it. Future `loadBuffer`
    // calls will re-decode and create a fresh OpenAL buffer, which
    // is the same trade ResourceManager's eviction makes (re-decode
    // beats unbounded growth).
    m_bufferCache[filePath] = buffer;
    m_bufferOrder.push_front(filePath);
    enforceBufferCacheLimit();
    return buffer;
}

void AudioEngine::enforceBufferCacheLimit()
{
    while (m_bufferCache.size() > m_bufferCacheLimit && !m_bufferOrder.empty())
    {
        const std::string victim = m_bufferOrder.back();
        m_bufferOrder.pop_back();
        m_loudnessLufs.erase(victim);  // AX9 — keep the loudness map bounded
        auto it = m_bufferCache.find(victim);
        if (it != m_bufferCache.end())
        {
            ALuint buffer = it->second;
            if (m_available && buffer != 0)
            {
                alDeleteBuffers(1, &buffer);
            }
            m_bufferCache.erase(it);
        }
    }
}

void AudioEngine::setBufferCacheLimit(size_t maxEntries)
{
    m_bufferCacheLimit = maxEntries;
    enforceBufferCacheLimit();
}

void AudioEngine::flushBufferCache()
{
    // Phase 10.9 Slice 8 W8: per-scene flush — release every cached
    // buffer's OpenAL handle and clear the recency list. Active voices
    // keep their bound buffer's data alive until they finish (OpenAL
    // semantics), so this is safe to call mid-frame between scenes.
    for (auto& [path, buffer] : m_bufferCache)
    {
        if (m_available && buffer != 0)
        {
            alDeleteBuffers(1, &buffer);
        }
    }
    m_bufferCache.clear();
    m_bufferOrder.clear();
    m_loudnessLufs.clear();  // AX9 — measurements are parallel to the cache
}

float AudioEngine::loudnessMakeupForPath(const std::string& path) const
{
    // AX9 — disabled, or never measured (e.g. a streamed clip that never
    // went through loadBuffer) → unity, leaving the source volume untouched.
    if (!m_loudnessEnabled)
    {
        return 1.0f;
    }
    auto it = m_loudnessLufs.find(path);
    if (it == m_loudnessLufs.end())
    {
        return 1.0f;
    }
    // Makeup is derived from the stored intrinsic LUFS against the *current*
    // target, so a runtime target change is reflected without re-measuring.
    return loudnessMakeupGain(it->second, m_loudnessTargetLufs);
}

unsigned int AudioEngine::acquireSource(SoundPriority incomingPriority)
{
    if (!m_available)
    {
        return 0;
    }

    for (size_t i = 0; i < m_sourcePool.size(); ++i)
    {
        if (!m_sourceInUse[i])
        {
            m_sourceInUse[i] = true;
            return m_sourcePool[i];
        }
    }

    // Pool exhausted — try reclaiming finished sources
    reclaimFinishedSources();
    for (size_t i = 0; i < m_sourcePool.size(); ++i)
    {
        if (!m_sourceInUse[i])
        {
            m_sourceInUse[i] = true;
            return m_sourcePool[i];
        }
    }

    // Phase 10.9 P7 — still no slot. Build VoiceCandidates from live
    // playbacks and ask the admission-controlled evictor. A victim is
    // returned only if its priority tier is strictly lower than the
    // incoming; otherwise the incoming drops.
    const auto now = std::chrono::steady_clock::now();
    std::vector<VoiceCandidate> candidates;
    std::vector<unsigned int>   candidateSources;
    candidates.reserve(m_livePlaybacks.size());
    candidateSources.reserve(m_livePlaybacks.size());
    for (const auto& [source, mix] : m_livePlaybacks)
    {
        VoiceCandidate c;
        c.priority      = mix.priority;
        c.effectiveGain = resolveSourceGain(
            currentMixer(), mix.bus, mix.sourceVolume, effectiveDuck(mix.bus));
        c.ageSeconds = std::chrono::duration<float>(now - mix.startTime).count();
        candidates.push_back(c);
        candidateSources.push_back(source);
    }

    const std::size_t victimIdx =
        chooseVoiceToEvictForIncoming(candidates, incomingPriority);
    if (victimIdx != static_cast<std::size_t>(-1))
    {
        const unsigned int victimSource = candidateSources[victimIdx];
        releaseSource(victimSource);  // stops the voice, frees pool slot
        for (size_t i = 0; i < m_sourcePool.size(); ++i)
        {
            if (!m_sourceInUse[i])
            {
                m_sourceInUse[i] = true;
                return m_sourcePool[i];
            }
        }
        // Defensive: releaseSource must have freed a slot, so reaching
        // here means the pool vector and m_sourceInUse got out of sync.
        Logger::warning("[AudioEngine] Eviction freed no slot (pool state desynchronised)");
        return 0;
    }

    Logger::warning("[AudioEngine] Source pool exhausted — incoming priority too low to evict");
    return 0;
}

void AudioEngine::releaseSource(unsigned int source)
{
    for (size_t i = 0; i < m_sourcePool.size(); ++i)
    {
        if (m_sourcePool[i] == source)
        {
            alSourceStop(source);
            alSourcei(source, AL_BUFFER, 0);
            m_sourceInUse[i] = false;
            m_livePlaybacks.erase(source);
            return;
        }
    }
}

unsigned int AudioEngine::playSound(const std::string& filePath, const glm::vec3& position,
                                     float volume, bool loop, AudioBus bus,
                                     SoundPriority priority)
{
    // Phase 10.9 P4: fire the caption announcer BEFORE the availability
    // check — a user with broken audio hardware / deafness still needs
    // the caption when game code intends to play a sound.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available)
    {
        return 0;
    }

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0)
    {
        return 0;
    }

    ALuint source = acquireSource(priority);
    if (source == 0)
    {
        return 0;
    }

    // AX9 — fold the per-clip loudness makeup gain into the source volume
    // before it is stored / resolved, so updateGains and the eviction
    // candidate scan reuse it. Returns 1.0 (no-op) when loudness is disabled
    // or the clip has no cached measurement.
    volume *= loudnessMakeupForPath(filePath);
    m_livePlaybacks[source] = SourceMix{
        bus, volume, priority, std::chrono::steady_clock::now()};
    const float initialGain =
        resolveSourceGain(
            currentMixer(), bus, volume, effectiveDuck(bus));

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
    alSourcef(source, AL_MAX_DISTANCE, 50.0f);
    alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);  // 3D positioned
    alSourcePlay(source);
    return source;
}

unsigned int AudioEngine::playSoundSpatial(const std::string& filePath,
                                           const glm::vec3& position,
                                           const AttenuationParams& params,
                                           float volume,
                                           bool loop,
                                           AudioBus bus,
                                           SoundPriority priority)
{
    // Phase 10.9 P4 — see playSound() above for rationale.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available) return 0;

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0) return 0;

    ALuint source = acquireSource(priority);
    if (source == 0) return 0;

    // AX9 — fold the per-clip loudness makeup gain into the source volume
    // before it is stored / resolved, so updateGains and the eviction
    // candidate scan reuse it. Returns 1.0 (no-op) when loudness is disabled
    // or the clip has no cached measurement.
    volume *= loudnessMakeupForPath(filePath);
    m_livePlaybacks[source] = SourceMix{
        bus, volume, priority, std::chrono::steady_clock::now()};
    const float initialGain =
        resolveSourceGain(
            currentMixer(), bus, volume, effectiveDuck(bus));

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(source, AL_REFERENCE_DISTANCE, params.referenceDistance);
    alSourcef(source, AL_MAX_DISTANCE,       params.maxDistance);
    alSourcef(source, AL_ROLLOFF_FACTOR,     params.rolloffFactor);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSourcePlay(source);
    return source;
}

unsigned int AudioEngine::playSoundSpatial(const std::string& filePath,
                                           const glm::vec3& position,
                                           const glm::vec3& velocity,
                                           const AttenuationParams& params,
                                           float volume,
                                           bool loop,
                                           AudioBus bus,
                                           SoundPriority priority)
{
    // Phase 10.9 P4 — see playSound() above for rationale.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available) return 0;

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0) return 0;

    ALuint source = acquireSource(priority);
    if (source == 0) return 0;

    // AX9 — fold the per-clip loudness makeup gain into the source volume
    // before it is stored / resolved, so updateGains and the eviction
    // candidate scan reuse it. Returns 1.0 (no-op) when loudness is disabled
    // or the clip has no cached measurement.
    volume *= loudnessMakeupForPath(filePath);
    m_livePlaybacks[source] = SourceMix{
        bus, volume, priority, std::chrono::steady_clock::now()};
    const float initialGain =
        resolveSourceGain(
            currentMixer(), bus, volume, effectiveDuck(bus));

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSource3f(source, AL_VELOCITY, velocity.x, velocity.y, velocity.z);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(source, AL_REFERENCE_DISTANCE, params.referenceDistance);
    alSourcef(source, AL_MAX_DISTANCE,       params.maxDistance);
    alSourcef(source, AL_ROLLOFF_FACTOR,     params.rolloffFactor);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSourcePlay(source);
    return source;
}

unsigned int AudioEngine::playSound2D(const std::string& filePath, float volume,
                                       AudioBus bus,
                                       SoundPriority priority)
{
    // Phase 10.9 P4 — see playSound() above for rationale.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available)
    {
        return 0;
    }

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0)
    {
        return 0;
    }

    ALuint source = acquireSource(priority);
    if (source == 0)
    {
        return 0;
    }

    // AX9 — fold the per-clip loudness makeup gain into the source volume
    // before it is stored / resolved, so updateGains and the eviction
    // candidate scan reuse it. Returns 1.0 (no-op) when loudness is disabled
    // or the clip has no cached measurement.
    volume *= loudnessMakeupForPath(filePath);
    m_livePlaybacks[source] = SourceMix{
        bus, volume, priority, std::chrono::steady_clock::now()};
    const float initialGain =
        resolveSourceGain(
            currentMixer(), bus, volume, effectiveDuck(bus));

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, AL_FALSE);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);  // Relative to listener (2D)
    alSourcePlay(source);
    return source;
}

unsigned int AudioEngine::playSynth(SurfaceMaterial material, float approachSpeed,
                                    const glm::vec3& position, float envelopeScale,
                                    AudioBus bus, SoundPriority priority)
{
    // AX4 S9 — master procedural-audio gate. Off mutes every synthesised
    // footstep/impact (both route through here) without touching sample
    // playback or the Sfx bus gain.
    if (!m_available || !m_proceduralAudioEnabled)
    {
        return 0;
    }

    // Synthesise into the reused staging buffer. The injected uniform draws from
    // the engine's single impact-emission RNG (fixed seed → replayable, §5c);
    // PhISEM clamps it away from 0 internally.
    std::uniform_real_distribution<float> dist(1e-6f, 1.0f);
    auto sample = [this, &dist]() { return dist(m_synthRng); };
    const std::size_t produced =
        m_synthBank.synthesize(material, approachSpeed, envelopeScale, sample, m_synthScratch);
    if (produced == 0 || m_synthScratch.empty())
    {
        return 0;  // silent strike (no modes / zero rate) — nothing to play.
    }

    ALuint source = acquireSource(priority);
    if (source == 0)
    {
        return 0;
    }

    // Per-source dedicated synth buffer: generated once on this slot's first
    // synth use, reused thereafter. Refilling is safe because acquireSource
    // only returns a slot whose prior buffer was detached (AL_BUFFER, 0) by
    // releaseSource / reclaimFinishedSources, so it is not attached to any
    // playing source.
    ALuint buffer = 0;
    auto it = m_synthBuffers.find(source);
    if (it == m_synthBuffers.end())
    {
        alGenBuffers(1, &buffer);
        if (buffer == 0)
        {
            releaseSource(source);
            return 0;
        }
        m_synthBuffers[source] = buffer;
    }
    else
    {
        buffer = it->second;
    }

    alBufferData(buffer, AL_FORMAT_MONO16, m_synthScratch.data(),
                 static_cast<ALsizei>(m_synthScratch.size() * sizeof(std::int16_t)),
                 static_cast<ALsizei>(Procedural::kSynthSampleRate));
    if (alGetError() != AL_NO_ERROR)
    {
        Logger::error("[AudioEngine] Failed to upload synth buffer");
        releaseSource(source);
        return 0;
    }

    // Amplitude is the impactLoudnessGain curve baked into the samples, so the
    // per-source volume is unity (no AX9 LUFS makeup — synth has no path key).
    const float volume = 1.0f;
    m_livePlaybacks[source] = SourceMix{
        bus, volume, priority, std::chrono::steady_clock::now()};
    const float initialGain =
        resolveSourceGain(currentMixer(), bus, volume, effectiveDuck(bus));

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, AL_FALSE);
    alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
    alSourcef(source, AL_MAX_DISTANCE, 50.0f);
    alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);  // 3D positioned
    alSourcePlay(source);
    return source;
}

void AudioEngine::setDistanceModel(AttenuationModel model)
{
    m_distanceModel = model;
    if (m_available)
    {
        alDistanceModel(static_cast<ALenum>(alDistanceModelFor(model)));
    }
}

void AudioEngine::setDopplerFactor(float factor)
{
    // Clamp negatives — OpenAL rejects them with AL_INVALID_VALUE
    // and our own pure-function math returns unity for factor <= 0
    // anyway, so normalise to 0.0 (Doppler disabled) at the boundary.
    m_doppler.dopplerFactor = (factor < 0.0f) ? 0.0f : factor;
    if (m_available)
    {
        alDopplerFactor(m_doppler.dopplerFactor);
    }
}

void AudioEngine::setSpeedOfSound(float speed)
{
    // Speed must be strictly positive; clamp to a small epsilon so
    // OpenAL doesn't throw AL_INVALID_VALUE and our formula stays
    // well-defined. Values <= 0 would also trip the pure-function
    // guard in computeDopplerPitchRatio.
    m_doppler.speedOfSound = (speed > 0.0f) ? speed : 1e-3f;
    if (m_available)
    {
        alSpeedOfSound(m_doppler.speedOfSound);
    }
}

void AudioEngine::stopAll()
{
    if (!m_available)
    {
        return;
    }

    for (size_t i = 0; i < m_sourcePool.size(); ++i)
    {
        if (m_sourceInUse[i])
        {
            alSourceStop(m_sourcePool[i]);
            alSourcei(m_sourcePool[i], AL_BUFFER, 0);
            m_sourceInUse[i] = false;
        }
        m_livePlaybacks.erase(m_sourcePool[i]);
    }
}

void AudioEngine::setHrtfMode(HrtfMode mode)
{
    if (m_hrtf.mode == mode)
    {
        return;
    }
    m_hrtf.mode = mode;
    applyHrtfSettings();
}

void AudioEngine::setHrtfDataset(const std::string& name)
{
    if (m_hrtf.preferredDataset == name)
    {
        return;
    }
    m_hrtf.preferredDataset = name;
    applyHrtfSettings();
}

void AudioEngine::setOutputLayout(AudioOutputLayout layout)
{
    if (m_outputLayout == layout)
    {
        return;
    }
    m_outputLayout = layout;
    // AX8 — the speaker layout rides the same device-reset path as HRTF;
    // applyHrtfSettings() rebuilds both attributes in a single reset.
    applyHrtfSettings();
}

bool AudioEngine::isSurroundOutputSupported() const
{
    return m_available && m_device != nullptr
        && alcIsExtensionPresent(m_device, "ALC_SOFT_output_mode") == ALC_TRUE;
}

HrtfStatus AudioEngine::getHrtfStatus() const
{
    if (!m_available || m_alcResetDeviceSOFT == nullptr)
    {
        return HrtfStatus::Unknown;
    }
    ALCint status = ALC_HRTF_DISABLED_SOFT;
    alcGetIntegerv(m_device, ALC_HRTF_STATUS_SOFT, 1, &status);
    switch (status)
    {
        case ALC_HRTF_DISABLED_SOFT:            return HrtfStatus::Disabled;
        case ALC_HRTF_ENABLED_SOFT:             return HrtfStatus::Enabled;
        case ALC_HRTF_DENIED_SOFT:              return HrtfStatus::Denied;
        case ALC_HRTF_REQUIRED_SOFT:            return HrtfStatus::Required;
        case ALC_HRTF_HEADPHONES_DETECTED_SOFT: return HrtfStatus::HeadphonesDetected;
        case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT:  return HrtfStatus::UnsupportedFormat;
        default:                                return HrtfStatus::Unknown;
    }
}

std::vector<std::string> AudioEngine::getAvailableHrtfDatasets() const
{
    std::vector<std::string> names;
    if (!m_available || m_alcGetStringiSOFT == nullptr)
    {
        return names;
    }
    ALCint count = 0;
    alcGetIntegerv(m_device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &count);
    if (count <= 0)
    {
        return names;
    }
    auto getStringi =
        reinterpret_cast<LPALCGETSTRINGISOFT>(m_alcGetStringiSOFT);
    names.reserve(static_cast<size_t>(count));
    for (ALCint i = 0; i < count; ++i)
    {
        const ALCchar* name =
            getStringi(m_device, ALC_HRTF_SPECIFIER_SOFT, i);
        names.emplace_back(name ? name : "");
    }
    return names;
}

void AudioEngine::applyHrtfSettings()
{
    // Phase 10.9 Slice 2 P8 — device-reset path is gated on a live
    // device + the ALC_SOFT_HRTF extension, but the event listener
    // fires unconditionally so the Settings UI can reflect pre-init
    // user choices too (in which case `actualStatus` is Unknown).
    if (m_available && m_alcResetDeviceSOFT != nullptr)
    {
        // Build an ALC attribute list that instructs the driver how to
        // treat HRTF on the next context reset. Auto mode omits the
        // attribute entirely — the driver's own heuristics (headphone
        // detection, output-format check) then apply.
        //
        // AX8 — the same reset also carries the ALC_OUTPUT_MODE_SOFT
        // (speaker layout) attribute when the extension is present, so a
        // layout change and an HRTF change share one reset (no double
        // reset). Capacity: HRTF (2) + HRTF_ID (2) + output mode (2) +
        // terminator (1) = 7.
        ALCint attrs[7] = {0, 0, 0, 0, 0, 0, 0};
        int n = 0;
        switch (m_hrtf.mode)
        {
            case HrtfMode::Disabled:
                attrs[n++] = ALC_HRTF_SOFT;
                attrs[n++] = ALC_FALSE;
                break;
            case HrtfMode::Forced:
                attrs[n++] = ALC_HRTF_SOFT;
                attrs[n++] = ALC_TRUE;
                break;
            case HrtfMode::Auto:
                // Leave ALC_HRTF_SOFT unset so the driver's own auto
                // detection path runs. An explicit dataset may still
                // follow below.
                break;
        }

        if (!m_hrtf.preferredDataset.empty())
        {
            const auto available = getAvailableHrtfDatasets();
            const int idx = resolveHrtfDatasetIndex(available, m_hrtf.preferredDataset);
            if (idx >= 0)
            {
                attrs[n++] = ALC_HRTF_ID_SOFT;
                attrs[n++] = idx;
            }
        }

        // AX8 — speaker layout. Only request it when the device
        // advertises ALC_SOFT_output_mode; otherwise leave it unset and
        // the driver keeps its default downmix (graceful degrade to
        // today's behaviour). resolveOutputMode honours the HRTF
        // precedence rule (HRTF on => ALC_ANY_SOFT, never surround).
        if (alcIsExtensionPresent(m_device, "ALC_SOFT_output_mode") == ALC_TRUE)
        {
            const bool hrtfEnabledSetting = (m_hrtf.mode != HrtfMode::Disabled);
            attrs[n++] = ALC_OUTPUT_MODE_SOFT;
            attrs[n++] = resolveOutputMode(m_outputLayout, hrtfEnabledSetting);
        }

        attrs[n] = 0;  // ALC attribute list terminator

        auto resetDevice =
            reinterpret_cast<LPALCRESETDEVICESOFT>(m_alcResetDeviceSOFT);
        if (resetDevice(m_device, attrs) != ALC_TRUE)
        {
            Logger::warning("[AudioEngine] alcResetDeviceSOFT failed — HRTF settings may not be applied");
        }
    }

    if (m_hrtfStatusListener)
    {
        m_hrtfStatusListener(composeHrtfStatusEvent(m_hrtf, getHrtfStatus()));
    }
}

void AudioEngine::onDeviceChanged(const std::string& newDeviceName)
{
    // Runs on OpenAL's event thread (or a test thread). Minimal work: stash
    // the name under the mutex and flag the main thread. No ALC calls, no
    // engine mutation beyond these two cross-thread fields.
    {
        std::lock_guard<std::mutex> lock(m_deviceChangeMutex);
        m_pendingDeviceName = newDeviceName;
    }
    m_deviceChangePending.store(true, std::memory_order_release);
}

bool AudioEngine::pollAndHandleDeviceChange()
{
    // Idle fast path: a single relaxed-acquire exchange per frame. The
    // exchange also clears the flag so a change is handled exactly once.
    if (!m_deviceChangePending.exchange(false, std::memory_order_acquire))
    {
        return false;
    }

    std::string name;
    {
        std::lock_guard<std::mutex> lock(m_deviceChangeMutex);
        name = m_pendingDeviceName;
    }

    switch (decideDeviceSwapAction(m_deviceHotSwapMode))
    {
        case DeviceSwapAction::Ignore:
            return false;
        case DeviceSwapAction::Notify:
            if (m_deviceChangeListener)
            {
                m_deviceChangeListener(name);
            }
            return false;
        case DeviceSwapAction::Swap:
            return reopenToDefaultDevice(name);
    }
    return false;
}

bool AudioEngine::reopenToDefaultDevice(const std::string& deviceName)
{
    bool swapped = false;
    if (m_available && m_alcReopenDeviceSOFT != nullptr && m_device != nullptr)
    {
        auto reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(m_alcReopenDeviceSOFT);
        const ALCchar* name = deviceName.empty() ? nullptr : deviceName.c_str();
        if (reopen(m_device, name, nullptr) == ALC_TRUE)
        {
            swapped = true;
            Logger::info("[AudioEngine] Reopened audio device" +
                         (deviceName.empty() ? std::string(" (driver default)")
                                             : ": " + deviceName));
        }
        else
        {
            Logger::warning(
                "[AudioEngine] alcReopenDeviceSOFT failed — device unchanged");
        }
    }

    // Re-evaluate HRTF after the change so `Auto` re-detects headphones and
    // the surround layout is re-applied on the (possibly new) device.
    // applyHrtfSettings fires the HRTF status listener unconditionally — even
    // with no live device — so the re-eval is observable headlessly; the
    // actual device reset inside it is gated on the extension being present.
    applyHrtfSettings();
    return swapped;
}

void AudioEngine::setDuckingSnapshot(float duckingGain)
{
    // Phase 10.9 P3: clamp on ingest so every downstream reader sees
    // a canonical [0, 1] value and doesn't need to re-clamp.
    m_duckingSnapshot = std::max(0.0f, std::min(1.0f, duckingGain));
}

void AudioEngine::setBusDuckSnapshot(
    const std::array<float, AudioBusCount>& duck)
{
    // AX13: clamp each per-bus router duck on ingest, mirroring the
    // global snapshot's canonical-[0,1] contract.
    for (std::size_t i = 0; i < AudioBusCount; ++i)
    {
        m_busDuckSnapshot[i] = std::max(0.0f, std::min(1.0f, duck[i]));
    }
}

bool AudioEngine::isSourcePlaying(unsigned int source) const
{
    if (!m_available || source == 0)
    {
        return false;
    }
    ALint state = AL_INITIAL;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

void AudioEngine::applySourceState(unsigned int source,
                                   const AudioSourceAlState& state)
{
    // Phase 10.9 P2: per-frame AL state push for component-driven
    // sources. Same call shape as playSoundSpatial's initial
    // upload but issued every frame so edits to the component
    // (pitch slider, moving velocity, changing material) are heard
    // immediately rather than only on next playback.
    if (!m_available || source == 0)
    {
        return;
    }
    alSource3f(source, AL_POSITION,
               state.position.x, state.position.y, state.position.z);
    alSource3f(source, AL_VELOCITY,
               state.velocity.x, state.velocity.y, state.velocity.z);
    alSourcef(source, AL_PITCH,               state.pitch);
    alSourcef(source, AL_GAIN,                state.gain);
    alSourcef(source, AL_REFERENCE_DISTANCE,  state.referenceDistance);
    alSourcef(source, AL_MAX_DISTANCE,        state.maxDistance);
    alSourcef(source, AL_ROLLOFF_FACTOR,      state.rolloffFactor);
    alSourcei(source, AL_SOURCE_RELATIVE,
              state.spatial ? AL_FALSE : AL_TRUE);

    // AX6 — per-source high-frequency damping via the EFX direct
    // low-pass filter. Binding AL_DIRECT_FILTER copies the filter's
    // *current* params into the source, so we rewrite GAINHF and
    // re-bind every frame. Skipped for 2D/non-spatial sources (distance
    // is meaningless) and when EFX is unavailable. A gainHf at unity
    // means "no damping" → clear any previously-bound filter, since
    // pool sources are reused and a stale filter must not linger.
    if (m_lowPassFilter != 0)
    {
        const float hf = std::clamp(state.lowPassGainHf, 0.0f, 1.0f);
        if (state.spatial && hf < 0.9999f)
        {
            auto filterf = reinterpret_cast<LPALFILTERF>(m_alFilterf);
            filterf(m_lowPassFilter, AL_LOWPASS_GAIN,   1.0f);  // broadband: AL_GAIN already set
            filterf(m_lowPassFilter, AL_LOWPASS_GAINHF, hf);
            alSourcei(source, AL_DIRECT_FILTER,
                      static_cast<ALint>(m_lowPassFilter));
        }
        else
        {
            alSourcei(source, AL_DIRECT_FILTER, AL_FILTER_NULL);
        }
    }

    // AX2 R1 — route this source into the reverb aux-effect slot when it has
    // a send and the slot is live. Pool sources are reused, so a source that
    // is now dry (or 2D) must be cleared to AL_EFFECTSLOT_NULL or it would
    // linger on the slot from a previous owner. Send 0 is the only send used
    // in R1 (one engine-wide reverb).
    if (m_reverbSlot != 0 && m_maxAuxSends > 0)
    {
        if (state.spatial && state.reverbSend > 0.0f)
        {
            alSource3i(source, AL_AUXILIARY_SEND_FILTER,
                       static_cast<ALint>(m_reverbSlot), 0, AL_FILTER_NULL);
        }
        else
        {
            alSource3i(source, AL_AUXILIARY_SEND_FILTER,
                       AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
        }
    }
}

void AudioEngine::setReverbParams(const ReverbParams& params)
{
    // Push the standard (non-EAX) AL_REVERB_* properties onto the effect,
    // then re-attach the effect to the slot so the change takes hold. No-op
    // until the slot + effect exist (EFX absent or driver-refused).
    if (m_reverbEffect == 0 || m_reverbSlot == 0)
    {
        return;
    }
    auto effectf = reinterpret_cast<LPALEFFECTF>(m_alEffectf);
    auto sloti   =
        reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(m_alAuxiliaryEffectSloti);
    effectf(m_reverbEffect, AL_REVERB_DECAY_TIME,        params.decayTime);
    effectf(m_reverbEffect, AL_REVERB_DENSITY,           params.density);
    effectf(m_reverbEffect, AL_REVERB_DIFFUSION,         params.diffusion);
    effectf(m_reverbEffect, AL_REVERB_GAIN,              params.gain);
    effectf(m_reverbEffect, AL_REVERB_GAINHF,            params.gainHf);
    effectf(m_reverbEffect, AL_REVERB_REFLECTIONS_DELAY, params.reflectionsDelay);
    effectf(m_reverbEffect, AL_REVERB_LATE_REVERB_DELAY, params.lateReverbDelay);
    sloti(m_reverbSlot, AL_EFFECTSLOT_EFFECT,
          static_cast<ALint>(m_reverbEffect));
}

void AudioEngine::setReverbWetGain(float gain)
{
    m_reverbWetGain = std::clamp(gain, 0.0f, 1.0f);
    if (m_reverbSlot == 0)
    {
        return;
    }
    auto slotf =
        reinterpret_cast<LPALAUXILIARYEFFECTSLOTF>(m_alAuxiliaryEffectSlotf);
    slotf(m_reverbSlot, AL_EFFECTSLOT_GAIN, m_reverbWetGain);
}

void AudioEngine::updateGains()
{
    if (!m_available) return;

    // Reap stopped sources first so we don't burn an AL_GAIN upload
    // on a source that is about to drop out of m_livePlaybacks.
    reclaimFinishedSources();

    for (const auto& [source, mix] : m_livePlaybacks)
    {
        // Phase 10.9 P3: fold the ducking snapshot into every AL_GAIN
        // upload so the engine-wide duck reaches every live source.
        const float gain = resolveSourceGain(
            currentMixer(), mix.bus, mix.sourceVolume, effectiveDuck(mix.bus));
        alSourcef(source, AL_GAIN, gain);
    }
}

void AudioEngine::reclaimFinishedSources()
{
    for (size_t i = 0; i < m_sourcePool.size(); ++i)
    {
        if (m_sourceInUse[i])
        {
            ALint state;
            alGetSourcei(m_sourcePool[i], AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED)
            {
                alSourcei(m_sourcePool[i], AL_BUFFER, 0);
                m_sourceInUse[i] = false;
                m_livePlaybacks.erase(m_sourcePool[i]);
            }
        }
    }
}

} // namespace Vestige
