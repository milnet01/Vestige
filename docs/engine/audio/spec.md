# Subsystem Specification — `engine/audio`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/audio` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (Phase 9 / 10 / 10.7 / 10.9 — bus mixer, captions, HRTF, ducking, voice eviction) |

---

## 1. Purpose

`engine/audio` is the engine's spatial-audio + accessibility-audio layer. It owns the OpenAL Soft device + context, the 32-slot OpenAL source pool, the loaded-buffer cache, the HRTF (Head-Related Transfer Function) policy, the six-bus mixer (Master / Music / Voice / SFX / Ambient / UI), the ducking state machine, the priority-based voice-eviction logic, the dynamic music layer / streaming / stinger primitives, the ambient zones + time-of-day scheduler, the reverb-zone preset table + EFX (OpenAL Effects Extension) blend math, the material-based occlusion / obstruction model, the per-source AL-state composer, and the caption-routing hook that fires every time game code asks for a sound to play. It exists as its own subsystem because audio touches a different external library (OpenAL Soft) on a different thread (OpenAL's mixer thread) with a different lifecycle (device may disconnect mid-session) than the rest of the engine — pushing it into renderer or scene would force unrelated subsystems to depend on the audio device. For the engine's primary use case — first-person architectural walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — `engine/audio` is what makes wind in the courtyard, footsteps on stone, and narrator voice-overs land at the right place, the right volume, with the right captions for partially-sighted users.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `AudioEngine` — OpenAL Soft device, context, source pool (32 slots), buffer cache | OpenAL Soft itself (vendored via FetchContent; `GIT_TAG` line at `external/CMakeLists.txt:362`) |
| `AudioMixer` — six-bus gain table (Master / Music / Voice / SFX / Ambient / UI) | Settings UI panel — `engine/editor/settings_panel/audio_panel.cpp` |
| `DuckingState` + `updateDucking` — sidechain dip with attack / release / floor | Policy of *which* events trigger ducking — `engine/systems/audio_system.cpp` |
| `SoundPriority` + `chooseVoiceToEvictForIncoming` — admission-controlled eviction | Game-side priority assignment — author-set on `AudioSourceComponent::priority` |
| `AudioClip` — WAV / MP3 / FLAC / OGG decode via dr_libs + stb_vorbis | Asset hot-reload (engine reloads via `loadBuffer` cache invalidation only) |
| `AmbientZone`, `TimeOfDayWeights`, `RandomOneShotScheduler` | Weather → ambient modulation (Phase 15 weather controller drives multiplier) |
| `MusicLayer*`, `MusicStingerQueue`, `MusicStreamState` + `planStreamTick` | Music-track authoring / asset list — content authored, not code |
| `AmbientSystem` / `AudioSystem` / `MusicSystem` (`ISystem` implementations) | `ISystem` mechanism itself — `engine/core/i_system.h` |
| `AttenuationModel` + `computeAttenuation` (Linear / Inverse / Exponential / None) | OpenAL's native curve evaluation — engine + AL share formulas via parity tests |
| `DopplerParams` + `computeDopplerPitchRatio` | Per-frame source position updates — `engine/scene/transform_component.h` |
| `AudioOcclusionMaterial*` + `computeObstructionGain` / `LowPass` | The raycast that determines `occlusionFraction` — `engine/physics/raycast.h` |
| `ReverbPreset` table + `blendReverbParams` + zone-weight falloff | EFX `alEffectf` driver calls — engine-side `AudioReverbAdapter` |
| `HrtfMode` / `HrtfStatus` / `HrtfSettings` / `composeHrtfStatusEvent` | HRTF dataset blobs (driver-shipped, selected by name) |
| `AudioSourceAlState` + `composeAudioSourceAlState` (pure compose) | The `alSource*f` calls — `AudioEngine::applySourceState` |
| `CaptionAnnouncer` hook — fired before AL availability check | `CaptionMap` + `SubtitleQueue` — `engine/ui/caption_map.h`, `engine/ui/subtitle.h` |
| Path-sandbox for `loadBuffer` (Phase 10.9 Slice 5 D11) | The shared sandbox helper — mirrors `ResourceManager::setSandboxRoots` |

If a feature blends audio with another subsystem (raycast occlusion, weather modulation, settings persistence), the *audio-shaped* primitive lives here and the cross-subsystem driver lives in the consumer.

## 3. Architecture

