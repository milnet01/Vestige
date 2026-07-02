# Phase 10 — Convolution Reverb (AX2) + Acoustic Pre-Bake (AX3) Design Doc

**Status:** SIGNED OFF 2026-07-02 — cold-eyes converged (5 loops, §14).
Implementation starting at AX2 slice R1.
**Author:** in-session 2026-07-02. Follows the AX1 occlusion design
(`phase_10_audio_occlusion_design.md`) and the MT2 job system
(`phase_10_6_design.md`), both shipped.

**Why one doc for two roadmap bullets.** AX2 (runtime convolution reverb) and
AX3 (offline acoustic pre-bake) share one thing that does not exist in the
codebase yet: an **impulse-response (IR) → OpenAL → per-source reverb send**
runtime path. AX2 *builds* that path and feeds it hand-authored / bundled IRs
(plus a parametric fallback). AX3 *fills* that path with IRs it generates by
simulating sound bouncing around the static level geometry at build time. If
they were designed separately, AX3 would be guessing at AX2's IR contract.
Designed together, AX3 targets the exact slot AX2 ships. AX2 is independently
shippable and delivers value before AX3 exists.

> **Plain-English summary.** Today the engine can *describe* room reverb (six
> tunable presets — Cave, Hall, …) but nothing is actually plumbed to the
> speakers: reverb is silent math. AX2 makes reverb audible for the first time
> by handing OpenAL a recorded "sound fingerprint" of a room (an *impulse
> response*) and letting it colour every sound played in that room — so a
> voice in Solomon's Temple gets the temple's real stone-hall tail, not a
> generic preset. On sound drivers too old for that feature, it automatically
> falls back to the six presets. AX3 is the level-build-time tool that
> *generates* those fingerprints by simulating sound bouncing around each
> room, so artists don't have to record or hand-tune them.

---

## Section index

- **§0** — What already exists (reality check)
- **§1** — Goals & non-goals
- **§2** — Approach decision: native convolution + parametric fallback (the fork)
- **§3** — Slice plan & order (AX2 R1–R5, then AX3 B1–B5)
- **§4** — Shared IR + convolution infrastructure
- **§5** — AX2 runtime — `ReverbSystem`
- **§6** — AX3 offline — acoustic pre-bake
- **§7** — Performance (60 FPS hard floor)
- **§8** — Accessibility
- **§9** — CPU / GPU placement
- **§10** — Formula Workbench
- **§11** — Dependencies
- **§12** — References
- **§13** — Resolved decisions
- **§14** — Cold-eyes loop log

---

## 0. What already exists (reality check, verified 2026-07-02)

Grounded against current `main` (HEAD `630b6e9`), verified by direct read of the
cited source.

**Reverb is pure math with no sound attached.**
- `engine/audio/audio_reverb.h` (read in full): `enum class ReverbPreset`
  (`Generic / SmallRoom / LargeHall / Cave / Outdoor / Underwater`, lines
  36–44); `struct ReverbParams` (7 floats mapping to the **standard**
  `AL_REVERB_*` model — `decayTime / density / diffusion / gain / gainHf /
  reflectionsDelay / lateReverbDelay`, lines 49–70); `reverbPresetParams`,
  `reverbPresetLabel`, `computeReverbZoneWeight(coreRadius, falloffBand,
  distance)` (sphere-with-linear-falloff, lines 94–96), `blendReverbParams(a,
  b, t)` (component-wise lerp, lines 104–106).
- The header comment (lines 11–21) *already specifies* the intended
  consumer: "The engine-side ReverbSystem chooses the single highest-weighted
  zone and the next-highest neighbour, then blends their `ReverbParams` … the
  engine-side adapter does the `alEffectf` calls." **That ReverbSystem does
  not exist.** This doc builds it. (Note: the header comment mentions
  `AL_EAXREVERB_*` in one line and `AL_REVERB_*` in another; the fields map to
  the *standard* non-EAX model per lines 46–48 — we use `AL_EFFECT_REVERB`.)
- `computeReverbZoneWeight` is reused today only by the ambient system
  (`audio_ambient.cpp:18`) for its falloff — no reverb runtime consumes it.
 - `engine/editor/panels/audio_panel.{h,cpp}` has an **editor-draft-only**
  `ReverbZoneInstance` (`name` / `center` / `coreRadius` / `falloffBand` /
  `ReverbPreset preset`, `audio_panel.h:54–61`) with add/remove/select UI. Its header comment says
  it is a draft "until the runtime reverb placement surface lands." It is
  **not serialized and not consumed by any system.**
**OpenAL / EFX reverb wiring: none.** Grep across `engine/` for
`alGenAuxiliaryEffectSlots`, `AL_EFFECT_REVERB`, `AL_EFFECT_EAXREVERB`,
`AL_AUXILIARY_SEND_FILTER`, `alGenEffects` → **zero hits**.
**EFX today = one `AL_FILTER_LOWPASS` direct filter (AX6).** Verified
`audio_engine.cpp:139–170`: gated on `alcIsExtensionPresent(device,
"ALC_EXT_EFX")`, loads `alGenFilters/alDeleteFilters/alFilteri/alFilterf` via
`alGetProcAddress`, creates one filter, and drops it gracefully if the driver
refuses (`m_lowPassFilter = 0` → gain-only). Applied per source as
`AL_DIRECT_FILTER` in `applySourceState` (`audio_engine.cpp:1203–1218`).
**There is no aux effect slot and no `AL_AUXILIARY_SEND_FILTER` anywhere** —
AX2 adds the first. This proc-load-then-graceful-fallback block is the exact
template AX2's aux-slot setup mirrors.

**Convolution tokens are absent from the vendored headers.** OpenAL Soft is
pinned at `GIT_TAG 1.25.1` (`external/CMakeLists.txt:373`, LGPL v2.1 per
`THIRD_PARTY_NOTICES.md`, dynamic link). OpenAL Soft is pulled via FetchContent,
so its headers live in the build tree (`build/_deps/openal-soft-src/…`), not
`external/`. Grep of the fetched `include/AL/efx.h` + `include/AL/alext.h` for
`CONVOLUTION` / `SOFT_convolution` → **zero hits**; `efx.h` *does* expose
`AL_EFFECT_REVERB` (0x0001), `AL_EFFECT_EAXREVERB` (0x8000),
`AL_AUXILIARY_SEND_FILTER` (0x20006), and the `AL_REVERB_*` property tokens
(verified in §4.2 below). The convolution effect is a real
but **experimental** OpenAL Soft extension whose token (`0xA000`) lives in the
fetched internal `alc/inprogext.h` (and `examples/alconvolve.c`), **not** in
the shipped public `AL/alext.h` header — see
§2 and §4.2.

