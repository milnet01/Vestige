/// @file audio_engine.h
/// @brief OpenAL wrapper for device management, source pooling, and listener control.
#pragma once

#include "audio/audio_clip.h"

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
    /// @param position Listener world position (typically camera position).
    /// @param forward Listener forward direction.
    /// @param up Listener up direction.
    void updateListener(const glm::vec3& position, const glm::vec3& forward,
                        const glm::vec3& up);

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
    void playSound(const std::string& filePath, const glm::vec3& position,
                   float volume = 1.0f, bool loop = false);

    /// @brief Plays a non-spatial (2D) sound (fire-and-forget).
    /// @param filePath Path to the audio file.
    /// @param volume Volume (0.0 to 1.0).
    void playSound2D(const std::string& filePath, float volume = 1.0f);

    /// @brief Stops all playing sources.
    void stopAll();

    /// @brief Checks if audio is available (device opened successfully).
    bool isAvailable() const { return m_available; }

private:
    ALCdevice* m_device = nullptr;
    ALCcontext* m_context = nullptr;
    bool m_available = false;

    // Source pool
    std::vector<unsigned int> m_sourcePool;
    std::vector<bool> m_sourceInUse;

    // Buffer cache (path -> OpenAL buffer ID)
    std::unordered_map<std::string, unsigned int> m_bufferCache;

    /// @brief Reclaims finished sources back to the pool.
    void reclaimFinishedSources();
};

} // namespace Vestige
