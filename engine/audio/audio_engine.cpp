// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_engine.cpp
/// @brief AudioEngine implementation — OpenAL device, source pool, buffer cache.
#include "audio/audio_engine.h"
#include "core/logger.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

namespace Vestige
{

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

    // Apply the stored HRTF settings so callers that configured the
    // engine before initialize() see their preference honoured.
    applyHrtfSettings();

    m_available = true;
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

    // Delete all cached buffers
    for (auto& [path, buffer] : m_bufferCache)
    {
        alDeleteBuffers(1, &buffer);
    }
    m_bufferCache.clear();

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

unsigned int AudioEngine::loadBuffer(const std::string& filePath)
{
    if (!m_available)
    {
        return 0;
    }

    // Check cache first
    auto it = m_bufferCache.find(filePath);
    if (it != m_bufferCache.end())
    {
        return it->second;
    }

    // Load and decode the audio file
    auto clip = AudioClip::loadFromFile(filePath);
    if (!clip)
    {
        return 0;
    }

    // Create OpenAL buffer and upload PCM data
    ALuint buffer;
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

    m_bufferCache[filePath] = buffer;
    return buffer;
}

unsigned int AudioEngine::acquireSource()
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

    Logger::warning("[AudioEngine] Source pool exhausted");
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

void AudioEngine::playSound(const std::string& filePath, const glm::vec3& position,
                             float volume, bool loop, AudioBus bus)
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
        return;
    }

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0)
    {
        return;
    }

    ALuint source = acquireSource();
    if (source == 0)
    {
        return;
    }

    m_livePlaybacks[source] = SourceMix{bus, volume};
    const float initialGain =
        resolveSourceGain(m_mixerSnapshot, bus, volume);

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
    alSourcef(source, AL_MAX_DISTANCE, 50.0f);
    alSourcef(source, AL_ROLLOFF_FACTOR, 1.0f);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);  // 3D positioned
    alSourcePlay(source);
}

void AudioEngine::playSoundSpatial(const std::string& filePath,
                                   const glm::vec3& position,
                                   const AttenuationParams& params,
                                   float volume,
                                   bool loop,
                                   AudioBus bus)
{
    // Phase 10.9 P4 — see playSound() above for rationale.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available) return;

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0) return;

    ALuint source = acquireSource();
    if (source == 0) return;

    m_livePlaybacks[source] = SourceMix{bus, volume};
    const float initialGain =
        resolveSourceGain(m_mixerSnapshot, bus, volume);

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
}

void AudioEngine::playSoundSpatial(const std::string& filePath,
                                   const glm::vec3& position,
                                   const glm::vec3& velocity,
                                   const AttenuationParams& params,
                                   float volume,
                                   bool loop,
                                   AudioBus bus)
{
    // Phase 10.9 P4 — see playSound() above for rationale.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available) return;

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0) return;

    ALuint source = acquireSource();
    if (source == 0) return;

    m_livePlaybacks[source] = SourceMix{bus, volume};
    const float initialGain =
        resolveSourceGain(m_mixerSnapshot, bus, volume);

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
}

void AudioEngine::playSound2D(const std::string& filePath, float volume,
                               AudioBus bus)
{
    // Phase 10.9 P4 — see playSound() above for rationale.
    if (m_captionAnnouncer)
    {
        m_captionAnnouncer(filePath);
    }
    if (!m_available)
    {
        return;
    }

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0)
    {
        return;
    }

    ALuint source = acquireSource();
    if (source == 0)
    {
        return;
    }

    m_livePlaybacks[source] = SourceMix{bus, volume};
    const float initialGain =
        resolveSourceGain(m_mixerSnapshot, bus, volume);

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(source, AL_GAIN, initialGain);
    alSourcei(source, AL_LOOPING, AL_FALSE);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);  // Relative to listener (2D)
    alSourcePlay(source);
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
    if (!m_available || m_alcResetDeviceSOFT == nullptr)
    {
        return;
    }

    // Build an ALC attribute list that instructs the driver how to
    // treat HRTF on the next context reset. Auto mode omits the
    // attribute entirely — the driver's own heuristics (headphone
    // detection, output-format check) then apply.
    ALCint attrs[5] = {0, 0, 0, 0, 0};
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

    attrs[n] = 0;  // ALC attribute list terminator

    auto resetDevice =
        reinterpret_cast<LPALCRESETDEVICESOFT>(m_alcResetDeviceSOFT);
    if (resetDevice(m_device, attrs) != ALC_TRUE)
    {
        Logger::warning("[AudioEngine] alcResetDeviceSOFT failed — HRTF settings may not be applied");
    }
}

void AudioEngine::setDuckingSnapshot(float duckingGain)
{
    // Phase 10.9 P3 (red): stub silently discards the input so every
    // updateGains call still composes without ducking. Green sets
    // m_duckingSnapshot = clamp01(duckingGain).
    (void)duckingGain;
}

void AudioEngine::updateGains()
{
    if (!m_available) return;

    // Reap stopped sources first so we don't burn an AL_GAIN upload
    // on a source that is about to drop out of m_livePlaybacks.
    reclaimFinishedSources();

    for (const auto& [source, mix] : m_livePlaybacks)
    {
        const float gain = resolveSourceGain(
            m_mixerSnapshot, mix.bus, mix.sourceVolume);
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
