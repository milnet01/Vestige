// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_music_stream.cpp
/// @brief Streaming-music tick planner — state machine implementation.
#include "audio/audio_music_stream.h"

#include <algorithm>
#include <cstdint>

namespace Vestige
{

float computeStreamBufferedSeconds(const MusicStreamState& state)
{
    if (state.sampleRate == 0)
    {
        return 0.0f;
    }
    if (state.totalFramesDecoded < state.totalFramesConsumed)
    {
        // Defensive: consumer outran decoder (should never happen
        // in practice) — report zero rather than a negative value.
        return 0.0f;
    }
    const std::uint64_t gap = state.totalFramesDecoded - state.totalFramesConsumed;
    return static_cast<float>(gap) / static_cast<float>(state.sampleRate);
}

void notifyStreamFramesConsumed(MusicStreamState& state,
                                 std::uint64_t framesConsumed)
{
    state.totalFramesConsumed += framesConsumed;
    if (state.trackFullyDecodedOnce &&
        state.totalFramesConsumed >= state.totalFramesDecoded)
    {
        state.finished = true;
    }
}

void notifyStreamFramesDecoded(MusicStreamState& state,
                                std::uint64_t framesDecoded,
                                bool eofReached)
{
    state.totalFramesDecoded += framesDecoded;
    if (eofReached)
    {
        state.trackFullyDecodedOnce = true;
        state.loopCount++;
    }
}

StreamTickPlan planStreamTick(MusicStreamState& state, bool decoderAtEof)
{
    StreamTickPlan plan;

    if (state.finished)
    {
        plan.trackFinished = true;
        return plan;
    }

    // Back-pressure: stop decoding once we have enough in-flight
    // audio. Prevents the decoder thread from monopolising CPU
    // when the source is tiny and already fully read.
    if (computeStreamBufferedSeconds(state) >= state.maxSecondsBuffered)
    {
        return plan;
    }

    // EOF handling: if the decoder said EOF, either loop or finish.
    if (decoderAtEof)
    {
        const bool loopForever = state.maxLoops < 0;
        const bool roomForMore = state.loopCount <
            static_cast<std::uint32_t>(std::max(0, state.maxLoops + 1));
        if (loopForever || roomForMore)
        {
            plan.rewindForLoop = true;
            // Fall through and request the next chunk after rewind.
        }
        else
        {
            state.finished     = true;
            plan.trackFinished = true;
            return plan;
        }
    }

    // How many frames do we need to reach the keep-ahead target?
    const float bufferedNow = computeStreamBufferedSeconds(state);
    const float secondsNeeded =
        std::max(0.0f, state.minSecondsBuffered - bufferedNow);
    const std::uint64_t framesNeeded =
        static_cast<std::uint64_t>(secondsNeeded *
                                    static_cast<float>(state.sampleRate));

    if (framesNeeded == 0)
    {
        return plan;
    }

    // Round up to a whole chunk so the decoder can work in its
    // natural unit. Clamp to a single chunk per tick so one slow
    // frame doesn't avalanche decoder work.
    const std::uint32_t chunk = std::max<std::uint32_t>(1, state.framesPerChunk);
    plan.framesToDecode = chunk;
    return plan;
}

} // namespace Vestige
