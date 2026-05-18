# Phase 10 Audio — Streaming Music Player Design (W8 part 2/2)

## Status

Design doc, **awaiting blocking review** (project rule 1: research →
design → review → code). No code lands until this is reviewed.

Closes Phase 10.9 Slice 8 **W8 (part 2/2)** in the ROADMAP — wires the
streaming music path. W8 (part 1/2) shipped 2026-05-02 (`74a34fc`):
the per-buffer LRU cache + per-scene flush. Part 2/2 fills the gap
this exposed — there is no consumer for the `MusicStreamState` chunk
pump today, so streaming music is dead code in the engine even though
the pure state machine is unit-tested and ready.

## What exists today

Three layers are already in place:

1. **`engine/audio/audio_music_stream.{h,cpp}`** — pure-data
   `MusicStreamState` + pure-function `planStreamTick`. Per-tick
   plan answers: how many frames to decode, whether to rewind for
   loop, whether the track is finished. No IO, no OpenAL — testable
   without a sound card.

2. **`engine/audio/audio_music.{h,cpp}`** — `MusicLayer` enum (6
   layers: Ambient/Tension/Exploration/Combat/Discovery/Danger),
   `MusicLayerState` per-layer gain slew, `intensityToLayerWeights`
   mapping a single `intensity ∈ [0, 1]` to per-layer weights, and
   a `MusicStingerQueue`. Also pure data + pure functions.

3. **`engine/audio/audio_engine.{h,cpp}`** — OpenAL wrapper. Manages
   device, context, source pool (32 sources), buffer cache (LRU),
   listener position, `playSound`/`playSoundSpatial` for one-shots.
   **No streaming-source API** today: `playSound` binds a single
   `AL_BUFFER` and starts; it never calls
   `alSourceQueueBuffers`/`alSourceUnqueueBuffers`.

4. **`engine/audio/audio_clip.cpp`** — whole-file decode of
   WAV/MP3/FLAC/OGG via dr_libs + stb_vorbis. The OGG path uses the
   *decode-the-whole-file-at-once* helper
   `stb_vorbis_decode_filename`. Fine for a 5-second clip; ~34 MB of
   RAM for a 3-minute 48 kHz stereo track, which is exactly the
   no-go case `audio_music_stream` was written to avoid.

What is missing:

- A streaming OGG decoder that pulls samples a chunk at a time.
- An engine-side player that holds N streaming voices (one per
  active music layer), drives `planStreamTick`, queues decoded
  PCM onto an OpenAL source, unqueues spent buffers, and applies
  the layer's `currentGain` each frame.
- A scene → music wiring path so the player tracks per-scene
  load/unload.

## Push-back (CLAUDE.md rule 9)

Before settling on the implementation, the simpler-path question:

**Argument for the full streaming player.** A 3-minute Ogg at 48 kHz
stereo decodes to ~34 MB of PCM; six layers × that is ~204 MB. The
project targets 60 FPS on a 32 GB box, but heap fragmentation and
the second-or-two CPU stall of decoding six tracks on scene change
are real. Streaming is the industry-standard answer for music. The
project already has the pure state machine; the design here is the
glue.

**Argument for a "load everything as one-shots" stop-gap.** A
single-layer "play this music file at scene start" hook over the
existing `playSound` path is maybe 50 lines and unblocks scene-music
visuals while we figure out the real player. Risk: it silently locks
in a design where the layer system can't carry its weight, because
crossfading six pre-decoded 30-MB buffers via one-shot starts is the
wrong shape — no per-frame gain control, no fade slewing without
extra source-pool churn, and re-uploading on every scene swap is
~200 MB of CPU→GPU buffer transfer.

**Resolution.** Go with the full streaming design here. The
existing `MusicStreamState` exists specifically because the team
already chose streaming over one-shot at the previous design pass;
re-deriving that choice now would be churn. Scope the first cut
narrow (single layer working end-to-end, not all six) and grow.

