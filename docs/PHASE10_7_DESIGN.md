# Phase 10.7 — Accessibility + Audio Integration (Design Doc)

**Status:** Approved 2026-04-23 — all six §6 questions signed off (A1 / B3 / P1+P2 split / one-per-game / 2-of-4 scope reduction / slice order B → C → A). Implementation proceeds.
**Roadmap item:** `ROADMAP.md` Phase 10.7 (added in commit `acf94aa`).
**Scope:** Retrofit downstream consumers of the three engine-owned Settings stores that slice 13.5e wired up but left unconsumed — `AudioMixer`, `SubtitleQueue`, and `PhotosensitiveLimits`/`photosensitiveEnabled`. Settings already drive the stores live; this phase closes the store → runtime gap.

---

## 1. Why this doc exists

Phase 10 shipped the Settings UI + persistence + the "apply" path, and slice 13.5e parked the authoritative stores on `Engine`:

- `Engine::getAudioMixer()` — 6-bus gain table (Master / Music / Voice / Sfx / Ambient / UI), live-updated by `AudioMixerApplySink`.
- `Engine::getSubtitleQueue()` — FIFO caption queue with size preset / max-concurrent, live-updated by `SubtitleQueueApplySink`.
- `Engine::photosensitiveEnabled()` + `Engine::photosensitiveLimits()` — safe-mode flag + clamp parameters, live-updated by `PhotosensitiveStoreApplySink`.

But **no downstream code reads from these stores yet.** A user toggling "Music bus: 50 %" mutates `m_audioMixer`; a user picking "Subtitles: Large" mutates `m_subtitleQueue`; a user enabling "Photosensitive safe mode" flips `m_photosensitiveEnabled`. None of those mutations reach a consumer that would change what the user sees or hears. Phase 10 delivered the dial; Phase 10.7 connects the dial to the thing it was supposed to move.

This doc's job is to:
1. Fix the scope honestly — of the 9 retrofits originally listed in the roadmap, not every downstream consumer exists in the codebase yet. Camera shake and flash overlay subsystems are **not present** today; the clamp helpers are pre-built but sit unused.
2. Specify how each *existing* consumer consumes its store.
3. Decide the CPU/GPU placement per CLAUDE.md Rule 12.
4. Slice the work so each commit is review-sized and independently testable.
5. Get user sign-off before we touch `AudioSystem::update` or the renderer composite pass.

---

## 2. Current state (inventory)

Surveyed by Explore agent + spot-reads 2026-04-22.

### 2.1 Audio subsystem

| Piece | Location | State |
|---|---|---|
| `AudioMixer` — 6-bus linear-gain table | `engine/audio/audio_mixer.h:65–104` | Complete. `setBusGain(bus, gain)` clamps [0,1]; `effectiveBusGain(mixer, bus)` = master × bus. |
| `Engine::m_audioMixer` (authoritative) | `engine/core/engine.h` (slice 13.5e) | Owned + live-updated by `AudioMixerApplySink`. |
| `AudioSourceComponent` | `engine/audio/audio_source_component.h:23–92` | **Missing a `bus` field** — every source today is implicitly routed through nothing. |
| `AudioSystem::update` | `engine/systems/audio_system.cpp:37–50` | Only syncs listener to camera. **Does not iterate components; does not drive OpenAL gain from mixer.** |
| `AudioEngine::playSound*` | `engine/audio/audio_engine.h:85–122` | Fire-and-forget; `volume` parameter is stored on the OpenAL source once and never revisited. |
| `AL_GAIN` write sites | `engine/audio/audio_engine.cpp:277, 303, 330, 360` | Four — one per `playSound` overload. Each takes a single pre-multiplied gain. |
| Editor `AudioPanel` with its own `AudioMixer` | `engine/editor/panels/audio_panel.h:85–87` | **Parallel store** — the panel mutates its local `m_mixer`, never `Engine::getAudioMixer()`. Source of truth forked. |

**Implication:** Two design questions to resolve before writing code —
**(Q1)** where does the component→OpenAL gain resolution pass live (per-frame in `AudioSystem::update`, or at `playSound` call time)?
**(Q2)** how do we unify the editor `AudioPanel` with the engine mixer without breaking its mute/solo/ducking features?

