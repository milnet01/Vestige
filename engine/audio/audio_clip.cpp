/// @file audio_clip.cpp
/// @brief AudioClip implementation — loads and decodes audio files.
#include "audio/audio_clip.h"
#include "core/logger.h"

#include <AL/al.h>

#include <dr_wav.h>
#include <dr_mp3.h>
#include <dr_flac.h>

// stb_vorbis declarations only (implementation compiled in stb_vorbis_impl.cpp)
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <algorithm>

namespace Vestige
{

float AudioClip::getDurationSeconds() const
{
    if (m_sampleRate == 0 || m_channels == 0)
    {
        return 0.0f;
    }
    return static_cast<float>(m_frameCount) / static_cast<float>(m_sampleRate);
}

int AudioClip::getALFormat() const
{
    return (m_channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
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

    if (ext == ".wav")
    {
        return loadWav(filePath);
    }
    else if (ext == ".mp3")
    {
        return loadMp3(filePath);
    }
    else if (ext == ".flac")
    {
        return loadFlac(filePath);
    }
    else if (ext == ".ogg")
    {
        return loadOgg(filePath);
    }

    Logger::error("[AudioClip] Unsupported audio format: " + ext);
    return std::nullopt;
}

std::optional<AudioClip> AudioClip::loadWav(const std::string& filePath)
{
    drwav wav;
    if (!drwav_init_file(&wav, filePath.c_str(), nullptr))
    {
        Logger::error("[AudioClip] Failed to open WAV: " + filePath);
        return std::nullopt;
    }

    auto totalFrames = wav.totalPCMFrameCount;
    auto channels = wav.channels;
    auto sampleRate = wav.sampleRate;

    AudioClip clip;
    clip.m_channels = channels;
    clip.m_sampleRate = sampleRate;
    clip.m_frameCount = totalFrames;
    clip.m_samples.resize(static_cast<size_t>(totalFrames * channels));

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
    drmp3_config config;
    drmp3_uint64 totalFrames;
    drmp3_int16* pSamples = drmp3_open_file_and_read_pcm_frames_s16(
        filePath.c_str(), &config, &totalFrames, nullptr);

    if (!pSamples)
    {
        Logger::error("[AudioClip] Failed to open MP3: " + filePath);
        return std::nullopt;
    }

    AudioClip clip;
    clip.m_channels = config.channels;
    clip.m_sampleRate = config.sampleRate;
    clip.m_frameCount = totalFrames;
    auto totalSamples = static_cast<size_t>(totalFrames * config.channels);
    clip.m_samples.assign(pSamples, pSamples + totalSamples);
    drmp3_free(pSamples, nullptr);

    Logger::info("[AudioClip] Loaded MP3: " + filePath +
                 " (" + std::to_string(config.channels) + "ch, " +
                 std::to_string(config.sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

std::optional<AudioClip> AudioClip::loadFlac(const std::string& filePath)
{
    unsigned int channels;
    unsigned int sampleRate;
    drflac_uint64 totalFrames;
    drflac_int16* pSamples = drflac_open_file_and_read_pcm_frames_s16(
        filePath.c_str(), &channels, &sampleRate, &totalFrames, nullptr);

    if (!pSamples)
    {
        Logger::error("[AudioClip] Failed to open FLAC: " + filePath);
        return std::nullopt;
    }

    AudioClip clip;
    clip.m_channels = channels;
    clip.m_sampleRate = sampleRate;
    clip.m_frameCount = totalFrames;
    auto totalSamples = static_cast<size_t>(totalFrames * channels);
    clip.m_samples.assign(pSamples, pSamples + totalSamples);
    drflac_free(pSamples, nullptr);

    Logger::info("[AudioClip] Loaded FLAC: " + filePath +
                 " (" + std::to_string(channels) + "ch, " +
                 std::to_string(sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

std::optional<AudioClip> AudioClip::loadOgg(const std::string& filePath)
{
    int channels;
    int sampleRate;
    short* pSamples;
    int totalFrames = stb_vorbis_decode_filename(
        filePath.c_str(), &channels, &sampleRate, &pSamples);

    if (totalFrames < 0 || !pSamples)
    {
        Logger::error("[AudioClip] Failed to open OGG: " + filePath);
        return std::nullopt;
    }

    AudioClip clip;
    clip.m_channels = static_cast<uint32_t>(channels);
    clip.m_sampleRate = static_cast<uint32_t>(sampleRate);
    clip.m_frameCount = static_cast<uint64_t>(totalFrames);
    auto totalSamples = static_cast<size_t>(totalFrames * channels);
    clip.m_samples.assign(pSamples, pSamples + totalSamples);
    free(pSamples);  // stb_vorbis uses malloc

    Logger::info("[AudioClip] Loaded OGG: " + filePath +
                 " (" + std::to_string(channels) + "ch, " +
                 std::to_string(sampleRate) + "Hz, " +
                 std::to_string(clip.getDurationSeconds()) + "s)");
    return clip;
}

} // namespace Vestige