```
                                ┌──────────────────────────────────┐
                                │           AudioSystem            │
                                │  (engine/systems/audio_system.h) │
                                │   ISystem · UpdatePhase=PostCam  │
                                └──────────────┬───────────────────┘
                                               │ owns
                                ┌──────────────▼───────────────────┐
                                │           AudioEngine            │
                                │ device · context · source pool   │
                                │ buffer cache · listener · HRTF   │
                                └──────┬─────────────┬─────────────┘
                                       │             │
                       ┌───────────────▼──┐    ┌─────▼──────┐
                       │    OpenAL Soft   │    │  AudioMixer │
                       │ (own audio thrd) │    │  6 bus gains│
                       └──────────────────┘    └──────────────┘
                                                       ▲
        ┌───────────────────┬──────────────────────────┘
        │                   │                   │
   ┌────▼────┐       ┌──────▼──────┐    ┌───────▼────────┐
   │ Ducking │       │ Priority +  │    │  Caption       │
   │  state  │       │ Eviction    │    │  Announcer hook│
   └─────────┘       └─────────────┘    └────────────────┘

  Pure-data / pure-function helpers (no AL linkage, headless-testable):
    audio_attenuation · audio_doppler · audio_occlusion · audio_reverb
    audio_ambient · audio_music · audio_music_stream · audio_hrtf
    audio_source_state · audio_clip
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `AudioEngine` | class | OpenAL device + context + source pool + buffer cache + listener. `engine/audio/audio_engine.h:35` |
| `AudioMixer` | struct | 6-slot bus gain table (Master / Music / Voice / SFX / Ambient / UI). `engine/audio/audio_mixer.h:82` |
| `AudioBus` | enum class | Bus identifier — referenced by `AudioSourceComponent::bus` and the mixer. `engine/audio/audio_mixer.h:65` |
| `SoundPriority` | enum class | 4 tiers (Low / Normal / High / Critical) for eviction admission. `engine/audio/audio_mixer.h:47` |
| `DuckingParams` / `DuckingState` | struct + struct | Sidechain dip configuration + state machine (attack / release / floor). `engine/audio/audio_mixer.h:148, 159` (Params first at 148, State second at 159). |
| `VoiceCandidate` + `chooseVoiceToEvictForIncoming` | struct + free fn | Admission-controlled pool-pressure eviction. `engine/audio/audio_mixer.h:180, 216` |
| `AudioClip` | class | Pulse-Code Modulation (PCM) container; static `loadFromFile` decodes WAV / MP3 / FLAC / OGG. `engine/audio/audio_clip.h:20` |
| `AttenuationModel` + `AttenuationParams` + `computeAttenuation` | enum + struct + free fn | Distance-attenuation curves matching OpenAL `_CLAMPED` variants. `engine/audio/audio_attenuation.h:47, 59, 82` |
| `DopplerParams` + `computeDopplerPitchRatio` | struct + free fn | OpenAL §3.5 Doppler-shift formula, CPU-side. `engine/audio/audio_doppler.h:56, 87` |
| `AudioOcclusionMaterial*` + obstruction free fns | enum + struct + fns | 8 material presets + transmission / low-pass blend math. `engine/audio/audio_occlusion.h:44, 63, 95, 103` |
| `ReverbPreset` + `ReverbParams` + `blendReverbParams` | enum + struct + free fn | 6 EFX presets + per-zone weight falloff + linear blend. `engine/audio/audio_reverb.h:36, 49, 104` |
| `AmbientZone` + `TimeOfDayWeights` + `RandomOneShotScheduler` | structs + free fns | Sphere-falloff loops, 4-window ToD weighting, stateless cooldown one-shots. `engine/audio/audio_ambient.h:47, 78, 110` |
| `MusicLayer*` + `MusicStingerQueue` + `MusicStreamState` + `planStreamTick` | enum + classes + struct + free fn | Layer slew, intensity → weights mapping, capped first-in-first-out (FIFO) stinger queue, streaming-decode planner. `engine/audio/audio_music.h:59, 144`, `audio_music_stream.h:45, 64, 119` |
| `HrtfMode` + `HrtfStatus` + `HrtfSettings` + `HrtfStatusEvent` + `resolveHrtfDatasetIndex` | enums + structs + free fn | HRTF policy + driver-status reporting + dataset index resolution. `engine/audio/audio_hrtf.h:40, 51, 64, 104, 132` |
| `AudioSourceAlState` + `composeAudioSourceAlState` | struct + free fn | Per-frame compose of every `alSource*f` value. `engine/audio/audio_source_state.h:33, 75` |
| `AudioSourceComponent` | `Component` | Per-entity authoring of clip path + bus + attenuation + occlusion + priority. `engine/audio/audio_source_component.h:24` |
| `AudioSystem` | `ISystem` | `PostCamera`-phase system that drives listener sync, per-source state push, ducking decay, caption queue. `engine/systems/audio_system.h:23` |

## 4. Public API

`engine/audio` is a **facade subsystem with many headers** (≥ 8 public headers). Per CODING_STANDARDS §18 the headline functions group by header.

```cpp
// audio_engine.h — OpenAL device / source pool / listener.
bool                   AudioEngine::initialize();
void                   AudioEngine::shutdown();
void                   AudioEngine::updateListener(pos, fwd, up);
unsigned int           AudioEngine::loadBuffer(const std::string& path);
unsigned int           AudioEngine::playSound      (path, pos, vol, loop, bus, priority);
unsigned int           AudioEngine::playSoundSpatial(path, pos, params, vol, loop, bus, prio);
unsigned int           AudioEngine::playSoundSpatial(path, pos, vel, params, vol, loop, bus, prio);  // velocity-aware overload (Doppler-bearing sources)
unsigned int           AudioEngine::playSound2D    (path, vol, bus, priority);
void                   AudioEngine::stopSound(unsigned int source);
void                   AudioEngine::stopAll();
void                   AudioEngine::updateGains();           // per-frame sweep
void                   AudioEngine::applySourceState(id, AudioSourceAlState);
void                   AudioEngine::setMixerSnapshot(const AudioMixer*);
void                   AudioEngine::setDuckingSnapshot(float duckGain);
void                   AudioEngine::setHrtfMode(HrtfMode);
void                   AudioEngine::setHrtfDataset(const std::string&);
HrtfStatus             AudioEngine::getHrtfStatus() const;
void                   AudioEngine::setCaptionAnnouncer(CaptionAnnouncer);
void                   AudioEngine::setHrtfStatusListener(HrtfStatusListener);
void                   AudioEngine::setSandboxRoots(std::vector<std::filesystem::path>);
// see audio_engine.h:35 for full surface (32 public methods).
```

```cpp
// audio_mixer.h — gain table + ducking + eviction (pure data / functions).
void   AudioMixer::setBusGain(AudioBus, float);             // clamps [0, 1]
float  AudioMixer::getBusGain(AudioBus) const;
float  effectiveBusGain(const AudioMixer&, AudioBus);       // master × bus
float  resolveSourceGain(mixer, bus, sourceVolume);         // 3-arg
float  resolveSourceGain(mixer, bus, sourceVolume, duckingGain); // 4-arg (Phase 10.9 P3)
void   updateDucking(DuckingState&, const DuckingParams&, float dt);
std::size_t chooseVoiceToEvict(const std::vector<VoiceCandidate>&);
std::size_t chooseVoiceToEvictForIncoming(voices, SoundPriority incoming);
const char* soundPriorityLabel(SoundPriority);
const char* audioBusLabel(AudioBus);
```

```cpp
// audio_attenuation.h — distance curves.
float       computeAttenuation(AttenuationModel, AttenuationParams, float distance);
const char* attenuationModelLabel(AttenuationModel);
int         alDistanceModelFor(AttenuationModel);  // returns AL_*_DISTANCE_CLAMPED
```

```cpp
// audio_doppler.h — pitch ratio.
float computeDopplerPitchRatio(params, srcPos, srcVel, lstPos, lstVel);
```

```cpp
// audio_occlusion.h — material-based muffling.
AudioOcclusionMaterial occlusionMaterialFor(AudioOcclusionMaterialPreset);
float                  computeObstructionGain   (openGain, T, fractionBlocked);
float                  computeObstructionLowPass(LP,       fractionBlocked);
const char*            occlusionMaterialLabel(AudioOcclusionMaterialPreset);
```

```cpp
// audio_reverb.h — EFX zone presets.
ReverbParams reverbPresetParams(ReverbPreset);
float        computeReverbZoneWeight(coreRadius, falloffBand, distance);
ReverbParams blendReverbParams(a, b, t);
const char*  reverbPresetLabel(ReverbPreset);
```

```cpp
// audio_ambient.h — environmental loops + ToD + cooldown one-shots.
float            computeAmbientZoneVolume(const AmbientZone&, float distance);
TimeOfDayWeights computeTimeOfDayWeights(float hourOfDay);
bool             tickRandomOneShot(scheduler, dt, sampleFn);
const char*      timeOfDayWindowLabel(TimeOfDayWindow);
```

```cpp
// audio_music.h + audio_music_stream.h — dynamic music + streaming.
void               advanceMusicLayer(MusicLayerState&, float dt);
MusicLayerWeights  intensityToLayerWeights(float intensity, float silence = 0);
void               MusicStingerQueue::enqueue(const MusicStinger&);
std::vector<MusicStinger> MusicStingerQueue::advance(float dt);
StreamTickPlan     planStreamTick(MusicStreamState&, bool decoderAtEof);
float              computeStreamBufferedSeconds(const MusicStreamState&);
void               notifyStreamFramesConsumed(state, frames);
void               notifyStreamFramesDecoded(state, frames, eofReached);
```

```cpp
// audio_hrtf.h — HRTF policy + status reporting.
const char*      hrtfModeLabel(HrtfMode);
const char*      hrtfStatusLabel(HrtfStatus);
HrtfStatusEvent  composeHrtfStatusEvent(const HrtfSettings&, HrtfStatus);
int              resolveHrtfDatasetIndex(available, preferred);  // -1 = no match
```

```cpp
// audio_source_state.h — per-frame compose.
AudioSourceAlState composeAudioSourceAlState(component, entityPos, mixer, duckGain);
```

```cpp
// audio_clip.h — decoder.
std::optional<AudioClip> AudioClip::loadFromFile(const std::string&);
// Accessors: getSamples / getSampleRate / getChannels / getFrameCount /
//            getDurationSeconds / getALFormat / getDataSizeBytes.
```

```cpp
// audio_source_component.h — entity component (consumed by AudioSystem).
class AudioSourceComponent : public Component {
    std::string clipPath;
    AudioBus    bus            = AudioBus::Sfx;
    float       volume = 1.0f, pitch = 1.0f;
    float       minDistance = 1.0f, maxDistance = 50.0f, rolloffFactor = 1.0f;
    AttenuationModel attenuationModel = AttenuationModel::InverseDistance;
    glm::vec3   velocity      = glm::vec3(0.0f);
    AudioOcclusionMaterialPreset occlusionMaterial = AudioOcclusionMaterialPreset::Air;
    float       occlusionFraction = 0.0f;
    bool        loop = false, autoPlay = false, spatial = true;
    SoundPriority priority = SoundPriority::Normal;
};
```

**Non-obvious contract details:**

- `AudioEngine::initialize()` returning `false` is **non-fatal** — the engine continues without sound (no audio hardware on a CI runner / minimal Linux box / muted desktop). Every play / update / load call short-circuits when `m_available == false`. Captions still fire (Phase 10.9 P4).
- `playSound*` returning `0` means "no source acquired" — pool exhausted with no evictable victim, audio unavailable, buffer load failure, or sandbox rejection. **Looping callers must store the returned ID and call `stopSound(id)`** — a `loop = true` sound played fire-and-forget holds its slot until `shutdown()`. Phase 10.9 Slice 15 Au1 made this contract explicit.
- The `CaptionAnnouncer` hook fires **before** the `!m_available` early-return — captions are the accessibility substitute for the audio itself, not a side-effect of audio reaching speakers. Deaf users + broken-audio users + zero-volume users all need the caption.
- `setMixerSnapshot(const AudioMixer*)` takes a pointer with a lifetime-equal-to-AudioEngine contract — typically `&Engine::m_audioMixer`. Phase 10.9 W7 replaced the previous per-frame value-copy because the read happens on the same thread as the publish (single-threaded today; the prior comment that justified the copy was inaccurate).
- `setSandboxRoots(empty)` means **no sandbox** — any path the caller supplies is forwarded to the decoder. Production wires install + project + asset-library roots once at startup; tests leave it empty so fixtures resolve.
- `AudioMixer::setBusGain` is the only sanctioned way to mutate gain — direct `busGain[i]` writes bypass the [0, 1] clamp. The Settings apply path uses the setter so a hand-edited `settings.json` can't sneak out-of-range values past validation.
- `chooseVoiceToEvictForIncoming` returns `std::size_t{-1}` when no voice has *strictly lower* priority than the incoming sound — equal-priority ties go to the incumbent so a rapid burst of Normal-tier sounds doesn't churn the pool.
- `composeAudioSourceAlState` folds occlusion into the `volume` input of `resolveSourceGain` (rather than as a separate clamp site) so the mixer / bus / duck / clamp pipeline applies uniformly.
- `loadBuffer` is **case-sensitive** and **path-keyed** — the cache stores by exact path string. Two different relative paths to the same file will decode twice and cache twice. Acceptable trade-off; canonicalisation deferred (§15).

**Stability:** facade is **semver-respecting from v1.0** (the engine is pre-1.0 today, so anything-goes per the SemVer spec — but new public-API breaks are still flagged here so consumers can plan migrations). Known evolution points: (a) the 4-arg `resolveSourceGain` is the canonical form — 3-arg overload kept for fire-and-forget gain previews where ducking is irrelevant. (b) See the spec change log (§16) for landed pre-1.0 breaks (e.g. Phase 10.9 W7's lifetime-pointer mixer-publish change).

## 5. Data Flow

**Steady-state per-frame (`AudioSystem::update` — `engine/systems/audio_system.cpp`):**

1. Listener sync — `AudioSystem::update` reads the camera transform (camera was already stepped this frame because the system runs in `UpdatePhase::PostCamera`) and calls `AudioEngine::updateListener(pos, fwd, up)` + `setListenerVelocity`.
2. Per-entity tick — for every `AudioSourceComponent` in the scene:
   - `composeAudioSourceAlState(comp, entityPos, mixer, duckingGain)` builds the per-frame AL push.
   - `AudioEngine::applySourceState(handle, state)` issues the `alSource*f` calls.
   - `isSourcePlaying(handle)` reaps stopped sources from `m_activeSources`.
3. Ducking decay — `updateDucking(state, params, dt)` slews the global duck gain toward floor-or-1; the result is published via `AudioEngine::setDuckingSnapshot`.
4. Mixer publish — `AudioEngine::setMixerSnapshot(&engineMixer)` (no-op when pointer unchanged; lifetime-equal-to-engine).
5. `AudioEngine::updateGains()` — sweeps `m_livePlaybacks`, releases AL_STOPPED sources back to the pool, re-uploads composed gain for every still-playing source. Picks up Settings slider changes on the next frame.
6. Caption queue tick — handled by `engine/ui/subtitle.h` consumers, fed by `CaptionAnnouncer` callbacks fired during step 2's `playSound*` calls.

**`AmbientSystem::update` (separate `ISystem`, Update phase):**

1. For each `AmbientZone`, compute `computeAmbientZoneVolume(zone, distanceToListener)` and route to a per-zone pooled source.
2. Apply `computeTimeOfDayWeights(hourOfDay)` as a per-clip multiplier (window-keyed clips fade smoothly across dawn / day / dusk / night).
3. `tickRandomOneShot(scheduler, dt, sampleFn)` — fires environmental one-shots on a randomised cooldown.

**`MusicSystem::update` (separate `ISystem`, Update phase):**

1. `intensityToLayerWeights(currentIntensity, currentSilence)` → per-layer target gain.
2. For each layer: `advanceMusicLayer(state, dt)` slews `currentGain` toward target; the value drives `AL_GAIN` on that layer's streaming voice.
3. `MusicStingerQueue::advance(dt)` returns ready stingers, started as fire-and-forget `playSound2D` calls on the Music bus.
4. For each streaming voice: `planStreamTick(state, decoderAtEof)` returns frames-to-decode + rewind / finished flags; the engine-side music streamer dispatches the actual `dr_libs` calls and `alSourceQueueBuffers`.

**Cold start (`AudioEngine::initialize`):**

1. `alcOpenDevice(nullptr)` — default device. On failure, log + return false (engine continues without sound).
2. `alcCreateContext` + `alcMakeContextCurrent`.
3. Load `ALC_SOFT_HRTF` extension function pointers (`alcResetDeviceSOFT`, `alcGetStringiSOFT`); silent skip if unavailable.
4. Pre-allocate 32 OpenAL sources (`alGenSources`) into the pool.
5. Apply stored HRTF settings (`applyHrtfSettings()` → `alcResetDeviceSOFT`).
6. Apply stored Doppler params + distance model.

**Shutdown (`AudioEngine::shutdown`):**

1. `stopAll()` — every live source.
2. `alDeleteSources` for the entire 32-slot pool.
3. `alDeleteBuffers` for every cached buffer.
4. `alcDestroyContext` + `alcCloseDevice`.

**Exception path:** every public method short-circuits when `!m_available`. Buffer-decode failure returns `0` from `loadBuffer`; callers see `playSound*` return `0` and skip the source. Sandbox rejection logs and returns `0`. No exceptions propagate out of `engine/audio` in steady state.

## 6. CPU / GPU placement

Not applicable — pure CPU subsystem.

Audio mixing, DSP (Digital Signal Processing) low-pass filtering, HRTF convolution, reverb, and resampling all happen on the CPU inside OpenAL Soft's own audio thread. The engine never uploads sample data to the GPU. Pattern A in `SPEC_TEMPLATE.md` §6.

## 7. Threading model

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (`AudioSystem::update` / editor / scripts) | All of `AudioEngine`, `AudioMixer`, every pure-function helper, `AudioSourceComponent` mutation. | None — main thread is single-threaded by contract. |
| **OpenAL Soft mixer thread** (owned + spawned by `alcCreateContext`) | None — the engine never calls back into OpenAL Soft from this thread. AL Soft pulls the source state we pushed via `alSource*f` and produces the output stream internally. | OpenAL Soft's own internal locks (opaque to the engine). |
| **Worker threads** | None — `engine/audio` does not own a worker pool; nothing in the public API is thread-safe for concurrent main-thread calls. | n/a |

**OpenAL's audio thread is not the engine's concern.** OpenAL Soft creates and owns it inside `alcCreateContext`; the engine's contract is "set source / listener / mixer state from the main thread, the AL thread reads it." Our pure-function helpers are deliberately lock-free and side-effect-free so a future migration to a job-system worker for the per-source compose step (§15) won't require redesign — only `AudioEngine`'s state-publish methods would need atomicity.

**Lock-free / atomic:** none required today. The mixer snapshot is published via a raw pointer (`m_mixerSnapshot`) read on the same thread that writes it; the duck snapshot is a single `float` read on the same thread as the writer; the caption announcer is a `std::function` set once at startup and called from the main thread. If any of those readers move off the main thread later, an `std::atomic<const AudioMixer*>` + a `std::atomic<float>` cover the publish without locks.

**Device-disconnected handling (§10):** OpenAL Soft fires the disconnect event on its own thread but exposes it via `alcGetIntegerv(device, ALC_CONNECTED, ...)` which the engine polls from the main thread.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. `engine/audio` budget is small; the heavy DSP is OpenAL Soft's responsibility on its own thread.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `AudioSystem::update` (≤ 32 live sources) | < 0.3 ms | TBD — measure by Phase 11 entry |
| `AudioEngine::updateGains` (per-frame sweep, 32 sources) | < 0.05 ms | TBD — measure by Phase 11 entry |
| `composeAudioSourceAlState` (per source) | < 0.005 ms | TBD — measure by Phase 11 entry |
| `playSound*` cold path (buffer cache miss, decode, AL upload) | < 50 ms (varies by clip length / format) | TBD — measure by Phase 11 entry |
| `playSound*` hot path (cache hit) | < 0.1 ms | TBD — measure by Phase 11 entry |
| `AudioEngine::initialize` | < 200 ms | TBD — measure by Phase 11 entry |
| `loadBuffer` (cached) | < 0.01 ms | TBD — measure by Phase 11 entry |
| `MusicSystem::update` (4 layers + queue + 1 stream) | < 0.2 ms | TBD — measure by Phase 11 entry |
| `AmbientSystem::update` (≤ 8 zones + ToD + scheduler) | < 0.1 ms | TBD — measure by Phase 11 entry |

Profiler markers / capture points: `AudioSystem::update`, `AudioEngine::updateGains`, `MusicSystem::update`, `AmbientSystem::update`. No `glPushDebugGroup` markers — `engine/audio` issues no GPU work.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (long-lived `unique_ptr` for `AudioEngine` + `unordered_map` buffer cache + `vector` source pool). No per-frame transient allocator. |
| Reusable per-frame buffers | `m_livePlaybacks` (`unordered_map<unsigned int, SourceMix>`) retains capacity across frames. `MusicStingerQueue` capacity-capped FIFO. Voice-eviction uses a `std::vector<VoiceCandidate>` rebuilt per acquisition (only fires on pool exhaustion). |
| Peak working set | Buffer cache dominates — typical scene 5–30 MB of decoded PCM (16-bit stereo @ 48 kHz ≈ 192 KB / s; 30s SFX clip ≈ 5.6 MB; 3-min music track ≈ 33 MB if non-streamed, or 0 MB streaming). HRTF dataset: 100–300 KB driver-side. Pool overhead: 32 source IDs ≈ 128 B. |
| Ownership | `Engine` owns `AudioSystem` via `SystemRegistry::m_systems`. `AudioSystem` owns `AudioEngine` by value. `AudioEngine` owns the buffer cache, source pool, mixer snapshot pointer (non-owning), HRTF settings, sandbox roots. OpenAL Soft owns its own internal mixer / device buffers. |
| Lifetimes | Buffer cache: scene-load duration in practice (cleared on `shutdown`; no per-scene eviction yet — §15). Source pool: engine-lifetime. `AudioSourceComponent`: scene-load to scene-unload. |

No `new`/`delete` in feature code. AL handles (`alGenSources` / `alGenBuffers`) are RAII-wrapped via `AudioEngine` ctor / dtor.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady state.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| No audio device (CI runner, muted desktop) | `AudioEngine::initialize()` returns `false`, sets `m_available = false` | Engine continues without sound; every play call short-circuits; captions still fire. |
| Audio file missing / decode failure | `loadBuffer` returns `0`; `playSound*` returns `0` | Substitute silence — fire-and-forget callers ignore the `0`; per-entity callers (`AudioSystem`) log and skip. Caption still fires (§12). |
| Sandbox rejection (path escapes configured roots) | `loadBuffer` returns `0` after `Logger::warning` | Substitute silence; security-relevant — log retains the rejected path. |
| Pool exhausted, no evictable voice | `playSound*` returns `0` | Caller raises `priority` to evict a lower-tier voice next time, or accepts the drop. |
| Looping sound played fire-and-forget | Held source slot until `shutdown()` | **Programmer error** — store the returned handle and call `stopSound(handle)`. Phase 10.9 Slice 15 Au1 made this explicit in the doxygen. |
| Buffer underflow on streaming music | `planStreamTick` returns `framesToDecode > 0` next tick to refill — silent gap in audio, no throw | Increase `minSecondsBuffered` if it recurs; usually a sign of decode-thread starvation. |
| Device disconnected mid-session (USB headset unplugged) | `ALC_CONNECTED` polls false; `m_available` flips to false | Engine continues without sound; UI surfaces a notification; user can rerun device-detection (Phase 11 — §15). |
| HRTF requested but driver denies | `getHrtfStatus()` returns `Denied` / `UnsupportedFormat`; `HrtfStatusListener` fires with mismatched `requestedMode` vs `actualStatus` | Settings UI shows "Requested: Forced / Actual: Denied (UnsupportedFormat)"; user picks a different output device or mode. |
| Sample rate / channel mismatch (decoder produces a non-AL format) | `AudioClip::getALFormat` only returns mono/stereo; multichannel files reject in the decoder | Re-author asset as mono / stereo. |
| Programmer error (null clip path, OOB AudioBus cast) | `assert` (debug) / UB (release) | Fix the caller. |
| OpenAL `alGenSources` exhaustion at init | `Logger::error` + `m_available = false` | Treated like no-device — engine continues without sound. |
| Out of memory | `std::bad_alloc` propagates | App aborts (matches CODING_STANDARDS §11). |

`Result<T,E>` / `std::expected` not yet adopted in `engine/audio` (predates the engine-wide policy). Most failure paths use `0`-as-sentinel for source IDs and `bool` returns for status. Migration is on the engine-wide debt list — see `engine/core` spec §15 #4.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Mixer bus gain + clamp + `effectiveBusGain` + 3/4-arg `resolveSourceGain` + ducking + voice eviction | `tests/test_audio_mixer.cpp` (45 tests) | Public API contract |
| Distance attenuation curves vs OpenAL spec formulas | `tests/test_audio_attenuation.cpp` (15 tests) | Parity with OpenAL native |
| Doppler-shift pitch ratio formula | `tests/test_audio_doppler.cpp` (15 tests) | Parity with OpenAL native |
| Material occlusion gain + low-pass blend | `tests/test_audio_occlusion.cpp` (15 tests) | Per-material + edge cases |
| Reverb preset table + zone weight + blend | `tests/test_audio_reverb.cpp` (13 tests) | Public API contract |
| Ambient zones + ToD weights + random one-shot scheduler | `tests/test_audio_ambient.cpp` (17 tests) | Pure-function math |
| Music layer slew + intensity → weights + stinger queue capacity | `tests/test_audio_music.cpp` (21 tests) | State-machine determinism |
| Streaming-music tick planner state machine | `tests/test_audio_music_stream.cpp` (16 tests) | Decision tree branches |
| HRTF mode / status labels + dataset index resolution + status event compose | `tests/test_audio_hrtf.cpp` (17 tests) | Headless — no AL device |
| Per-frame `AudioSourceAlState` compose | `tests/test_audio_source_state.cpp` (12 tests) | Pure-function compose |
| `AudioSourceComponent` clone + defaults | `tests/test_audio_source_component.cpp` (8 tests) | Component contract |
| `AudioEngine::loadBuffer` sandbox-roots rejection | `tests/test_audio_engine_sandbox.cpp` (5 tests) | Path-sandbox invariants |
| `AudioEngine::stopSound` + `playSound` fire-and-forget contract | `tests/test_audio_stop_sound.cpp` (4 tests) | Pool-slot lifecycle |
| Audio settings panel ImGui rendering + apply | `tests/test_audio_panel.cpp` (26 tests) | Editor integration |

**Adding a test for `engine/audio`:** drop a new `tests/test_audio_<thing>.cpp` next to its peers, link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use the pure-function helpers (`computeAttenuation`, `composeAudioSourceAlState`, `chooseVoiceToEvictForIncoming`, `planStreamTick`, …) directly without an `AudioEngine` instance — every primitive in this subsystem **except `AudioEngine` itself** is unit-testable headlessly without an AL device. `AudioEngine`-bound paths use `m_available = false` short-circuits in CI; full-loop AL exercise happens in the visual-test runner on a developer box.

**Coverage gap:** the OpenAL-thread side of mixing / HRTF / EFX is not unit-tested by the engine — that's OpenAL Soft's own test suite. Engine-side parity tests (`test_audio_attenuation.cpp`, `test_audio_doppler.cpp`) pin our CPU formulas to OpenAL's; cumulative drift would surface there before reaching playback.

## 12. Accessibility

`engine/audio` is **the highest-stakes accessibility surface in the engine** — for partially-sighted users (per project memory) and Deaf / hard-of-hearing users, audio output and audio-cue substitution are the difference between a navigable product and an unusable one. Phase 10.7 designed the integration; this section enumerates the contract.

**Separate buses (Game Accessibility Guideline + Xbox Accessibility Guideline 105):**

Six independent buses with persisted gain — Master, Music, Voice, SFX, Ambient, UI — exposed in Settings → Audio. Users who need narration loud and explosions quiet (concentration, hearing loss, screen-reader competition) get fine-grained control. The mixer storage lives in `AudioMixer` and is wired to the Settings apply chain via `AudioMixerApplySink` (in `engine/core/settings_apply.h`).

**Caption queue (Phase 10.7 + Phase 10.9 P4):**

`CaptionAnnouncer` fires inside every `playSound*` overload **before** the `!m_available` check. The engine-startup wires it to `CaptionMap::enqueueFor(clip, SubtitleQueue&)`. Captions are the substitute for the audio itself — Deaf users, users with broken hardware, users on muted output, and users in a noisy environment all see the announcement that game code intended to play sound.

**Subtitle apply-sink (Phase 10.7 Slice B):**

`SubtitleApplySink` (in `engine/core/settings_apply.h`) routes Settings → `SubtitleQueue` configuration: scale preset (Small 1.0× / Medium 1.25× / Large 1.5× / XL 2.0×), background opacity, per-category styling. The audio subsystem produces the queue events; the UI subsystem renders them; the apply-sink configures the rendering — three separate concerns, single Settings vocabulary.

**HRTF policy (headphones-only, off by default for speakers):**

`HrtfMode::Auto` is the sane default — the driver enables HRTF when stereo headphones are detected, disables it on speakers (where HRTF is *worse* than plain panning because the listener's ears re-convolve). Users on headphones who want HRTF unconditionally pick `Forced`; users on surround setups pick `Disabled`. The Settings UI surfaces `HrtfStatusEvent` so the user sees "Requested: Forced / Actual: Denied (UnsupportedFormat)" rather than a silent downgrade.

**Audible-cue alternatives for visual events (Phase 10.7 Area 4):**

UI bus is the canonical channel for accessibility cues — focus-ring move, menu confirm, error chime. Visual-only events (flashing icons, colour highlights) must publish a UI-bus sound when their meaning is gameplay-relevant. The engine doesn't enforce this; the convention lives in design docs and editor lint.

**Ducking for narration (Phase 10.9 P3):**

`DuckingState` ramps the global duck gain down to `duckFactor` (0.35 default) when narration / voice is active so SFX and music don't drown out spoken content. `resolveSourceGain`'s 4-arg overload folds the duck multiplier into the final `AL_GAIN`. Users with hearing loss or attention difficulties benefit from the louder voice : SFX ratio without manually rebalancing every slider.

**Photosensitive safety:**

Audio doesn't trigger seizures, so `PhotosensitiveLimits` doesn't constrain `engine/audio`. **However** — strobe / flash visuals that *also* play a sound need both clamps; the audio side stays at full authored volume, the visual side clamps via `engine/accessibility/photosensitive_safety.h`.

**Constraint summary for downstream UIs that consume `engine/audio`:**

- Settings UI must surface every bus on the Audio tab; defaults match `AudioMixer` constructor (every gain = 1.0).
- Caption / subtitle UI must accept `SubtitleQueue` events at any time; `CaptionAnnouncer` may fire mid-frame.
- HRTF UI must show the *resolved* status, not just the requested mode — ship the `HrtfStatusListener` payload to the UI.
- Volume sliders must persist via `AudioApplySink` so a user-configured mix survives engine restart.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/i_system.h` | engine subsystem | `AudioSystem`, `AmbientSystem`, `MusicSystem` are `ISystem` implementors. |
| `engine/core/event_bus.h` | engine subsystem | Subscribes to weather / scene events; publishes audio events when relevant. |
| `engine/core/system_events.h` | engine subsystem | Canonical event-type vocabulary. |
| `engine/core/logger.h` | engine subsystem | Failure logging (decoder, sandbox, pool exhaustion). |
| `engine/scene/component.h` | engine subsystem | `AudioSourceComponent` derives from `Component`. |
| `engine/scene/transform_component.h` | engine subsystem | Per-source world position read by `AudioSystem`. |
| `engine/renderer/camera.h` | engine subsystem | Listener pose / velocity sourced from the active camera. |
| `engine/ui/caption_map.h`, `engine/ui/subtitle.h` | engine subsystem | Caption-routing target — wired via `setCaptionAnnouncer`. |
| `engine/physics/raycast.h` | engine subsystem | Source→listener raycast feeds `occlusionFraction`. |
| `engine/utils/path_sandbox.h` | engine subsystem | Mirror of `ResourceManager::setSandboxRoots` (Phase 10.9 D11). |
| `OpenAL Soft 1.25.1` (`<AL/al.h>`, `<AL/alc.h>`, `<AL/alext.h>`) | external | Audio device, mixer, source playback, HRTF (`ALC_SOFT_HRTF`), EFX. Pinned at `external/CMakeLists.txt:362`. Upgraded 2026-04-30 from 1.24.1 (was Open Q1 in §15 — closed). |
| `dr_libs` (vendored) | external | WAV / MP3 / FLAC decoders (single-header, public domain). |
| `stb_vorbis` (vendored) | external | OGG Vorbis decoder. |
| `<glm/glm.hpp>` | external | `vec3` for positions / velocities. |
| `<chrono>`, `<filesystem>`, `<unordered_map>`, `<vector>`, `<array>`, `<optional>`, `<functional>`, `<string>` | std | Timing, paths, caches, source pool, fixed-size bus table, decoder-result optional, hooks, paths. |