### 2.2 Subtitle subsystem

| Piece | Location | State |
|---|---|---|
| `SubtitleQueue` | `engine/ui/subtitle.h:40–149` | Complete: `enqueue`, `tick(dt)`, `activeSubtitles()`, `setSizePreset`, `setMaxConcurrent`. |
| `SubtitleSizePreset` | `engine/ui/subtitle.h:24–30` | `Small (1.0×) / Medium (1.25×) / Large (1.5×) / XL (2.0×)`. |
| `SubtitleCategory` | `engine/ui/subtitle.h:15–21` | `Dialogue / Narrator / SoundCue`. Not yet mapped to styling. |
| `Engine::m_subtitleQueue` (authoritative) | `engine/core/engine.h` (slice 13.5e) | Owned + live-updated by `SubtitleQueueApplySink`. |
| `TextRenderer::renderText2D(...)` | `engine/renderer/text_renderer.h:45` | Complete. Batch primitive — ≤1024 glyphs/call. |
| Engine per-frame tick | `engine/core/engine.cpp` update loop | **`SubtitleQueue::tick(dt)` is not called anywhere.** Queue is a dead store. |
| HUD render pipeline | `engine/ui/game_screen.*` | Pure state machine. No render pass that would pick up subtitles. |
| `AudioClip → caption` map | — | Does not exist. Captions must be enqueued manually today. |

**Implication:** Slice B adds one call site for `tick`, one render-pass for active captions, and a declarative caption-lookup primitive. No new render *pass* — piggyback on the existing 2D overlay pass.

### 2.3 Photosensitive subsystem

| Piece | Location | State |
|---|---|---|
| `PhotosensitiveLimits` struct | `engine/accessibility/photosensitive_safety.h:31–45` | 4 fields: `maxFlashAlpha=0.25`, `shakeAmplitudeScale=0.25`, `maxStrobeHz=2.0`, `bloomIntensityScale=0.6`. |
| Clamp helpers | `engine/accessibility/photosensitive_safety.h:67–97` | `clampFlashAlpha`, `clampShakeAmplitude`, `clampStrobeHz`, `limitBloomIntensity`. Pure, unit-tested. |
| `Engine::photosensitiveEnabled/Limits` | `engine/core/engine.h:289–292` | Live-updated by `PhotosensitiveStoreApplySink`. |
| Bloom intensity write site | `engine/renderer/renderer.cpp:1209` | **Single choke point** — `m_screenShader.setFloat("u_bloomIntensity", m_bloomIntensity)`. Trivial retrofit. |
| Particle flicker | `engine/scene/particle_emitter.cpp:344–352` | Single choke point — `m_elapsedTime * m_config.flickerSpeed`. Trivial retrofit. |
| **Camera shake** | — | **Does not exist in the codebase.** Grep returns no shake accumulator on `Camera`, no shake API, no shake consumers. The clamp helper is ready but there is nothing to clamp. |
| **Flash overlay** | — | **Does not exist in the codebase.** No hit-flash, no screen-wipe, no transition flash. Clamp helper sits idle. |

**Implication:** The roadmap's 4-consumer list is aspirational. Only **bloom** and **strobe/flicker** retrofits are real work under Phase 10.7. Shake and flash consumers don't exist yet; building them belongs to Phase 11 (combat/gameplay) where the subsystems will actually originate. Slice C must not invent subsystems just to clamp them — that's CLAUDE.md Rule 6 (no over-engineering) territory.

---

## 3. CPU / GPU placement (CLAUDE.md Rule 12)

All three slices are **CPU work** and remain so.

**Slice A (audio).** Per-frame gain resolution iterates the owned `AudioSourceComponent` list (single-digit to low-hundreds of sources typical; OpenAL source pool caps at 32 regardless). Arithmetic is trivial (three multiplies per source + a clamp). OpenAL itself handles the per-sample DSP on a driver thread; we just push `AL_GAIN` uniforms. No GPU placement makes sense — these scalars are not per-pixel/per-vertex work, and the data never enters the graphics pipeline.