**Clip decode + the AX9 loudness boundary.** `AudioEngine::loadBuffer`
(`audio_engine.cpp:350–437`) decodes via `AudioClip::loadFromFile` (dr_wav /
dr_mp3 / dr_flac / stb_vorbis, all → **s16 PCM**), uploads with
`alBufferData`, and measures integrated LUFS once per clip
(`audio_engine.cpp:412–421`). WAV support means an IR `.wav` loads through the
existing decoder — but `loadBuffer` is source-oriented (LRU cache + LUFS), so
IRs get a **dedicated load path** (§4.1).
**Per-source AL state is a pure compose → apply split.**
`composeAudioSourceAlState(comp, entityPos, mixer, ducking, listenerPos, air,
lodTier, loudnessMakeup)` → `struct AudioSourceAlState` (position / gain /
distances / `lowPassGainHf` / `lodTier`, `audio_source_state.h:35–67`,
signature 122–130); `applySourceState` (`audio_engine.cpp:1172–1219`) issues
the `alSource*` calls. This is exactly where a per-source reverb send is added
(§4.3).
**AX1 pattern to mirror.** `AudioOcclusionSystem` (`PostCamera`, registered
**before** `AudioSystem`) populates each source's occlusion fields *before*
`AudioSystem`'s compose loop reads them. `ReverbSystem` uses the same
populate-before-AudioSystem shape.
**Probe / bake infrastructure for AX3: runtime-only, nothing on disk.**
- `SHProbeGrid` (`sh_probe_grid.h`) — GPU SH-irradiance grid with
  `struct SHGridConfig { glm::vec3 worldMin, worldMax; glm::ivec3 resolution; }`;
  captured at runtime (`Renderer::captureSHGrid`), lives only in GPU textures +
  a CPU `std::vector`. `RadiosityBaker` is an in-memory bounce iterator, **not**
  a disk baker. `LightProbe` carries position + influence-AABB + blend-weight.
 - **No probe positions, SH grids, or lightmaps are serialized to scene JSON**
  (grep of `engine/editor/scene_serializer.cpp` + `environment/*` → nothing). The "Phase 5G
  environment painting + probe placement" the AX3 roadmap bullet references is
  **GPU-runtime IBL/SH capture, not a serializable acoustic-probe asset
  system.** AX3 reuses the *placement geometry* (grid-config shape,
  AABB-influence + blend-weight pattern, the shared falloff math) but the
  on-disk bake format is **net-new.**
**Scene serialization convention.** `SceneSerializer` (nlohmann JSON) writes a
scene envelope; large binary payloads (terrain heightmap + splatmap) are written
as **epoch-tokened sidecars next to `scene.json`**, with `scene.json` acting as
the manifest that references them (the Ed11 atomicity block,
`engine/editor/scene_serializer.cpp:176–238`). ECS components persist via
`component_serializer_registry`. AX3's bake follows that sidecar convention (a
flat `<scene>_acoustics/` dir is a *simpler* analogue — no epoch token needed,
staleness handled by a geometry hash instead); reverb zones persist as an ECS
component with a hand-written serialize/deserialize pair + `registerEntry`
(§5.1).
**Dependencies (relevant).** OpenAL Soft 1.25.1 (dynamic), libebur128 1.2.6
(AX9), dr_libs + stb_vorbis (vendored), enkiTS 1.11 (MT2), nlohmann/json,
Jolt 5.3.0. **No FFT library (no pocketfft / kissfft / fftw) and no Steam
Audio anywhere in the tree.** This shapes §11.

**Code-side follow-ups (surfaced during this doc's review — out of scope, a
separate cleanup, not blocking):**
- `engine/audio/audio_reverb.h:18` header comment says the adapter drives
  `AL_EAXREVERB_*`, but the `ReverbParams` fields (lines 46–48) map to the
  standard `AL_REVERB_*` model — line 18 should read `AL_REVERB_*`.
- OpenAL Soft's licence string is inconsistent across the tree:
  `THIRD_PARTY_NOTICES.md` says LGPL v2.1, `external/CMakeLists.txt:369` says
  "LGPL 2.0+" — align the CMake comment to v2.1.

---

## 1. Goals & non-goals

### Goals
- **G1 (AX2).** Reverb becomes *audible* for the first time: sources played in
  a reverb zone are coloured by that zone's acoustics via OpenAL's auxiliary
  effect-slot send. This is the first `AL_AUXILIARY_SEND_FILTER` in the engine.
- **G2 (AX2).** A zone can carry a **measured / baked impulse response** and be
  rendered by OpenAL Soft's **convolution effect** — the "acoustically correct"
  path the roadmap asks for. Zones without an IR (or on drivers lacking the
  extension) render via the **standard `AL_EFFECT_REVERB`** driven by the
  existing `ReverbParams` presets. One unified zone model, two backends.
- **G3 (AX2).** Reverb zones become real, serialized scene data (an ECS
  component), replacing the editor-only draft. Zone selection + neighbour blend
  runs each frame on the main thread, mirroring `AudioOcclusionSystem`.
- **G4 (AX3).** A build-time baker generates a per-probe IR for the static
  level geometry via the **image-source method + a statistical late tail**, and
  writes it as a scene sidecar. At runtime the nearest probe's IR feeds the
  **same** convolution slot AX2 built — zero ray-tracing during play.
- **G5.** 60 FPS hard floor is untouched: runtime cost is OpenAL's mix
  (driver-managed) plus O(zones)+O(probes-near-listener) main-thread selection;
  the bake is offline.

### Non-goals
- **N1.** A from-scratch real-time FFT convolution engine. Rejected in §2 —
  OpenAL won't expose the mixed source PCM a self-built reverb send bus needs
  (the wall AX9 already hit: no master-bus PCM during live playback), so it
  would mean partially replacing OpenAL's mixer. Out of scope.
- **N2.** Steam Audio SDK integration. AX3 is clean-room image-source; we do
  not adopt Steam Audio's dependency tree or data model (§11).
- **N3.** Binaural / SOFA-format spatial IRs, per-direction IR grids,
  head-tracked convolution. Mono/stereo IRs only. SOFA noted as a future
  upgrade path (§12).
- **N4.** Dynamic (moving-geometry) acoustic simulation. AX3 bakes **static**
  geometry only; doors that open are approximated by zone/occlusion, not re-bake.
