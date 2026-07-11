// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_music_player.h
/// @brief Phase 10.9 Slice 8 W8 (part 2/2) — streaming-music player.
///
/// `AudioMusicPlayer` is the consumer the three existing music
/// modules were always missing:
///   - `audio_music_stream.{h,cpp}` (the `planStreamTick` decode
///     state machine),
///   - `audio_music.{h,cpp}` (`MusicLayer`, `MusicLayerState` gain
///     slew, `intensityToLayerWeights`, `MusicStingerQueue`),
///   - `audio_engine.{h,cpp}` (the OpenAL source pool + buffer LRU).
///
/// It holds one streaming voice per active `MusicLayer`: an
/// stb_vorbis decoder pulling chunks, a ring of OpenAL buffers
/// queued onto a pool source via `alSourceQueueBuffers`, and a
/// slewed per-layer gain pushed to `AL_GAIN` each frame. The
/// `MusicSystem` ISystem wrapper (`engine/systems/music_system.h`)
/// ticks it once per frame; gameplay drives the mix through
/// `MusicSystem::setIntensity`.
///
/// Design: `docs/phases/phase_10_audio_music_player_design.md`
/// (user-approved 2026-06-03). Three numbers/behaviours diverge
/// from the signed-off doc to resolve contradictions found at
/// implementation time — each is flagged `[design-reconcile
/// 2026-06-04]` inline below and recorded in the doc's ## Status
/// and CHANGELOG:
///   1. `kBuffersPerLayer` is 8, not 3 — three 4096-frame buffers
///      hold only 0.256 s, below the 0.30 s `minSecondsBuffered`
///      keep-ahead target the planner aims for, so the ring could
///      never reach it.
///   2. `update()` tops the ring up to the planner's keep-ahead
///      target each tick (a bounded refill loop), not exactly one
///      chunk — so a single post-`playLayer` tick reaches the
///      keep-ahead window regardless of `deltaSeconds`.
///   3. With no AL device, `update()` advances the consume counter
///      by `deltaSeconds × sampleRate` (capped at in-flight) so the
///      planner's back-pressure + loop logic progress
///      deterministically in headless tests (the doc named this
///      intent but left the mechanism unspecified).
#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "audio/audio_music.h"
#include "audio/audio_music_stream.h"

// stb_vorbis declarations only — the implementation symbols come from
// the existing external/stb/stb_vorbis_impl.cpp TU (same pattern as
// audio_clip.cpp). A forward declaration would be enough for the
// header, but the member array needs no stb type, so we keep the
// header free of the stb include and forward-declare instead.
typedef struct stb_vorbis stb_vorbis;

namespace Vestige
{

class AudioEngine;

/// @brief OpenAL buffers per streaming layer.
///
/// [design-reconcile 2026-06-04] The design doc specified 3 (the
/// classic triple-buffer). At `framesPerChunk = 4096` / 48 kHz a
/// buffer is 0.085 s, so 3 buffers hold only 0.256 s — below the
/// planner's 0.30 s `minSecondsBuffered` keep-ahead target and far
/// below its 0.60 s `maxSecondsBuffered` ceiling. 8 buffers hold
/// 0.683 s, covering the full keep-ahead window. Hoisted to
/// namespace scope so `StreamingLayer` can use it as an array
/// dimension.
inline constexpr std::size_t kBuffersPerLayer = 8;

/// @brief Per-layer streaming voice state.
struct StreamingLayer
{
    MusicLayer       id = MusicLayer::Ambient;
    std::string      clipPath;            ///< .ogg track for this layer.
    stb_vorbis*      decoder = nullptr;
    MusicStreamState stream;
    MusicLayerState  gain;

    /// AL source ID acquired from the engine pool while playing;
    /// 0 when idle. Released back via `AudioEngine::releaseSource`
    /// on stopLayer / clearAllLayers.
    unsigned int     source = 0;

    /// Full set of AL buffers owned by this layer. Allocated at
    /// loadLayer() time, deleted at unloadLayer(). Population is
    /// stable for the layer's lifetime; `freeBuffers` tracks which
    /// of these are currently NOT queued on the source.
    std::array<unsigned int, kBuffersPerLayer> buffers{};

    /// Subset of `buffers` currently NOT bound to the source. Seeded
    /// with every entry from `buffers` at loadLayer() time (all
    /// start free). Each enqueue pops one; each unqueue pushes one
    /// back.
    std::deque<unsigned int> freeBuffers;

    int  channels = 0;
    int  sampleRate = 48000;
    bool active = false;                  ///< false = loaded but not playing.

    /// Transient EOF flag — set on the tick the decoder returns 0
    /// frames, cleared the tick the rewind runs. MUST NOT be
    /// confused with `MusicStreamState::trackFullyDecodedOnce`,
    /// which is sticky (true forever after the first EOF). Passing
    /// the sticky flag to `planStreamTick` would re-fire the rewind
    /// every frame.
    bool decoderJustHitEof = false;

    // AX12 — editor spectrum viewer tap (active only while the Debug tab shows).
    // `analysisGain` is this frame's pre-solo content gain (stashed in update
    // step 2); `analysisFrame` accumulates this frame's newly-decoded mono-float
    // PCM, concatenated across decode chunks, for one submit per layer per frame.
    float              analysisGain = 0.0f;
    std::vector<float> analysisFrame;
};

/// @brief Streaming-music player. One voice per active layer.
///
/// Lifetime: owned by `Engine` (a `std::unique_ptr`), constructed
/// after `AudioSystem` so the `AudioEngine&` it borrows is live.
/// `MusicSystem` holds a reference and ticks `update()` each frame.
class AudioMusicPlayer
{
public:
    explicit AudioMusicPlayer(AudioEngine& engine);
    ~AudioMusicPlayer();