**Slice B (subtitles).** Caption tick is an O(active) decrement per frame (bounded at `DEFAULT_MAX_CONCURRENT = 3`). Rendering goes through the existing `TextRenderer` 2D glyph-batch path, which is already GPU-side via a vertex buffer of ≤6144 vertices for a maxed-out queue. Subtitle *tick* is CPU; subtitle *render* is the existing GPU text path — nothing new. Budget: < 0.05 ms/frame at worst case.

**Slice C (photosensitive).** Clamp helpers are pure-function `float → float` guards at CPU-side parameter-setting call sites. The *effects* they cap (bloom, flicker) execute on the GPU, but the clamp decides the uniform value that gets uploaded. Clamping is the correct CPU/GPU boundary — no point pushing a "is safe mode on" uniform to the shader when we can just multiply the intensity at the source.

No piece of this phase migrates to GPU. If a future shader-level photosensitive pass becomes desirable (e.g. a post-process luminance-delta limiter to enforce WCAG 2.3.1 directly), that would be a separate design doc.

---

## 4. Design

### 4.1 Slice A — Audio mixer → playback path

**Goal.** The per-bus gains in `Engine::getAudioMixer()` actually scale OpenAL playback. A user setting Music=0.5 audibly halves music; Master=0 silences everything.

**Design decision Q1 — where does the gain chain apply?**