## CPU / GPU placement

**CPU, end-to-end.**

- Streaming decode (stb_vorbis): CPU. No GPU equivalent and the
  chunk sizes (4096 frames = 16 KB stereo PCM) are too small for
  GPU dispatch to win.
- Layer gain slewing (`advanceMusicLayer`): CPU. Single FMA per
  layer per frame; six layers = trivial.
- Stream-tick planning (`planStreamTick`): CPU. Pure-function call
  per layer per frame.
- OpenAL queue management: CPU. OpenAL Soft does its own threaded
  mix on the audio driver thread; the engine just queues/unqueues.

GPU placement section per CLAUDE.md Rule 7 is therefore the trivial
case ("decision: CPU; reason: no per-pixel / per-particle math; pure
state-machine + small chunks"). No CPU/GPU parity test needed for
this slice.

## Architecture

```
                      AudioMusicPlayer
                            |
              +-------------+-------------+
              |   (one entry per layer)   |
        StreamingLayer[6]                 |
              |                           |
   +----------+----------+                |
   |          |          |                |
MusicStream- stb_vorbis- AL stream-      MusicLayerState
State (state) handle    ing source        (slewed gain)
                                          |
                                          v
                                  per-frame gain
                                  applied via
                                  alSourcef(AL_GAIN)
                                          |
              ------------------------------
                            |
                  AudioEngine source pool
                  (one source per active layer)
                            |
                            v
                       OpenAL Soft
```

Per-layer state lives in one `StreamingLayer` struct holding all
five things the player needs to drive that layer:

```cpp
struct StreamingLayer
{
    MusicLayer            id;
    std::string           clipPath;        // .ogg track for this layer
    stb_vorbis*           decoder = nullptr;
    MusicStreamState      stream;
    MusicLayerState       gain;
    unsigned int          source = 0;      // OpenAL source ID, 0 = idle
    std::array<unsigned int, kBuffersPerLayer> buffers{};
    int                   channels = 0;
    int                   sampleRate = 48000;
    bool                  active = false;  // false = loaded but not playing
};
```

`AudioMusicPlayer` holds a fixed `std::array<StreamingLayer, MusicLayerCount>`
keyed by `MusicLayer`. No dynamic allocation per layer.

## API surface

`engine/audio/audio_music_player.{h,cpp}` — new file. Class only
depends on `AudioEngine` + the three existing audio modules. No
`Scene*` includes in the header so the audio module stays
self-contained (the scene-side wiring lives in `SceneManager` and
calls back into the player).

```cpp
namespace Vestige
{

class AudioEngine;

class AudioMusicPlayer
{
public:
    // Triple-buffer per layer — standard streaming size. Each
    // buffer holds one chunk = framesPerChunk samples.
    static constexpr std::size_t kBuffersPerLayer = 3;

    explicit AudioMusicPlayer(AudioEngine& engine);
    ~AudioMusicPlayer();

    AudioMusicPlayer(const AudioMusicPlayer&) = delete;
    AudioMusicPlayer& operator=(const AudioMusicPlayer&) = delete;

    /// Loads (but does not start) a track for the named layer.
    /// Opens the decoder, allocates AL buffers, leaves the source
    /// stopped. Returns false on decode-open failure or sandbox
    /// rejection. Replaces any existing track on this layer.
    bool loadLayer(MusicLayer layer, const std::string& clipPath);

    /// Starts streaming the loaded layer. No-op if already playing
    /// or no track loaded. Sets `targetGain` to 1.0; gameplay
    /// drives `setLayerTargetGain` afterwards.
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
    /// decoder. Called from SceneManager on scene unload.
    void clearAllLayers();

    /// Convenience: drives every layer's target gain from a single
    /// intensity signal using intensityToLayerWeights(intensity,
    /// silence). Caller still loads + plays the layers it wants.
    void applyIntensity(float intensity, float silence = 0.0f);

    /// Per-frame tick: advance gain slews, refill stream buffers,
    /// unqueue spent buffers, mark finished layers. Called from
    /// SceneManager::update or Engine main-loop audio tick.
    void update(float deltaSeconds);

    /// Layer-stinger helper: enqueues a one-shot stinger on the
    /// player's shared MusicStingerQueue. update() pops ready
    /// stingers and routes them to AudioEngine::playSound for
    /// one-shot playback (no streaming voice — short clips can
    /// live as whole-file buffers).
    void enqueueStinger(const MusicStinger& stinger);

    // ---- Introspection (for tests / mixer panel) -----------------

    bool   isLayerLoaded(MusicLayer layer) const;
    bool   isLayerPlaying(MusicLayer layer) const;
    float  getLayerGain(MusicLayer layer) const;
    float  getLayerTargetGain(MusicLayer layer) const;
    float  getLayerBufferedSeconds(MusicLayer layer) const;

private:
    AudioEngine& m_engine;
    std::array<StreamingLayer, MusicLayerCount> m_layers;
    MusicStingerQueue m_stingers;
};

} // namespace Vestige
```

Header is ~80 lines including doc-comments. Cpp ~250 LoC (decoder
wrapper + the per-tick stream pump).

## Per-frame algorithm (`update`)

For each `StreamingLayer` in `m_layers`:

1. **Skip if not loaded** (`decoder == nullptr` and `source == 0`).
2. **Advance gain slew**: `advanceMusicLayer(layer.gain,
   deltaSeconds)`.
3. **Apply gain to source**: `alSourcef(layer.source, AL_GAIN,
   layer.gain.currentGain)` if source is live.
4. **Unqueue spent buffers**:
   ```cpp
   ALint processed = 0;
   alGetSourcei(layer.source, AL_BUFFERS_PROCESSED, &processed);
   while (processed-- > 0) {
       ALuint freed = 0;
       alSourceUnqueueBuffers(layer.source, 1, &freed);
       ALint sizeBytes = 0;
       alGetBufferi(freed, AL_SIZE, &sizeBytes);
       std::uint64_t framesConsumed =
           sizeBytes / (sizeof(short) * layer.channels);
       notifyStreamFramesConsumed(layer.stream, framesConsumed);
       layer.freeBuffers.push(freed);
   }
   ```
5. **Plan + decode + queue**:
   ```cpp
   StreamTickPlan plan = planStreamTick(layer.stream, decoderAtEof);
   if (plan.rewindForLoop) {
       stb_vorbis_seek_start(layer.decoder);
   }
   if (plan.framesToDecode > 0 && !layer.freeBuffers.empty()) {
       // Decode plan.framesToDecode into a scratch buffer
       std::vector<short> scratch(plan.framesToDecode * layer.channels);
       int gotFrames = stb_vorbis_get_samples_short_interleaved(
           layer.decoder, layer.channels, scratch.data(),
           scratch.size());
       // Upload + queue
       ALuint dst = layer.freeBuffers.front();
       layer.freeBuffers.pop();
       alBufferData(dst, layer.channels == 1 ? AL_FORMAT_MONO16
                                              : AL_FORMAT_STEREO16,
                    scratch.data(), gotFrames * layer.channels * sizeof(short),
                    layer.sampleRate);
       alSourceQueueBuffers(layer.source, 1, &dst);
       notifyStreamFramesDecoded(layer.stream, gotFrames,
                                  gotFrames < (int)plan.framesToDecode);
   }
   if (plan.trackFinished) {
       stopLayer(layer.id);
   }
   ```
6. **Resume if underran**: OpenAL stops a source when its queue
   empties before the next queue. Detect via
   `AL_SOURCE_STATE == AL_STOPPED` while `active && !finished` and
   call `alSourcePlay`. Logs a warning the first time it happens
   per session — repeated underruns mean the chunk size is too
   small for the IO budget.

Stinger drain (after the layer loop):

```cpp
auto ready = m_stingers.advance(deltaSeconds);
for (const auto& s : ready) {
    m_engine.playSound(s.clipPath, /*pos*/{}, s.volume,
                       /*loop=*/false, AudioBus::Music,
                       SoundPriority::Normal);
}
```

Stingers ride the existing one-shot path — short clips fit fine in
a whole-file buffer and the AudioEngine LRU cache handles the
working set.

## SceneManager integration

Smallest viable hook: `SceneManager` holds a reference to
`AudioMusicPlayer` and calls into it on scene change. The
per-scene music config rides in the scene JSON envelope (already
manifest-backed since Ed11).

New optional JSON section in scene files:

```json
{
  "music": {
    "layers": [
      { "layer": "Ambient",     "clip": "assets/music/tabernacle_amb.ogg" },
      { "layer": "Tension",     "clip": "assets/music/tabernacle_tns.ogg" }
    ],
    "loopAll": true
  }
}
```

Absent → no music; partial → only listed layers load. The "layer"
key matches the `MusicLayer` enum's labels (Ambient, Tension,
Exploration, Combat, Discovery, Danger).

