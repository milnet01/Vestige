// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_music_stream.h
/// @brief Phase 10 audio — streaming-music decode state machine.
///
/// A music track that fits in RAM can be uploaded to one OpenAL
/// buffer and played with `alSourcei(AL_BUFFER, ...)`. A track that
/// *doesn't* fit (a 3-minute 48 kHz stereo OGG is ~34 MB as raw
/// PCM) must be decoded in small chunks and queued onto the source
/// via `alSourceQueueBuffers` / `alSourceUnqueueBuffers`.
///
/// This module models the decoder-side state of that pipeline,
/// independent of OpenAL. It tracks:
///   - total frames decoded from the source file,
///   - total frames already consumed by the audio driver,
///   - sample rate (so "seconds buffered" can be computed),
///   - whether the file has been fully decoded,
///   - loop policy and loop counter,
///   - the minimum buffer-ahead window that must be kept filled.
///
/// The engine-side MusicPlayer calls `planStreamTick` once per
/// update. The returned `StreamTickPlan` says how many frames to
/// ask the decoder for on this tick, whether to rewind + loop,
/// and whether to signal end-of-track. Keeping the planner a pure
/// function lets tests exercise the state machine without actual
/// files or an OpenAL context.
///
/// Reference: OpenAL Soft examples/alstream.c + dr_libs streaming
/// examples. The chunk-sized / triple-buffered approach is the
/// same — this module just separates the policy from the IO.
#pragma once

#include <cstdint>

namespace Vestige
{

/// @brief Persistent state for a single streaming-music session.
///
/// All counters are in *frames* (one frame = one sample per
/// channel). Track length, if known, is `totalFramesInFile`; set
/// to 0 while still discovering length from the decoder (e.g. Ogg
/// streams without a seek table).
struct MusicStreamState
{
    std::uint64_t totalFramesInFile     = 0;     ///< 0 means unknown.
    std::uint64_t totalFramesDecoded    = 0;     ///< Across loops too.
    std::uint64_t totalFramesConsumed   = 0;     ///< Across loops too.
    std::uint32_t sampleRate            = 48000;
    std::uint32_t loopCount             = 0;     ///< Completed loops so far.
    std::int32_t  maxLoops              = -1;    ///< −1 = infinite, 0 = one-shot.
    float         minSecondsBuffered    = 0.30f; ///< Keep-ahead target.
    float         maxSecondsBuffered    = 0.60f; ///< Don't decode past this.
    std::uint32_t framesPerChunk        = 4096;  ///< Chunk size for each request.
    bool          trackFullyDecodedOnce = false; ///< Reached EOF at least once.
    bool          finished              = false; ///< No more playback expected.
};

/// @brief Output of the tick planner — what the caller should do
///        right now. All fields default to "do nothing" so the
///        caller can treat the struct as a series of optional
///        actions.
struct StreamTickPlan
{
    /// @brief Frames to request from the decoder this tick.
    /// 0 means "no decoding work needed". Always a multiple of
    /// `MusicStreamState::framesPerChunk` up to the refill gap.
    std::uint32_t framesToDecode = 0;

    /// @brief True when the decoder has hit end-of-file and the
    ///        caller should seek back to the start to continue
    ///        the loop. Set before returning any
    ///        `framesToDecode` so the engine-side loop order is:
    ///        1) rewind, 2) decode next chunk.
    bool rewindForLoop = false;

    /// @brief True when playback is done and the voice can be
    ///        stopped. Mirrors `MusicStreamState::finished`.
    bool trackFinished = false;
};

/// @brief Returns how many whole seconds of audio are currently
///        buffered between the decoder and the consumer.
///
/// `decoded − consumed` frames divided by `sampleRate`. Zero
/// sample rate → 0 to avoid division-by-zero in early init /
/// paused states.
float computeStreamBufferedSeconds(const MusicStreamState& state);

/// @brief Advances the playback counter by `framesConsumed` —
///        called when OpenAL unqueues a buffer. Sets the
///        `finished` flag if `trackFullyDecodedOnce` is already
///        true and we've consumed everything decoded.
void notifyStreamFramesConsumed(MusicStreamState& state,
                                 std::uint64_t framesConsumed);

/// @brief Advances the decoder counter by `framesDecoded` —
///        called after the decoder fills a chunk. When
///        `eofReached` is true, increments `loopCount` and marks
///        `trackFullyDecodedOnce`.
void notifyStreamFramesDecoded(MusicStreamState& state,
                                std::uint64_t framesDecoded,
                                bool eofReached);

/// @brief Computes the next action for this stream tick.
///
/// Decision tree, in order:
///   1. If `finished`, return `trackFinished = true`.
///   2. If buffered seconds >= `maxSecondsBuffered`, return zero
///      work (back-pressure so we don't over-decode).
///   3. If an EOF has been reached and the loop policy is
///      exhausted, mark finished (returning trackFinished next
///      tick).
///   4. If an EOF has been reached but we may still loop, set
///      `rewindForLoop` and return the next chunk to decode.
///   5. Otherwise return the number of frames needed to reach
///      `minSecondsBuffered`, rounded up to `framesPerChunk`.
StreamTickPlan planStreamTick(MusicStreamState& state, bool decoderAtEof);

} // namespace Vestige
