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

namespace
{

/// True once the loop policy allows no further play-through.
bool noLoopsRemain(const MusicStreamState& state)
{
    return state.maxLoops >= 0
        && state.loopCount >= static_cast<std::uint32_t>(state.maxLoops + 1);
}

} // namespace

void notifyStreamFramesConsumed(MusicStreamState& state,
                                 std::uint64_t framesConsumed)
{
    state.totalFramesConsumed += framesConsumed;
    // Finished only when no loop remains. A looping track's first EOF set
    // trackFullyDecodedOnce for good, so any later underrun that drained the
    // queue used to end the music (lane-10 lead).
    if (state.trackFullyDecodedOnce && noLoopsRemain(state) &&
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
            // Out of loops: decode nothing more. Finish only once the queued
            // audio has played, or the player's stop cuts the track's last
            // buffers (lane-10 lead); notifyStreamFramesConsumed finishes it
            // when the queue drains.
            if (state.totalFramesConsumed >= state.totalFramesDecoded)
            {
                state.finished     = true;
                plan.trackFinished = true;
            }
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