**Direction:** `engine/audio` is depended on by `engine/systems/audio_system.h` (consumer of `AudioEngine`), `engine/core/settings_apply.h` (apply-sinks target the engine), and `engine/editor/settings_panel/audio_panel.cpp` (editor UI). It must **not** depend on `engine/renderer/renderer.h`, `engine/scene/scene_manager.h`, or any concrete `ISystem` other than its own.

## 14. References

Cited research / authoritative external sources:

- OpenAL Soft Project. *OpenAL Soft Releases — 1.25.1 (2025).* — point release, fmtlib build fix, WASAPI dynamic device enumeration, polyphase resampler hardening, debug assertion under HRTF. <https://github.com/kcat/openal-soft/releases>
- OpenAL Soft Project. *ChangeLog — master.* — current development line, Tetraphonic Surround Matrix Encoding, JACK/CoreAudio capture fixes. <https://github.com/kcat/openal-soft/blob/master/ChangeLog>
- OpenAL Soft Project. *`ALC_SOFT_HRTF` extension specification.* — `alcResetDeviceSOFT` semantics, `ALC_HRTF_STATUS_SOFT` enum values, dataset enumeration. <https://openal-soft.org/openal-extensions/SOFT_HRTF.txt>
- OpenAL 1.1 Specification. *§3.4 Attenuation by Distance, §3.5 Doppler Shift.* — canonical curves + Doppler formula consumed by `audio_attenuation.h` / `audio_doppler.h`. <https://openal.org/documentation/openal-1.1-specification.pdf>
- Microsoft. *Xbox Accessibility Guideline 105 — Allow audio adjustments by group.* — separate music / speech / SFX / background / TTS / accessibility-cue / voice-chat volume controls. (≤ 1 year old — current 2025 revision.) <https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/105>
- Microsoft. *Xbox Accessibility Guideline 104 — Subtitles.* — subtitle styling, scale, and per-category control. <https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/104>
- Game Accessibility Guidelines. *Subtitles & captions — clear, easy-to-read presentation.* <https://gameaccessibilityguidelines.com/if-any-subtitles-captions-are-used-present-them-in-a-clear-easy-to-read-way/>
- Naughty Dog. *The Last of Us Part II Accessibility Features Detailed* (industry-reference for caption tier scale presets, audible-cue substitution). <https://www.naughtydog.com/blog/the_last_of_us_part_ii_accessibility_features_detailed>
- PulseGeek. *Audio Pipeline Basics for Game Engines* (2025). — bus architecture overview, ducker placement, HRTF + spatial-audio choices for indie engines. <https://pulsegeek.com/articles/audio-pipeline-basics-for-game-engines/>
- Audiokinetic. *Blind Accessibility in Interactive Entertainment* (2024–2025). — sonification techniques, audio-cue alternatives for visual events, ducking-for-narration patterns. <https://www.audiokinetic.com/en/blog/blind-accessibility-in-interactive-entertainment/>
- Stride 3D Engine Manual. *Head-Related Transfer Function (HRTF) Audio.* — HRTF-on-by-default-for-headphones policy precedent. <https://doc.stride3d.net/latest/en/manual/audio/hrtf.html>
- Resonance Audio Project (Unreal Engine integration, 2025). — HRTF default option for spatialised sources. <https://resonance-audio.github.io/resonance-audio/develop/unreal/getting-started.html>
- Wikipedia. *Ambisonics.* — overview + game-industry adoption (OpenAL Soft uses ambisonics internally for 3D rendering). <https://en.wikipedia.org/wiki/Ambisonics>
- Creative Labs. *EFX Reverb Preset Table (`efx-presets.h`).* — source for `ReverbPreset` numeric values (Generic / Room / Hall / Cave / Plain / Underwater). Adapted to the standard reverb model (non-EAX).

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU placement — pure CPU rationale), §18 (public API), §22 (DI / globals).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement at design time).
- `docs/phases/phase_10_7_design.md` — accessibility + audio integration design doc shipped 2026-04-23 (mixer apply-sink, caption map, subtitle queue, ducking).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| ~~1~~ | ~~OpenAL Soft 1.24.1 → 1.25.1 upgrade — **closed 2026-04-30**: pin bumped in `external/CMakeLists.txt:362`, configure + build + tests green. 1.25.1 ships polyphase-resampler hardening, fmtlib integration fix, WASAPI dynamic device enumeration, JACK / CoreAudio capture fixes, Tetraphonic Surround Matrix Encoding. (Closed items live here in struck-out form for one revision before being moved to §16; subsequent unrelated revisions remove this row.)~~ | milnet01 | resolved |
| 2 | Buffer-cache eviction on scene unload. Currently `m_bufferCache` lives engine-lifetime; long sessions accumulate decoded PCM across scene changes. Design a per-scene reference-count or LRU eviction. | milnet01 | Phase 11 entry |
| 3 | Path canonicalisation for `loadBuffer` — case-sensitive exact-string keys mean two relative paths to the same file decode + cache twice. Canonicalise via `std::filesystem::weakly_canonical` before hashing. | milnet01 | Phase 11 entry |
| 4 | Device-disconnected hot-recovery (USB headset unplugged mid-session). `ALC_CONNECTED` polling exists; the UI / `AudioEngine` reset cycle does not. Decide: auto-reopen default device, or surface a UI prompt. | milnet01 | Phase 11 entry |
| 5 | Move per-frame source compose (`composeAudioSourceAlState` × N) to a worker thread. Pure-function helpers are already lock-free; only the `AudioEngine` state-publish would need atomicity. Profile first to confirm the move is worth it. | milnet01 | Phase 12 (post-MIT release) |
| 6 | Multi-channel (5.1 / 7.1) source files — `AudioClip::getALFormat` only returns mono / stereo. Authoring guidance is "submix to stereo offline"; if multi-channel sources land, decide whether to expand support or keep the mono/stereo contract. | milnet01 | triage |
| 7 | EFX reverb integration end-to-end — the `audio_reverb` math is pure-function tested; the engine-side `AudioReverbAdapter` that issues `alEffectf` calls is not yet present (Phase 10 deferred to Phase 12). Either land the adapter or remove the orphan headers. | milnet01 | Phase 12 |
| 8 | Performance budgets in §8 are placeholders. Need a Tracy capture of `AudioSystem::update` + `updateGains` under load to fill in measured numbers. | milnet01 | Phase 11 audit |
| 9 | `Result<T, E>` / `std::expected` adoption — `loadBuffer` / `playSound*` use `0`-as-sentinel; migration to typed errors covers the engine-wide debt thread. | milnet01 | Phase 12 (post-MIT release) |
| 10 | Ambisonics output / scene encoding for outdoor scenes. OpenAL Soft already renders internally via ambisonics; explicit ambisonics input bed + scene-graph routing would benefit large outdoor walkthroughs. Research-only at this stage. | milnet01 | triage |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/audio` formalised post-Phase 10.9. Captures the full Phase 9 / 10 / 10.7 / 10.9 surface (mixer, ducking, eviction, HRTF, captions, sandbox, source-state compose). |