- **N5.** Diffraction / edge modelling — remains AX1's non-goal.
- **N6.** Per-source *individual* IRs. Reverb is a property of the room, not the
  source: all spatial sources in a zone share one convolution slot (§4.3). This
  is also what keeps the cost O(1) in source count.

---

## 2. Approach decision: native convolution + parametric fallback (the fork, resolved)

The roadmap's AX2 wording ("convolved at runtime via FFT, uniform-partitioned,
Gardner 1995, pocketfft") assumes a **self-built** convolution engine. Research
(2026-07-02) plus the reality check surfaced three viable shapes; the user
deferred the choice ("choose the best and most efficient option"). Decision:

| Option | Delivers baked-IR sound? | New code | Risk | Verdict |
|---|---|---|---|---|
| **A. Native convolution effect + parametric fallback** | ✅ | Small | Experimental OpenAL extension (mitigated) | **CHOSEN** |
| B. Self-built partitioned-FFT convolver (pocketfft) | ✅ | Very large | Needs OpenAL mixer replacement (N1); 60 FPS risk | Rejected |
| C. Parametric `AL_EFFECT_REVERB` presets only | ❌ (no IRs) | Smallest | None | Rejected as *sole* path; **kept as A's fallback** |

**Why A.** It meets AX2's actual goal (per-room *measured/baked* IRs, not flat
presets) with the least code, because OpenAL Soft's convolution effect does the
send-bus mixing and the convolution internally — the exact work Option B can't
do without tapping OpenAL's mix. Option C alone is "the flat-preset sound AX2
was meant to move beyond," but it is the *correct graceful degradation* when the
convolution extension is absent, and it finally gives the six shipped presets a
voice. So A **subsumes** C as its fallback tier.

**The experimental-extension risk, and how it's contained.** OpenAL Soft's
convolution effect (`AL_SOFTX_convolution_effect`; effect enum
`AL_EFFECT_CONVOLUTION_SOFT = 0xA000`) is an *in-progress* extension: its token
is not in the shipped public `alext.h`, and availability depends on the OpenAL
Soft build the target system ships — its token exists in the pinned 1.25.1's
internal headers, but runtime availability is still probed, never assumed.
Mitigations:
1. **Runtime probe.** `alIsExtensionPresent("AL_SOFTX_convolution_effect")` at
   init — never assume presence (mirrors the AX6 `ALC_EXT_EFX` and AX11
   `ALC_SOFT_reopen_device` gates).
2. **Locally-defined token with a citation comment**, since it's absent from the
   header — value pinned to `0xA000`, documented as experimental (project
   Rule 5 workaround-logging: it's a deliberate reliance, named in code +
   CHANGELOG).
3. **Graceful fallback to parametric `AL_EFFECT_REVERB`** (which *is* in the
   1.25.1 headers) whenever convolution is absent or the driver refuses the
   effect — same fallback discipline as the AX6 filter.
4. **A presence/parity test** that asserts the engine picks convolution when the
   extension is present and parametric otherwise, and that both produce a
   non-dry wet path.

Net: we lean on the extension only where it's the dramatically simpler path, and
we are never *hostage* to it. If the pin ever moves to a build without it, the
game still reverberates via presets.

---

## 3. Slice plan & order (dependency-respecting, simplest-first)

**AX2 (ships first, independently valuable):**
- **R1 — Aux-slot + parametric reverb backend.** EFX aux effect slot + one
  `AL_EFFECT_REVERB` object in `AudioEngine`, driven by a single
  `ReverbParams`; per-source `AL_AUXILIARY_SEND_FILTER` wired through
  `AudioSourceAlState`. No zones yet — one engine-wide reverb, off by default.
  *Verify:* a spatial source routed to the slot is audibly wetter than dry; a
  driver without `ALC_EXT_EFX` is bit-for-bit unchanged (dry).
- **R2 — IR loading + convolution backend.** `loadReverbIr(path)` → AL buffer;
  convolution effect selected at init when the extension is present, else R1's
  parametric backend. *Verify:* with a bundled reference IR, the tail matches
  the IR (not the preset); extension-absent path falls back to R1; parity test;
  loading IRs past the 64 MB pool ceiling (§7) triggers LRU eviction while the
  slot's currently-attached IR is never evicted.
- **R3 — `ReverbZoneComponent` + `ReverbSystem` (selection + blend).** Real
  serialized zones; per-frame highest-weighted-zone + neighbour selection;
  parametric zones blend params continuously, convolution zones swap IR with a
  glitch-free transition (§5.2). *Verify:* walking through a doorway crossfades
  reverb; zones persist across save/load; a Release-gated micro-benchmark asserts
  per-frame zone-selection main-thread cost ≤ 0.05 ms (§7, the AX8/AX9
  perf-benchmark precedent).
- **R4 — Settings + editor.** `reverbEnabled` + wet-level cap + convolution
  on/off; promote the editor draft to drive real `ReverbZoneComponent`s.
  *Verify:* settings round-trip + clamp tests; editor placement writes scene JSON.
- **R5 — Audit / CHANGELOG / ROADMAP flip AX2.**

**AX3 (follows AX2, consumes its slot):**
- **B1 — Acoustic probe placement + serialization.** `AcousticProbe` positions
  (grid or hand-placed, reusing `SHGridConfig` shape + `LightProbe`
  AABB/blend), serialized into the scene. No bake yet. *Verify:* probes persist;
  nearest-probe query returns the expected probe.
- **B2 — Image-source bake core (offline, headless, unit-tested).** Pure
  `bakeProbeIr(facets, probePos, params)` → IR PCM: image sources up to order K
  + per-bounce reflection factor + a statistical late tail fit to the room's
  Sabine RT60 (§6.2). *Verify:* single Stone wall at perpendicular distance
  `d = 2 m` (image-source distance = `2d = 4 m`) → first image at delay `2d/c`
  (±1 sample), and amplitude within ±1% of the analytic `√(1−α)/(2d) =
  √0.97/4 ≈ 0.246` for Stone's α = 0.03 (the §6.2 absorption table); sealed box
  (6 `Stone` walls, 4×4×3 m → V = 48 m³, total surface ΣSᵢ = 80 m², α = 0.03) →
  RT60 (Schroeder backward-integration of the baked IR, T30 extrapolation) within
  ±10% of `0.161·V/(ΣSᵢ·α) = 0.161·48/(80·0.03) ≈ 3.2 s`.
- **B3 — Bake driver + sidecar artifact.** Walk probes (MT2 `parallelFor`),
  write per-probe IR sidecar next to `scene.json` + a JSON index. *Verify:*
  bake a test scene; artifact round-trips; re-bake is deterministic.
