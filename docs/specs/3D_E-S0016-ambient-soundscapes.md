<!-- ants-spec-format: 1 -->
# 3D_E-S0016 — Ambient soundscapes that play in the running app

**Status:** draft (2026-10-02).
**Kind:** implement.
**Source:** ROADMAP 3D_E-S0016 ("Ambient soundscapes (biome-based, time-of-day-based)").

Layman: places in a scene can now carry background sound, such as wind, a stream or night insects, which fades in as you approach and changes with the time of day.

## 1. Goal

A scene author places ambient zones as scene entities. In the running app each
zone plays a looping bed on the `Ambient` bus whose volume follows the
listener's distance, the time of day and the zones of higher priority around
it, and may scatter random one-shots (a bird call, a creak) inside itself.
Zones save with the scene and are edited in the Audio panel.

## 2. Problem

`engine/audio/audio_ambient.h` holds the pieces and nothing uses them:
`AmbientZone`, `computeAmbientZoneVolume`, `computeTimeOfDayWeights` and
`RandomOneShotScheduler`. Its header says an "engine-side AmbientSystem"
composes them; no such system exists, and no engine code outside
`audio_ambient.*` calls those functions (`rg -n
"computeAmbientZoneVolume|computeTimeOfDayWeights|tickRandomOneShot" engine
--glob '!engine/audio/audio_ambient.*'` prints nothing).

The Audio panel's Zones tab keeps ambient zones in `AudioPanel::m_ambientZones`,
a list owned by the panel. Its own header calls it "still editor-draft, no
runtime ambient component yet": the zones are not saved and nothing plays them.

The engine has no clock for the time of day; nothing else in `engine/` reads
or sets one. A looping non-spatial sound became possible only with 3D_E-0738,
which gave `AudioEngine::playSound2D` a loop flag, and a holder can tell
whether a source is still its own only since 3D_E-0739 added
`AudioEngine::playbackTicket`.

## 3. Scope decisions (agreed with the user)

Proposed by Claude and agreed by the user on 2026-10-02.

- **No biome input.** The engine has no biome concept to read. A zone is placed
  where its sound belongs; that covers what a biome would select.
- **The clock lives in `AmbientSystem` and is not saved with the scene.** It
  defaults to midday with the clock stopped, and the Audio panel and code set
  it. Saving it would add a scene-file block and a format version for a clock
  that a future day/night sky system should own.
- **No weather modulation.** The header's Phase 15 weather hook stays future
  work.

## 4. Design

### 4.1 The component

`AmbientZoneComponent` (new, `engine/audio/ambient_zone_component.h`) marks an
entity as an ambient zone. Its position is the entity's world position, as
`ReverbZoneComponent`'s is. It holds:

- `AmbientZone zone` — clip, core radius, falloff band, maximum volume and
  priority, unchanged from `audio_ambient.h`.
- `std::uint8_t windows` — a bit per `TimeOfDayWindow` (bit `n` is the window
  whose enum value is `n`). Default: all four set.
- `std::vector<std::string> oneShotClips`, `float oneShotVolume` (default 1),
  and the scheduler's `minIntervalSeconds` / `maxIntervalSeconds` (defaults
  15 and 45, as `RandomOneShotScheduler`'s). An empty list disables one-shots.

`EntitySerializer` saves and loads it beside `ReverbZoneComponent`, under its
own key. A file without the key loads with no component; no scene format
version changes.

### 4.2 The mix

A pure function in `audio_ambient.h` computes every zone's weight for one
frame from each zone's `AmbientZone`, `windows` and distance to the listener,
plus the hour:

1. `base = computeAmbientZoneVolume(zone, distance) × windowWeight(windows, hour)`,
   where `windowWeight` is the sum of `computeTimeOfDayWeights(hour)`'s
   entries for the windows whose bit is set.
2. `shadow` = the largest `base` among zones of strictly higher `priority`, or
   0 when there are none.
3. `weight = base × (1 − shadow)`.

Zones of equal priority do not shadow each other; they mix at their own
weights. This is what "higher-priority zones override" in `audio_ambient.h`
means here: a cave zone at full strength silences the outdoor wind, and half
strength halves it.

### 4.3 The system

`AmbientSystem` (new, `engine/systems/ambient_system.{h,cpp}`) runs in
`UpdatePhase::PostCamera` and is registered after `ReverbSystem` and before
`AudioSystem`, for the reason `ReverbSystem` gives: it reads the settled
listener, and the gains it sets are uploaded by `AudioSystem::update`'s call
to `AudioEngine::updateGains` in the same frame. It caches `AudioSystem`'s
`AudioEngine` at initialisation as `ReverbSystem` does; with none, `update`
does nothing.

Each frame it:

