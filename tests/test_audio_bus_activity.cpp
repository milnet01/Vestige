// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_bus_activity.cpp
/// @brief 3D_E-0742 — a sound played straight through AudioEngine (a
///        script's dialogue line, a stinger) marks its bus active, so the
///        Voice → Music duck route fires for it.
///
/// Opens OpenAL Soft's null backend (`ALSOFT_DRIVERS=null`) and plays a
/// short generated WAV; skips where no device opens. POSIX only, like the
/// other env-driven tests.
#include "audio/audio_engine.h"
#include "audio/audio_mixer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;

#ifndef _WIN32

namespace
{

bool openNullDevice(AudioEngine& engine)
{
    const char* previous = std::getenv("ALSOFT_DRIVERS");
    const std::string saved = (previous != nullptr) ? previous : "";
    ::setenv("ALSOFT_DRIVERS", "null", 1);
    const bool opened = engine.initialize();
    if (previous != nullptr) ::setenv("ALSOFT_DRIVERS", saved.c_str(), 1);
    else                     ::unsetenv("ALSOFT_DRIVERS");
    return opened;
}

void putU32(std::ofstream& out, std::uint32_t v)
{
    const char b[4] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu),
                       static_cast<char>((v >> 16) & 0xFFu), static_cast<char>((v >> 24) & 0xFFu)};
    out.write(b, 4);
}

void putU16(std::ofstream& out, std::uint16_t v)
{
    const char b[2] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu)};
    out.write(b, 2);
}

/// Writes `seconds` of a quiet 16-bit mono tone at 22050 Hz as a WAV.
std::filesystem::path writeTestWav(const std::string& name, float seconds)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    constexpr std::uint32_t kRate = 22050;
    const auto frames = static_cast<std::uint32_t>(seconds * static_cast<float>(kRate));
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    putU32(out, 36 + frames * 2);
    out.write("WAVEfmt ", 8);
    putU32(out, 16);
    putU16(out, 1);           // PCM
    putU16(out, 1);           // mono
    putU32(out, kRate);
    putU32(out, kRate * 2);   // byte rate
    putU16(out, 2);           // block align
    putU16(out, 16);          // bits per sample
    out.write("data", 4);
    putU32(out, frames * 2);
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        putU16(out, static_cast<std::uint16_t>((i / 25) % 2 == 0 ? 2000 : 63536));
    }
    return path;
}

}  // namespace

TEST(AudioBusActivity, DirectPlaybackMarksItsBusActive)
{
    AudioEngine engine;
    if (!openNullDevice(engine))
    {
        GTEST_SKIP() << "no OpenAL device could be opened";
    }
    const auto wav = writeTestWav("vestige_bus_activity.wav", 2.0f);

    const unsigned int voice = engine.playSound2D(wav.string(), 1.0f, AudioBus::Voice,
                                                  SoundPriority::Critical);
    const unsigned int quiet = engine.playSound2D(wav.string(), 0.001f, AudioBus::Music);
    ASSERT_NE(voice, 0u);
    ASSERT_NE(quiet, 0u);

    const auto active = engine.busesWithLivePlayback(0.01f);
    EXPECT_TRUE(active[static_cast<std::size_t>(AudioBus::Voice)])
        << "a dialogue line played straight through the engine left Voice idle";
    EXPECT_FALSE(active[static_cast<std::size_t>(AudioBus::Music)])
        << "a playback under the volume floor counted as activity";
    EXPECT_FALSE(active[static_cast<std::size_t>(AudioBus::Sfx)]);

    engine.stopSound(voice);
    EXPECT_FALSE(engine.busesWithLivePlayback(0.01f)[static_cast<std::size_t>(AudioBus::Voice)])
        << "a stopped playback still counted as activity";

    engine.shutdown();
    std::filesystem::remove(wav);
}

#endif
