// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_clip.cpp
/// @brief AudioClip implementation — loads and decodes audio files.
#include "audio/audio_clip.h"
#include "profiler/run_inputs.h"
#include "core/logger.h"

#include <AL/al.h>
#include <AL/alext.h>

#include <dr_wav.h>
#include <dr_mp3.h>
#include <dr_flac.h>

// stb_vorbis declarations only (implementation compiled in stb_vorbis_impl.cpp)
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <algorithm>
#include <cstdint>

namespace Vestige
{

/// @brief Maximum audio duration in frames (CVE-2025-14369). loadWav checks
/// it before allocating; the MP3, FLAC and OGG loaders decode a chunk at a
/// time and stop at it (decodeCapped), so no file allocates past it.
static constexpr uint64_t MAX_AUDIO_FRAMES = 48000ULL * 60 * 30;  // ~30 minutes at 48kHz
static constexpr uint32_t MAX_AUDIO_CHANNELS = 8;
static constexpr uint32_t MIN_SAMPLE_RATE = 8000;
static constexpr uint32_t MAX_SAMPLE_RATE = 192000;

namespace
{

bool formatInRange(uint32_t channels, uint32_t sampleRate)
{
    return channels > 0 && channels <= MAX_AUDIO_CHANNELS
        && sampleRate >= MIN_SAMPLE_RATE && sampleRate <= MAX_SAMPLE_RATE;
}

/// Decodes into @a out a chunk at a time and refuses a file longer than
/// MAX_AUDIO_FRAMES once it passes it. @a readFrames fills up to the asked
/// number of interleaved frames and returns how many it read, 0 at the end.
template <typename ReadFrames>
bool decodeCapped(uint32_t channels, std::vector<int16_t>& out, uint64_t& frames,
                  ReadFrames readFrames)
{
    constexpr uint64_t CHUNK_FRAMES = 4096;
    std::vector<int16_t> chunk(static_cast<size_t>(CHUNK_FRAMES) * channels);
    frames = 0;
    for (;;)
    {
        const uint64_t got = readFrames(chunk.data(), CHUNK_FRAMES);
        if (got == 0)
        {
            return true;
        }
        if (frames + got > MAX_AUDIO_FRAMES)
        {
            return false;
        }
        out.insert(out.end(), chunk.begin(),
                   chunk.begin() + static_cast<std::ptrdiff_t>(got * channels));
        frames += got;
    }
}

}  // namespace

float AudioClip::getDurationSeconds() const
{
    if (m_sampleRate == 0 || m_channels == 0)
    {
        return 0.0f;
    }
    return static_cast<float>(m_frameCount) / static_cast<float>(m_sampleRate);
}

int alFormatForChannels(std::uint32_t channels)
{
    switch (channels)
    {
    case 1: return AL_FORMAT_MONO16;
    case 2: return AL_FORMAT_STEREO16;
    case 4: return AL_FORMAT_BFORMAT3D_16;
    case 6: return AL_FORMAT_51CHN16;
    case 8: return AL_FORMAT_71CHN16;
    default: return 0;
    }
}

bool uploadPcm16(unsigned int buffer, std::uint32_t channels,
                 const std::int16_t* samples, std::size_t bytes,
                 std::uint32_t sampleRate)
{
    const int format = alFormatForChannels(channels);
    if (format == 0)
    {
        return false;
    }
    alBufferData(buffer, static_cast<ALenum>(format), samples,
                 static_cast<ALsizei>(bytes), static_cast<ALsizei>(sampleRate));
    if (channels == 4)
    {
        alBufferi(buffer, AL_AMBISONIC_LAYOUT_SOFT, AL_ACN_SOFT);
        alBufferi(buffer, AL_AMBISONIC_SCALING_SOFT, AL_SN3D_SOFT);
    }
    return true;
}

int AudioClip::getALFormat() const
{
    return alFormatForChannels(m_channels);
}

std::optional<AudioClip> AudioClip::loadFromFile(const std::string& filePath)
{
    // Auto-detect format by extension
    std::string ext;
    auto dotPos = filePath.rfind('.');
    if (dotPos != std::string::npos)
    {
        ext = filePath.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    std::optional<AudioClip> clip;
    if (ext == ".wav")
    {
        clip = loadWav(filePath);
    }
    else if (ext == ".mp3")
    {
        clip = loadMp3(filePath);
    }
    else if (ext == ".flac")
    {
        clip = loadFlac(filePath);
    }
    else if (ext == ".ogg")
    {
        clip = loadOgg(filePath);
    }
    else
    {
        Logger::error("[AudioClip] Unsupported audio format: " + ext);
        return std::nullopt;
    }

    // 3D_E-0743 — refuse a channel layout nothing can play, rather than
    // upload it in the wrong format and play noise.
    if (clip && alFormatForChannels(clip->getChannels()) == 0)
    {
        Logger::error("[AudioClip] " + std::to_string(clip->getChannels())
                      + " channels is not a supported layout (1 mono, 2 stereo, "
                        "4 first-order ambisonics, 6 for 5.1, 8 for 7.1): " + filePath);
        return std::nullopt;
    }
    return clip;
}

std::optional<AudioClip> AudioClip::loadWav(const std::string& filePath)
{
    drwav wav;
    noteRunInput(filePath);
    if (!drwav_init_file(&wav, filePath.c_str(), nullptr))
    {
        Logger::error("[AudioClip] Failed to open WAV: " + filePath);
        return std::nullopt;
    }

    auto totalFrames = wav.totalPCMFrameCount;
    auto channels = wav.channels;
    auto sampleRate = wav.sampleRate;

    if (channels == 0 || channels > MAX_AUDIO_CHANNELS ||
        sampleRate < MIN_SAMPLE_RATE || sampleRate > MAX_SAMPLE_RATE ||
        totalFrames > MAX_AUDIO_FRAMES)
    {
        Logger::error("[AudioClip] WAV metadata out of range: " + filePath);
        drwav_uninit(&wav);
        return std::nullopt;
    }

    AudioClip clip;
    clip.m_channels = channels;
    clip.m_sampleRate = sampleRate;
    clip.m_frameCount = totalFrames;
    clip.m_samples.resize(static_cast<size_t>(totalFrames) * static_cast<size_t>(channels));

    drwav_uint64 framesRead = drwav_read_pcm_frames_s16(
        &wav, totalFrames, clip.m_samples.data());
    drwav_uninit(&wav);

    if (framesRead != totalFrames)
    {
        Logger::warning("[AudioClip] WAV partial read: " + filePath);
    }

    Logger::info("[AudioClip] Loaded WAV: " + filePath +
                 " (" + std::to_string(channels) + "ch, " +
                 std::to_string(sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

std::optional<AudioClip> AudioClip::loadMp3(const std::string& filePath)
{
    drmp3 mp3;
    noteRunInput(filePath);
    if (!drmp3_init_file(&mp3, filePath.c_str(), nullptr))
    {
        Logger::error("[AudioClip] Failed to open MP3: " + filePath);
        return std::nullopt;
    }
    const uint32_t channels = mp3.channels;
    const uint32_t sampleRate = mp3.sampleRate;
    if (!formatInRange(channels, sampleRate))
    {
        Logger::error("[AudioClip] MP3 metadata out of range: " + filePath);
        drmp3_uninit(&mp3);
        return std::nullopt;
    }

    AudioClip clip;
    const bool fits = decodeCapped(channels, clip.m_samples, clip.m_frameCount,
        [&mp3](int16_t* buffer, uint64_t frames)
        {
            return drmp3_read_pcm_frames_s16(&mp3, frames, buffer);
        });
    drmp3_uninit(&mp3);
    if (!fits)
    {
        Logger::error("[AudioClip] MP3 longer than the frame cap: " + filePath);
        return std::nullopt;
    }
    clip.m_channels = channels;
    clip.m_sampleRate = sampleRate;

    Logger::info("[AudioClip] Loaded MP3: " + filePath +
                 " (" + std::to_string(channels) + "ch, " +
                 std::to_string(sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

std::optional<AudioClip> AudioClip::loadFlac(const std::string& filePath)
{
    noteRunInput(filePath);
    drflac* flac = drflac_open_file(filePath.c_str(), nullptr);
    if (!flac)
    {
        Logger::error("[AudioClip] Failed to open FLAC: " + filePath);
        return std::nullopt;
    }
    const uint32_t channels = flac->channels;
    const uint32_t sampleRate = flac->sampleRate;
    if (!formatInRange(channels, sampleRate))
    {
        Logger::error("[AudioClip] FLAC metadata out of range: " + filePath);
        drflac_close(flac);
        return std::nullopt;
    }

    AudioClip clip;
    const bool fits = decodeCapped(channels, clip.m_samples, clip.m_frameCount,
        [flac](int16_t* buffer, uint64_t frames)
        {
            return drflac_read_pcm_frames_s16(flac, frames, buffer);
        });
    drflac_close(flac);
    if (!fits)
    {
        Logger::error("[AudioClip] FLAC longer than the frame cap: " + filePath);
        return std::nullopt;
    }
    clip.m_channels = channels;
    clip.m_sampleRate = sampleRate;

    Logger::info("[AudioClip] Loaded FLAC: " + filePath +
                 " (" + std::to_string(channels) + "ch, " +
                 std::to_string(sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

std::optional<AudioClip> AudioClip::loadOgg(const std::string& filePath)
{
    noteRunInput(filePath);
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_filename(filePath.c_str(), &error, nullptr);
    if (!vorbis)
    {
        Logger::error("[AudioClip] Failed to open OGG: " + filePath);
        return std::nullopt;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    const auto channels = static_cast<uint32_t>(std::max(info.channels, 0));
    const uint32_t sampleRate = info.sample_rate;
    if (!formatInRange(channels, sampleRate))
    {
        Logger::error("[AudioClip] OGG metadata out of range: " + filePath);
        stb_vorbis_close(vorbis);
        return std::nullopt;
    }

    AudioClip clip;
    const bool fits = decodeCapped(channels, clip.m_samples, clip.m_frameCount,
        [vorbis, channels](int16_t* buffer, uint64_t frames)
        {
            const int got = stb_vorbis_get_samples_short_interleaved(
                vorbis, static_cast<int>(channels), buffer,
                static_cast<int>(frames * channels));
            return static_cast<uint64_t>(std::max(got, 0));
        });
    stb_vorbis_close(vorbis);
    if (!fits)
    {
        Logger::error("[AudioClip] OGG longer than the frame cap: " + filePath);
        return std::nullopt;
    }
    clip.m_channels = channels;
    clip.m_sampleRate = sampleRate;

    Logger::info("[AudioClip] Loaded OGG: " + filePath +
                 " (" + std::to_string(channels) + "ch, " +
                 std::to_string(sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

} // namespace Vestige
