# Phase 10 — Audio Quick-Wins Bundle (Design Doc)

**Status:** ✅ Signed off for implementation (2026-06-29). Cold-eyes looped to clean
(4 loops; sign-off delegated per session standing instruction — loop 4 returned zero
CRITICAL/HIGH, polish-only). See the Cold-eyes loop log at §14.
**Scope:** Six self-contained audio enhancements that reuse the shipped audio
subsystem and need no new heavy plumbing (no MT2 job system, no Phase 16 AI
Director): **AX8** surround output, **AX6** air absorption, **AX5** audio LOD
ladder, **AX13** generalised side-chain ducking, **AX11** device hot-swap, and
**AX9** loudness normalisation. These are the "quick-wins tier" of the
ROADMAP `## Audio System` section (AX1–AX14).
**Deferred (out of scope, with reason):** AX1 (geometric ray-traced occlusion —
hard-depends on the **MT2 job system in Phase 10.6, which is unbuilt**),
AX2/AX3 (convolution reverb + acoustic pre-bake — heavy, needs an FFT dependency;
separate bundle), AX4 (procedural footstep synthesis — separate bundle),
AX7 (ambisonics), AX12 (spectrum viewer — editor-only, depends on FFT),
AX10/AX14 (need the Phase 16 AI Director encounter score).
**References:** see §13 (OpenAL Soft extensions, ISO 9613-1, EBU R128 / ITU-R
BS.1770, libebur128).

**Contents:** §0 what exists · §1 goals · §2 slice plan & order · §3 AX8 surround ·
§4 AX6 air absorption · §5 AX5 LOD ladder · §6 AX13 ducking · §7 AX11 hot-swap ·
§8 AX9 loudness · §9 cross-cutting · §10 performance · §11 accessibility ·
§12 resolved decisions · §13 references · §14 cold-eyes log.

---

## 0. What already exists (reality check, verified 2026-06-29)

Every claim below was read out of current source this session (global Rule 13 —
no recall). File:line citations are the authoritative anchor for the implementer.