- **B4 — Runtime nearest-probe → convolution slot.** `ReverbSystem` prefers a
  baked probe IR over an authored zone when a bake exists. *Verify:* baked scene
  uses probe IRs; unbaked scene falls back to zones; zero per-frame ray casts.
- **B5 — Bake UI (editor button / CLI) + audit / CHANGELOG / ROADMAP flip AX3.**

Each slice is one reviewable commit. AX2 R1–R5 can ship and be used with
bundled/authored IRs before any AX3 work begins.

---

## 4. Shared IR + convolution infrastructure

### 4.1 IR representation & loading

- IRs are **mono or stereo `.wav`** (the format OpenAL Soft's convolution
  example consumes; §12). Bundled reference IRs live under
  `assets/audio/ir/` (CC0 / self-generated); AX3 writes baked IRs as scene
  sidecars (§6.3).
- **Dedicated load path**, not `loadBuffer`: `AudioEngine::loadReverbIr(const
  std::string& path)` reuses `AudioClip::loadFromFile` for decode (WAV → s16
  PCM), then `alGenBuffers` + `alBufferData`. It **skips** the LRU source cache
  and the AX9 LUFS measurement (an IR is not a played clip). IR buffers are held
  in a small `std::unordered_map<std::string, ALuint> m_reverbIrBuffers`,
  deleted only at shutdown or on explicit eviction — **never while attached to
  the slot** (the extension forbids modifying/deleting an attached IR buffer —
  the glitch-free swap in §5.2 is built around this constraint).
- Sandbox: IR paths go through the same `validatePath` gate as clips.

### 4.2 OpenAL aux effect slot + convolution / reverb effect

Mirror the AX6 EFX block (`audio_engine.cpp:139–170`) exactly:

1. Already inside the `ALC_EXT_EFX` gate. Load the aux-slot + effect procs via
   `alGetProcAddress`: `alGenAuxiliaryEffectSlots`, `alDeleteAuxiliaryEffectSlots`,
   `alAuxiliaryEffectSloti`, `alAuxiliaryEffectSlotf`, `alGenEffects`,
   `alDeleteEffects`, `alEffecti`, `alEffectf`. Any null → no reverb (dry), same
   as the AX6 gain-only fallback.