`SceneManager` changes:

- New optional ctor parameter: `AudioMusicPlayer* musicPlayer`.
  Nullable so headless tests / non-audio configs stay neutral.
- `loadScene(name)` end: if music section present, call
  `musicPlayer->loadLayer + playLayer` for each entry.
- `unloadScene(name)` start: `musicPlayer->clearAllLayers()`.
- `update(dt)` end: `if (musicPlayer) musicPlayer->update(dt);`.

Total SceneManager edit: ~30 lines including null-guards.

`SceneSerializer` changes:

- Read+write a new `MusicSceneSettings` struct mirroring the JSON.
  Mirror Ed11's pattern: pack into the same scene.json envelope,
  no separate side-files. Validation: layer string must match one
  of the six enum labels, clip path must be ≤ 260 chars (NTFS
  baseline) and pass the `setSandboxRoots` check at load time.
- Backwards-compatible read: missing `music` key → empty
  `MusicSceneSettings` (no audio change).

Total SceneSerializer edit: ~60 lines + serializer tests.

## Decoder concerns

stb_vorbis fits the streaming shape:

- `stb_vorbis_open_filename(const char*, int* error, stb_vorbis_alloc*)`
  → opens the file, returns a streaming handle. Single allocation;
  no whole-file decode.
- `stb_vorbis_get_info(handle)` → channels + sample rate. Used
  once to populate `StreamingLayer::channels` / `sampleRate`.
