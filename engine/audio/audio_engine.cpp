// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_engine.cpp
/// @brief AudioEngine implementation — OpenAL device, source pool, buffer cache.
#include "audio/audio_engine.h"
#include "core/logger.h"

#include <AL/al.h>
#include <AL/alc.h>

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
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);

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
            return;
        }
    }
}

void AudioEngine::playSound(const std::string& filePath, const glm::vec3& position,
                             float volume, bool loop)
{
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

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSourcef(source, AL_GAIN, volume);
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
                                   bool loop)
{
    if (!m_available) return;

    ALuint buffer = loadBuffer(filePath);
    if (buffer == 0) return;

    ALuint source = acquireSource();
    if (source == 0) return;

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, position.x, position.y, position.z);
    alSourcef(source, AL_GAIN, volume);
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(source, AL_REFERENCE_DISTANCE, params.referenceDistance);
    alSourcef(source, AL_MAX_DISTANCE,       params.maxDistance);
    alSourcef(source, AL_ROLLOFF_FACTOR,     params.rolloffFactor);
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSourcePlay(source);
}

void AudioEngine::playSound2D(const std::string& filePath, float volume)
{
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

    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(source, AL_GAIN, volume);
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
            }
        }
    }
}

} // namespace Vestige