2. `alGenAuxiliaryEffectSlots(1, &m_reverbSlot)` + `alGenEffects(1, &m_reverbEffect)`.
3. **Backend selection (once, at init):**
   - If `alIsExtensionPresent("AL_SOFTX_convolution_effect")`:
     `m_reverbBackend = Convolution`; locally define
     `constexpr ALenum AL_EFFECT_CONVOLUTION_SOFT = 0xA000;` (value confirmed in
     the fetched `alc/inprogext.h`; comment citing the experimental extension +
     `examples/alconvolve.c`) and
     `alEffecti(m_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_CONVOLUTION_SOFT)`.
     **Spelling trap — both forms are real and deliberate:** the runtime probe
     string is `"AL_SOFTX_convolution_effect"` (**with X**, per
     `alconvolve.c:433`); the header feature macro in `alc/inprogext.h` is
     `AL_SOFT_convolution_effect` (**no X**). Don't "unify" them — the X-form is
     only ever the `alIsExtensionPresent` argument.
   - Else: `m_reverbBackend = Parametric`;
     `alEffecti(m_reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB)`.
   - If either `alEffecti` errors (driver refusal), drop to the *other* backend;
     if both fail, `m_reverbSlot = 0` → dry (matches AX6's graceful zeroing).
4. Attach the effect to the slot: `alAuxiliaryEffectSloti(m_reverbSlot,
   AL_EFFECTSLOT_EFFECT, m_reverbEffect)`.
5. **Loud-guard.** The convolution effect is documented as very loud; the slot
   gain starts scaled (`alAuxiliaryEffectSlotf(m_reverbSlot, AL_EFFECTSLOT_GAIN,
   g)` with `g` from the wet-level setting, capped by `reverbWetCap` (default
   `0.5f`, §5.3) — §8).

Convolution backend applies an IR by attaching the IR buffer to the slot:
`alAuxiliaryEffectSloti(m_reverbSlot, AL_BUFFER, irBuffer)`. Parametric backend
applies a `ReverbParams` by setting the `AL_REVERB_*` properties on the effect
then re-attaching the effect to the slot.

**Cleanup** deletes effect + slot in `shutdown()` alongside the existing filter
deletion, IR buffers after the slot is detached.

### 4.3 Per-source reverb send

- Add `float reverbSend = 0.0f;` to `AudioSourceAlState`
  (`audio_source_state.h`), set in `composeAudioSourceAlState` (trailing
  defaulted param so all existing call sites — 2 production in
  `audio_system.cpp` plus the 22 in `tests/test_audio_source_state.cpp` —
  compile unchanged; the AX6 / AX9 pattern).
- In `applySourceState`, for a spatial source with `reverbSend > 0` and a live
  slot: `alSource3i(source, AL_AUXILIARY_SEND_FILTER, m_reverbSlot, 0,
  AL_FILTER_NULL)`. For `reverbSend == 0` or no slot: clear with
  `alSource3i(source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0,
  AL_FILTER_NULL)` (mirrors how the direct filter clears to `AL_FILTER_NULL`).
- **Aux-send availability.** At init, probe `alcGetIntegerv(device,
  ALC_MAX_AUXILIARY_SENDS, …)`; a device reporting **0 sends** cannot route
  reverb, so the whole reverb path degrades to **dry** (slot never created,
  sends never set) with a single logged line — the same graceful-no-op posture
  as the AX6 `ALC_EXT_EFX`-absent path.
- **v1 send policy (simplest correct):** a source in a zone with a live slot
  sends at a fixed unity into the slot; per-room wet level is controlled by the
  **slot gain** (one knob, room-accurate). Per-source send *scaling* (dry/wet by
  distance or occlusion) is a documented follow-up, not v1 — the room's slot
  gain + AX1's dry-path occlusion already give a plausible mix. This keeps
  reverb **O(1) in source count** at the effect and O(sources) only in the cheap
  per-source `alSource3i` bind.

---

## 5. AX2 runtime — `ReverbSystem`

### 5.1 Reverb zones as scene data (persistence)

Replace the editor-draft `ReverbZoneInstance` with a real ECS component,
persisted the same way `AudioSourceComponent` is: a **hand-written**
`serialize`/`deserialize` pair plus a `ComponentSerializerRegistry::registerEntry`
call (the pattern in `engine/utils/entity_serializer.cpp` — `serializeAudioSource`
at :812, its `registerEntry` at :934). Writing those two callbacks +
the registration **is R3 / B1 scope** — the registry is a manual callback table,
not an auto-serializer, so there is no "free" persistence:

```cpp
struct ReverbZoneComponent
{
    float       coreRadius  = 5.0f;   // full-weight core (m)
    float       falloffBand = 2.0f;   // linear-falloff band (m)
    ReverbPreset preset     = ReverbPreset::Generic;  // parametric fallback
    std::string irPath;               // optional; empty ⇒ parametric only
    float       wetGain     = 0.30f;  // slot gain when this zone wins [0,1]
};
```

Zone position comes from the entity transform (no duplicate position field).
`irPath` unifies both backends: non-empty + convolution available → convolution
with that IR; otherwise → parametric `preset`.

### 5.2 Zone selection + blend + glitch-free swap

`ReverbSystem : ISystem`. Registration **phase + order** mirror
`AudioOcclusionSystem`: `UpdatePhase::PostCamera`, registered **before**
`AudioSystem`. **Ownership deliberately differs from AX1**:
`AudioOcclusionSystem::getOwnedComponentTypes()` returns `AudioSourceComponent`
(`engine/systems/audio_occlusion_system.cpp:498`);
`ReverbSystem::getOwnedComponentTypes()`
returns `{ ReverbZoneComponent }` instead, so it inherits the default
`isForceActive() == false` and activates when a scene has reverb zones (a scene
with sources but no zone stays dry — the intended no-reverb path). It reads
spatial `AudioSourceComponent`s inside its update via `Scene::forEachEntity`, as
`AudioSystem` does. Each frame:

1. Read listener position (camera). Iterate `ReverbZoneComponent`s, compute
   `computeReverbZoneWeight(coreRadius, falloffBand, dist)` for each; find the
   highest-weighted zone `A` and next-highest neighbour `B`.
2. **Parametric backend:** blend `blendReverbParams(paramsA, paramsB, t)` where
   `t` derives from the relative weights, and push to the effect every frame the
   blend changes — continuous crossfade through doorways (the exact use the
   `audio_reverb.h` comment describes).
3. **Convolution backend:** IRs cannot be lerped cheaply (that needs two slots
   convolving in parallel). v1 **snaps to the winning zone's IR** and, on a
   change, performs a **short wet-gain dip** (ramp slot gain → 0, swap
   `AL_BUFFER`, ramp back over a few frames) so there is no click. A dual-slot
   crossfade is a documented refinement, not v1. The dip is honest, cheap, and
   inaudible at walking speed; documented per Rule 5.
4. Set the winning `wetGain` on the slot; set `reverbSend` on sources within any
   zone (via the compose path). Sources outside every zone → `reverbSend = 0`.
5. **Slew** the slot gain (reuse the AX1 per-frame slew idiom) so entering /
   leaving a zone fades rather than steps.

No zones present, or reverb disabled → slot gain 0 / sends cleared → dry, exactly
today's behaviour.

### 5.3 Settings + editor

- `AudioSettings`: `reverbEnabled` (default **on**, wet-capped for
  accessibility — §8), `reverbWetCap` (0..1 ceiling on any zone's `wetGain`,
  default **`0.5f`** to tame the "really loud" convolution), `reverbConvolutionEnabled`
  (default on; lets a user force the parametric backend). Additive tolerant JSON
  keys + `validate()` clamps + a sibling `AudioReverbApplySink` /
  `AudioEngineReverbApplySink` + `ApplyTargets::audioReverb` — the AX5/AX6/AX8/
  AX9 settings pattern: additive `j.value` defaults plus a schema-version bump
  with a self-describing `migrate_*` arm (AX8/AX9 rode a `v3→v4` bump to add
  their audio keys; these keys ride the current version and bump it, with the
  migration defaulting them, exactly as those did — not a "no-bump" shortcut).
- Editor: the existing `audio_panel` Zones tab is promoted to create/edit real
  `ReverbZoneComponent`s (core radius / falloff / preset / IR path picker /
  wet gain), plus a debug line showing the active backend (Convolution /
  Parametric / Dry) and current winning zone.

---

## 6. AX3 offline — acoustic pre-bake

### 6.1 Acoustic probes (placement)

`AcousticProbe` = a world position + influence radius. Placement reuses existing
*shapes*, not existing *systems*:
- A grid option reusing `SHGridConfig`'s `{worldMin, worldMax, resolution}`
  shape (do **not** couple to `SHProbeGrid` — it's GPU/IBL; just borrow the
  struct shape for a familiar author-facing control).
- Hand-placed probes as entities carrying an `AcousticProbeComponent` (position
  from transform + influence radius + `bakedIrPath`), borrowing the `LightProbe`
  `getBlendWeight` falloff *pattern* for runtime interpolation weight — note
  `AcousticProbe` uses a scalar **influence radius**, not `LightProbe`'s
  influence **AABB** (a deliberate simplification; probes are point-like).
- Probes serialize as ECS components (hand-written serialize/deserialize +
  `registerEntry`, per §5.1) + the sidecar IR reference (§6.3).

### 6.2 Image-source bake core (the algorithm)

Per probe, offline, pure and unit-testable:
`bakeProbeIr(const std::vector<ReflectingFacet>& geometry, glm::vec3 probePos,
BakeParams) → std::vector<float> ir`, where a `ReflectingFacet` is
`{ glm::vec4 plane; float area; SurfaceMaterial material; }` — plane
(normal + offset), the facet's **finite surface area** (sum of its merged
triangle areas, the `Sᵢ` Sabine needs), and the physics material tag.

**Bake input — facet extraction (B2 scope, code-side prerequisite).** The
`ReflectingFacet` list is built once per scene from the **static physics
bodies**. Primary source: each static `RigidBody`'s stored mesh —
`collisionVertices` + `collisionIndices` (finite CCW triangles, `rigid_body.h:65–71`)
tagged with the body's `RigidBody::surfaceMaterial` (`rigid_body.h:63`); merge
coplanar triangles into facets, summing their areas. Fallback for bodies with no
stored mesh (box / convex-hull shapes): decode triangles from the Jolt shape via
`Shape::GetTrianglesStart(GetTrianglesContext&, const AABox&, Vec3Arg comPos,
QuatArg rot, Vec3Arg scale)` + repeated `GetTrianglesNext(ctx, maxTris,
Float3* outVerts, const PhysicsMaterial**)` (verified in the pinned Jolt 5.3.0,
`Shape.h:358/366`; the context is a 4288-byte scratch struct + an AABox query
region; request ≥ 32 triangles per call — `cGetTrianglesMinTrianglesRequested`,
`Shape.h:351`), with the material from `PhysicsWorld::getSurfaceMaterial(bodyId)`
(main-thread, `physics_world.h:117`) — not Jolt's per-triangle `PhysicsMaterial`.
Because triangles are finite, the enclosing **volume `V`** for Sabine is the
facets' AABB volume. **Untagged geometry:** a body with no tag reads back
`SurfaceMaterial::Default`; consistent with AX1's convention
(`engine/audio/occlusion_material_map.h:49` maps `Default → Concrete`, a hard
reflective bucket), the bake assigns `Default` a **reflective α** and **logs a
warning** so
the author knows to tag the architecture — it bakes a plausible stone-like room,
not a bake failure. **No facet-enumeration helper exists today** — building it is
part of B2. This replaces the vague "level brushes" notion (the engine has no
brush primitive); the geometry source is the static collision mesh.