- `stb_vorbis_get_samples_short_interleaved(handle, channels,
  buffer, num_shorts)` → pulls up to `num_shorts / channels`
  frames into the caller's buffer. Returns the actual frame count
  (less than asked → EOF).
- `stb_vorbis_seek_start(handle)` → rewind for the loop case.
- `stb_vorbis_close(handle)` → release on layer unload.

No new dep — `stb_vorbis.c` is already vendored at
`external/stb/stb_vorbis.c` and built into the engine (`audio_clip.cpp`
calls `stb_vorbis_decode_filename`). The streaming entry points are
in the same TU once we add `#include "stb_vorbis.c"` to the
implementation. Alternative: move stb_vorbis to its own header-only
include surface so `audio_music_player.cpp` can pull it without
duplicating the impl macro. Simpler: leave `audio_clip.cpp` as the
single impl unit and forward-declare the needed entry points in
`audio_music_player.cpp` via an extern "C" block (matches the
existing `audio_clip.cpp` style).

Decision: extern declarations in `audio_music_player.cpp` (no new
TU, no impl macro shuffle).

## Performance budget

60 FPS minimum is the hard rule. Per-frame budget for the player
(target: < 0.5 ms even at six active layers):

- Six `advanceMusicLayer` calls + six `alSourcef`: negligible.
- Six `alGetSourcei(BUFFERS_PROCESSED)`: ~6 × 1 µs = 6 µs.
- Six `planStreamTick` calls: pure-function, < 1 µs total.
- Decode work: only fires when buffered seconds < min. With
  `minSecondsBuffered = 0.30` and `maxSecondsBuffered = 0.60`, each
  layer decodes one chunk roughly every 0.15 s of playback. At
  4096 frames/chunk on 48 kHz, that is one decode every ~85 ms per
  layer. stb_vorbis decode rate is ~50-100 Mbps on this hardware,
  so 4096 stereo samples = 16 KB raw PCM ≈ 1 KB compressed ≈ 0.1 ms
  to decode. Six layers worst case: 0.6 ms — but spread across
  ~85-ms cycles, so a single 16-ms frame sees at most one or two
  layer decodes (~0.2 ms).