1. Advances the clock: `hour += deltaTime × hoursPerMinute / 60`, wrapped into
   [0, 24). Public `hourOfDay`, `setHourOfDay`, `hoursPerMinute` and
   `setHoursPerMinute`; defaults 12 and 0.
2. Computes §4.2's weights for every entity carrying an `AmbientZoneComponent`
   with a non-empty clip, using the camera position as the listener.
3. Reconciles one looping bed per zone, keyed by entity id. Each zone's bed
   is held as its source and `AudioEngine::playbackTicket` for it, and the
   zone **owns** the bed only while that source still carries that ticket and
   is playing. Eviction hands a source's name to another sound (3D_E-0739),
   so the system never calls `setSourceVolume` or `stopSound` on a source it
   does not own.
   - weight > 0 and no owned bed → `playSound2D(clip, weight,
     AudioBus::Ambient, SoundPriority::Low, loop = true)`;
   - weight > 0 and an owned bed → set its volume to the weight;
   - weight = 0, the clip emptied, the entity gone, or the component removed
     → `stopSound` on an owned bed, and forget any bed it no longer owns;
   - clip changed → `stopSound` on the owned bed, then start the new clip as
     above.
   A start that returns source 0 is not retried for 1 second, so a missing
   clip or a full pool does not repeat the engine's warning every frame. No
   start is attempted while `AudioEngine::isAvailable()` is false: every
   `playSound*` call announces its clip's caption before it checks for a
   device, so retries with no device would show a caption every second.
4. Arms each zone's `RandomOneShotScheduler` when the system first sees the
   zone, with `timeUntilNextFire` drawn from its interval as a fire would draw
   it, and ticks it only while the zone's weight is above 0. On a fire it
   picks one of `oneShotClips` and plays it with
   `playSoundSpatial` on the `Ambient` bus at `SoundPriority::Low`, at a point
   inside the zone's core radius on the horizontal plane through the zone,
   with volume `oneShotVolume × windowWeight(windows, hour)`. One-shots do not
   loop and are not tracked.

The planning in step 3 is a pure function of the previous frame's beds and
their clips, whether each is still owned, the time since each zone's last
failed start, and this frame's weights and clips. `AmbientSystem`
applies its result to the `AudioEngine`, so it can be tested without a device.
Randomness comes from one `std::mt19937` per system, seeded once at
initialisation. Tests pass their own uniform samples to the pure helpers.

### 4.4 Setting a playing source's volume

`AudioEngine::setSourceVolume(unsigned int source, float volume)` (new)
replaces the stored volume of a source the engine is tracking, so the next
`updateGains` uses it. Callers pass the same pre-makeup volume they gave
`playSound2D`; the engine keeps each source's loudness makeup in `SourceMix`
at play time and applies it again. An unknown or released source is
ignored. Writing `AL_GAIN` directly would be overwritten by `updateGains`,
which recomputes every tracked source's gain each frame.

### 4.5 The editor

The Zones tab's ambient list becomes scene entities, as its reverb list already
is. `AudioPanel::createAmbientZone(Scene&)` creates an entity named
"Ambient Zone" with an `AmbientZoneComponent` and selects it;
`removeAmbientZone(Scene&, entityId)` removes it. The panel edits the selected
zone's fields, including a checkbox per time window and the one-shot list. The
draft list, `AmbientZoneInstance`, `addAmbientZone(const AmbientZoneInstance&)`
and the index-based selection go. The tab also shows and sets
`AmbientSystem`'s hour and rate.

## 5. Invariants

