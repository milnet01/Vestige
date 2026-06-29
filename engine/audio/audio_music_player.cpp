// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_music_player.cpp
/// @brief Streaming-music player implementation (W8 part 2/2).

#include "audio/audio_music_player.h"

#include "audio/audio_engine.h"
#include "audio/audio_mixer.h"
#include "core/logger.h"

#include <AL/al.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// stb_vorbis declarations only; implementation symbols come from the
// existing external/stb/stb_vorbis_impl.cpp TU (mirrors audio_clip.cpp).
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace Vestige
{

namespace
{
/// Bound on the per-tick refill loop. A healthy track returns data on
/// every non-EOF decode, so the loop terminates when the planner's
/// keep-ahead target is met or the ring is full (≤ kBuffersPerLayer
/// queues). The +1 absorbs a single EOF/rewind hiccup mid-fill. A
/// corrupt or zero-length file (perpetual 0-frame reads) is the only
/// case that would otherwise spin; this cap stops it.
constexpr std::size_t kMaxRefillIterations = kBuffersPerLayer + 1;
}

AudioMusicPlayer::AudioMusicPlayer(AudioEngine& engine)
    : m_engine(engine)
{
    for (std::size_t i = 0; i < MusicLayerCount; ++i)
    {
        m_layers[i].id = static_cast<MusicLayer>(i);
    }
}

AudioMusicPlayer::~AudioMusicPlayer()
{
    clearAllLayers();
}

StreamingLayer& AudioMusicPlayer::layerFor(MusicLayer layer)
{
    return m_layers[static_cast<std::size_t>(layer)];
}

const StreamingLayer& AudioMusicPlayer::layerFor(MusicLayer layer) const
{
    return m_layers[static_cast<std::size_t>(layer)];
}

bool AudioMusicPlayer::loadLayer(MusicLayer layer, const std::string& clipPath)
{
    // Drop any existing track on this layer first (closes the decoder,
    // releases the source + AL buffers).
    unloadLayer(layer);

    // Sandbox the path the same way AudioEngine::loadBuffer does before
    // any decoder touches it. Empty return == rejected (escaped roots).
    const std::string safePath = m_engine.resolveSandboxedPath(clipPath);
    if (safePath.empty())
    {
        Logger::warning("[AudioMusicPlayer] music clip rejected by sandbox: "
                        + clipPath);
        return false;
    }

    int error = 0;
    stb_vorbis* decoder =
        stb_vorbis_open_filename(safePath.c_str(), &error, nullptr);
    if (decoder == nullptr)
    {
        Logger::warning("[AudioMusicPlayer] failed to open music clip "
                        + safePath + " (stb_vorbis error "
                        + std::to_string(error) + ")");
        return false;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(decoder);

    StreamingLayer& slot = layerFor(layer);
    slot.id         = layer;
    slot.clipPath   = clipPath;
    slot.decoder    = decoder;
    slot.channels   = info.channels;
    slot.sampleRate = static_cast<int>(info.sample_rate);
    slot.active     = false;
    slot.decoderJustHitEof = false;

    slot.stream = MusicStreamState{};
    slot.stream.sampleRate = info.sample_rate;
    slot.stream.totalFramesInFile =
        stb_vorbis_stream_length_in_samples(decoder);

    slot.gain = MusicLayerState{};

    // Allocate the AL buffer ring (only with a live device; headless
    // tests run without one and never queue to AL). freeBuffers is
    // seeded with every owned buffer so the refill loop has somewhere
    // to decode into.
    slot.buffers.fill(0);
    slot.freeBuffers.clear();
    if (m_engine.isAvailable())
    {
        alGenBuffers(static_cast<ALsizei>(kBuffersPerLayer),
                     slot.buffers.data());
    }
    for (unsigned int buffer : slot.buffers)
    {
        slot.freeBuffers.push_back(buffer);
    }

    return true;
}

void AudioMusicPlayer::playLayer(MusicLayer layer)
{
    StreamingLayer& slot = layerFor(layer);
    if (slot.decoder == nullptr || slot.active)
    {
        return;  // No track loaded, or already playing.
    }

    slot.active = true;
    slot.gain.targetGain = 1.0f;

    if (m_engine.isAvailable())
    {
        slot.source = m_engine.acquireSource(SoundPriority::Normal);
    }

    // Prime the ring to the planner's keep-ahead target before play so
    // the source never starts underran. refillLayer queues whatever
    // buffers it fills; with no device it only advances the decode
    // state machine.
    refillLayer(slot);

    if (m_engine.isAvailable() && slot.source != 0)
    {
        alSourcePlay(slot.source);
    }
}

void AudioMusicPlayer::setLayerTargetGain(MusicLayer layer, float targetGain)
{
    layerFor(layer).gain.targetGain = std::clamp(targetGain, 0.0f, 1.0f);
}

void AudioMusicPlayer::setLayerLooping(MusicLayer layer, bool loop)
{
    layerFor(layer).stream.maxLoops = loop ? -1 : 0;
}

void AudioMusicPlayer::stopLayer(MusicLayer layer)
{
    StreamingLayer& slot = layerFor(layer);
    if (slot.source != 0)
    {
        m_engine.releaseSource(slot.source);  // stops the source first.
        slot.source = 0;
    }
    slot.active = false;
}

void AudioMusicPlayer::unloadLayer(MusicLayer layer)
{
    StreamingLayer& slot = layerFor(layer);
    stopLayer(layer);

    if (m_engine.isAvailable() && slot.buffers[0] != 0)
    {
        alDeleteBuffers(static_cast<ALsizei>(kBuffersPerLayer),
                        slot.buffers.data());
    }
    slot.buffers.fill(0);
    slot.freeBuffers.clear();

    if (slot.decoder != nullptr)
    {
        stb_vorbis_close(slot.decoder);
        slot.decoder = nullptr;
    }
    slot.clipPath.clear();
    slot.channels = 0;
    slot.decoderJustHitEof = false;
    slot.stream = MusicStreamState{};
    slot.gain = MusicLayerState{};
}

void AudioMusicPlayer::clearAllLayers()
{
    for (std::size_t i = 0; i < MusicLayerCount; ++i)
    {
        unloadLayer(static_cast<MusicLayer>(i));
    }
}

void AudioMusicPlayer::applyIntensity(float intensity, float silence)
{
    const MusicLayerWeights weights = intensityToLayerWeights(intensity, silence);
    for (StreamingLayer& slot : m_layers)
    {
        if (slot.decoder != nullptr)
        {
            slot.gain.targetGain = std::clamp(weights.weightOf(slot.id), 0.0f, 1.0f);
        }
    }
}

StreamTickPlan AudioMusicPlayer::stepDecodeOnce(StreamingLayer& layer)
{
    // Pass the *transient* per-tick EOF flag, never the sticky
    // MusicStreamState::trackFullyDecodedOnce — the sticky flag would
    // re-fire rewindForLoop every call.
    StreamTickPlan plan = planStreamTick(layer.stream, layer.decoderJustHitEof);

    if (plan.rewindForLoop)
    {
        stb_vorbis_seek_start(layer.decoder);
        layer.decoderJustHitEof = false;
    }

    if (plan.framesToDecode == 0)
    {
        return plan;  // Keep-ahead met / back-pressure / finished.
    }

    // planStreamTick clamps framesToDecode to one chunk
    // (audio_music_stream.cpp:106-107); decode exactly that.
    const int channels = std::max(1, layer.channels);
    std::vector<short> scratch(static_cast<std::size_t>(plan.framesToDecode)
                               * static_cast<std::size_t>(channels));
    const int gotFrames = stb_vorbis_get_samples_short_interleaved(
        layer.decoder, channels, scratch.data(),
        static_cast<int>(scratch.size()));

    // 0 frames == EOF (or an unrecoverable decode error). A genuinely
    // corrupt file rewinds, re-hits the bad packet, and would loop fast;
    // warn once so the operator can spot it. Partial reads (0 < got <
    // requested) are packet-boundary short reads, NOT EOF.
    const bool reachedEof = (gotFrames <= 0);
    if (reachedEof)
    {
        layer.decoderJustHitEof = true;
        if (!m_warnedCorruptDecode && layer.stream.totalFramesInFile > 0
            && layer.stream.totalFramesDecoded == 0)
        {
            Logger::warning("[AudioMusicPlayer] music clip " + layer.clipPath
                            + " decoded 0 frames on first read — corrupt or "
                              "empty input?");
            m_warnedCorruptDecode = true;
        }
    }

    if (gotFrames > 0 && layer.source != 0 && !layer.freeBuffers.empty())
    {
        const ALuint dst = layer.freeBuffers.front();
        layer.freeBuffers.pop_front();
        alBufferData(dst,
                     channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
                     scratch.data(),
                     static_cast<ALsizei>(gotFrames * channels
                                          * static_cast<int>(sizeof(short))),
                     layer.sampleRate);
        alSourceQueueBuffers(layer.source, 1, &dst);
    }

    notifyStreamFramesDecoded(layer.stream,
                              static_cast<std::uint64_t>(std::max(0, gotFrames)),
                              reachedEof);
    return plan;
}

void AudioMusicPlayer::refillLayer(StreamingLayer& layer)
{
    // Top the ring up to the planner's keep-ahead target. Bounded so a
    // corrupt/empty file can't spin (see kMaxRefillIterations).
    for (std::size_t i = 0; i < kMaxRefillIterations; ++i)
    {
        // With a live device the ring caps in-flight chunks; once every
        // buffer is queued, wait for the next unqueue.
        if (m_engine.isAvailable() && layer.freeBuffers.empty())
        {
            break;
        }

        const StreamTickPlan plan = stepDecodeOnce(layer);
        if (plan.trackFinished)
        {
            stopLayer(layer.id);
            return;
        }
        if (plan.framesToDecode == 0 && !plan.rewindForLoop)
        {
            break;  // Keep-ahead met / back-pressure — nothing more to do.
        }
    }
}

void AudioMusicPlayer::update(float deltaSeconds)
{
    // True no-op for non-positive dt (mirrors MusicStingerQueue::advance).
    if (deltaSeconds <= 0.0f)
    {
        return;
    }

    const bool deviceLive = m_engine.isAvailable();
    const AudioMixer* mixer = m_engine.getMixerSnapshot();
    // AX13 — streaming music is the prime side-chain target (it dips under
    // dialogue), so fold in the router's Music-bus duck on top of the
    // global manual duck. The music player is a separate gain path from
    // AudioEngine::updateGains, so it resolves the effective duck itself.
    const float duckGain =
        m_engine.getDuckingSnapshot()
        * m_engine.getBusDuckSnapshot()[static_cast<std::size_t>(AudioBus::Music)];

    for (StreamingLayer& layer : m_layers)
    {
        if (layer.decoder == nullptr)
        {
            continue;  // Not loaded.
        }

        // 1. Advance the gain slew.
        advanceMusicLayer(layer.gain, deltaSeconds);

        // 2. Push the composed gain to the source. Fold the Music-bus +
        //    master + ducking gain in so the mixer's Music slider
        //    actually controls streaming music (a raw pool source is not
        //    in AudioEngine::updateGains' registered set, so we resolve
        //    it here — same math playSound* uses at upload time).
        if (deviceLive && layer.source != 0)
        {
            const float effective =
                mixer != nullptr
                    ? resolveSourceGain(*mixer, AudioBus::Music,
                                        layer.gain.currentGain, duckGain)
                    : layer.gain.currentGain;
            alSourcef(layer.source, AL_GAIN, effective);
        }

        // 3. Advance the consume counter.
        if (deviceLive && layer.source != 0)
        {
            // OpenAL drains the queue; reap processed buffers and credit
            // their frames to the consume counter.
            ALint processed = 0;
            alGetSourcei(layer.source, AL_BUFFERS_PROCESSED, &processed);
            while (processed-- > 0)
            {
                ALuint freed = 0;
                alSourceUnqueueBuffers(layer.source, 1, &freed);
                ALint sizeBytes = 0;
                alGetBufferi(freed, AL_SIZE, &sizeBytes);
                const int channels = std::max(1, layer.channels);
                const std::uint64_t framesConsumed =
                    static_cast<std::uint64_t>(sizeBytes)
                    / (sizeof(short) * static_cast<std::uint64_t>(channels));
                notifyStreamFramesConsumed(layer.stream, framesConsumed);
                layer.freeBuffers.push_back(freed);
            }
        }
        else if (layer.active)
        {
            // [design-reconcile 2026-06-04] No AL device to drain the
            // queue — advance the consume counter by wall-clock so the
            // planner's back-pressure + loop logic progress
            // deterministically in headless tests. Capped at the
            // in-flight gap so consumed never outruns decoded.
            const std::uint64_t elapsed = static_cast<std::uint64_t>(
                deltaSeconds * static_cast<float>(layer.sampleRate));
            const std::uint64_t inFlight =
                layer.stream.totalFramesDecoded >= layer.stream.totalFramesConsumed
                    ? layer.stream.totalFramesDecoded - layer.stream.totalFramesConsumed
                    : 0;
            notifyStreamFramesConsumed(layer.stream, std::min(elapsed, inFlight));
        }

        // 4. Skip the rest for a loaded-but-stopped layer.
        if (!layer.active)
        {
            continue;
        }

        // 5. Top the buffer ring back up to the keep-ahead target.
        const bool wasFinished = layer.stream.finished;
        refillLayer(layer);
        if (layer.stream.finished && !wasFinished)
        {
            // refillLayer already called stopLayer on a finished track.
            continue;
        }

        // 6. Resume if OpenAL stopped the source on a transient underrun
        //    (queue briefly emptied between ticks). Only when we still
        //    have queued buffers to play.
        if (deviceLive && layer.source != 0)
        {
            ALint state = 0;
            alGetSourcei(layer.source, AL_SOURCE_STATE, &state);
            ALint queued = 0;
            alGetSourcei(layer.source, AL_BUFFERS_QUEUED, &queued);
            if (state == AL_STOPPED && queued > 0)
            {
                alSourcePlay(layer.source);
                if (!m_warnedUnderrun)
                {
                    Logger::warning("[AudioMusicPlayer] streaming-music underrun "
                                    "(queue emptied); resuming. Recurrence means "
                                    "the chunk size is too small for the IO budget.");
                    m_warnedUnderrun = true;
                }
            }
        }
    }

    // Drain ready stingers onto the one-shot path, routed through the
    // Music bus so they duck with the streaming layers.
    const std::vector<MusicStinger> ready = m_stingers.advance(deltaSeconds);
    for (const MusicStinger& s : ready)
    {
        m_engine.playSound2D(s.clipPath, s.volume, AudioBus::Music,
                             SoundPriority::Normal);
    }
}

void AudioMusicPlayer::enqueueStinger(const MusicStinger& stinger)
{
    m_stingers.enqueue(stinger);
}

bool AudioMusicPlayer::isLayerLoaded(MusicLayer layer) const
{
    return layerFor(layer).decoder != nullptr;
}

bool AudioMusicPlayer::isLayerPlaying(MusicLayer layer) const
{
    return layerFor(layer).active;
}

float AudioMusicPlayer::getLayerGain(MusicLayer layer) const
{
    return layerFor(layer).gain.currentGain;
}

float AudioMusicPlayer::getLayerTargetGain(MusicLayer layer) const
{
    return layerFor(layer).gain.targetGain;
}

float AudioMusicPlayer::getLayerBufferedSeconds(MusicLayer layer) const
{
    return computeStreamBufferedSeconds(layerFor(layer).stream);
}

std::size_t AudioMusicPlayer::getActiveLayerCount() const
{
    std::size_t count = 0;
    for (const StreamingLayer& layer : m_layers)
    {
        if (layer.active && layer.source != 0)
        {
            ++count;
        }
    }
    return count;
}

std::size_t AudioMusicPlayer::getLayerFreeBufferCount(MusicLayer layer) const
{
    return layerFor(layer).freeBuffers.size();
}

const MusicStreamState& AudioMusicPlayer::getLayerStreamState(MusicLayer layer) const
{
    return layerFor(layer).stream;
}

bool AudioMusicPlayer::getLayerDecoderAtEof(MusicLayer layer) const
{
    return layerFor(layer).decoderJustHitEof;
}

} // namespace Vestige