- AL queue/unqueue + alBufferData upload: ~0.1 ms per chunk.

Worst-case per-frame: ~0.5 ms with all six layers active. Cushion
against the 16.6 ms frame budget.

Pessimistic case (cold cache, all six layers decode on the same
frame): ~1.5 ms. Still well inside budget but worth observing.

The decoder runs on the main thread (same as the existing
`audio_clip.cpp` path). A worker-thread decode is a real
improvement but not for this slice — the budget number above
gives the headroom to defer it. If profiling later shows the
streaming step is the audio system's tall pole, lift the per-layer
decode into a per-layer worker (one job per chunk, completion-
queue back to the main thread for `alBufferData`).

## Accessibility

The existing `AudioMixer` (`engine/audio/audio_mixer.h`) already
owns the `AudioBus::Music` gain. The Phase 10.7 accessibility
toggles route the master + music bus through it; this player
inherits that for free — its `alSourcef(AL_GAIN)` is multiplicative
with whatever the mixer hands the source pool.

No new accessibility surface here. The mixer panel's existing
Music slider muting works the moment the player starts queueing.

## Test plan (verify-step per CLAUDE.md rule 12)

All tests live in `tests/test_audio_music_player.cpp` and are
runnable without a sound card (`audio_engine` already handles the
"no device" case by going neutral). The tests cover the
state-machine wiring; the actual OpenAL queue rolls a no-op when
the device is absent, but the per-layer state machine and stream
plan still tick.