| Option | Pros | Cons |
|---|---|---|
| **A1. Per-frame in `AudioSystem::update`**: iterate owned `AudioSourceComponent`s, compute `master × bus × source.volume`, push via `alSourcef(id, AL_GAIN, …)`. | Runtime slider moves are heard immediately (tick-accurate). Handles mid-play bus changes. Uniform path for all sources. | Requires tracking which OpenAL source ID each component drives (fire-and-forget model doesn't). Adds a per-frame component-iteration pass. |
| **A2. At `playSound*` call time only** — `AudioEngine` queries mixer once when acquiring the source. | Zero per-frame cost. Simple. | Mid-play slider move does not affect already-playing sounds. Violates "immediately-observable change" milestone. |

**Decision: A1.** The milestone literally says *"toggling any Settings tab option produces an immediately-observable change"* — A2 fails that. Cost of the iteration pass is negligible (≤32 sources × 5 multiplies = ~160 FLOPs/frame).

**Design decision Q2 — editor AudioPanel unification.**

The panel owns its own `AudioMixer m_mixer`. Editor users expect mute/solo/ducking to work in the editor without perturbing the player-facing Settings. Three options:

- **B1. Point panel at `Engine::getAudioMixer()`.** Editing in the panel writes to the player's settings store. Simple, but conflates editor-test gains with user preferences.
- **B2. Route panel through `SettingsEditor`.** Clean separation, but the panel's mute/solo/ducking are editor-local (not user settings) and don't belong in the settings schema.
- **B3. Keep panel-local mixer, read engine mixer as display-only.** Panel's mute/solo/ducking stay editor-local; the *authoritative bus gains* come from the engine. When the panel adjusts a bus gain, it routes through `SettingsEditor::mutate`. Mute/solo/ducking remain panel-local.

**Decision: B3.** Fewest concept collisions. Editor-only affordances stay editor-only; shared bus gains funnel through the single source of truth.

**Implementation shape:**

1. Add `AudioBus bus = AudioBus::Sfx;` to `AudioSourceComponent` (default SFX — matches current implicit routing). Serializer gains a field; missing key defaults to Sfx.
2. `AudioSystem` tracks `std::unordered_map<Entity, ALuint> m_activeSources` so a component can find its live OpenAL source.
3. `AudioSystem::update(dt)` adds a gain-resolution pass: for each owned component with a live source, compute `finalGain = clamp(effectiveBusGain(mixer, comp.bus) * comp.volume * comp.occlusionGain * comp.ducking, 0, 1)` and `alSourcef(src, AL_GAIN, finalGain)`.
4. `AudioEngine::playSoundSpatial(...)` accepts an optional `AudioBus` + `const AudioMixer*` so the *initial* gain upload already accounts for bus gain, avoiding a one-frame blip at full volume.
5. Editor `AudioPanel` routes bus-slider edits through `m_settingsEditor->mutate([](Settings& s){ ... })` rather than its local `m_mixer`. Mute/solo/ducking stay local.

**Gain composition order** (research-cited, Area 1): **master × bus × source** in linear space; sliders display dB as `20 × log10(linear)`; stored in `[0, 1]` with no boost trim in Phase 10.7 (defer boost toggle to Phase 11 if requested).

**Tests.**
- `test_audio_system_gain_chain.cpp`: given `mixer.setBusGain(Music, 0.5)` and a source tagged `Music` with `volume=0.8`, verify the AL_GAIN value is `0.4` after an update tick. Use a headless `AudioEngine` mock that captures `AL_GAIN` writes without opening a device.
- `test_audio_source_component_bus_field.cpp`: serializer round-trip preserves `bus`; missing field defaults to `Sfx`.
- `test_audio_panel_unified_mixer.cpp`: moving the panel's bus slider mutates `Engine::getAudioMixer()`, not only the panel-local copy.

### 4.2 Slice B — Subtitle tick + HUD render

**Goal.** `SubtitleQueue::tick(dt)` is called each frame; `activeSubtitles()` is rendered; size-preset + category drive the visual presentation.

**Design decision Q3 — tick call site.**

The obvious location is alongside the existing subsystem update loop. Place the call in `Engine::update(dt)` between input handling and physics so captions expire before the render pass they'd be drawn in. Single line: `m_subtitleQueue.tick(dt);`.

**Design decision Q4 — render pass placement.**

Two options:
- **R1. Dedicated 2D overlay pass in `Renderer::render()`** after post-process composite, before ImGui. Uses `TextRenderer::renderText2D` directly.
- **R2. Embed in `GameScreen`-driven HUD pipeline.** Requires first building a HUD pipeline (not present today — `game_screen.h` is a state machine only).

**Decision: R1.** R2 is Phase 11+ work (full HUD authoring). R1 gets captions on screen now through the existing primitive.

**Layout recipe** (research-cited, Area 2):

- **Base font size:** `basePx = 46 × (viewport_h / 1080)`, i.e. 46 px at 1080p, scaled with resolution. Matches Game Accessibility Guidelines.
- **Scale preset multiplier:** uses the existing `subtitleScaleFactorOf(preset)` helper — Small 1.0×, Medium 1.25×, Large 1.5×, XL 2.0×. (Research cites TLOU2-style 0.85×/1.0×/1.25×/1.5×; Vestige's existing 1.0–2.0 span gives more accessibility headroom without breaking authoring.)
- **Line budget:** soft-wrap at 40 characters; hard max 2 lines per entry (3 only if speaker label overflows — rare).
- **Background plate:** solid black at 50 % alpha, 8-px horizontal padding per line. Opacity becomes a user-tunable in a future phase (out of scope here).
- **Position:** bottom-center, 12 % from bottom edge. Configurable later.
- **Per-category styling** (Phase 10.7 MVP):
  - `Dialogue` — speaker label in yellow, dialogue in white.
  - `Narrator` — italic white.
  - `SoundCue` — `[bracketed]` cyan-grey.

**AudioClip → caption map.**

Ship a declarative `std::unordered_map<std::string, SubtitleTemplate> m_captionMap;` in the subtitle subsystem, loaded from `assets/captions.json`. Keys are clip paths; values are `{ category, template, duration }`. When an `AudioSourceComponent` plays a clip with a matching key, the audio system auto-enqueues a caption. No key = no caption (silent).

This keeps captions data-driven. The schema:

```json
{
  "audio/dialogue/moses_01.wav": {
    "category": "Dialogue",
    "speaker": "Moses",
    "text": "Draw near the mountain.",
    "duration": 3.5
  }
}
```

**Tests.**
- `test_subtitle_tick_integration.cpp`: `engine.update(1.0f)` advances queue countdown by 1 s.
- `test_subtitle_render_layout.cpp`: given viewport 1920×1080 + Medium preset, first caption renders at pixel size 57.5 (46 × 1.25) centered at y = 0.88 × height.
- `test_caption_map_lookup.cpp`: playing a clip with a mapped key auto-enqueues the caption; unmapped key plays silently (no log spam).

### 4.3 Slice C — Photosensitive consumer retrofits

**Goal.** Safe mode actually attenuates the effects that exist today.

**Scope honesty.** Only two subsystems have real consumers:

1. **Bloom** (`engine/renderer/renderer.cpp:1209`). Change `m_bloomIntensity` at the upload site to `limitBloomIntensity(m_bloomIntensity, m_engine->photosensitiveEnabled(), m_engine->photosensitiveLimits())`. Requires the renderer to hold a back-pointer to `Engine` (or a `PhotosensitiveLimits` reader). One-line change + 1 include.
2. **Particle flicker** (`engine/scene/particle_emitter.cpp:344`). The effective flicker frequency is `m_config.flickerSpeed`; wrap it with `clampStrobeHz(m_config.flickerSpeed, engine.photosensitiveEnabled(), engine.photosensitiveLimits())` at the emitter tick. Particle presets keep their authored flicker; safe mode caps them at the runtime.

**Camera shake** and **flash overlay** are retrofits to subsystems that **do not exist in the codebase today** (grep returns no shake accumulator on `Camera`, no hit-flash, no screen-wipe). Phase 10.7 does **not** invent these just to clamp them — that violates Rule 6. Instead, both appear in this doc as *deferred retrofits*: when Phase 11 (combat/gameplay) lands either subsystem, its design doc **must** wire in the appropriate clamp helper as part of the subsystem's initial implementation. The clamp helpers are already unit-tested and ready.

This shrinks Slice C from 4 to 2 real retrofits. The roadmap entry will be amended accordingly.

**Renderer back-pointer.** `Renderer` currently does not hold an `Engine*`. Two options:
- **P1.** Add `void Renderer::setPhotosensitive(bool enabled, const PhotosensitiveLimits& limits)` and have `Engine::update` push the values every frame. Cheap; no lifetime coupling.
- **P2.** Pass `Engine&` into `Renderer::render()`. Tighter coupling; easier to extend to other store reads (e.g. subtitle queue for render access in slice B).

**Decision: P1 for bloom; P2 for slice B's subtitle render.** Keeps the bloom retrofit minimal (one setter). Slice B needs broader engine access (mixer for caption volume triggers, subtitle queue), so it justifies passing `Engine&`.

**Tests.**
- `test_photosensitive_bloom_retrofit.cpp`: with `photosensitiveEnabled=true` and `bloomIntensityScale=0.6`, a set-point of `0.05` intensity uploads `0.03` to the shader.
- `test_photosensitive_flicker_retrofit.cpp`: with `photosensitiveEnabled=true` and `maxStrobeHz=2.0`, an emitter authored at `flickerSpeed=12` ticks at effective 2 Hz.
- Existing `test_photosensitive_safety.cpp` stays green (pure clamp helpers unchanged).

---

## 5. Slicing + commit plan

Each slice is a review-sized commit with tests + CHANGELOG entry. Slices are independent; Slice A and Slice C can proceed in either order. Slice B depends on nothing.

| Slice | Rough LOC | Commits | Notes |
|---|---|---|---|
| **A1** — `AudioBus` on `AudioSourceComponent` + serializer | ~80 | 1 | Pure data-model extension. No behavior change yet. |
| **A2** — `AudioSystem` per-frame gain-resolution pass | ~150 | 1 | Adds m_activeSources tracking + tick. Depends on A1. |
| **A3** — `AudioPanel` unification | ~100 | 1 | Routes panel bus sliders through SettingsEditor. Depends on A2. |
| **B1** — `SubtitleQueue::tick(dt)` in `Engine::update` | ~20 | 1 | One-line tick call + test. |
| **B2** — Subtitle 2D render pass | ~200 | 1 | New render-pass hookup + layout recipe. |
| **B3** — Declarative `captions.json` + auto-enqueue | ~180 | 1 | Data-driven caption map loaded at engine init. |
| **C1** — Bloom intensity retrofit | ~40 | 1 | `Renderer::setPhotosensitive` setter + upload-site change. |
| **C2** — Particle flicker retrofit | ~40 | 1 | `clampStrobeHz` wrap at emitter tick. |

**Total:** 8 slices, ~810 LOC. Comparable to Phase 10 slice 13.3 sizing.

---

## 6. Open questions (blocking approval)

1. **Audio mixer — A1 vs A2 for gain-chain placement?** Doc recommends A1 (per-frame tick) for mid-play responsiveness. Confirm.
2. **AudioPanel unification — B3 preferred (panel = display-layer over engine mixer, mute/solo local)?** Confirm.
3. **Renderer access pattern — P1 (setter per frame) + P2 (engine& in render) split?** Alternative: consolidate on P2 everywhere. Confirm.
4. **Caption map format — JSON path `assets/captions.json`, one-per-game?** Alternative: per-scene caption maps. Recommend one-per-game for Phase 10.7; per-scene is a future concern.
5. **Scope reduction — accept 2-of-4 retrofits in Slice C (bloom + flicker only; shake + flash deferred to Phase 11)?** Confirm.
6. **Slice ordering — start with A (audio) or B (subtitles) or C (photosensitive)?** Recommendation: **Slice B first** — smallest blast radius, most immediately visible, no data-model changes. Then Slice C (tiny). Then Slice A (largest; touches the audio system). Confirm.

---

## 7. Non-goals (explicitly out of scope)

- **Per-channel mute/solo** persistence. Editor-local only.
- **Ducking** changes. Existing ducking logic stays unchanged.
- **HDR audio / make-up gain / boost > 1.0.** Deferred.
- **Caption customization UI** (font, color, outline). Phase 11 or later — FCC 2024 compliance target is Aug 2026.
- **Camera shake / flash overlay subsystems.** These don't exist; they belong to their originating phase (Phase 11 combat / Phase 11 UI transitions).
- **Harding FPA offline verification pass.** Worth doing eventually (CLAUDE.md calibration), but out of scope for this retrofit.
- **Audio bus routing beyond the 6 fixed buses.** Phase 11 can add sub-buses if needed.

---

## 8. Research citations

**Audio.**
- Wwise 101 — Master Audio Bus: https://www.audiokinetic.com/en/courses/wwise101/?id=master_audio_bus
- FMOD Studio — Mixing: https://www.fmod.com/docs/2.03/studio/mixing.html
- Unity Manual — Audio Mixer: https://docs.unity3d.com/Manual/AudioMixer.html
- Unreal Engine 5.7 — Submix overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-submixes-in-unreal-engine

**Subtitles.**
- Game Accessibility Guidelines — subtitles: https://gameaccessibilityguidelines.com/if-any-subtitles-captions-are-used-present-them-in-a-clear-easy-to-read-way/
- Xbox Accessibility Guideline 104: https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/104
- TLOU2 Accessibility Features: https://www.naughtydog.com/blog/the_last_of_us_part_ii_accessibility_features_detailed
- FCC 2024 caption display-settings rule (CVAA): https://www.dwt.com/blogs/broadband-advisor/2024/07/fcc-rules-on-closed-captioning-accessibility

**Photosensitive.**
- WCAG 2.3.1: https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html
- Harding FPA — interpret results: https://www.hardingfpa.com/technical-support/how-to-interpret-hardingfpa-results/
- Xbox Accessibility Guideline 118: https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/118

---

## 9. Approval checklist

- [x] §6 Q1 — audio gain-chain placement (A1) — approved 2026-04-23
- [x] §6 Q2 — audio panel unification (B3) — approved 2026-04-23
- [x] §6 Q3 — renderer access pattern (P1 + P2) — approved 2026-04-23
- [x] §6 Q4 — caption map format (`assets/captions.json` one-per-game) — approved 2026-04-23
- [x] §6 Q5 — Slice C scope reduction (2-of-4) — approved 2026-04-23
- [x] §6 Q6 — slice order (B → C → A) — approved 2026-04-23
- [x] ROADMAP.md amended to reflect shake/flash deferral — 2026-04-23
- [ ] CHANGELOG.md Unreleased entry prepared once slicing starts
