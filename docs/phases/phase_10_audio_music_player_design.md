# Phase 10 Audio — Streaming Music Player Design (W8 part 2/2)

## Status

**Reviewed and approved 2026-06-03** (user sign-off, milnet01) — per
project `CLAUDE.md` rule 1 (research → design → review → code). Cleared
for implementation; the [Step plan](#step-plan) below is the build order.

**Implemented 2026-06-04 (W8 part 2/2 shipped).** Three contradictions in
this approved doc surfaced at implementation time (the cold-eyes loop
missed them); the user approved reconciling them in-flight rather than
re-looping the design. Each is flagged `[design-reconcile 2026-06-04]` in
`engine/audio/audio_music_player.{h,cpp}` and recorded in `CHANGELOG.md`:

1. **Buffer-ring size 3 → 8.** Three 4096-frame buffers hold only
   `3 × 4096 / 48000 = 0.256 s` — below the `minSecondsBuffered = 0.30 s`
   keep-ahead target the planner aims for, and far below the
   `maxSecondsBuffered = 0.60 s` ceiling (which needs 8 buffers). The
   ring could never reach the keep-ahead window. `kBuffersPerLayer = 8`
   (0.683 s) fixes it. The §"Memory footprint" 324 KB figure re-pins to
   ~768 KB (8 × 4096 × 2 ch × 2 B × 6 layers) — still trivial.
2. **Per-tick decode policy.** The doc's algorithm decoded exactly one
   chunk per `update()`, but `planStreamTick` clamps to one chunk/call,
   so a single post-`playLayer` tick could never reach the 0.30 s
   keep-ahead (row 2). `update()` (and `playLayer`'s prime) now run a
   *bounded refill loop* topping the ring up to the keep-ahead target
   each tick. Row 13's per-tick EOF→rewind choreography is therefore
   unobservable (the seam is crossed inside one tick); it was reworked to
   pin the observable post-conditions (loopCount, finished, transient-EOF
   reset, playback continues) instead.
3. **Headless consumption.** The doc said "state-machine progress ticks
   against deltaSeconds" but the algorithm only advanced the consume
   counter via AL unqueue (a no-op with no device). With no device,
   `update()` now advances the consume counter by `deltaSeconds ×
   sampleRate` (capped at in-flight) so the planner's back-pressure +
   loop logic progress deterministically in headless tests.

One spec gap was also filled: `MusicSceneSettings::loopAll` had no wiring
to the stream's loop policy — added `AudioMusicPlayer::setLayerLooping`
(maps to `MusicStreamState::maxLoops`), driven from `loopAll` at the
scene-load caller.

### Cold-eyes review log (global `CLAUDE.md` rule 14)

Reconstructed from the git audit trail — the loops were run and committed
at the time, but the per-loop log was not recorded inline (the gap this
entry closes):

- **Loop 1 — converged** (`3ae6ca4`, 2026-05-18): dedicated `/cold-eyes`
  pass on this doc; ~1121-line revision (architecture, API surface,
  per-frame algorithm, and performance budget reworked to a clean pass).
- **Cross-doc sweep** (`382e48c`, 2026-05-18): CE1–CE17 documentation
  follow-ups, 4 cold loops across the edited spec/design docs. W8-relevant
  fix landed: CE11 (audio §5 — stb_vorbis as the OGG decoder) plus a
  citation de-line-numbering touch. No W8 finding was left open — the one
  deferred item (CE3, land `engine/utils/result.h`) is an unrelated code
  slice, and CE18 was a pre-existing physics-spec drift.

Closes Phase 10.9 Slice 8 **W8 (part 2/2)** in `ROADMAP.md` — wires
the streaming music path. W8 (part 1/2) shipped 2026-05-02
(`74a34fc`): the per-buffer LRU cache + per-scene flush. Part 2/2
fills the gap this exposed — `MusicStreamState` and the layer
machinery have no consumer today, so streaming music is dead code
even though the state machines are unit-tested.

## Table of contents

1. [What exists today](#what-exists-today)
2. [What `docs/engine/audio/spec.md` already promises](#what-docsengineaudiospecmd-already-promises)
3. [Push-back (global `CLAUDE.md` rule 9)](#push-back-global-claudemd-rule-9)
4. [CPU / GPU placement (project `CLAUDE.md` rule 7)](#cpu--gpu-placement-project-claudemd-rule-7)
5. [Architecture](#architecture)
6. [API surface](#api-surface)
7. [Per-frame algorithm (`AudioMusicPlayer::update`)](#per-frame-algorithm-audiomusicplayerupdate)
8. [`MusicSystem` ISystem wrapper](#musicsystem-isystem-wrapper)
9. [SceneSerializer integration](#sceneserializer-integration)
10. [Decoder concerns](#decoder-concerns)
11. [Performance budget](#performance-budget)
12. [Accessibility](#accessibility)
13. [Test plan (global `CLAUDE.md` rule 12 verify-step)](#test-plan-global-claudemd-rule-12-verify-step)
14. [Step plan](#step-plan)
15. [What's out of scope here](#whats-out-of-scope-here)
16. [References](#references)

## What exists today

| Module | Status | What it gives us |
|---|---|---|
| `engine/audio/audio_music_stream.{h,cpp}` | shipped, no consumer | `MusicStreamState` data, `planStreamTick` decision-tree |
| `engine/audio/audio_music.{h,cpp}` | shipped, no consumer | `MusicLayer` enum (6 layers), `MusicLayerState` slew, `intensityToLayerWeights`, `MusicStingerQueue` |
| `engine/audio/audio_engine.{h,cpp}` | shipped | OpenAL wrapper, source pool (`MAX_SOURCES = 32`), buffer cache (LRU), `playSound`/`playSoundSpatial`/`playSound2D` for **one-shots only** — no streaming-source API today |
| `engine/audio/audio_clip.cpp` | shipped | Whole-file decode for WAV/MP3/FLAC/OGG via dr_libs + stb_vorbis. OGG path uses `stb_vorbis_decode_filename` — fine for short clips, ~34 MB RAM for a 3-minute 48 kHz stereo track |
| `engine/audio/audio_mixer.h` | shipped | Six-bus mixer including `AudioBus::Music` |
| `external/stb/stb_vorbis.c` + `stb_vorbis_impl.cpp` | vendored | stb_vorbis single-file lib; the impl TU at `external/stb/stb_vorbis_impl.cpp` includes it without `STB_VORBIS_HEADER_ONLY`; other TUs (e.g. `audio_clip.cpp`) include it with the header-only macro |

What is missing:

- A streaming OGG decoder that pulls samples a chunk at a time.
- A capability class that holds N streaming voices (one per active
  music layer), drives `planStreamTick`, queues decoded PCM onto
  an OpenAL source via `alSourceQueueBuffers`, unqueues spent
  buffers, and applies the layer's `currentGain` each frame.
- An ISystem wrapper that ticks the capability class per frame.
- A scene-load path that hands the wrapper a per-scene music
  configuration.

## What `docs/engine/audio/spec.md` already promises

The audio subsystem spec already names the player class and its
ISystem wrapper, so the design that lands here has to match.
Verified pointers:

| `docs/engine/audio/spec.md:` | Existing commitment |
|---|---|
| §2 scope (l.31) | Lists `AmbientSystem` / `AudioSystem` / `MusicSystem` as the three `ISystem` implementations. The `MusicSystem` name is the canonical one. |
| §3 inventory (l.92) | Lists `MusicLayer*`, `MusicStingerQueue`, `MusicStreamState`, `planStreamTick` as the music primitives, citing `audio_music.h:59, 144` and `audio_music_stream.h:45, 64, 119`. |
| §3 inventory (l.96) | Sibling `AudioSystem` row — the `ISystem` shape this design parallels for music. |
| §5 workflow (l.259-264) | `MusicSystem::update` (separate `ISystem`, Update phase) drives `intensityToLayerWeights` → `advanceMusicLayer` → `MusicStingerQueue::advance` → `planStreamTick` + per-voice `alSourceQueueBuffers`. |
| §6 perf table (l.317) | Budget line: `MusicSystem::update` (4 layers + queue + 1 stream) < 0.2 ms — pinned at "TBD — measure by Phase 11 entry". |
| §8 (l.345) | Names buffer underflow on streaming music as a recovery case (`planStreamTick` returns more frames next tick; raise `minSecondsBuffered` if it recurs). |
| §11 tests (l.366) | Lists `tests/test_audio_music_stream.cpp` (16 tests) for the planner. |

**Conflicts surfaced.** Two doc-vs-doc items the design has to
reconcile:

1. spec.md §5 (l.264) says "the engine-side music streamer
   dispatches the actual `dr_libs` calls and `alSourceQueueBuffers`".
   `dr_libs` is correct for WAV / MP3 / FLAC but the project's
   chosen OGG path is **stb_vorbis** (verified `audio_clip.cpp:15-17`
   uses `STB_VORBIS_HEADER_ONLY` + `#include "stb_vorbis.c"`). The
   spec line is loose; this design uses stb_vorbis for OGG and
   does not preclude a future dr_libs path for WAV/MP3 streaming.
   spec.md §5 needs a follow-up edit (tracked in step 7 below).

2. spec.md names the class `MusicSystem` (an `ISystem`); this
   design splits the work into `MusicSystem` (the thin ISystem
   wrapper) plus an `AudioMusicPlayer` capability class
   (streaming pipeline + decoder + AL queue work) that
   `MusicSystem` owns. Parallel to `AudioSystem` owning
   `AudioEngine`. Both names live in spec.md after step 7's edit.

## Push-back (global `CLAUDE.md` rule 9)

Before settling on the implementation, the simpler-path question:

**Argument for the full streaming player.** A 3-minute OGG at
48 kHz stereo decodes to ~34 MB of PCM; six layers × that is
~204 MB. Heap fragmentation and the second-or-two CPU stall of
decoding six tracks on scene change are real. The project already
has the pure state machine; the design here is the glue.

**Argument for a "load everything as one-shots" stop-gap.** A
single-layer "play this music file at scene start" hook over
`playSound2D(path, vol, AudioBus::Music, …)` is maybe 50 lines and
unblocks scene-music visuals while we figure out the real player.
Risk: it silently locks in a design where the layer system can't
carry its weight — crossfading six pre-decoded 30-MB buffers via
one-shot starts is the wrong shape (no per-frame gain control, no
fade slewing without source-pool churn, and re-uploading on every
scene swap is ~200 MB of CPU→GPU buffer transfer).

**Resolution.** Go with the full streaming design here. The
existing `MusicStreamState` exists because the team already chose
streaming over one-shot (see `audio_music_stream.h:5-31`
file-header docstring + spec.md §5); re-deriving that choice now
would be churn. Scope the first cut narrow (single layer working
end-to-end, not all six) and grow.

## CPU / GPU placement (project `CLAUDE.md` rule 7)

**Decision: CPU, end-to-end.**

| Stage | Cost shape | Placement reason |
|---|---|---|
| Streaming decode (stb_vorbis) | Branching + IO, ~16 KB chunks | CPU; chunks too small for GPU dispatch overhead to win |
| Layer gain slew (`advanceMusicLayer`) | One FMA per layer per frame | CPU; trivial |
| `planStreamTick` | Pure-function decision tree | CPU |
| OpenAL queue management | Stateful, driver-mediated | CPU; OpenAL Soft owns its mixer thread already |

No CPU/GPU parity test needed for this slice (rule 7 requires one
only when dual-impl is in play).

## Architecture

Two layers, parallel to `AudioSystem` / `AudioEngine`:

```
                       MusicSystem (ISystem)
                              |  owns
                              v
                       AudioMusicPlayer
                              |
                +-------------+-------------+
                |   (one entry per layer)   |
          StreamingLayer[MusicLayerCount]   |
                |                           |
     +----------+----------+                |
     |          |          |                |
  MusicStream- stb_vorbis  AL streaming    MusicLayerState
  State (state) handle    source +          (slewed gain)
                          ring of           |
                          kBuffersPerLayer  v
                          AL buffers   per-frame gain
                                       via alSourcef(AL_GAIN)
                                                |
                                                v
                                AudioEngine source pool
                                (acquireSource / releaseSource;
                                 one source per *active* layer)
                                                |
                                                v
                                          OpenAL Soft
```

### `MusicSystem` (ISystem wrapper)

`engine/systems/music_system.{h,cpp}` — new file. Thin `ISystem`
implementor. Holds:

- A reference to the `AudioMusicPlayer` (owned by the engine
  registry, same lifetime model as `AudioEngine` / `AudioSystem`).
- Current `intensity` + `silence` floats — driven externally
  (gameplay code calls `MusicSystem::setIntensity`).

Per-frame work in `MusicSystem::update(dt)`:

1. `applyIntensity(intensity, silence)` on the player (maps to
   per-layer target gains via `intensityToLayerWeights`).
2. `m_player.update(dt)` does the actual streaming work.

Default `getUpdatePhase()` → `UpdatePhase::Update` (per spec.md
§5). `AudioSystem` runs at `PostCamera` to sync listener; music
needs no camera dependence, so it stays in the default Update
slot.

### `AudioMusicPlayer` (capability class)

`engine/audio/audio_music_player.{h,cpp}` — new file. Class only
depends on `AudioEngine` + the three existing audio modules. No
`Scene*` includes in the header (the scene-side wiring lives in
the serializer's load path; see §"SceneSerializer integration").

### Per-layer state struct

```cpp
namespace Vestige
{

/// Triple-buffer per layer — standard streaming size. Each
/// buffer holds one chunk = MusicStreamState::framesPerChunk
/// samples. Hoisted to namespace scope so StreamingLayer (also
/// namespace-scope) can use it in its array dimension.
inline constexpr std::size_t kBuffersPerLayer = 3;

struct StreamingLayer
{
    MusicLayer       id;
    std::string      clipPath;       ///< .ogg track for this layer
    stb_vorbis*      decoder = nullptr;
    MusicStreamState stream;
    MusicLayerState  gain;

    /// AL source ID acquired from the engine pool while playing;
    /// 0 when idle. Released back via AudioEngine::releaseSource
    /// on stopLayer / clearAllLayers.
    unsigned int     source = 0;

    /// Full set of AL buffers owned by this layer. Allocated at
    /// loadLayer() time, deleted at unloadLayer(). Population is
    /// stable for the layer's lifetime; the freeBuffers queue
    /// below tracks which of these are currently NOT in the AL
    /// source's queue and may be re-uploaded.
    std::array<unsigned int, kBuffersPerLayer> buffers{};

    /// Subset of `buffers` currently NOT bound to the source.
    /// Populated with every entry from `buffers` at loadLayer()
    /// time (all three start free). Each enqueue pops one;
    /// each unqueue pushes one back. Empty queue + non-empty AL
    /// source-queue is the running steady state at high load.
    std::deque<unsigned int> freeBuffers;

    int  channels = 0;
    int  sampleRate = 48000;
    bool active = false;             ///< false = loaded but not playing

    /// Transient EOF flag — set on the tick where
    /// `stb_vorbis_get_samples_short_interleaved` returns 0,
    /// cleared the tick where `stb_vorbis_seek_start` runs.
    /// MUST NOT confuse with `MusicStreamState::trackFullyDecodedOnce`
    /// which is sticky (set forever after the first EOF). Passing
    /// the sticky flag to `planStreamTick` would re-fire the
    /// rewind every frame for the rest of the session.
    bool decoderJustHitEof = false;
};

} // namespace Vestige
```

`AudioMusicPlayer` holds a fixed
`std::array<StreamingLayer, MusicLayerCount>` keyed by
`MusicLayer`. No dynamic allocation per layer.

### Memory footprint

Per-layer, all-active:

- 3 AL buffers × 4096 frames × 2 channels × 2 bytes = 48 KB PCM
- `MusicStreamState` + `MusicLayerState` + decoder handle ≈ 6 KB

Six layers × 54 KB ≈ **324 KB total** working set for the player
itself (excluding the AudioEngine source-pool slots, which are
already accounted for in the parent subsystem). Well under the
spec.md §6 budget envelope.

> Note: the 4096-frame chunk size in this table is
> `MusicStreamState::framesPerChunk` (`audio_music_stream.h:55`).
> The "Out of scope" section lists dynamic chunk-size tuning as a
> follow-up; if that knob ever lands, re-pin this table.

## API surface

```cpp
namespace Vestige
{

class AudioEngine;

class AudioMusicPlayer
{
public:
    explicit AudioMusicPlayer(AudioEngine& engine);
    ~AudioMusicPlayer();

    AudioMusicPlayer(const AudioMusicPlayer&) = delete;
    AudioMusicPlayer& operator=(const AudioMusicPlayer&) = delete;

    /// Loads (but does not start) a track for the named layer.
    /// Opens the decoder, allocates AL buffers, populates
    /// `freeBuffers` with all kBuffersPerLayer entries, leaves
    /// the source unacquired. Returns false on decode-open
    /// failure or sandbox rejection. Replaces any existing track
    /// on this layer (unloads it first).
    bool loadLayer(MusicLayer layer, const std::string& clipPath);

    /// Starts streaming the loaded layer. No-op if already
    /// playing or no track loaded. Acquires an AL source from
    /// the pool at priority `SoundPriority::Normal`. Sets
    /// `targetGain` to 1.0; gameplay drives `setLayerTargetGain`
    /// afterwards.
    void playLayer(MusicLayer layer);

    /// Sets the target gain the slew converges to. Use this from
    /// the gameplay-driven intensity → layerWeights mapping each
    /// frame, OR set directly for hand-authored crossfades.
    void setLayerTargetGain(MusicLayer layer, float targetGain);

    /// Stops the layer and releases its source back to the pool.
    /// Decoder + AL buffers stay allocated so playLayer is cheap
    /// to resume. unloadLayer() releases those too.
    void stopLayer(MusicLayer layer);
    void unloadLayer(MusicLayer layer);

    /// Stops every layer + releases every source + closes every
    /// decoder. Called by MusicSystem on scene unload.
    void clearAllLayers();

    /// Convenience: drives every loaded layer's target gain from
    /// a single intensity signal using
    /// intensityToLayerWeights(intensity, silence). Caller still
    /// loads + plays the layers it wants.
    void applyIntensity(float intensity, float silence = 0.0f);

    /// Per-frame tick: advance gain slews, refill stream
    /// buffers, unqueue spent buffers, mark finished layers.
    /// Called by MusicSystem::update.
    ///
    /// Contract:
    ///   - `deltaSeconds` MUST be ≥ 0. The player early-outs for
    ///     `dt <= 0` (no slew, no decode, no AL probes). This
    ///     matches `MusicStingerQueue::advance`'s clamp.
    ///   - Becomes a no-op when AudioEngine reports
    ///     `!isAvailable()` (no AL device — the player still
    ///     tracks state-machine progress against `deltaSeconds`
    ///     so deterministic tests stay valid).
    void update(float deltaSeconds);

    /// Layer-stinger helper: enqueues a one-shot stinger on the
    /// player's shared MusicStingerQueue. update() pops ready
    /// stingers and routes them to AudioEngine::playSound2D for
    /// one-shot playback on AudioBus::Music — stingers
    /// duck/master alongside the streaming layers under the
    /// existing mixer's Music slider, which is the user-facing
    /// volume control for "music".
    void enqueueStinger(const MusicStinger& stinger);

    // ---- Introspection (for tests / mixer panel) -----------------

    bool   isLayerLoaded(MusicLayer layer) const;
    bool   isLayerPlaying(MusicLayer layer) const;
    float  getLayerGain(MusicLayer layer) const;
    float  getLayerTargetGain(MusicLayer layer) const;
    float  getLayerBufferedSeconds(MusicLayer layer) const;

    /// Number of layers currently in the `active && source != 0`
    /// state. Used by tests for the clearAllLayers no-leak
    /// assertion; exposed publicly so the mixer panel can show a
    /// "N music voices live" diagnostic without poking
    /// AudioEngine.
    std::size_t getActiveLayerCount() const;

    /// Test-only introspection: returns the layer's underlying
    /// `MusicStreamState` for assertions that need the raw
    /// fields (`loopCount`, `finished`, `trackFullyDecodedOnce`)
    /// — rows 5 and 13 of the test plan rely on this. NOT for
    /// production callers; the player owns the state and a
    /// non-test consumer holding a const reference past the
    /// next `update` tick will read stale values.
    const MusicStreamState& getLayerStreamState(MusicLayer layer) const;

private:
    AudioEngine& m_engine;
    std::array<StreamingLayer, MusicLayerCount> m_layers;
    MusicStingerQueue m_stingers;
};

} // namespace Vestige
```

Header is ~100 lines including doc-comments. Cpp ~280 LoC
(decoder wrapper + the per-tick stream pump + book-keeping).

## Per-frame algorithm (`AudioMusicPlayer::update`)

Early-out: if `deltaSeconds <= 0.0f`, return without touching any
layer (no gain slew, no AL probes, no decode work). Tests that
need to verify "no-op tick" rely on this guard.

Then for each `StreamingLayer` in `m_layers`:

1. **Skip if not loaded** (`decoder == nullptr`).
2. **Advance gain slew**: `advanceMusicLayer(layer.gain,
   deltaSeconds)`.
3. **Apply gain to source**: `alSourcef(layer.source, AL_GAIN,
   layer.gain.currentGain)` if `layer.source != 0`.
4. **Unqueue spent buffers**:
   ```cpp
   ALint processed = 0;
   alGetSourcei(layer.source, AL_BUFFERS_PROCESSED, &processed);
   while (processed-- > 0)
   {
       ALuint freed = 0;
       alSourceUnqueueBuffers(layer.source, 1, &freed);
       ALint sizeBytes = 0;
       alGetBufferi(freed, AL_SIZE, &sizeBytes);
       const std::uint64_t framesConsumed =
           static_cast<std::uint64_t>(sizeBytes)
           / (sizeof(short) * layer.channels);
       notifyStreamFramesConsumed(layer.stream, framesConsumed);
       layer.freeBuffers.push_back(freed);
   }
   ```
5. **Plan + decode + queue**: short-circuit when no free buffer is
   available so the planner's `rewindForLoop` decision can't be
   silently dropped on a buffer-starved tick.

   ```cpp
   if (layer.freeBuffers.empty())
   {
       // All three buffers in flight on the AL source. Skip
       // decode this tick; unqueue (step 4) will free one
       // shortly. Crucially: do NOT call planStreamTick on a
       // buffer-starved tick — a stale `rewindForLoop` could
       // fire stb_vorbis_seek_start and then no decode follows.
       continue;
   }

   // Pass the *transient* EOF flag, not the sticky
   // MusicStreamState::trackFullyDecodedOnce. The transient
   // flag is set the tick gotFrames == 0 and cleared the tick
   // the rewind runs. The sticky flag stays true forever once
   // set, so passing it would re-fire rewind every frame.
   StreamTickPlan plan = planStreamTick(layer.stream,
                                         layer.decoderJustHitEof);
   if (plan.rewindForLoop)
   {
       stb_vorbis_seek_start(layer.decoder);
       layer.decoderJustHitEof = false;
   }
   if (plan.framesToDecode > 0)
   {
       // planStreamTick already clamps to one chunk per tick
       // (audio_music_stream.cpp:106-107). Decode exactly the
       // planner's request — back-pressure does amortisation.
       std::vector<short> scratch(plan.framesToDecode
                                   * layer.channels);
       const int gotFrames =
           stb_vorbis_get_samples_short_interleaved(
               layer.decoder,
               layer.channels,
               scratch.data(),
               static_cast<int>(scratch.size()));
       // EOF detection: stb_vorbis returns 0 frames on EOF
       // OR on an unrecoverable decode failure (corrupt
       // packet). Both are treated as "end of usable stream"
       // here — the loop branch handles the EOF case; for a
       // genuinely corrupt file the player will rewind, hit
       // the same bad packet, and loop fast — log a one-shot
       // warning the first time `reachedEof` fires before
       // the expected track length so the operator can spot
       // the corrupt-input case. Partial reads (gotFrames > 0
       // but < framesToDecode) are packet-boundary short
       // reads and NOT EOF.
       const bool reachedEof = (gotFrames == 0);
       if (reachedEof)
       {
           layer.decoderJustHitEof = true;
       }
       if (gotFrames > 0)
       {
           ALuint dst = layer.freeBuffers.front();
           layer.freeBuffers.pop_front();
           alBufferData(dst,
                        layer.channels == 1 ? AL_FORMAT_MONO16
                                            : AL_FORMAT_STEREO16,
                        scratch.data(),
                        static_cast<ALsizei>(gotFrames
                                              * layer.channels
                                              * sizeof(short)),
                        layer.sampleRate);
           alSourceQueueBuffers(layer.source, 1, &dst);
       }
       notifyStreamFramesDecoded(layer.stream,
                                  static_cast<std::uint64_t>(
                                      gotFrames),
                                  reachedEof);
   }
   if (plan.trackFinished)
   {
       stopLayer(layer.id);
   }
   ```

   **One-tick rewind latency.** On the tick where `gotFrames`
   first hits 0, `decoderJustHitEof` flips from `false` to `true`
   but `planStreamTick` has already run for the tick with the
   old value, so `rewindForLoop` is NOT set yet. The rewind
   fires on the *next* tick. This adds one frame (16.6 ms at 60
   FPS) of "no new audio queued" between the last decoded chunk
   and the loop's first chunk. The keep-ahead target maintains
   ≥ `minSecondsBuffered` (0.30 s) of buffered audio under
   steady state — the actual occupancy at EOF lies somewhere in
   `[minSecondsBuffered, maxSecondsBuffered]` (≈ 0.30–0.60 s)
   depending on how recently the consumer drained — so the
   listener has many frames of audio still queued past the EOF
   tick. The 16.6 ms latency is structural, not audible. The
   test plan pins this contract (row 13).
6. **Resume if underran**: OpenAL stops a source when its queue
   empties before the next queue. Detect via
   `AL_SOURCE_STATE == AL_STOPPED` while `active &&
   !plan.trackFinished` and call `alSourcePlay`. Log a warning
   the first time it happens per session — repeated underruns
   mean the chunk size is too small for the IO budget.

After the layer loop, drain the stinger queue:

```cpp
auto ready = m_stingers.advance(deltaSeconds);
for (const auto& s : ready)
{
    m_engine.playSound2D(s.clipPath, s.volume,
                         AudioBus::Music, SoundPriority::Normal);
}
```

Stingers ride the existing one-shot path — short clips fit in a
whole-file buffer and the AudioEngine LRU cache handles the
working set. Routing them through `AudioBus::Music` (not
`AudioBus::Sfx`) means the mixer's Music slider ducks them in
lockstep with the streaming layers, matching player intuition for
"this is music".

## `MusicSystem` ISystem wrapper

Spec.md §3 (l.31, l.96, l.259) commits to `MusicSystem` as an
`ISystem` implementor, parallel to `AudioSystem`:

- spec.md:31 lists `MusicSystem` in the scope inventory of
  `ISystem` implementations.
- spec.md:96 documents the sibling `AudioSystem` row.
- spec.md:259-264 names `MusicSystem::update` (separate
  `ISystem`, Update phase) as the driver of the workflow this
  design implements.

This design honours that. The `ISystem` interface
(`engine/core/i_system.h:78-100`) requires four pure virtuals —
`getSystemName()` returning `const std::string&`, `initialize(Engine&)`,
`shutdown()`, and `update(float)`. The wrapper:

```cpp
#include "core/i_system.h"
#include "audio/audio_music_player.h"

namespace Vestige
{

class MusicSystem final : public ISystem
{
public:
    explicit MusicSystem(AudioMusicPlayer& player);

    // ---- ISystem pure virtuals ----------------------------------

    const std::string& getSystemName() const override
    {
        return m_name;
    }

    bool initialize(Engine& /*engine*/) override
    {
        // Player is already constructed and owned by Engine; the
        // system just holds the reference. No setup needed here.
        return true;
    }

    void shutdown() override
    {
        // Player teardown is the owner's job; calling
        // clearAllLayers here would race with the AudioEngine
        // shutdown order. No-op.
    }

    void update(float deltaSeconds) override;

    // ---- Gameplay surface ---------------------------------------

    /// Gameplay-driven inputs. Calls accumulate within a frame
    /// (last write wins). The next `update(dt)` applies them.
    void setIntensity(float intensity);
    void setSilence(float silence);

    // ---- Default phase ------------------------------------------

    // getUpdatePhase() not overridden — default UpdatePhase::Update
    // matches spec.md §5 (l.259) "(separate ISystem, Update phase)".

private:
    AudioMusicPlayer& m_player;
    const std::string m_name = "MusicSystem";
    float m_intensity = 0.0f;
    float m_silence   = 0.0f;
};

} // namespace Vestige
```

`update` body: `m_player.applyIntensity(m_intensity, m_silence);`
followed by `m_player.update(deltaSeconds);`. That's the entire
ISystem responsibility — the wrapper exists to give music a
canonical phase slot in the dispatch loop and a public surface
for gameplay code to push intensity / silence at, without
gameplay coupling to `AudioMusicPlayer` directly.

### Ownership

`Engine` owns the player directly as a `std::unique_ptr<AudioMusicPlayer>
m_musicPlayer`. The `AudioEngine&` passed to the player ctor is
**not an Engine-level member** — `AudioEngine` is owned by
`AudioSystem` (`engine/systems/audio_system.h:64` declares
`AudioEngine m_audioEngine;`, with accessor
`getAudioEngine()` at line 50). Construction order in
`Engine::initialize()`:

1. Register + initialise `AudioSystem` (the existing path; AudioEngine comes up inside its `initialize`).
2. `m_musicPlayer = std::make_unique<AudioMusicPlayer>(m_systemRegistry.getSystem<AudioSystem>()->getAudioEngine());`
   — same indirection `Engine` already uses for the caption-announcer
   wire-up at `engine/core/engine.cpp:379, 435`.
3. `m_systemRegistry.registerSystem<MusicSystem>(*m_musicPlayer);`

The system holds a reference to the player; destruction order
(system before player before AudioSystem) is the natural reverse
of construction.

## SceneSerializer integration

`SceneManager` (verified `engine/scene/scene_manager.h:18-53`) has
no `loadScene` / `unloadScene` API — the surface is
`createScene` / `setActiveScene` / `getActiveScene` / `update` /
`removeScene` / `getSceneCount`. Scene content is materialised by
the free static `SceneSerializer::loadScene(Scene&, path,
ResourceManager&[, FoliageManager*, Terrain*])` (verified
`engine/editor/scene_serializer.h:68-95`).

The hook site is therefore the **caller** of
`SceneSerializer::loadScene`, not `SceneManager` itself. The
caller knows when it has just materialised a scene; the
serializer is a pure data-in / data-out function.

### Scene JSON envelope addition

New optional `music` block in scene files (sibling to the
existing `terrain` block; Ed11 manifest pattern):

```json
{
  "music": {
    "layers": [
      { "layer": "Ambient",  "clip": "assets/music/tab_amb.ogg" },
      { "layer": "Tension",  "clip": "assets/music/tab_tns.ogg" }
    ],
    "loopAll": true
  }
}
```

- Absent → no music; partial → only listed layers load.
- `"layer"` string matches the `MusicLayer` enum's labels exactly
  (`musicLayerLabel`): Ambient / Tension / Exploration / Combat /
  Discovery / Danger. Unknown strings emit a warning and skip
  the layer; other layers still load.
- `"clip"` path is sandbox-checked the same way `AudioClip`
  loads are (`AudioEngine::setSandboxRoots`).

### Serializer surface

Add to `engine/editor/scene_serializer.h`:

```cpp
struct MusicLayerEntry
{
    MusicLayer layer;
    std::string clipPath;
};

struct MusicSceneSettings
{
    std::vector<MusicLayerEntry> layers;
    bool loopAll = true;
};
```

Extend `SceneSerializerResult` with `MusicSceneSettings music;`
(populated on load, used on save). Bumping `CURRENT_FORMAT_VERSION`
from 1 → 2 with a backwards-compatible read (missing `music`
key → empty `MusicSceneSettings`).

> **Scene-format-version ownership (locked 2026-06-01, CE5).** This design
> doc (Slice 8, step 8) **owns the scene `CURRENT_FORMAT_VERSION` 1 → 2 bump**
> (`engine/editor/scene_serializer.h`, currently `= 1`). No other Phase 10 doc
> touches scene-JSON fields — verified 2026-06-01 (phase_11a's "format-version"
> references are the *replay* format, not the scene format). Any other phase
> that needs to add scene-JSON fields before this slice lands must fold them
> into the same v1 → v2 migration here, or wait and own v2 → v3; it must not
> open a parallel 1 → 2 bump.

Two new overloads parallel to the existing
environment/terrain pair:

```cpp
static SceneSerializerResult saveScene(
    const Scene&, const std::filesystem::path&,
    const ResourceManager&,
    const FoliageManager*, const Terrain*,
    const MusicSceneSettings*);

static SceneSerializerResult loadScene(
    Scene&, const std::filesystem::path&,
    ResourceManager&,
    FoliageManager*, Terrain*,
    MusicSceneSettings*);
```

`MusicSceneSettings*` is nullable so non-music tests pass
`nullptr` and the serializer ignores any `music` JSON block.

### Wiring at the caller

`engine/core/engine.cpp` (or the editor's "open scene" handler —
whichever owns the SceneSerializer call today) does:

```cpp
MusicSceneSettings music;
auto result = SceneSerializer::loadScene(
    scene, path, resources, &foliage, &terrain, &music);
if (result.success && m_musicSystem)
{
    m_musicPlayer->clearAllLayers();        // drop previous scene's music
    for (const auto& entry : music.layers)
    {
        if (!m_musicPlayer->loadLayer(entry.layer, entry.clipPath))
        {
            ++result.warningCount;
            Logger::warning("Failed to load music layer "
                            + std::string(musicLayerLabel(entry.layer))
                            + " clip " + entry.clipPath);
            continue;
        }
        m_musicPlayer->playLayer(entry.layer);
    }
}
```

A `loadLayer` failure (bad path / decode-open failure / sandbox
rejection) increments `warningCount`, logs, and continues with
the remaining layers. Scene load itself does not fail on music
problems — gameplay should still come up.

`unloadScene` path: the same caller calls
`m_musicPlayer->clearAllLayers()` before invoking `removeScene`.

## Decoder concerns

stb_vorbis fits the streaming shape:

- `extern stb_vorbis* stb_vorbis_open_filename(const char* filename, int* error, const stb_vorbis_alloc* alloc_buffer);`
  (verified `external/stb/stb_vorbis.c:274-275`). Opens the file,
  returns a streaming handle. Single allocation; no whole-file
  decode.
- `stb_vorbis_get_info(handle)` → channels + sample rate. Used
  once to populate `StreamingLayer::channels` / `sampleRate`.
- `stb_vorbis_get_samples_short_interleaved(handle, channels,
  buffer, num_shorts)` → pulls up to `num_shorts / channels`
  frames into the caller's buffer. Returns the actual frame
  count (0 = EOF; less than asked but > 0 = packet boundary,
  not EOF).
- `stb_vorbis_seek_start(handle)` → rewind for the loop case.
- `stb_vorbis_close(handle)` → release on layer unload.

### Compilation unit shape

The vendored layout (verified):

- `external/stb/stb_vorbis.c` — the single-file library.
- `external/stb/stb_vorbis_impl.cpp` — includes it WITHOUT
  `STB_VORBIS_HEADER_ONLY`, producing the implementation symbols.
- `engine/audio/audio_clip.cpp` — `#define STB_VORBIS_HEADER_ONLY`
  then `#include "stb_vorbis.c"`, pulls declarations only.

`engine/audio/audio_music_player.cpp` follows `audio_clip.cpp`'s
pattern verbatim: `#define STB_VORBIS_HEADER_ONLY` then
`#include "stb_vorbis.c"`. The impl symbols come from the existing
`stb_vorbis_impl.cpp` TU; no new impl unit, no `extern "C"` block,
no duplicate symbols.

## Performance budget

60 FPS minimum is the hard rule (16.6 ms frame budget). Per-frame
target for the player: **< 0.5 ms in the amortised case, < 1.5 ms
in the cold-cache worst case**.

| Item | Cost | Per frame (six active layers) |
|---|---|---|
| `advanceMusicLayer` × 6 | < 0.1 µs each | < 1 µs |
| `alSourcef(AL_GAIN)` × 6 | ~1 µs each | ~6 µs |
| `alGetSourcei(BUFFERS_PROCESSED)` × 6 | ~1 µs each | ~6 µs |
| `planStreamTick` × 6 | pure-fn, < 0.2 µs each | < 2 µs |
| Decode (`stb_vorbis_get_samples_short_interleaved`, 4096 frames, stereo) | ~100 µs per chunk on the Ryzen 5 5600 dev box (estimate — pin in step 1) | varies; see below |
| `alBufferData` + `alSourceQueueBuffers` | ~100 µs per chunk | varies |

**Amortisation.** With `minSecondsBuffered = 0.30`,
`maxSecondsBuffered = 0.60`, `framesPerChunk = 4096` at 48 kHz,
each layer needs one decode roughly every 85 ms of playback —
~1.2× per second. Across six layers that is ~7 chunks/second
total; at 60 FPS = ~0.12 chunks/frame on average. Per-frame
amortised cost: ~25 µs (planning + 0.12 × 200 µs decode+upload).

**Cold-cache worst case** (scene just loaded, every layer's
`stream.totalFramesDecoded == 0`, all six demand a chunk on the
same frame): 6 × 200 µs = 1.2 ms of work in one frame, plus the
~15 µs of book-keeping. Still inside the 16.6 ms budget but
visible in profiler traces. If profiling later shows this is a
tall pole, the follow-up is worker-thread decode (see
"Out of scope").

spec.md §6 (l.317) pins the budget as "< 0.2 ms" with "TBD —
measure by Phase 11 entry". The estimates above match the spec's
0.2 ms target for the amortised case (~25 µs is well inside) but
exceed it in the cold-cache worst case. The verify-step plan
includes a measurement step that pins both numbers concretely;
spec.md's "TBD" is the place that final number replaces.

### Decode-rate citation

The ~100 µs decode estimate is for 4096 stereo frames on the
dev-box CPU. No published benchmark for stb_vorbis at exactly
this chunk size, so this number is an **estimate to verify in
step 1** of the step plan. If the real number is ≥ 2× off the
estimate, the budget table gets re-pinned before any other step
proceeds.

## Accessibility

The existing `AudioMixer` (`engine/audio/audio_mixer.h`) owns the
`AudioBus::Music` gain. The Phase 10.7 accessibility toggles
route the master + music bus through it; this player inherits
that for free — its `alSourcef(AL_GAIN)` is multiplicative with
whatever the mixer hands the source pool.

No new accessibility surface here. The mixer panel's existing
Music slider muting works the moment the player starts queueing.

## Test plan (global `CLAUDE.md` rule 12 verify-step)

All tests live in `tests/test_audio_music_player.cpp` and are
runnable without a sound card (`AudioEngine::initialize` returns
`false` on no-device and `isAvailable()` stays false; the player
short-circuits AL calls but keeps state-machine progress). Test
fixture is a 0.5-second pre-baked OGG at 48 kHz stereo at
`tests/fixtures/audio/music_loop_test.ogg` — see step plan for
authoring.

| Row | Step | Verify |
|---|---|---|
| 1 | `loadLayer(Ambient, fixture.ogg)` returns true | `isLayerLoaded(Ambient)` true; `getLayerBufferedSeconds(Ambient) == 0.0f`; introspection accessor returns `freeBuffers.size() == kBuffersPerLayer` |
| 2 | `playLayer(Ambient)` + `update(1.0)` | `isLayerPlaying(Ambient)` true; `getLayerBufferedSeconds(Ambient) >= minSecondsBuffered` (0.30) |
| 3 | `setLayerTargetGain(Ambient, 1.0)` + `update(2.0)` | `getLayerGain(Ambient)` within `1e-5` of `1.0` (`fadeSpeedPerSecond = 0.5/s` needs 2 s for a full 0→1 swing) |
| 4 | `setLayerTargetGain(Ambient, 0.0)` + `update(2.1)` | `getLayerGain(Ambient)` within `1e-5` of `0.0` — `2.1 s > 2.0 s` swing time avoids the at-the-boundary timing flake |
| 5 | Play loop fixture for 2× track-length seconds | `stream.loopCount >= 1`; `stream.finished` false |
| 6 | `clearAllLayers()` mid-play | `isLayerPlaying(Ambient) == false`; `getActiveLayerCount() == 0` (player exposes this directly, test doesn't reach into AudioEngine) |
| 7 | `enqueueStinger({path, 0.5s, 1.0})` + `update(0.6)` | One source acquired from `AudioEngine` during the update; `playSound2D` called once on `AudioBus::Music` (verified by a thin test fake that intercepts the AudioEngine call) |
| 8 | Scene-serializer round-trip | A scene with three music layers, serialized + reloaded, yields three loaded layers with matching clip paths and `MusicSceneSettings.loopAll == true` |
| 9 | Sandbox reject | `loadLayer(Ambient, "../../../etc/passwd")` returns false; no decoder opened; `isLayerLoaded(Ambient)` false |
| 10 | Unknown enum string in serializer | A scene whose JSON has `"layer": "Bogus"` increments `result.warningCount`; other listed layers still load |
| 11 | `update(0.0)` is a true no-op | No AL calls (verified via test-only counter on a thin AL fake), no gain change, no buffered-seconds change |
| 12 | `loadLayer` on an already-loaded layer | After re-load: a test-only buffer-ID set shows the three old AL buffer IDs were `alDeleteBuffers`-released and three new IDs allocated; `getLayerBufferedSeconds(Ambient) == 0.0f`; `freeBuffers.size() == kBuffersPerLayer` |
| 13 | **Loop boundary** — drive the fixture to EOF in one `update`, then tick once more, then a third time | Tick N (EOF): `decoderJustHitEof == true`, no `stb_vorbis_seek_start` call, last decoded chunk queued; Tick N+1: `stb_vorbis_seek_start` called exactly once, `decoderJustHitEof` reset to `false`, fresh chunk queued from the start; Tick N+2: business as usual |
| 14 | `update(-1.0f)` (negative dt) | Same as row 11 — true no-op, no decode, no AL calls, state unchanged |

Edge cases pinned by tests:

- Decode short-read mid-track (gotFrames > 0 but < framesToDecode)
  → does NOT mark `trackFullyDecodedOnce`, no loop.
- Decode 0-frame read → marks `trackFullyDecodedOnce`, loop or
  finish per policy.
- Player constructed without a live `AudioEngine` device → every
  call neutral; state-machine progress still ticks against
  `deltaSeconds` so row 5's loop-count assertion stays valid.

## Step plan

Per global `CLAUDE.md` rule 12 (state a verify-step plan for
multi-step work):

| Step | Verify |
|---|---|
| 0. **Author the 0.5 s OGG fixture** (`tests/fixtures/audio/music_loop_test.ogg`) — 48 kHz stereo, 0.5 s of a sine + harmonic with an audible step at the loop seam so the loop assertion in row 5 has a signal worth checking. Generate once locally and commit the binary (the fixture is content, not output of the test). Exact recipe (requires `ffmpeg`, a maintainer-machine prereq, NOT a CI dep — fixture is committed): `ffmpeg -f lavfi -i "aevalsrc=0.4*sin(2*PI*440*t)+0.2*sin(2*PI*880*t):s=48000:c=stereo:d=0.5" -c:a libvorbis -q:a 4 tests/fixtures/audio/music_loop_test.ogg`. Script committed at `tools/audio/regenerate_music_loop_test_fixture.sh` next to a one-line README explaining when to re-run it (basically never — only if the loop assertion needs a different signal). | Fixture exists; `stb_vorbis_get_info` on it returns `channels == 2`, `sample_rate == 48000`; total-samples ≈ 24000 (0.5 s × 48 kHz) |
| 1. **Streaming-decoder smoke test.** Bench `stb_vorbis_open_filename` + `stb_vorbis_get_samples_short_interleaved` on the fixture and a longer reference clip; measure decode µs / 4096 frames | Decode-rate estimate (~100 µs) within 2× of the bench; if not, re-pin §"Performance budget" before continuing |
| 2. **Write `audio_music_player.h`** per the API surface above | Compiles standalone; no `Scene*` / serializer includes in header |
| 3. **Write `audio_music_player.cpp`** with the per-frame algorithm | Existing audio tests stay green; player can be default-constructed + torn down without a live AL device |
| 4. **Wire `audio_music_player.{h,cpp}`** into `engine/CMakeLists.txt` (engine target) | Build green on Linux + Windows |
| 5. **Write `engine/systems/music_system.{h,cpp}`** + wire into `engine/CMakeLists.txt` | Build green; `MusicSystem::getSystemName()` returns `"MusicSystem"` |
| 6. **Add `tests/test_audio_music_player.cpp`** covering the 14 test rows + wire into `tests/CMakeLists.txt` | Green on headless CI runner |
| 7a. **Additive `docs/engine/audio/spec.md` extensions** (design-doc authority — additive only): §3 inventory adds `AudioMusicPlayer` + `MusicSystem` rows; §6 budget table re-pins the `MusicSystem::update` TBD with the step-1-measured amortised number (and adds a cold-cache annotation if the worst-case exceeds 0.2 ms); §11 tests adds `tests/test_audio_music_player.cpp` line; §13 dependencies adds the `MusicSceneSettings` JSON schema | `grep -nE 'AudioMusicPlayer\|MusicSystem' docs/engine/audio/spec.md` shows new rows in §3 + §6 + §11 + §13; spec.md §5 workflow text at l.259-264 unchanged (covered by 7b) |
| 7b. ✅ **LANDED (2026-06-01, CE11).** Spec-owner approved the §5 amendment: spec.md §5 step 4 now reads *"`AudioMusicPlayer::update` decodes one chunk via `stb_vorbis` for OGG (the existing `audio_clip.cpp` `STB_VORBIS_HEADER_ONLY` pattern) and queues it via `alSourceQueueBuffers`. A future WAV/MP3 streaming follow-up would add a `dr_libs` path…"*. The amendment is factually correct — code confirms OGG decodes via `stb_vorbis`, only WAV/MP3/FLAC use `dr_libs`. Recorded in audio spec §16 (v1.0.2). | ✅ Done — doc-vs-spec drift resolved before step 9 implementation |
| 8. **Extend `SceneSerializer`** with `MusicSceneSettings` + the two new overloads. Bump `CURRENT_FORMAT_VERSION` 1 → 2 with backwards-compatible load — add a `migrateV1toV2(json&)` helper (the commented-out template at `scene_serializer.cpp:285` shows where it plugs into the existing `migrateScene` switch) implemented as a no-op for the missing `music` block (v1 has no `music` block; v2 reads the missing-key case as an empty `MusicSceneSettings`). Add a fixture `tests/fixtures/scenes/pre_v2_no_music.scene` with `format_version: 1` and assert it loads green with `music.layers.empty()` and the post-load `format_version` flipped to 2 in memory | Scene round-trip test (load → save → reload) preserves the music block; pre-v2 scene fixture loads green with an empty `MusicSceneSettings` |
| 9. **Wire the caller-side integration** at all four current `SceneSerializer::loadScene` call sites: `engine/core/engine.cpp:228` (start-up scene), `engine/editor/file_menu.cpp:493, 606, 758` (three editor "open scene" paths). At each site, after the result returns success, call `m_musicPlayer->clearAllLayers()` then loop `loadLayer + playLayer` for every entry in the populated `MusicSceneSettings.layers`. Shipping one site at a time leaves a half-wired feature (start-up loads music but editor "Open" doesn't, or vice versa), so all four land together | A scene with a music block triggers `loadLayer + playLayer` at every entry point; integration test `tests/test_scene_load_audio_integration.cpp` exercises both the start-up path and an editor-open path |
| 10. **Flip W8 (part 2/2) to shipped** in ROADMAP.md, update the bullet summary to match the as-shipped design | `roadmap_query` shows zero Phase 10.9 Slice 8 active items |

## What's out of scope here

- **Worker-thread decode.** Budget numbers above show main-thread
  decode comfortable in the amortised case. If the cold-cache
  worst case shows up in profiler traces, lift the per-layer
  decode into a worker (one job per chunk, completion-queue back
  to the main thread for `alBufferData`). Player's per-frame
  shape doesn't change.
- **Crossfading between two tracks on the same layer**
  (level-transition crossfade). Useful but distinct from the
  layer crossfade this design already covers via slewing.
- **Format support beyond OGG.** WAV/MP3/FLAC streaming via
  dr_libs streaming APIs is a separate decoder-wrapper effort.
  OGG is the canonical music format here (already-vendored
  stb_vorbis, smaller files than uncompressed WAV).
- **Positional / 3D music sources.** Music is non-spatial here —
  the streaming source uses `AudioBus::Music` without an
  attenuation model. Spatial music (e.g. a juke box you can walk
  away from) is a separate feature.
- **Dynamic chunk-size tuning.** `framesPerChunk = 4096` is
  hard-coded. If a project hits underruns or memory pressure,
  expose a setter as a follow-up.
- **Per-layer solo / mute in the mixer panel.** The
  `AudioBus::Music` slider already mutes every layer at once;
  per-layer panel UI is a discoverability win, not a blocker.
- **Per-layer mixer pan / EFX.** Layers all route through the
  Music bus with no per-layer effect; if individual layers need
  reverb tails or low-pass, add a per-layer EFX hook as a
  follow-up.

## References

- OpenAL Soft `examples/alstream.c` (canonical streaming
  example, same buffer-queue / unqueue idiom):
  https://github.com/kcat/openal-soft/blob/master/examples/alstream.c
- dr_libs README (mackron):
  https://github.com/mackron/dr_libs — streaming examples in
  the per-decoder header docstrings (`dr_wav.h`, `dr_flac.h`,
  `dr_mp3.h`).
- stb_vorbis.c public-API header docstring:
  `external/stb/stb_vorbis.c:1-180` (vendored copy in this repo).
- `engine/audio/audio_music_stream.h:5-31` — file-header
  docstring laying out the streaming state-machine contract
  this player executes.
- `docs/engine/audio/spec.md:31, 92, 96, 259-264, 317, 345,
  366` — the audio subsystem spec passages that name
  `MusicSystem` (l.31, 259), `MusicLayer*` + `MusicStreamState`
  + `planStreamTick` (l.92), the sibling `AudioSystem` shape
  (l.96), the < 0.2 ms TBD budget (l.317), the underflow
  recovery contract (l.345), and the existing planner-test
  inventory (l.366).
- `ROADMAP.md:640-641` — W8 (part 1/2) shipped 2026-05-02
  `74a34fc`; W8 (part 2/2) bullet (this design doc's target).
- Phase 10.9 Slice 8 section header in ROADMAP.md (`### Slice 8:
  Subsystem wiring / dead-code cleanup`).
