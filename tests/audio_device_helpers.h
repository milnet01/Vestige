// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_device_helpers.h
/// @brief Tests that need a real OpenAL device: open OpenAL Soft's silent
///        null backend and write a short WAV to play on it.
///
/// `ALSOFT_DRIVERS=null` selects the null backend, which the bundled OpenAL
/// build includes; a test skips when `openNullAudioDevice` returns false.
/// The device opener is POSIX only: the Windows (Wine) behaviour of the null
/// backend is unchecked. The WAV writer works everywhere.
#pragma once

#include "audio/audio_engine.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Vestige::TestAudio
{

#ifndef _WIN32
/// Initialises `engine` on the null backend, restoring ALSOFT_DRIVERS.
inline bool openNullAudioDevice(AudioEngine& engine)
{
    const char* previous = std::getenv("ALSOFT_DRIVERS");
    const std::string saved = (previous != nullptr) ? previous : "";
    ::setenv("ALSOFT_DRIVERS", "null", 1);
    const bool opened = engine.initialize();
    if (previous != nullptr) ::setenv("ALSOFT_DRIVERS", saved.c_str(), 1);
    else                     ::unsetenv("ALSOFT_DRIVERS");
    return opened;
}
#endif

namespace detail
{
inline void putU32(std::ofstream& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
    {
        out.put(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}

inline void putU16(std::ofstream& out, std::uint16_t v)
{
    out.put(static_cast<char>(v & 0xFFu));
    out.put(static_cast<char>((v >> 8) & 0xFFu));
}
}  // namespace detail

/// Writes `seconds` of a quiet 16-bit square tone at 22050 Hz, with
/// `channels` interleaved channels, to the temp directory as `name` and
/// returns its path.
inline std::filesystem::path writeTestWav(const std::string& name, float seconds,
                                          std::uint16_t channels = 1)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    constexpr std::uint32_t kRate = 22050;
    const auto frames = static_cast<std::uint32_t>(seconds * static_cast<float>(kRate));
    const std::uint32_t frameBytes = 2u * channels;
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    detail::putU32(out, 36 + frames * frameBytes);
    out.write("WAVEfmt ", 8);
    detail::putU32(out, 16);
    detail::putU16(out, 1);                       // PCM
    detail::putU16(out, channels);
    detail::putU32(out, kRate);
    detail::putU32(out, kRate * frameBytes);      // byte rate
    detail::putU16(out, static_cast<std::uint16_t>(frameBytes));  // block align
    detail::putU16(out, 16);                      // bits per sample
    out.write("data", 4);
    detail::putU32(out, frames * frameBytes);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        const auto v = static_cast<std::uint16_t>((i / 25) % 2 == 0 ? 2000 : 63536);
        for (std::uint16_t c = 0; c < channels; ++c)
        {
            detail::putU16(out, v);
        }
    }
    return path;
}

}  // namespace Vestige::TestAudio