| Step | Verify |
|---|---|
| 1. `loadLayer(Ambient, fixture.ogg)` returns true | `isLayerLoaded(Ambient)` is true; `getLayerBufferedSeconds(Ambient)` is 0 |
| 2. `playLayer(Ambient)` + `update(1.0)` | `isLayerPlaying(Ambient)` is true; with 4096 frames/chunk @ 48 kHz, after one update buffered ≥ minSecondsBuffered (0.30 s) |
| 3. `setLayerTargetGain(Ambient, 1.0)` + `update(2.0)` | `getLayerGain(Ambient) ≈ 1.0` (within `fadeSpeedPerSecond` × dt slack) |
| 4. `setLayerTargetGain(Ambient, 0.0)` + `update(2.0)` | `getLayerGain(Ambient) ≈ 0.0` |
| 5. Play loop fixture for 2× track-length seconds | `loopCount ≥ 1` in the layer's `MusicStreamState`; no `trackFinished` |
| 6. `clearAllLayers()` mid-play | `isLayerPlaying(Ambient) == false`; no leaked AL sources (`AudioEngine::getActiveSourceCount` returns 0 after the call) |
| 7. `enqueueStinger({path, 0.5s, 1.0})` + `update(0.6)` | one source acquired from `AudioEngine` during the update; `playSound` called once |
| 8. Scene serializer round-trip | a scene with three music layers, serialized + reloaded, yields three loaded layers with matching clip paths |
| 9. Sandbox reject | `loadLayer(Ambient, "../../../etc/passwd")` returns false; no decoder opened |
| 10. Out-of-range layer enum value (only reachable through serializer's string→enum) | logs error + skips layer; other layers still load |

Fixture: a 0.5-second pre-baked Ogg at 48 kHz stereo
(`tests/fixtures/audio/music_loop_test.ogg`). Short loop maximises
the loop-count test without dragging out the suite.

Edge cases pinned by tests:

- Decode short-read → marks `trackFullyDecodedOnce` on first EOF,
  loop or finish per policy.
- `loadLayer` called on an already-loaded layer → unload then load,
  no leaked decoder or AL buffers.
- `update` with `deltaSeconds == 0` → no decode work, no gain
  change, no AL calls.
- Player constructed without a live AudioEngine device → every
  call is neutral (decoder still opens for the buffered-seconds
  query, but no AL traffic).

## Step plan

Per CLAUDE.md rule 12 (state a verify-step plan for multi-step
work):

1. **Add a streaming-decoder smoke test against stb_vorbis** →
   verify: with a known fixture, `stb_vorbis_open_filename` +
   `stb_vorbis_get_samples_short_interleaved` returns the same
   total sample count as `stb_vorbis_decode_filename`.
2. **Write `audio_music_player.h` per the API above** → verify:
   compiles standalone, no `Scene*` include in the header.
3. **Write `audio_music_player.cpp` with the per-frame algorithm**
   → verify: existing audio tests still green; the player can be
   default-constructed and torn down without a live device.
4. **Add `test_audio_music_player.cpp` covering the 10 test rows
   above** → verify: green on the headless CI runner.
5. **Wire `MusicSceneSettings` into `SceneSerializer`** → verify:
   round-trip test (load → save → reload) preserves the music
   block; legacy scenes without the block still load.
6. **Wire `AudioMusicPlayer` into `SceneManager`** → verify: a
   scene with a music block triggers `loadLayer` + `playLayer`;
   unload triggers `clearAllLayers`. Integration test in
   `tests/test_scene_manager_audio_integration.cpp`.
7. **Add scene-music documentation to `docs/engine/audio/spec.md`**
   → verify: the new spec section names the JSON schema and
   the player's contract; grep-clean against ROADMAP and the new
   tests.
8. **Flip W8 (part 2/2) to shipped in ROADMAP** → verify:
   `roadmap_query` shows zero Phase 10.9 Slice 8 active items.

## What's out of scope here

- **Worker-thread decode.** The budget numbers above show the
  main-thread decode comfortable inside the 60-FPS budget. If
  profiling later shows otherwise, fold a job-queue in as a
  follow-up — the player's per-frame algorithm doesn't change
  shape, only the `stb_vorbis_get_samples_short_interleaved` call
  moves into a worker.
- **Crossfading between two tracks on the same layer.** Useful
  for level transitions but distinct from the layer crossfade
  this design already covers via `MusicLayerState` slewing. Track
  separately when a real use case appears.
- **Format support beyond OGG.** WAV/MP3/FLAC are already in
  `audio_clip.cpp` for one-shots; streaming them is a separate
  effort and the dr_libs streaming APIs differ enough to warrant
  their own decoder-wrapper design. OGG is the canonical music
  format for the project (already-vendored stb_vorbis + smaller
  files).
- **Per-layer mute toggles in the mixer panel.** The existing
  `AudioBus::Music` mute already silences all music; per-layer
  visibility in the panel is a discoverability win but not a
  blocker for shipping the player.

## References

- OpenAL Soft `examples/alstream.c` — the canonical streaming
  example. Same buffer-queue / unqueue idiom.
- dr_libs streaming examples (mackron/dr_libs README) — confirms
  the triple-buffer-at-chunk-size shape is correct for short
  music chunks.
- stb_vorbis.c header docstring (lines 1-180) — pull-data API
  contract.
- Existing `audio_music_stream.h` docstring — the decision tree
  for `planStreamTick` that this player executes.