| Subsystem | Where | Notes relevant to this bundle |
|-----------|-------|-------------------------------|
| `AudioEngine` lifecycle | `engine/audio/audio_engine.h:51` (`initialize`), `:54` (`shutdown`), `:66` (`updateListener`); pool constant `MAX_SOURCES = 32` at `:40` | `initialize()` opens `alcOpenDevice(nullptr)` → `alcCreateContext` → `alcMakeContextCurrent` (the ALC calls are in `audio_engine.cpp`, not the header). 32-source pool. **Main thread only**; no audio thread. |
| `alcResetDeviceSOFT` already loaded | `engine/audio/audio_engine.h:439` (`m_alcResetDeviceSOFT`) | Loaded via `alcGetProcAddress` and **already used for HRTF** (`applyHrtfSettings()`, audio_engine.h:445). AX8 + AX11 extend this exact path. |
| 6-bus mixer | `engine/audio/audio_mixer.h:65–73` | `AudioBus { Master, Music, Voice, Sfx, Ambient, Ui }`, `AudioBusCount = 6`. `effectiveBusGain`, `resolveSourceGain` (3- and 4-arg, the 4-arg folds in `duckingGain`). All gains clamped [0,1]. |
| **No master-bus PCM tap** | — | OpenAL Soft does **not** expose post-mix master samples during normal hardware playback. Load-bearing for AX9 (§8). |
| `AudioSourceComponent` | `engine/audio/audio_source_component.h:30–118` | `volume, bus, pitch, minDistance, maxDistance, rolloffFactor, attenuationModel, velocity, occlusionMaterial, occlusionFraction, spatial, priority` (`SoundPriority`). AX5/AX6 read these. |
| Occlusion is **gain-only** today; no EFX filter exists | `engine/audio/audio_occlusion.h:95–104` | `computeObstructionGain()` folds into the source **gain** (audio_source_state.h:67–71). `computeObstructionLowPass(lowPassAmount, fractionBlocked)` is **computed but never applied** — there is **no `alFilter*` / `AL_LOWPASS_GAINHF` call anywhere in `engine/`** (verified by grep) and `AudioSourceAlState` carries **no HF-gain/filter field**. ⟹ AX6 must **introduce** per-source EFX low-pass plumbing (it cannot "compose into" a filter that isn't wired). See §4 + the code-side open question in §12-Q6. |
| `AudioSourceAlState` composition seam | `engine/audio/audio_source_state.h` (struct :33–50, fn `composeAudioSourceAlState(comp, entityPosition, mixer, duckingGain)` :75–79) | Called per source per frame in `AudioSystem::update` (audio_system.cpp:147,164). Fields: `position, velocity, pitch, gain, referenceDistance, maxDistance, rolloffFactor, attenuationModel, spatial` — **no HF/low-pass field**. Signature takes the **entity (source) position only — no listener position and no `EnvironmentForces` handle.** Hook point for AX5 + AX6, but AX6 needs a **signature change** (§4). |
| Ducking | `DuckingParams` audio_mixer.h:148–153, `DuckingState` :159–163, `updateDucking` :168–170 | `DuckingParams {attackSeconds, releaseSeconds, duckFactor}`, `DuckingState {currentGain, triggered}`, `updateDucking(state, params, dt)`. Single **global** duck. Driven by setting `Engine::getDuckingState().triggered` (engine.h:297) from editor/gameplay — **there is no `triggerDuck()` method**; callers flip the `.triggered` bool directly. Advanced at audio_system.cpp:61–63, published via `setDuckingSnapshot()` (:64–65), folded into gains by `updateGains()` (:76); the `duck` scalar is *also* passed into `composeAudioSourceAlState` (:147,164). **AX13 generalises this** (see §6 for which site it owns). |
| Update path / seam map | `AudioSystem::update` audio_system.cpp:41–187 | Stage order: `updateListener` → `updateDucking` → `setMixerSnapshot`+`updateGains` → per-entity `composeAudioSourceAlState`+`applySourceState` → reap. AX11 polls at the top; AX13 replaces the `updateDucking` stage; AX5/AX6 extend the compose stage. |
| `EnvironmentForces` weather query | `engine/environment/environment_forces.h:20–28` (struct), `:74` (`getTemperature`), `:77` (`getHumidity`) | `WeatherState { temperature(°C)=20, humidity[0,1]=0.5, precipitation, wetness, cloudCover, airDensity=1.225 }`; `getTemperature(const glm::vec3&)`, `getHumidity(const glm::vec3&)`. **AX6 pulls these** — but the compose seam has no `EnvironmentForces` handle today (§4). (Weather is global today — a single `WeatherState` member, environment_forces.h:163 — so the `worldPos` arg is currently uniform across space; AX6 takes a once-per-frame snapshot.) |
| `Settings` add-pattern | `engine/core/settings.h:111–126` (`AudioSettings { busGains[6], hrtfEnabled }`), `settings.cpp` `toJson/fromJson/validate`, `settings_apply.h` (`AudioApplySink`, `AudioHrtfApplySink`), `settings_editor.h:56–67` (`ApplyTargets`), `engine/editor/panels/audio_panel.{h,cpp}` | Adding a setting = field → JSON wire → ApplySink → `ApplyTargets` → panel widget, with `schemaVersion` migration + `validate()` clamp. This is the shipped L5 localization pattern. |
| HRTF path | `audio_hrtf.h`; `AudioEngine::setHrtfMode` (audio_engine.h:374) → `applyHrtfSettings` (445) → `alcResetDeviceSOFT` with `ALC_HRTF_SOFT` | AX8 must compose with this (HRTF ⟹ stereo output; see §3). |
| `AudioAnalyzer` — **lip-sync tool, NOT a loudness primitive** | `engine/experimental/animation/audio_analyzer.h` | Operates on caller-fed **mono** PCM to estimate visemes / jaw-open weight (`getEstimatedViseme`, `feedSamples`). It is **not** a master-bus or LUFS primitive and is **not reused by AX9** — AX9 brings its own measurement (§8). Listed here only to pre-empt the false assumption that a master-bus spectral tap exists. |
| Test pattern | `tests/test_audio_*.cpp` (17 files) | Pure-function numeric tests (`EXPECT_NEAR(x,y,kEps)`, `kEps=1e-4f`); state-machine slew tests; headless (`AudioEngine::isAvailable()` gates all AL calls so tests run with no device); mock `ApplySink` records calls (`test_settings.cpp:1105`). |

**Two ROADMAP API names are stale and are corrected by this doc (Rule 13):**

- AX11 cites `ALC_DEVICE_NOTIFICATIONS_SOFT` — **no such extension.** The current
  OpenAL Soft extension is **`ALC_SOFT_system_events`** (events
  `ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT`, `…_DEVICE_ADDED_SOFT`,
  `…_DEVICE_REMOVED_SOFT`; functions `alcEventIsSupportedSOFT`,
  `alcEventControlSOFT`, `alcEventCallbackSOFT`), and the device swap that keeps
  the context + sources alive is **`alcReopenDeviceSOFT`** (`ALC_SOFT_reopen_device`).
- AX8 cites `ALC_FORMAT_CHANNELS_SOFT` — the modern channel-layout selector is
  **`ALC_SOFT_output_mode`**: pass `ALC_OUTPUT_MODE_SOFT` in the attribute list of
  `alcResetDeviceSOFT`. The full spec value set is `ALC_ANY_SOFT`, `ALC_MONO_SOFT`,
  `ALC_STEREO_SOFT`, `ALC_STEREO_HRTF_SOFT`, `ALC_QUAD_SOFT`, `ALC_5POINT1_SOFT`,
  `ALC_6POINT1_SOFT`, `ALC_7POINT1_SOFT` — but **AX8 maps only the five it needs**
  (`ANY`, `MONO`, `STEREO`, `5POINT1`, `7POINT1`; see the §3 table). `QUAD`,
  `6POINT1`, and `STEREO_HRTF` are intentionally **unmapped** (no enum value / no
  table row — HRTF goes through the separate `ALC_HRTF_SOFT` attribute, §3).
  **Implementer: confirm the exact token spellings against the installed
  `AL/alext.h`** (system vs. vendored OpenAL Soft) before use — the names above are
  from the published `SOFT_output_mode` spec (§13).

**Project has no `docs/subsystems.md`** — the `subsystem` lane map fell back to
CLAUDE.md. Not blocking; noted for the audit trail.

---

## 1. Goals & non-goals

**Goals**

- Ship six independently-committable audio features, each with its own tests,
  matching the Phase 10 audio cadence (one slice = one commit + tests).
- Stay inside the **60 FPS hard floor**. Audio runs on the main thread today
  (audio_system.cpp), so every per-frame cost added here is a main-thread CPU
  cost and is budgeted in §10. No feature here may push the audio update over
  **0.5 ms/frame** at 64 active sources (measured, not assumed).
- Reuse existing seams (`composeAudioSourceAlState`, `updateDucking`,
  `applyHrtfSettings`/`alcResetDeviceSOFT`, the Settings add-pattern) rather than
  add parallel machinery (Rule 3 reuse-before-rewrite).
- Numerical DSP (AX6 absorption curve, AX9 K-weighting) authored/validated via
  the **Formula Workbench** where reference data exists (project Rule 6), with a
  CPU↔runtime parity test pinning any dual implementation (Rule 7).

**Non-goals**

- No audio thread / job-system work (that is Phase 10.6 MT2; AX1 waits on it).
- No new convolution / FFT-reverb dependency (AX2/AX3 bundle).
- No editor visualiser (AX12).
- AX9 does **not** attempt true post-mix master-bus metering (impossible via
  OpenAL's normal playback path — §8); it normalises at clip-decode time.

---

## 2. Slice plan & implementation order

Ordered cheapest / most-independent first so each lands green before the next:

| # | Slice | Item | Complexity | One-line |
|---|-------|------|-----------|----------|
| 1 | A-OUT | **AX8** Surround output | S | Expose `ALC_SOFT_output_mode` as a Settings enum; reuse the HRTF `alcResetDeviceSOFT` path. |
| 2 | A-AIR | **AX6** Air absorption | M | Per-source HF low-pass from `(distance, T, humidity)` ≈ ISO 9613-1. **Introduces** the per-source EFX low-pass path (none exists today) + threads listener pos + weather into the compose seam; also wires up the currently-dead occlusion low-pass. |
| 3 | A-LOD | **AX5** Audio LOD ladder | S/M | Pure tier function `(distance, occlusion, priority) → {Full, CheapSpatial, Drop2D, Mute}` with hysteresis, applied in the compose stage. |
| 4 | A-DUCK | **AX13** Side-chain ducking | M | Generalise the single global duck to N bus→bus routes from `assets/audio/mix_graph.json`; current dialogue duck becomes the default route. |
| 5 | A-HOT | **AX11** Device hot-swap | M | `ALC_SOFT_system_events` callback → atomic flag → main-thread `alcReopenDeviceSOFT`; re-evaluate HRTF Auto. |
| 6 | A-LUFS | **AX9** Loudness normalisation | M | Per-clip integrated LUFS (libebur128, MIT) measured at decode → per-clip makeup gain → master target trim. |

Order rationale: 1–3 touch only the compose/reset seams and are pure-function-
heavy (cheap to test); 4 reworks one update stage; 5 introduces threading; 6
introduces a dependency + decode-time work. Slices 1–3 carry almost no
regression risk; 4–6 each carry one focused risk called out in their section.

---

## 3. AX8 — Surround output (5.1 / 7.1) [slice A-OUT]

**User value:** players on 5.1/7.1 speaker rigs get true multichannel output
instead of a stereo downmix; headphone users keep HRTF.

**Design.** Add one Settings enum and route it through the **existing**
`alcResetDeviceSOFT` reset path that HRTF already uses (audio_engine.h:445). No
new device-open path.

```cpp
// engine/audio/audio_output_mode.h  (new, ~40 LOC, pure enum + mapping)
// Mono is included (Q1 resolved — §12): one enum value, real accessibility value
// for single-sided-hearing users.
enum class AudioOutputLayout { Auto, Mono, Stereo, Surround51, Surround71 };

// Returns the ALC_OUTPUT_MODE_SOFT value for the chosen layout.
// `hrtfEnabledSetting` is the PERSISTED bool (AudioSettings::hrtfEnabled), NOT
// the live driver status — the decision is keyed on the user's setting so it is
// deterministic at apply time (the live status is only known after a reset).
ALCenum resolveOutputMode(AudioOutputLayout layout, bool hrtfEnabledSetting);
```

Precedence rule (the load-bearing interaction, defined against the **persisted
setting** to avoid the runtime-status ambiguity): HRTF is a *separate* ALC
attribute (`ALC_HRTF_SOFT`), and `AudioSettings::hrtfEnabled` is a bool that
`applyAudioHrtf` (declared settings_apply.h:280, defined in settings_apply.cpp)
maps to `HrtfMode::Auto` (true) / `Disabled` (false) — the bool→mode rule is
documented at settings_apply.h:268–271; `Auto` lets the driver pick stereo-HRTF on
headphones.
Therefore:
- **`hrtfEnabled == true`** (Auto/Forced): **do NOT request a surround output
  mode** — let the HRTF attribute + driver resolve to stereo-HRTF.
  `resolveOutputMode` returns `ALC_ANY_SOFT` (driver decides). The UI greys the
  layout dropdown with a "HRTF forces stereo headphone output" hint.
- **`hrtfEnabled == false`** (Disabled): the layout drives the output mode per the
  table below.

| `hrtfEnabled` | `outputLayout` | `ALC_OUTPUT_MODE_SOFT` value | UI |
|---------------|----------------|------------------------------|-----|
| true (Auto/Forced) | *(any — ignored)* | `ALC_ANY_SOFT` (HRTF via the separate `ALC_HRTF_SOFT` attr) | layout dropdown greyed, "HRTF forces stereo headphone output" hint |
| false | `Auto` | `ALC_ANY_SOFT` | dropdown active |
| false | `Mono` | `ALC_MONO_SOFT` | |
| false | `Stereo` | `ALC_STEREO_SOFT` | |
| false | `Surround51` | `ALC_5POINT1_SOFT` | |
| false | `Surround71` | `ALC_7POINT1_SOFT` | |

This keys the decision on the persisted bool (known at apply time), not on
`getHrtfStatus()` (only known *after* the reset) — so the attribute list is built
deterministically before the single `alcResetDeviceSOFT` call. Note `resolveOutputMode`
**never returns `ALC_STEREO_HRTF_SOFT`**: HRTF is applied through the separate
`ALC_HRTF_SOFT` attribute (the existing path), so HRTF is never double-specified.

**Settings.** Add `AudioOutputLayout outputLayout = Auto;` to `AudioSettings`
beside the existing `hrtfEnabled` scalar (settings.h:122; the struct spans
111–126). JSON key `"outputLayout"` as a string enum. `validate()` falls
back to `Auto` on an unknown token, mirroring the existing `localization.language`
/ `uiScalePreset` unknown-token fallbacks inside `validate()` (settings.cpp). Bump `schemaVersion`; migration defaults the
new field to `Auto` for old files (no behavioural change → existing users keep
their current downmix).

**Apply path.** The HRTF reset is **not** in `AudioHrtfApplySink` — that is a pure
abstract interface with one method `setHrtfMode(HrtfMode)` (settings_apply.h:272–277).
The actual `alcResetDeviceSOFT` lives in **`AudioEngine::applyHrtfSettings()`**
(private, audio_engine.h:445), reached through the production sink
**`AudioEngineHrtfApplySink`** (settings_apply.h:286) → **`AudioEngine::setHrtfMode`**
(audio_engine.h:374) → `applyHrtfSettings()`. AX8 mirrors that exact shape: add
`AudioEngine::setOutputLayout(AudioOutputLayout)` storing the layout and calling
the **same** private reset, plus a sibling `AudioOutputApplySink` /
`AudioEngineOutputApplySink` pair wired into `ApplyTargets` (settings_editor.h:56–67).
`applyHrtfSettings()` is refactored so HRTF **and** layout build **one** attribute
list and issue a **single** `alcResetDeviceSOFT(device, attrs)` per change (no
double reset). (Keeping the existing method name avoids a rename cascade to §7's
post-swap re-apply; the implementer may rename later if desired, but the doc cites
`applyHrtfSettings()` throughout.)

**Capability probing.** Before offering 5.1/7.1, probe `alcIsExtensionPresent(
device, "ALC_SOFT_output_mode")`; if absent, the dropdown shows only Auto/Stereo
and logs a one-line capability notice. A requested layout the device can't honour
falls back to `Auto` (OpenAL Soft picks the nearest) — surfaced as a toast, not a
hard error.

**CPU/GPU:** CPU, trivial — a settings-change-time `alcResetDeviceSOFT`, not
per-frame. **Perf:** zero per-frame cost.

**Accessibility:** mono-output users (a real accessibility config for
single-sided hearing) are served by adding **`AudioOutputLayout::Mono` →
`ALC_MONO_SOFT`**? — *open decision, §12-Q1*: include Mono now or defer. Default
recommendation: include Mono (one enum value, `ALC_MONO_SOFT` exists, real
accessibility value).

**Tests** (`tests/test_audio_output_mode.cpp`): pure-function table over
`resolveOutputMode(layout, hrtf)` for all 4×2 (or 5×2 with Mono) combinations,
asserting the precedence rule (HRTF wins). Settings round-trip + unknown-token
fallback + migration default in `test_settings.cpp`. No device needed.

---

## 4. AX6 — Air absorption + atmospheric filtering [slice A-AIR]

**User value:** distant outdoor sounds lose their high frequencies (a far-off
horn is dull, not just quiet) — large perceived-realism gain for open scenes.

**Design.** A per-source low-pass whose HF attenuation is a function of
`(distance, temperature, humidity)` approximating ISO 9613-1 atmospheric
absorption. **Prerequisite — there is no EFX low-pass wired today.** The engine
applies occlusion as a *gain* multiplier only (`computeObstructionGain` → the gain
input of `resolveSourceGain`, audio_source_state.h:67–71); `computeObstructionLowPass`
is **computed but never sent to OpenAL**, there is **no `alFilter*` /
`AL_LOWPASS_GAINHF` call in `engine/`**, and `AudioSourceAlState` has **no HF
field**. So AX6's first job is to **introduce** the per-source EFX low-pass path:

1. Add EFX filter wiring in `AudioEngine` (`alGenFilters`, `AL_FILTER_LOWPASS`,
   `alFilterf(f, AL_LOWPASS_GAINHF, …)`, `alSourcei(src, AL_DIRECT_FILTER, f)`),
   guarded by an `ALC_EXT_EFX` presence probe — silent no-op if absent
   (degrades to gain-only, matching today).
2. Add a `float lowPassGainHf = 1.0f;` field to `AudioSourceAlState` and push it in
   `applySourceState`.
3. Route **both** the existing (currently-dead) occlusion low-pass **and** the new
   air-absorption term into that single `AL_LOWPASS_GAINHF`, combined
   multiplicatively in the linear HF-gain domain.

This closes the latent "occlusion low-pass is dead computed data" gap as a
deliberate in-scope side-effect (see §12-Q6 — surfaced as a code observation, not
assumed). It also raises AX6 from a trivial add to **Medium** complexity (§2).

```cpp
// engine/audio/audio_air_absorption.h  (new, pure functions)
struct AirAbsorptionParams {
    float humidity01   = 0.5f;   // from EnvironmentForces::getHumidity
    float temperatureC = 20.0f;  // from EnvironmentForces::getTemperature
    bool  enabled      = true;
};
// Returns a linear HF-gain multiplier in [0,1] for a source at `distanceMeters`.
// 1.0 = no HF loss (near); → 0 as distance grows. Monotonically non-increasing in distance.
float airAbsorptionHfGain(float distanceMeters, const AirAbsorptionParams& p);
```

**The curve.** ISO 9613-1 gives a frequency-dependent absorption coefficient
`α(f, T, h, p)` in dB/m. The pressure term `p` is **intentionally held at the ISO
reference (sea level, 101.325 kPa) and excluded from `AirAbsorptionParams`** — its
effect on `α` is small over playable altitude ranges and it would add a third fit
axis for negligible perceptual gain (this is why `WeatherState::airDensity` exists
but AX6 leaves it unused). A full per-band filter is also overkill for the
quick-wins tier; we collapse `α` to a single representative HF anchor (≈4 kHz, the
band the ear most reads as "air") and convert `α·distance` dB to a linear gain. The
`α(T,h)` surface at 4 kHz is smooth and bounded over the standard validity range
(T ∈ [-20,50] °C, h ∈ [10,100] %) → **a Formula Workbench fit** (project Rule 6):
author the ISO reference points, fit a cheap closed form (target: ≤0.5 dB abs
error over the range), export coefficients, and pin the runtime curve to the
Workbench CPU reference with a parity test (Rule 7). Until the fit ships, a
documented placeholder constant carries a `TODO: revisit via Formula Workbench`
comment (Rule 6).

**Distance source — requires a signature change (not free).** `distanceMeters` =
listener↔source distance, but `composeAudioSourceAlState(comp, entityPosition,
mixer, duckingGain)` (audio_source_state.h:75–79) has **only the source position —
no listener position and no `EnvironmentForces` handle.** AX6 must thread both
into the compose seam: add a `listenerPosition` parameter and a per-frame
`AirAbsorptionParams` (a weather snapshot the caller fills once per frame from
`EnvironmentForces::getTemperature/getHumidity` — snapshot, not a per-source query,
since weather is global today). This is a real change to the compose signature and
its call sites (audio_system.cpp:147,164), called out so the implementer budgets it.

**Composition order** (in `composeAudioSourceAlState`): `lowPassGainHf =
occlusionLowPassHf × airAbsorptionHfGain`, where `occlusionLowPassHf` is the
**newly-introduced** HF term from `computeObstructionLowPass` (today it is 1.0 in
effect because nothing applies it). Indoor sources (occlusion heavy) are dominated
by the occlusion term; the air term matters for clear-line-of-sight outdoor
distance, which is exactly its intent. A per-source `bool spatial` gate: 2D /
non-spatial sources skip air absorption (distance is meaningless for them).

**CPU/GPU:** CPU. Per active spatial source, per frame: one curve eval + one
multiply. **Perf budget:** ≤ a few ns/source; negligible at 64 sources. The
`EnvironmentForces` query is the only non-trivial cost — cache the weather snapshot
once per frame (it's global today) rather than per-source.

**Accessibility:** add to the existing audio-accessibility surface a master
`airAbsorptionEnabled` (default on); off restores today's gain-only attenuation
for users who find the HF rolloff muffling. Tie nothing to motion.

**Tests** (`tests/test_audio_air_absorption.cpp`): monotonic non-increasing in
distance; gain=1 at distance 0; bounded [0,1]; humidity/temperature endpoints;
parity against the Workbench CPU reference (`EXPECT_NEAR`, `kEps`). Compose-stage
test: air × occlusion combine multiplicatively; 2D sources unaffected.

---

## 5. AX5 — Audio LOD ladder [slice A-LOD]

**User value:** dense-source scenes (markets, crowds, battle) stay at 60 FPS —
distant/occluded low-priority sources stop paying for full spatialisation.

**Design.** A pure decision function picks a tier per source per frame; the
compose stage applies the tier. No new state on the component beyond the existing
`priority`, `minDistance/maxDistance`, `occlusionFraction`.

```cpp
// engine/audio/audio_lod.h  (new, pure)
enum class AudioLodTier { Full, CheapSpatial, Drop2D, Mute };
struct AudioLodConfig {
    float cheapDistanceFactor = 0.6f;  // × maxDistance → drop HRTF/occlusion
    float drop2DFactor        = 0.85f; // × maxDistance → panning only, no occlusion
    float muteFactor          = 1.0f;  // ≥ maxDistance → silent
    float hysteresis          = 0.05f; // × maxDistance dead-band to stop tier flapping
    bool  enabled             = true;
};
AudioLodTier audioLodTier(float distance, float maxDistance, float occlusionFraction,
                          SoundPriority priority, AudioLodTier previousTier,
                          const AudioLodConfig& cfg);
```

**Tier effects** (applied in compose). Note HRTF is a **device-global** ALC
attribute — there is no per-source HRTF toggle (audio_source_state.h has no HRTF
field), so a tier cannot "turn HRTF off for one source." Tiers act only on the
per-source work that *is* controllable: the EFX low-pass (occlusion + air
absorption, the AX6 path) and 3D-vs-2D positioning.
- `Full` — current behaviour: 3D positioned, full attenuation, and the per-source
  EFX low-pass (occlusion + air absorption) applied.
- `CheapSpatial` — keep 3D panning + distance gain; **skip the per-source EFX
  low-pass** (no occlusion/air-absorption filter update for this source). HRTF, if
  globally on, still applies (it's device-wide) — the saving is the skipped
  per-source filter math, not HRTF.
- `Drop2D` — collapse to 2D (`spatial=false` semantics) at attenuated gain.
- `Mute` — gain 0. Whether the AL source is **kept alive** (cheap re-promotion, no
  re-acquire churn) or **released** is decided **in the apply layer**, which knows
  live pool occupancy — *not* inside the pure `audioLodTier` function (which has no
  pool input by design). Default: keep alive; release only under 32-pool pressure
  (§12-Q2).

**Priority interaction:** `SoundPriority::Critical` (dialogue, scripted) never
drops below `CheapSpatial` regardless of distance; low-priority ambient one-shots
take the full ladder. Hysteresis dead-band prevents audible tier flapping when a
source hovers at a boundary.

**CPU/GPU:** CPU, pure function per source per frame; the *point* is to *reduce*
downstream CPU (skipped occlusion/HRTF work on demoted sources). **Perf:** net
negative cost on dense scenes (the win); a few comparisons per source otherwise.

**Accessibility:** no direct surface. Must not demote `Ui` bus or dialogue
(`Critical`) — those carry accessibility cues. Encoded in the priority floor.

**Tests** (`tests/test_audio_lod.cpp`): tier boundaries at each factor; hysteresis
(a source crossing a boundary by < dead-band keeps its previous tier);
Critical-priority floor; monotonic demotion with distance. Compose-stage test:
CheapSpatial skips occlusion; Mute → gain 0; source not stopped.

---

## 6. AX13 — Side-chain ducking from arbitrary bus [slice A-DUCK]

**User value:** music dips under dialogue *and* SFX can dip under music stingers,
etc. — proper mix automation instead of one hard-wired dialogue duck.

**Design.** Generalise the single global `DuckingState` (audio_mixer.h:148–168)
into a small router of N **routes**, each a `(sourceBus → targetBus)` pair with
its own params + state. Drive each route's trigger from whether its *source* bus
has audible activity this frame; accumulate, per target bus, the product of all
ducks aimed at it; the compose stage multiplies a source's gain by the duck gain
for *its* bus.

```cpp
// engine/audio/audio_ducking.h  (extends audio_mixer.h ducking types)
struct DuckingRoute {
    AudioBus     source;   // bus whose activity triggers the duck
    AudioBus     target;   // bus that gets attenuated
    DuckingParams params;  // reuse existing struct
};
struct DuckingRouter {
    std::vector<DuckingRoute> routes;
    std::vector<DuckingState> stateByRoute; // sized to routes.size() — one state PER ROUTE
                                            // (NOT AudioBusCount; routes are unbounded)
    // Advances every route; returns per-target-bus duck gain (product of inbound ducks).
    // Indexed by AudioBus (the OUTPUT is per target bus, of which there are AudioBusCount).
    std::array<float, AudioBusCount> advance(const std::array<bool, AudioBusCount>& busActive,
                                             float dt);
};
```

Note the two different dimensions: `stateByRoute` is per **route** (a `vector`,
one `DuckingState` per route); the `advance` **return** is per target **bus** (a
fixed `std::array<float, AudioBusCount>`, the accumulated duck gain each bus
receives). Conflating the two was the original sizing bug.

**Activity detection** (the one new input): a bus is "active" this frame if any
playing source on it exceeds a small gain threshold. Cheap to compute from the
existing per-source iteration in `AudioSystem::update` (we already walk every
source) — accumulate a `busActive[bus]` bitset in the same loop, no extra walk.

**Which gain site AX13 owns.** Today the duck reaches gains via **two sites**:
published to the engine via `setDuckingSnapshot()` (audio_system.cpp:64–65) and
folded in by `updateGains()` (:76) for fire-and-forget sources, and also passed as
the `duckingGain` arg into `composeAudioSourceAlState` (:147,164) for tracked
sources. AX13 keeps that two-site structure but replaces the *single scalar* with the router's
**per-target-bus** duck-gain array: `updateGains` and the compose call each look up
the duck gain for the bus they're handling. AX13 owns the computation of that
array (`DuckingRouter::advance`); it does **not** change *where* the gain is applied.

**Config:** `assets/audio/mix_graph.json` declares routes; absent file ⟹ a
**built-in default route set**. **Decision (Q3 resolved, §12): the default route is
driven by the existing manual `.triggered` flag**, not by bus-activity detection.
So the no-config default consumes the *same* trigger input as today, which makes
"reproduces today's behaviour" a well-defined, testable claim (the parity test
below). Bus-activity-driven routes are an *additive* capability available only via
an explicit `mix_graph.json`; they never change the no-config default.

**Migration of current behaviour:** the existing contract is editor/gameplay code
flipping `Engine::getDuckingState().triggered` (a bool — **there is no
`triggerDuck()` method**; §0). That manual flag becomes the `Voice→Music` /
`Voice→Ambient` default route fed by Voice-bus activity instead of the manual
flag — *or* (simpler, §12-Q3) keep the manual `.triggered` flag as an additional
always-on route and only *add* bus-activity routes. Recommendation: preserve the
manual `.triggered` contract (a route whose `source` is a synthetic "manual"
trigger driven by the existing `getDuckingState().triggered` bool) so existing
callers keep working; add data-driven bus routes alongside.

**CPU/GPU:** CPU. Per frame: ≤ `routes.size()` state advances + one bus-activity
bitset (folded into the existing source walk). **Perf:** routes are few (single
digits); negligible.

**Legal route targets.** `Ui` is a **rejected** target (accessibility cues must
not be ducked) and `Master` is a **rejected** target (ducking the global bus would
attenuate everything, including `Ui`, defeating the per-bus design). `mix_graph.json`
routes naming `Ui` or `Master` as `target` are rejected at parse time with a
`Logger::warning`. Legal targets: `Music, Voice, Sfx, Ambient`.

**Accessibility:** ducking that hides Ui/accessibility cues would be harmful — see
the rejected-target rule above. Documented invariant + test.

**Risk (called out):** this reworks the stage that touches *every* source's gain.
Mitigation: the no-config default (manual `.triggered` route — Q3 resolved)
reproduces the current behaviour, pinned by a regression test that runs the old
dialogue-duck scenario through the new router and asserts identical `currentGain`
slew. Because the default consumes the same trigger input as today, the parity test
is fully specified.

**Tests** (`tests/test_audio_ducking.cpp`): default-route parity with today's
`updateDucking` slew (attack/release/duckFactor); multi-route product accumulation;
a bus targeted by two routes ducks by the product; Ui never targeted; mix_graph.json
parse + unknown-bus rejection.

---

## 7. AX11 — Audio device hot-swap [slice A-HOT]

**User value:** plug in USB headphones mid-session → engine switches output without
a restart and re-enables HRTF automatically.

**Design.** Register an `ALC_SOFT_system_events` callback at `AudioEngine::initialize`
(right after the existing `alcGetProcAddress` extension-pointer loads). The
callback fires on **OpenAL's own event thread** — so it does the *minimum*: set a
`std::atomic<bool> m_deviceChangePending{true}` and stash the new default-device
name under a small mutex. The **main-thread** `AudioSystem::update` calls
`AudioEngine::pollAndHandleDeviceChange()` at the top of the frame; if the flag is
set it performs the actual swap on the main thread via
`alcReopenDeviceSOFT(device, newDeviceName, attrs)` — which keeps the context,
sources, and buffers alive (no re-acquire).

```cpp
// In AudioEngine (engine/audio/audio_engine.h)
// New extension-pointer slots. No load site exists today — AX11 adds these
// alcGetProcAddress loads in initialize() beside the two existing ones
// (m_alcResetDeviceSOFT / m_alcGetStringiSOFT, audio_engine.h:439–440).
void* m_alcReopenDeviceSOFT = nullptr;
void* m_alcEventControlSOFT = nullptr;
void* m_alcEventCallbackSOFT = nullptr;
std::atomic<bool> m_deviceChangePending{false};
std::mutex m_deviceChangeMutex;            // guards m_pendingDeviceName (event thread ↔ main)
std::string m_pendingDeviceName;
bool pollAndHandleDeviceChange();          // main-thread; returns true if a swap occurred
```

**Thread-safety (load-bearing):** the only cross-thread state is the atomic flag
and the mutex-guarded device-name string. The callback never touches OpenAL
objects, never calls back into the engine, never allocates beyond the std::string
assignment. All ALC mutation (`alcReopenDeviceSOFT`, `alcResetDeviceSOFT`) happens
on the main thread. This is the OpenAL-recommended pattern (the event callback is
explicitly documented as running on an internal thread).

**Policy:** a Settings toggle `deviceHotSwap { Off, Notify, Auto }` (default
`Notify`): `Auto` swaps silently; `Notify` raises a toast "Default device changed
(USB headphones) — switch?" and swaps on confirm; `Off` ignores. After a swap,
re-run `applyHrtfSettings()` so HRTF Auto re-detects headphones (the Phase 10 HRTF
Auto path already exists — audio_engine.h:374/445).

**CPU/GPU:** CPU, event-driven. Per-frame cost is one relaxed atomic load
(effectively free). The swap itself is rare and main-thread.

**Capability probe — pre-implementation gate.** AX11 rests on `ALC_SOFT_system_events`
+ `ALC_SOFT_reopen_device`, which are **not referenced anywhere in the code today**.
Before implementing, confirm both are present in the installed/vendored OpenAL Soft
`AL/alext.h` (`alcEventIsSupportedSOFT` / extension-present check at runtime). If
absent (older OpenAL Soft), the feature is silently disabled and logged once — but
the build-time presence of the tokens is a precondition for the slice, not a
footnote.

**Accessibility:** auto-enabling HRTF on headphone connect is itself an
accessibility win (spatial cues for HL players). `Notify` default avoids yanking
output from under a user mid-action.

**Risk:** a device reopen can transiently glitch audio. Acceptable (it's a
device change). Test the swap *decision* logic headlessly; the actual
`alcReopenDeviceSOFT` call is gated by `isAvailable()` and exercised manually.

**Tests** (`tests/test_audio_device_swap.cpp`): the poll state machine
(pending→handled), the `{Off,Notify,Auto}` policy decision table, name-stash
thread-safety contract (single-threaded simulation of "callback sets flag, main
clears it"), HRTF re-eval requested after swap. No device needed (the ALC calls
are behind `isAvailable()`).

---

## 8. AX9 — Loudness normalisation (EBU R128 / ITU-R BS.1770) [slice A-LUFS]

**User value:** the engine doesn't blow the user's eardrums after a quiet
podcast / between scenes — consistent perceived loudness (the Twitch/YouTube
complaint class).

**The constraint that reshapes this item (verified §0):** OpenAL Soft does **not**
expose post-mix master-bus PCM during normal hardware playback. The ROADMAP's
"measure integrated LUFS *across the master bus* and auto-trim master" is therefore
**not achievable at runtime** without an offline loopback render (`ALC_SOFT_loopback`,
which replaces hardware output — wrong for live play). Rather than build a
loopback metering rig (that would blow the "quick-win" scope), AX9 normalises at
the **clip-decode boundary**, which is correct, testable, and dependency-light.

**Design.** When a clip is decoded (the engine already decodes via the vendored
`dr_libs` — dr_wav/dr_mp3/dr_flac — plus stb_vorbis for OGG), measure its **integrated loudness (LUFS)** over
the decoded PCM with **libebur128** (MIT — §13), and store a per-clip *makeup gain*
that brings it to a reference loudness. The makeup gain multiplies into the source
gain in the existing `resolveSourceGain` composition (a new optional factor). A
global `loudnessTargetLufs` setting sets the reference. **The shipped default is
−16 LUFS** (the modern game-loudness norm — what players expect); **−23 LUFS**
(EBU R128 broadcast / streamer-friendly) is a selectable preset. *Note: this
intentionally **inverts the ROADMAP AX9 bullet's** "default −23 / −16 preset" — for
a game the player-facing default should be the game-loudness norm (−16), with −23
the opt-in for streamers. The ROADMAP bullet is reconciled when AX9 ships.* Sound-designer
per-source `volume` still applies on top, so relative mix intent is preserved.

```cpp
// engine/audio/audio_loudness.h  (new; thin wrapper over libebur128 + a pure makeup-gain fn)
// AudioClip stores signed-16-bit interleaved PCM (audio_clip.h:30,53), so AX9
// converts int16→float (÷32768) before feeding libebur128, which wants float.
float integratedLoudnessLufs(const int16_t* interleaved, size_t frames, int channels, int rate);
// Pure: makeup gain (linear) to move `measuredLufs` to `targetLufs`, clamped to a max boost.
float loudnessMakeupGain(float measuredLufs, float targetLufs, float maxBoostDb = 12.0f);
```

**Where it runs.** The decoded PCM is available at exactly one point: inside
`AudioEngine::loadBuffer` (function starts audio_engine.cpp:210). After the
cache-miss path, the `AudioClip` returned by `AudioClip::loadFromFile` (:245) is
live and exposes `getSamples()` (int16 interleaved), `getSampleRate()`,
`getChannels()`, `getFrameCount()` (audio_clip.h:30–40); it is uploaded via
`alBufferData` (:258) and goes out of scope after the buffer ID is cached (:282) —
only the AL buffer ID is kept (the LRU `m_bufferCache` is path→buffer-ID, not PCM).
So AX9 measures **on that live clip after the successful decode + upload
(audio_engine.cpp:245–270, i.e. past the `alGetError` upload guard) and before the
function returns**: compute integrated LUFS on
`clip->getSamples()` and store the resulting makeup gain in a small **path→gain**
map (parallel to the buffer-ID cache; one float per clip retained, not the PCM).
Measured **once per clip**, zero per-frame cost. Silent/near-silent clips
(integrated LUFS below the −70 LUFS absolute gate) get unity makeup (no boost of
the noise floor). This needs **no new decode path** — `AudioClip` already exposes
everything required (Q5 resolved, §12).

**Dependency:** `libebur128` via FetchContent (MIT). Per project Rule 8, the new
dep gets its own **cold-eyes review** and a `THIRD_PARTY_NOTICES.md` row. *Simpler
alternative considered & rejected for now:* self-implement BS.1770 K-weighting +
gating (~150 LOC, the K-weighting biquad is a Formula Workbench candidate). Rejected
because libebur128 is battle-tested, MIT, and the gating logic is fiddly to get
right; revisit only if the dep proves problematic. (§12-Q4 flags this as the one
genuine "new dependency vs. self-implement" decision in the bundle.)

**CPU/GPU:** CPU, at load time (off the frame path). The K-weighting + gating is
O(samples) once per clip.

**Accessibility:** loudness normalisation *is* an accessibility feature
(consistent levels help users who can't ride a volume knob). Expose
`loudnessTargetLufs` + an on/off in audio settings; default **on at −16 LUFS**.

**Tests** (`tests/test_audio_loudness.cpp`): `loudnessMakeupGain` pure-function
(target − measured = dB delta → linear; max-boost clamp; below-gate → unity); a
known-loudness synthetic signal (e.g. −23 LUFS 1 kHz sine block) measured through
`integratedLoudnessLufs` within tolerance; cache stores one measurement per clip.

---

## 9. Cross-cutting concerns

**Settings schema.** Three slices add fields to `AudioSettings`: `outputLayout`
(AX8), `airAbsorptionEnabled` (AX6), `deviceHotSwap` (AX11), `lodEnabled`+config
(AX5), `loudnessEnabled`+`loudnessTargetLufs` (AX9). One **single `schemaVersion`
bump** covers the whole bundle (not five). Migration defaults every new field to
its current-behaviour value (layout Auto / air-absorption on / hot-swap Notify / LOD on / loudness on at −16 LUFS) so existing settings
files are unchanged in effect. `validate()` clamps each (Rule: unknown enum token
→ documented fallback + `Logger::warning`, matching the unknown-token fallbacks in `validate()`, settings.cpp).

**Threading.** Only AX11 introduces cross-thread state (the event callback). The
contract: callback = atomic flag + mutex-guarded string only; all ALC mutation on
main thread. Nothing else in the bundle is concurrent (audio is main-thread).

**Dependencies.** Only AX9 adds one (`libebur128`, MIT, FetchContent) — cold-eyes
+ THIRD_PARTY_NOTICES row per Rule 8. AX8/AX11 use OpenAL Soft extensions already
shipped with the vendored/system OpenAL Soft (probe at runtime; degrade if absent).

**CPU/GPU placement summary (Rule 7).** Every item here is **CPU** and that is
correct: audio mixing/spatialisation in this engine is CPU + OpenAL, there is no
GPU audio path, and none of these are per-pixel/per-vertex/per-froxel work. AX6's
absorption curve and AX9's K-weighting are the only "numeric design" pieces and
are CPU pure-functions pinned by parity tests (no GPU mirror needed — there is no
GPU consumer).

---

## 10. Performance budget (60 FPS hard floor)

Audio update is main-thread (audio_system.cpp). Note two distinct counts: the AL
**source pool caps concurrent playback at 32** (audio_engine.h), but the per-frame
compose/LOD loop iterates **every `AudioSourceComponent` in the active scene**
(`scene->forEachEntity`, audio_system.cpp), which can exceed 32 — that iteration is
the per-component CPU cost AX5 reduces. Target: the whole audio update stays
**< 0.5 ms/frame at 64 in-scene `AudioSourceComponent`s** (concurrent AL playback
still capped at 32 by the pool; the surplus is LOD-demoted). It is well under that
today.

| Item | Per-frame cost | Notes |
|------|----------------|-------|
| AX8 | 0 | settings-change-time reset only |
| AX6 | ~1 curve eval + 1 mul / spatial source | weather snapshot cached once/frame |
| AX5 | a few comparisons / source | **net negative** — saves occlusion/HRTF work on demoted sources |
| AX13 | ≤ routes (single digits) advances + 1 bitset | bitset folded into existing source walk |
| AX11 | 1 relaxed atomic load | swap is rare + main-thread |
| AX9 | 0 | decode-time, off frame path |

Net: AX5 is expected to *reduce* worst-case audio CPU on dense scenes; the rest
add only nanoseconds/source. A Release-gated benchmark test
(`test_audio_benchmark.cpp`) asserts the audio update with 64 in-scene
`AudioSourceComponent`s stays under the 0.5 ms budget. Audio is pure CPU (no GL),
so the benchmark runs identically on any machine — no renderer-dependent skip
needed.

---

## 11. Accessibility summary

- AX8: include `Mono` output for single-sided-hearing users (§12-Q1).
- AX5: never demote `Ui` bus or `Critical` priority (accessibility cues + dialogue).
- AX13: default routes never target `Ui`.
- AX11: `Notify` default (don't yank output mid-action); auto-HRTF-on-headphones
  aids spatial-cue-dependent players.
- AX9: on by default at −16 LUFS — consistent levels are themselves an
  accessibility aid.
- AX6: master `airAbsorptionEnabled` toggle for users who find HF rolloff muffling.

None of these six couple to motion, so the photosensitive/reduce-motion surface is
untouched.

---

## 12. Resolved decisions

All blocking decisions are resolved in-doc so the spec is implementable without a
follow-up gate. Two are tuning defaults (Q2, Q4) stated explicitly; one (Q6) is a
maintainer confirmation about pre-existing code intent, not a blocker.

- **Q1 (AX8) — RESOLVED: include `Mono`.** One enum value, real accessibility value
  for single-sided-hearing users. `Mono` is in the `AudioOutputLayout` enum (§3) and
  maps to `ALC_MONO_SOFT`.
- **Q2 (AX5) — RESOLVED: `Mute` keeps the AL source alive** at gain 0 (cheap
  re-promotion, no re-acquire churn), **except** when the 32-source pool is under
  pressure — then a muted source is released to free a slot. **This release decision
  lives in the apply layer** (which reads live pool occupancy), keeping the pure
  `audioLodTier(...)` function pool-agnostic — its signature (§5) deliberately takes
  no pool input. One rule, one place; the pure tier function stays testable.
- **Q3 (AX13) — RESOLVED: preserve the manual `Engine::getDuckingState().triggered`
  flag** as the no-config default route's trigger; bus-activity routes are additive
  via `mix_graph.json` only (§6). This keeps existing callers working and makes the
  back-compat parity test fully specified.
- **Q4 (AX9) — RESOLVED: use `libebur128`** (MIT, correct, fiddly to hand-roll).
  Per Rule 8 the dependency add gets its own cold-eyes review + a
  `THIRD_PARTY_NOTICES.md` row when it lands.
- **Q5 (AX9) — RESOLVED: precondition met, no new plumbing.** `AudioClip`
  (audio_clip.h:30–40) already exposes interleaved int16 PCM + sample rate +
  channels; AX9 measures inside `AudioEngine::loadBuffer` on the freshly-decoded
  clip before it drops (§8). No decode-path change needed.
- **Q6 (AX6) — maintainer confirmation (code intent, not a blocker):**
  `computeObstructionLowPass` (within the audio_occlusion.h:95–104 block) is computed
  but never applied — its doc-comment (audio_occlusion.h:99–104) describes it feeding
  "the EFX low-pass filter cutoff calculation **when the engine-side EFX slot is
  connected**," i.e. the live path was *designed but not yet wired*. AX6 completes that wiring. The maintainer should confirm this reading
  (completing unfinished work, not changing designed behaviour) when AX6 lands. This
  is a docs-review observation about code; no code was changed here.

---

## 13. References

- **OpenAL Soft — `ALC_SOFT_output_mode`** (channel-layout selection via
  `alcResetDeviceSOFT`): https://openal-soft.org/openal-extensions/SOFT_output_mode.txt
- **OpenAL Soft — `ALC_SOFT_system_events`** (device-change notifications:
  `ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT`, `alcEventControlSOFT`,
  `alcEventCallbackSOFT`, `alcEventIsSupportedSOFT`). Spec + headers in the
  authoritative repo: https://github.com/kcat/openal-soft (see
  `alc/alc.cpp` + `include/AL/alext.h`; extension spec under the
  `openal-soft.org/openal-extensions/` index). Confirm exact tokens against the
  installed `AL/alext.h`.
- **OpenAL Soft — `ALC_SOFT_reopen_device`** (`alcReopenDeviceSOFT` — swap the
  output device while keeping the context + sources): https://github.com/kcat/openal-soft
- **ISO 9613-1:1993** — Acoustics — Attenuation of sound during propagation
  outdoors, Part 1: atmospheric absorption coefficient `α(f, T, h, p)`. NPL
  technical guide: http://resource.npl.co.uk/acoustics/techguides/absorption/
- **EBU R 128** loudness normalisation (−23 LUFS target):
  https://tech.ebu.ch/docs/r/r128.pdf ; **ITU-R BS.1770** measurement algorithm
  (K-weighting + gating).
- **libebur128** (MIT, reference EBU R128 implementation):
  https://github.com/jiixyj/libebur128

---

## 14. Cold-eyes loop log

Fresh subagent per loop, no authoring context; loop until zero verified findings,
then sign off if only verified polish remains (session standing instruction).

- **Loop 1 (2026-06-29)** — 3 reviewers (lanes: device/output, compose-stage DSP,
  ducking/loudness+cross-cutting). Verified findings fixed across §0/§2/§3/§4/§6/
  §8/§10/§12/§13: several CRITICAL/HIGH accuracy + structural corrections (not
  polish) — so this loop does **not** sign off. A few reviewer claims were
  dismissed as unverified (audio_panel path exists; ALC short-token spellings match
  the spec; `SoundPriority::Critical` exists). Re-review pending (cold).
- **Loop 2 (2026-06-29)** — same 3 lanes, cold. **Zero CRITICAL** ("accuracy is
  excellent — every load-bearing code claim verified TRUE"). Remaining findings were
  internal-consistency + accuracy-refinements (int16-not-float PCM; `AudioClip`
  already exposes the PCM so Q5 is met; 32-pool vs 64-component reconciliation;
  per-source-HRTF infeasibility restated; −16 vs −23 default picked; per-symbol line
  cites; Mono/`ALC_STEREO_HRTF_SOFT`/rename consistency). All fixed; the open
  decisions reviewers flagged as latent gaps (Q1/Q3/Q5) were **resolved in-doc**
  (§12). Re-review pending (cold).
- **Loop 3 (2026-06-29)** — same 3 lanes, cold. **Zero CRITICAL across all lanes;
  lane B zero HIGH.** Findings were polish (cite precision: `loadBuffer` :210 not
  :245; occlusion line-range reconcile; ALC value-list annotation for the 5-of-8
  used; HRTF mapping cite → `applyAudioHrtf`; AX11 load-site note; pressure-term
  exclusion stated) plus one contract-precision item: the resolved Q2 referenced
  pool occupancy the pure `audioLodTier` signature can't read — fixed by locating
  the Mute release decision in the apply layer and keeping the tier function
  pool-agnostic. All fixed. Re-review pending (cold) to confirm convergence.
- **Loop 4 (2026-06-29) — CONVERGED.** Same 3 lanes, cold. **Zero CRITICAL and zero
  HIGH across all lanes.** Lane B independently re-verified the §5 Mute/Q2 contract
  and §4 pressure-exclusion as internally consistent and code-accurate. Remaining
  findings were polish only: cite precision (`MAX_SOURCES` :40; `outputLayout`
  insertion at :122; occlusion comment 99–104; loadBuffer measurement window past
  the upload guard), the OGG/stb_vorbis codec line, an explicit AX11
  extension-presence precondition, and a note that AX9 intentionally inverts the
  ROADMAP's −23/−16 default (ships −16). All fixed. **Signed off** — only verified
  polish remained (session standing instruction).