- **INV-1** — A zone's weight is `computeAmbientZoneVolume × windowWeight ×
  (1 − shadow)`, where `shadow` is the largest base weight among zones of
  strictly higher priority.
  *Test:* `tests/test_audio_ambient.cpp`: a priority-1 zone at base 0.5 over a
  priority-0 zone at base 1 gives the lower zone 0.5; two priority-0 zones at
  base 1 each keep 1.
  *Breaks when:* equal priorities shadow each other, or the shadow takes the
  shadowing zone's weight after its own shadow instead of its base.

- **INV-2** — `windowWeight` is the sum of the enabled windows' weights. With
  all four enabled it is 1 at every hour; with none it is 0.
  *Test:* `tests/test_audio_ambient.cpp`, sampling hours 0 to 24 in steps of
  0.25.
  *Breaks when:* the mask is read as "any enabled window active", which gives
  a hard switch instead of a fade.

- **INV-3** — After a frame's plan is applied, each zone with weight above 0
  owns exactly one looping bed playing its current clip, unless its last start
  returned source 0 less than 1 second ago, and each zone with weight 0, an
  empty clip, a removed component or a removed entity owns none. The plan
  never sets the volume
  of, or stops, a source whose ticket is not the one the zone stored. A bed
  lost to eviction is started again on the first frame its weight is above 0;
  a start that returned source 0 is retried no sooner than 1 second later.
  *Test:* `tests/test_ambient_system.cpp`, driving the pure planner over
  frames with a fake pool whose sources carry tickets, including a source
  re-granted to another sound.
  *Breaks when:* a zone that falls to weight 0 keeps its source, holding one
  of the pool's sources while silent; or a zone whose bed was evicted stops
  the sound that now holds that source.

- **INV-4** — The clock advances by `deltaTime × hoursPerMinute / 60` and
  stays in [0, 24); a rate of 0 holds it.
  *Test:* `tests/test_ambient_system.cpp`: 23.5 advanced by 60 s at 1 hour per
  minute reads 0.5.
  *Breaks when:* the hour is not wrapped, so `computeTimeOfDayWeights` is fed
  an hour past 24.

- **INV-5** — A zone's one-shot scheduler is armed with a drawn interval when
  the zone is first seen, and advances only while its weight is above 0. A
  fire picks a point within the core radius on the zone's horizontal plane.
  *Test:* `tests/test_ambient_system.cpp`: with every uniform sample 0 (a
  15 s interval) and 1 s frames, a zone at weight 0 for 100 s and then above
  0 first fires on its 15th audible frame; the position helper, given samples
  of 0 and 1, stays within the radius.
  *Breaks when:* the scheduler starts unarmed, so a zone fires on its first
  audible frame; or it ticks while silent, so the first audible fire comes
  early.

- **INV-6** — `AmbientZoneComponent` round-trips through `EntitySerializer`
  with every field, and an entity saved without one loads without one.
  *Test:* `tests/test_entity_serializer_registry.cpp`.
  *Breaks when:* a field is written but not read, such as `windows`.

- **INV-7** — `AudioPanel::createAmbientZone` adds an entity carrying an
  `AmbientZoneComponent` and selects it; `removeAmbientZone` removes it and
  clears the selection.
  *Test:* `tests/test_audio_panel.cpp`, replacing the two draft-list tests.
  *Breaks when:* the panel keeps a private list again, so zones are not saved.

## 6. Failure modes

- A clip that fails to load returns source 0. The zone retries once a
  second (§4.3); the loader's own warning is the signal.
- More zones above weight 0 than the source pool holds: beds play at
  `SoundPriority::Low`, so the pool evicts them before anything else, and they
  restart when a source frees.
- No audio device: no bed is started (§4.3 step 3), so a bed's caption does
  not repeat every second. One-shots still fire and announce their captions,
  as every `playSound*` call does with no device.

## 7. Tests

- `tests/test_audio_ambient.cpp` — INV-1, INV-2.
- `tests/test_ambient_system.cpp` (new) — INV-3, INV-4, INV-5.
- `tests/test_entity_serializer_registry.cpp` — INV-6.
- `tests/test_audio_panel.cpp` — INV-7.
- Manual, in the app: place a zone with a looping clip in the meadow, walk in
  and out of it, and hear the bed fade; set the hour across a window boundary
  and hear a night-only zone fade in.

## 8. Alternatives considered (and rejected)

- **Drive a companion `AudioSourceComponent` on the zone entity** and let
  `AudioSystem` play it. It reuses the compose path, but a component holds its
  source while silent unless something stops it, and `AudioSystem` never stops
  a tracked source whose component still exists. Owning the beds in
  `AmbientSystem` keeps the start / stop decision in one place.
- **Spatial beds.** A bed is heard everywhere inside its core; distance
  attenuation would fight the falloff band. The falloff already places it.
- **Save the clock in the scene file** — §3.

## 9. Out of scope

- Biomes and weather (§3).
- A day/night sky that would own the clock.
- Crossfading between two clips within one zone; a zone has one bed.
- The meadow's own soundscape and its audio assets: 3D_E-0036 builds on this.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2 | `tests/test_audio_ambient.cpp` |
| INV-3, INV-4, INV-5 | `tests/test_ambient_system.cpp` |
| INV-6 | `tests/test_entity_serializer_registry.cpp` |
| INV-7 | `tests/test_audio_panel.cpp` |
| `setSourceVolume` changes the gain `updateGains` uploads (§4.4) | **nothing** — no test opens an audio device; the manual check in §7 |
| `AmbientSystem` is registered before `AudioSystem` (§4.3) | **nothing** — in the other order the volume lands a frame late |
| `AmbientSystem` applies the planner's result to the engine (§4.3) | **nothing** automated; the manual check in §7 |

## 11. Cross-doc impact

- `CHANGELOG.md`: one entry.
- `engine/audio/audio_ambient.h`'s header comment names the system; it is
  updated to name `AmbientSystem` and drop "once the Phase 15 weather
  controller publishes" from the present tense.
- `docs/phases/` designs that mention ambient zones are closed and not edited.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-S0016-ambient-soundscapes-loop-log.md`.
