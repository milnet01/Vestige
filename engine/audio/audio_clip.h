// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_clip.h
/// @brief Decoded audio data container for loading WAV, MP3, FLAC, and OGG files.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Holds decoded PCM audio data loaded from a file.
///
/// Supports WAV (dr_wav), MP3 (dr_mp3), FLAC (dr_flac), and OGG Vorbis
/// (stb_vorbis). Format is auto-detected by file extension.
class AudioClip
{
public:
    AudioClip() = default;

    /// @brief Loads and decodes an audio file.
    /// @param filePath Path to a .wav, .mp3, .flac, or .ogg file.
    /// @return The decoded clip, or std::nullopt on failure.
    static std::optional<AudioClip> loadFromFile(const std::string& filePath);

    /// @brief Gets the raw PCM sample data (signed 16-bit interleaved).
    const std::vector<int16_t>& getSamples() const { return m_samples; }

    /// @brief Gets the sample rate in Hz (e.g. 44100, 48000).
    uint32_t getSampleRate() const { return m_sampleRate; }

    /// @brief Gets the number of channels (1 = mono, 2 = stereo).
    uint32_t getChannels() const { return m_channels; }

    /// @brief Gets the total number of sample frames (samples / channels).
    uint64_t getFrameCount() const { return m_frameCount; }

    /// @brief Gets the duration in seconds.
    float getDurationSeconds() const;

    /// @brief Returns the OpenAL format constant for this clip's channel/bit layout.
    /// @return AL_FORMAT_MONO16 or AL_FORMAT_STEREO16.
    int getALFormat() const;

    /// @brief Returns the total size of the PCM data in bytes.
    size_t getDataSizeBytes() const { return m_samples.size() * sizeof(int16_t); }

private:
    std::vector<int16_t> m_samples;
    uint32_t m_sampleRate = 0;
    uint32_t m_channels = 0;
    uint64_t m_frameCount = 0;

    static std::optional<AudioClip> loadWav(const std::string& filePath);
    static std::optional<AudioClip> loadMp3(const std::string& filePath);
    static std::optional<AudioClip> loadFlac(const std::string& filePath);
    static std::optional<AudioClip> loadOgg(const std::string& filePath);
};

} // namespace Vestige
