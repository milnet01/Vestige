// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_clip_decode.cpp
/// @brief 3D_E-0874 lane-10 — the MP3, FLAC and OGG loaders decode a chunk
///        at a time so a long file stops at the frame cap instead of
///        decoding whole first. A chunked decode must give exactly the
///        samples the libraries' whole-file decoders give.
///
/// Fixtures (tests/fixtures/audio): clip_test.mp3 and clip_test.flac are a
/// 0.5 s 440 Hz stereo sine at 44.1 kHz made with ffmpeg; music_loop_test.ogg
/// is the music player's fixture.

#include "audio/audio_clip.h"

#include <gtest/gtest.h>

#include <dr_flac.h>
#include <dr_mp3.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{

std::string fixture(const char* name)
{
    return std::string(VESTIGE_AUDIO_FIXTURES_DIR) + "/" + name;
}

}  // namespace

TEST(AudioClipDecode, Mp3MatchesTheWholeFileDecoder)
{
    const std::string path = fixture("clip_test.mp3");
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    drmp3_int16* whole = drmp3_open_file_and_read_pcm_frames_s16(path.c_str(), &config,
                                                                 &frames, nullptr);
    ASSERT_NE(whole, nullptr);
    const std::vector<int16_t> expected(whole, whole + frames * config.channels);
    drmp3_free(whole, nullptr);

    const auto clip = AudioClip::loadFromFile(path);
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->getChannels(), config.channels);
    EXPECT_EQ(clip->getFrameCount(), frames);
    EXPECT_EQ(clip->getSamples(), expected);
}

TEST(AudioClipDecode, FlacMatchesTheWholeFileDecoder)
{
    const std::string path = fixture("clip_test.flac");
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drflac_uint64 frames = 0;
    drflac_int16* whole = drflac_open_file_and_read_pcm_frames_s16(path.c_str(), &channels,
                                                                   &sampleRate, &frames, nullptr);
    ASSERT_NE(whole, nullptr);
    const std::vector<int16_t> expected(whole, whole + frames * channels);
    drflac_free(whole, nullptr);

    const auto clip = AudioClip::loadFromFile(path);
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->getChannels(), channels);
    EXPECT_EQ(clip->getFrameCount(), frames);
    EXPECT_EQ(clip->getSamples(), expected);
}

TEST(AudioClipDecode, OggMatchesTheWholeFileDecoder)
{
    const std::string path = fixture("music_loop_test.ogg");
    int channels = 0;
    int sampleRate = 0;
    short* whole = nullptr;
    const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &sampleRate, &whole);
    ASSERT_GT(frames, 0);
    const std::vector<int16_t> expected(whole, whole + static_cast<size_t>(frames) * channels);
    std::free(whole);

    const auto clip = AudioClip::loadFromFile(path);
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->getChannels(), static_cast<uint32_t>(channels));
    EXPECT_EQ(clip->getFrameCount(), static_cast<uint64_t>(frames));
    EXPECT_EQ(clip->getSamples(), expected);
}