    AudioMusicPlayer(const AudioMusicPlayer&) = delete;
    AudioMusicPlayer& operator=(const AudioMusicPlayer&) = delete;

    /// Loads (but does not start) a track for the named layer. Opens
    /// the decoder, allocates AL buffers, seeds `freeBuffers` with
    /// all `kBuffersPerLayer` entries, leaves the source unacquired.
    /// Returns false on sandbox rejection or decode-open failure.
    /// Replaces any existing track on this layer (unloads it first).
    bool loadLayer(MusicLayer layer, const std::string& clipPath);

    /// Starts streaming the loaded layer. No-op if already playing or
    /// no track loaded. Acquires an AL source at `SoundPriority::Normal`,
    /// primes the buffer ring to the keep-ahead target, and starts
    /// playback. Sets `targetGain` to 1.0; gameplay drives
    /// `setLayerTargetGain` afterwards.
    void playLayer(MusicLayer layer);

    /// Sets the gain the per-frame slew converges to. Drive this from
    /// the intensity → layerWeights mapping each frame, or set
    /// directly for hand-authored crossfades.
    void setLayerTargetGain(MusicLayer layer, float targetGain);

    /// Sets the layer's loop policy: true = loop forever (the default),
    /// false = play once then finish. Maps to `MusicStreamState::maxLoops`
    /// (-1 / 0). The scene-load path drives this from
    /// `MusicSceneSettings::loopAll`.
    void setLayerLooping(MusicLayer layer, bool loop);

    /// Stops the layer and releases its source back to the pool.
    /// Decoder + AL buffers stay allocated so playLayer is cheap to
    /// resume. unloadLayer() releases those too.
    void stopLayer(MusicLayer layer);
    void unloadLayer(MusicLayer layer);

    /// Stops every layer + releases every source + closes every
    /// decoder. Called by MusicSystem / the scene-load path on
    /// scene unload.
    void clearAllLayers();

    /// Drives every loaded layer's target gain from a single intensity
    /// signal via `intensityToLayerWeights(intensity, silence)`. Caller
    /// still loads + plays the layers it wants.
    void applyIntensity(float intensity, float silence = 0.0f);

    /// Per-frame tick: advance gain slews, push gain to AL, unqueue
    /// spent buffers, top up the stream buffers, mark finished layers,
    /// drain the stinger queue.
    ///
    /// Contract:
    ///   - `deltaSeconds <= 0` is a true no-op (no slew, no decode, no
    ///     AL probes) — matches `MusicStingerQueue::advance`'s clamp.
    ///   - With no AL device (`AudioEngine::isAvailable() == false`),
    ///     AL calls are skipped but the decode/consume state machine
    ///     still advances against `deltaSeconds` so headless tests stay
    ///     deterministic.
    void update(float deltaSeconds);

    /// Enqueues a one-shot stinger on the shared queue. `update()`
    /// pops ready stingers and routes them to `AudioEngine::playSound2D`
    /// on `AudioBus::Music`, so they duck under the same Music slider as
    /// the streaming layers.
    void enqueueStinger(const MusicStinger& stinger);

    // ---- Introspection (tests / mixer panel) -------------------------

    bool        isLayerLoaded(MusicLayer layer) const;
    bool        isLayerPlaying(MusicLayer layer) const;
    float       getLayerGain(MusicLayer layer) const;
    float       getLayerTargetGain(MusicLayer layer) const;
    float       getLayerBufferedSeconds(MusicLayer layer) const;

    /// Number of layers in the `active && source != 0` state. Lets the
    /// mixer panel show a "N music voices live" diagnostic (and the
    /// clearAllLayers no-leak test assert) without poking AudioEngine.
    std::size_t getActiveLayerCount() const;

    /// Number of free (un-queued) buffers for a layer — test-only
    /// introspection for the ring-management assertions (rows 1, 12).
    std::size_t getLayerFreeBufferCount(MusicLayer layer) const;

    /// Pending (not-yet-fired) stingers on the shared queue. Lets the
    /// stinger test (row 7) observe that `update` drained the queue
    /// without a spy on the concrete `AudioEngine`.
    std::size_t getPendingStingerCount() const { return m_stingers.pending(); }

    /// Test-only: the layer's underlying `MusicStreamState` for
    /// assertions on raw fields (`loopCount`, `finished`,
    /// `trackFullyDecodedOnce`). NOT for production callers — the
    /// player owns the state and a held reference goes stale on the
    /// next `update`.
    const MusicStreamState& getLayerStreamState(MusicLayer layer) const;

    /// Test-only: the transient per-tick EOF flag (row 13).
    bool getLayerDecoderAtEof(MusicLayer layer) const;

private:
    StreamingLayer&       layerFor(MusicLayer layer);
    const StreamingLayer& layerFor(MusicLayer layer) const;

    /// Decode + queue chunks until the planner's keep-ahead target is
    /// met or no free buffer remains. Bounded by `kBuffersPerLayer + 1`
    /// iterations so a corrupt/empty file (perpetual 0-frame reads)
    /// can't spin. Returns once `planStreamTick` reports no work.
    void refillLayer(StreamingLayer& layer);

    /// Decode exactly one chunk per the planner and queue it. Returns
    /// the planner's decision so the refill loop can stop. Pulls the
    /// transient EOF flag, not the sticky one.
    StreamTickPlan stepDecodeOnce(StreamingLayer& layer);

    AudioEngine& m_engine;
    std::array<StreamingLayer, MusicLayerCount> m_layers;
    MusicStingerQueue m_stingers;
    bool m_warnedUnderrun = false;        ///< One-shot underrun warning latch.
    bool m_warnedCorruptDecode = false;   ///< One-shot corrupt-input latch.
};

} // namespace Vestige