1. **Early reflections — image-source method (Allen & Berkley 1979).** For each
   `ReflectingFacet`, mirror the probe across the facet plane to get an image
   source. Recurse
   to reflection order **K (default 2–3, capped)**. Each image contributes an
   impulse at delay `distance / speedOfSound`, attenuated by `1/distance` and
   the product of per-bounce **reflection factors** `√(1 − αᵢ)`. The absorption
   coefficient `αᵢ` is keyed off the surface's physics `SurfaceMaterial` tag
   (`engine/physics/surface_material.h`, the same tag AX1/AX4 read) via a **new absorption
   table** — AX1's `AudioOcclusionMaterial` values are *transmission /
   low-pass* coefficients (e.g. Concrete `{0.05, 0.90}`), **not** reflection
   absorption α, so they cannot be reused as-is; the α table is new data (below).
   Reused: the material *identity* (the enum), not its numbers. ISM is exact for
   planar convex rooms —
   which is precisely the Tabernacle / Temple rectilinear stone geometry.

   **Absorption-coefficient table (mid-frequency Sabine α, one row per
   `SurfaceMaterial`).** Literature-seeded starting values (standard
   architectural-acoustics tables at ~500 Hz–1 kHz); a Formula Workbench
   refinement candidate (§10). One row per `SurfaceMaterial` enumerator — all 10
   (`engine/physics/surface_material.h:34–43`; `kSurfaceMaterialCount = 10` at
   `:49`) — the same keying pattern `engine/audio/occlusion_material_map.h` uses
   for occlusion, a *separate* table of numbers:

   Rows follow the enum's append-only declaration order
   (`engine/physics/surface_material.h:34–43`) — the table is keyed **by name**,
   not by index, but listing it in enum order lets an implementer build the
   lookup array straight down:

   | `SurfaceMaterial` | α | Note |
   |---|---|---|
   | `Default` | 0.04 | Untagged → reflective bucket. |
   | `Stone` | 0.03 | Hard, reflective — long tail. |
   | `Wood` | 0.10 | Panelling — mild absorption. |
   | `Metal` | 0.05 | Reflective, slight ring. |
   | `Cloth` | 0.55 | Drapes / fabric — strongly absorptive. |
   | `Sand` | 0.30 | Loose — absorptive. |
   | `Water` | 0.02 | Reflective boundary (pool edge / ice). |
   | `Grass` | 0.30 | Absorptive. |
   | `Dirt` | 0.15 | Packed earth. |
   | `Glass` | 0.03 | Reflective. |

   `SurfaceMaterial` has **no `Concrete` enumerator** (concrete-like surfaces are
   tagged `Stone` or left `Default`); AX1's occlusion map sends `Default →
   Concrete` (a hard reflective preset), which is why `Default`'s α here is a
   low, reflective 0.04 — the single value used for all untagged geometry.
2. **Late diffuse tail — statistical.** Beyond order K, append an
   **exponentially-decaying-noise tail** (a windowed random buffer whose
   envelope decays to −60 dB over the room's RT60) — the simpler, cheaper of the
   two standard choices; a feedback-delay-network tail is a documented
   refinement, not v1. RT60 is estimated from the room via **Sabine's equation**
   `RT60 = 0.161 · V / (Σ Sᵢ·αᵢ)` (volume V, surface areas
   Sᵢ, absorptions αᵢ). This is the standard, cheap way to get a plausible tail
   without exploding ISM order. Caves (concave, non-planar) are approximated by
   coarse planar facets for early reflections + a longer statistical tail —
   documented as lower-fidelity than true ray tracing (an honest boundary, per
   Rule 5).
3. Output normalised mono (or stereo) float PCM at the mix sample rate, written
   to a `.wav` sidecar. Note the round-trip: the baker writes float-in-WAV, and
   the runtime load path (`AudioClip`, §4.1) decodes WAV back to **s16 PCM** —
   the quantisation is acceptable for a reverb IR (it is not the dry signal).

`BakeParams` (reflection order K, speed of sound, tail model, sample rate,
max IR length, and the **coplanar-merge tolerance** — the max normal-angle /
plane-offset delta under which adjacent triangles fold into one facet, which
trades facet count against `Sᵢ`/image-source fidelity) come from the design
defaults; the RT60 tail-shape curve is a
**Formula Workbench** candidate (§10).

### 6.3 Bake artifact format + runtime lookup

- **Sidecar next to `scene.json`** (terrain-heightmap precedent): a directory
  `<scene>_acoustics/` with one IR `.wav` per probe (`probe_<id>.wav`) plus
  `acoustics_index.json` mapping probe id → position + influence + IR filename +
  a bake hash (geometry fingerprint) for staleness detection.
- **Runtime:** at scene load, `ReverbSystem` reads the index and (B4) prefers
  the nearest probe's IR over any authored zone. Nearest-probe lookup is a cheap
  main-thread distance query each frame (O(probes-near-listener), same budget
  class as zone selection). Since every baked probe carries an IR (never
  parametric params), transitioning between the two nearest probes uses the
  `LightProbe` blend-weight pattern only to pick which probe wins, then takes the
  **IR snap-with-dip** path from §5.2 — the parametric-blend branch applies to
  authored parametric zones, not baked probes. Genuine IR-to-IR interpolation
  (dual-slot crossfade) is a future refinement.

### 6.4 Bake driver

- **Threading contract.** Facet extraction (the `SurfaceMaterial` reads via
  `getSurfaceMaterial` and the Jolt `GetTrianglesStart/Next` decode) is
  **main-thread-only** — `getSurfaceMaterial` takes a Jolt body lock
  (`physics_world.h:114–117`). So `AcousticBaker` builds the whole
  `std::vector<ReflectingFacet>` **once on the main thread first**, then the
  **pure** `bakeProbeIr(facets, probePos, …)` (no physics access) runs across MT2
  `parallelFor` over probes (probes are independent; CPU-bound, offline → pure
  speedup, no 60 FPS interaction). Writes the sidecar.
- Triggered from the editor (a "Bake Acoustics" button, beside existing bake
  affordances) and/or a headless CLI flag for CI/asset pipelines. Re-bake is
  gated on the geometry fingerprint so an unchanged scene skips work.

---

## 7. Performance (60 FPS hard floor)

- **Runtime convolution / reverb is OpenAL's cost**, on its own mixer thread,
  driver-managed — it does not touch our frame budget beyond the mix it already
  does. Convolution is heavier than parametric reverb (the research notes higher
  CPU/memory), but it is *one* convolution for the whole room regardless of
  source count (N6/§4.3), and the wet cap + backend toggle give the user an
  escape hatch on weak hardware.
- **Main-thread per frame:** zone selection O(zones) + nearest-probe O(probes
  near listener) + a handful of `alAuxiliaryEffectSlotf` / `alSource3i` binds on
  transitions. Zones/probes number in the tens, not thousands. Target ≤ 0.05 ms
  main-thread — negligible; no MT2 needed at runtime (unlike AX1's ray budget).
- **IR memory:** small room ~0.5 s ≈ 96 KB mono; stone hall / cave 4–8 s ≈
  1.5–3 MB. Baked probe grids multiply that by probe count → the bake writes to
  disk and the runtime holds only the IRs for near probes (LRU on
  `m_reverbIrBuffers`). A **runtime IR pool ceiling of 64 MB** (LRU-evict beyond
  it, eviction logged — Rule 5: no silent caps) bounds it — comfortably more
  than any listener's near-probe set (20+ cathedral-scale stereo IRs, or
  hundreds of small-room IRs, resident at once). **The IR currently attached to
  the slot is pinned — exempt from LRU eviction** (the extension forbids
  deleting an attached buffer, §4.1), so eviction only ever reclaims unattached
  IRs.
- **Bake is offline** — no frame budget; MT2-parallel across probes; gated by
  geometry fingerprint. Target: minutes for a full scene, not hours (bounded
  reflection order K + statistical tail keep it from exploding).
- **Transition glitch-freedom:** the wet-gain dip (§5.2) and slot-gain slew keep
  IR swaps click-free without a second convolution running.

---

## 8. Accessibility

- Reverb — especially long convolution tails — can muddy speech intelligibility
  and be fatiguing. **Ship `reverbEnabled` default on** (it's the headline
  feature) **but with `reverbWetCap` ≈ 0.5** so no zone is overwhelming, and a
  one-flip **"reduce reverb"** affordance that lowers the cap further (natural
  companion to the existing reduced-motion / photosensitive settings).
- The convolution loud-guard (§4.2 step 5) is itself an accessibility measure —
  no sudden loudness on entering a reverberant zone.
- Subtitles (already shipped) remain the dialogue-intelligibility backstop; a
  future "speech clarity" mode could duck reverb under the Voice bus (noted, not
  scoped here).

---

## 9. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Runtime convolution / parametric reverb | **CPU (OpenAL mixer thread)** | Audio DSP is OpenAL's domain; per-sample but off our frame thread and driver-optimised. Not GPU — audio doesn't live there and the round-trip latency would break sync. |
| Zone selection + neighbour blend | **CPU (main)** | Branching, sparse, per-listener, O(tens). Decision logic — the Rule 7 "branching/sparse/decision → CPU" default. |
| Nearest-probe lookup + interpolation | **CPU (main)** | Sparse distance query, tens of probes. Same class as zone selection. |
| AX3 image-source bake | **CPU (offline, MT2-parallel across probes)** | Ray/geometry but **not real-time** — build-time, complex branching, disk I/O output. GPU offers nothing here; probes are embarrassingly parallel on CPU workers. |

No GPU work in either feature. No dual CPU/GPU impl (no parity test needed —
Rule 7's dual-impl clause doesn't apply).

---

## 10. Formula Workbench (project Rule 6)

- **RT60 late-tail decay shape** (§6.2 step 2) is a fitted-curve candidate: the
  envelope of the statistical tail (energy decay vs time for a given RT60) can
  be authored/validated in the Workbench rather than hand-coded. `TODO: revisit
  via Formula Workbench` on the tail generator.
- **Sabine RT60** itself is a canonical closed form (no coefficients to fit) —
  Rule 6's "hand-code only when no reference data exists" doesn't bite; it's a
  standard equation, coded directly with the citation.
- **Image-source attenuation** (`1/distance` + reflection factor `√(1−α)`) is
  physical and canonical — no fit.
- **Absorption-coefficient table** (the per-`SurfaceMaterial` α values in §6.2)
  is literature-seeded but a legitimate Workbench refinement target — the α
  values could be fit against measured RT60s for the reference structures
  (Tabernacle / stone hall) if bake fidelity needs it. `TODO: revisit via
  Formula Workbench` on the α table.
- **Zone weight + param blend** already exist as canonical pure functions
  (`computeReverbZoneWeight`, `blendReverbParams`) — reused, not refitted.

---

## 11. Dependencies (project Rule 8)

- **AX2 adds no new runtime dependency.** It uses OpenAL Soft 1.25.1 (already
  present). The convolution effect token is defined locally (§4.2) with an
  experimental-extension comment — a *deliberate reliance*, logged per Rule 5,
  not a new dep.
- **We deliberately do NOT add pocketfft** (roadmap's assumption): Option A
  needs no runtime FFT, and AX3's image-source + time-domain tail synthesis is
  time-domain summation — no FFT either. Documented deviation with reason
  (Rule 8 "older/different choice needs a written reason"): the FFT was only
  needed for the rejected self-built convolver (§2, N1). If a future
  frequency-domain bake step (e.g. air-absorption filtering baked into the IR)
  is added, revisit pocketfft (BSD-3, header-only) for the **offline baker
  only**.
- **We deliberately do NOT add Steam Audio** (N2): clean-room ISM avoids its
  Apache-2.0 SDK footprint (Embree / Radeon Rays / IPP optional deps) and its
  data-model lock-in, and fits the project's lean dependency posture. Steam
  Audio remains readable as an algorithmic reference (Apache 2.0).
- Bundled reference IRs (§4.1) must be CC0 / public-domain (e.g. OpenAIR-sourced
  or self-baked once AX3 lands) with a `THIRD_PARTY_NOTICES.md` row.

---

## 12. References

**Cited in this design (load-bearing — implementer will reference these):**
- **OpenAL Soft convolution effect** — runtime probe `AL_SOFTX_convolution_effect`
  (with X), header macro `AL_SOFT_convolution_effect` (no X), token
  `AL_EFFECT_CONVOLUTION_SOFT = 0xA000` (`alc/inprogext.h`); usage per
  `examples/alconvolve.c` (buffer → aux-effect-slot `AL_BUFFER` →
  `AL_AUXILIARY_SEND_FILTER`). Experimental (`SOFTX`), token not in public
  `alext.h`. Mono/stereo IR; slot gain must be scaled down ("really loud").
- **OpenAL EFX standard reverb** — `AL_EFFECT_REVERB`, `AL_REVERB_*` properties
  (`efx.h`, present in the fetched OpenAL Soft 1.25.1 headers). The parametric fallback.
- **Allen, J.B. & Berkley, D.A. (1979).** "Image method for efficiently
  simulating small-room acoustics," *JASA* 65(4):943–950. (AX3 early
  reflections.)
- **Sabine reverberation equation** — `RT60 = 0.161·V/(Σ Sᵢαᵢ)`. (AX3 tail.)

**Future / non-dependency references (not used now; upgrade paths):**
- **Gardner, W. (1995).** "Efficient Convolution Without Input/Output Delay,"
  *JAES* 43(3):127–136. (Partitioned convolution — background for the rejected
  self-built Option B, §2.)
- **Steam Audio** (Valve, Apache 2.0) — probe-based baked-reflections *pattern*
  only; algorithmic reference, not a dependency (N2).
- **Funkhouser / Tsingos beam tracing** — a future fidelity upgrade for
  complex/concave geometry (caves); not the AX3 default.
- **SOFA / AES69** — future upgrade path for binaural / per-direction spatial
  IRs (N3).
- **pocketfft** (BSD-3, header-only) — a possible future offline bake step only
  (§11); not added now.

---

## 13. Resolved decisions

- **D1.** AX2 backend = **native convolution effect + parametric-reverb
  fallback** (Option A, §2). User deferred; chosen as best/most-efficient.
- **D2.** No self-built FFT convolver (N1) and no pocketfft dependency now (§11)
  — deviation from the roadmap wording, with reason.
- **D3.** No Steam Audio dependency; AX3 is clean-room **image-source method +
  statistical tail** (§6.2) — deviation from the roadmap's "integrate the
  baker" option, with reason.
- **D4.** Reverb zones = a real **ECS component** (`ReverbZoneComponent`),
  serialized via a hand-written serialize/deserialize pair + `registerEntry` in
  the component registry (the `AudioSourceComponent` pattern — not automatic);
  the editor draft is promoted to drive it (§5.1). Position from the entity
  transform.
- **D5.** One unified zone model, two backends keyed off `irPath` + extension
  presence (§5.1). Convolution zones **snap IR with a wet-gain dip**; parametric
  zones **blend params continuously** (§5.2). Dual-slot IR crossfade deferred.
- **D6.** Reverb is **per-room, not per-source** (N6): all zone sources share
  one slot; per-source send scaling deferred (§4.3).
- **D7.** AX3 probes feed the **same** convolution slot AX2 builds; a baked probe
  IR is preferred over an authored zone at runtime (§6.3).
- **D8.** `reverbEnabled` default **on**, gated by a wet cap (≈ 0.5) + a
  "reduce reverb" accessibility flip + the convolution loud-guard (§8).
- **D9.** AX2 (R1–R5) ships and is usable before AX3 (B1–B5) begins.

---

## 14. Cold-eyes loop log

Reviewed by 3 independent cold reviewers per loop (AX2-runtime / AX3-bake /
cross-cutting lanes), each briefed with no authoring context. Every actionable
finding (CRITICAL/HIGH/MEDIUM/LOW) was verified against source and fixed; loops
2+ were briefed identically to loop 1 (no prior-fix list), so a non-recurring
finding is the proof its fix held. 5 loops to convergence — the design was
stable from loop 2; loops 3–5 were citation precision + wording only.

- **Loop 1** — 1 CRITICAL (a new ECS component does not persist "for free" —
  it needs a hand-written serialize/deserialize + `registerEntry`), 5 HIGH
  (call-site undercount; `isForceActive` conflation; AX1 material coefficients
  are transmission/low-pass, not reflection-α; schema-bump claim contradicting
  AX8/AX9; the `Reverb ZoneComponent` typo), 7 MEDIUM (bake geometry input
  undefined; agent-mapped scaffolding; IR-memory budget unpinned; B2 tolerances;
  tail-synth fork; baked-probe parametric-blend; field-name drift). All fixed.
- **Loop 2** — 0 CRITICAL, 2 HIGH (over-specified Jolt triangle API from the
  loop-1 fix; untagged-body → `Default` α precondition unstated), 3 MEDIUM
  (ownership-vs-AX1 clarity; B2 tolerance reference/RT60 method; float-vs-s16 IR
  boundary). All fixed. No loop-1 finding recurred.
- **Loop 3** — 0 CRITICAL, ~4 HIGH (`AL_SOFT`/`AL_SOFTX` spelling trap; wrong
  citation lines; missing α table that B2 referenced; Sabine needs finite
  areas/volume), MEDIUM (LGPL over-editorializing; Steam Audio version; §12
  split). All fixed.
- **Loop 4** — 1 CRITICAL (phantom `Concrete` row in the loop-3 α-table
  contradicting "10 enumerators"), 2 HIGH (stale `registerEntry` line; `Default`
  α stated twice), MEDIUM (Jolt ≥32-triangle constraint; sealed-box fixture;
  "vendored" vs FetchContent header paths; call-site count). All fixed.
- **Loop 5** — 0 CRITICAL, 2 HIGH (α-table row order vs the append-only enum;
  "present in 1.25.1" overclaiming presence), MEDIUM (pin `reverbWetCap = 0.5f`;
  pin B2 `d = 2 m`; aux-send-count-0 failure mode; facet-extraction threading
  contract; Sabine-denominator notation; `loadBuffer` end-line). All fixed.

Two code-side inconsistencies surfaced but deliberately NOT fixed here (a docs
review does not edit code): `audio_reverb.h:18` (`AL_EAXREVERB_*` should be
`AL_REVERB_*`) and the OpenAL Soft LGPL string (`external/CMakeLists.txt:369`
"LGPL 2.0+" vs `THIRD_PARTY_NOTICES.md` "v2.1"). Both are recorded in §0's
"code-side follow-ups" note for a separate cleanup.
