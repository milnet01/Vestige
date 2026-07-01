# Phase 10 — Geometric / Ray-Traced Audio Occlusion (AX1) Design Doc

> **Roadmap:** `AX1. Geometric / ray-traced audio occlusion` (§ Audio System).
> First feature of the **Spatial Acoustics** bundle (AX1 → AX2 → AX3). AX2
> (convolution reverb) and AX3 (acoustic pre-bake) get their own design doc after
> AX1 ships — they share IR infrastructure that does not exist yet, and their
> design will reference AX1's shipped material-map + ray plumbing rather than
> guess at it (global Rule 13).
>
> **Status:** ✅ **SIGNED OFF — ready to implement.** Cold-eyes converged in 5 loops
> (§11 log): Loop 5 returned 0 CRITICAL/HIGH/MEDIUM on both lanes with an explicit
> "recommend sign-off"; no finding resurfaced across any loop. Signed off per the
> delegated gate (cold-eyes convergence to polish-only). Implement in the §2 slice
> order, S1 first.
>
> **Depends on:** MT2 job system (`engine/core/job_system.{h,cpp}`, shipped) — the
> reason MT2 was built first. AX4's `SurfaceMaterial` tag + Jolt body user-data
> packing (`engine/physics/surface_material.h`, shipped).

---

## 0. What already exists (reality check, verified 2026-07-01)

Every claim below was read out of current source this session (global Rule 13 —
no recall). File:line citations are the authoritative anchor for the implementer. (Paths are
basename where the name resolves uniquely in the tree; the bare `engine.h` /
`engine.cpp` / `system_registry.cpp` cites all live under `engine/core/`.)

| Subsystem | Where | Notes relevant to AX1 |
|-----------|-------|-----------------------|
| **Occlusion pure-math layer** | `engine/audio/audio_occlusion.h`: `struct AudioOcclusionMaterial{transmissionCoefficient=1.0f, lowPassAmount=0.0f}` (`:63`); `enum class AudioOcclusionMaterialPreset{Air,Cloth,Wood,Glass,Stone,Concrete,Metal,Water}` (`:44`, **8 members**); `occlusionMaterialFor(preset)` (`:75`); `computeObstructionGain(openGain, transmissionCoefficient, fractionBlocked)` (`:95`); `computeObstructionLowPass(lowPassAmount, fractionBlocked)` (`:103`) | **Geometry-blind by design** (`:8-28`: "the engine-side raycaster's responsibility to pick… Keeping the pure-function layer blind to geometry preserves testability"). AX1 is the driver that fills this seam. **No new math here** — AX1 reuses these fns verbatim. |
| **Per-source occlusion fields** | `engine/audio/audio_source_component.h`: `AudioOcclusionMaterialPreset occlusionMaterial = Air` (`:90`); `float occlusionFraction = 0.0f` (`:96`) | Doc (`:83-89`): "the engine-side raycaster reports [material] when the line-of-sight… hits a solid surface." **Today written only by tests/serializer** — no live raycaster. AX1 writes them each frame. `occlusionFraction` doubles as the smoothing state (§4.4). |
| **Compose (pure) → apply (AL)** | `composeAudioSourceAlState(comp, entityPos, mixer, duck, listenerPos, airAbsorption, lodTier, loudnessMakeup)` (`engine/audio/audio_source_state.h:122`). Occlusion gain folds into `resolveSourceGain`'s volume input (`:84-88`); `lowPassGainHf = occlusionLowPassHf × airAbsorptionHfGain` (`:102-105`). Applied by `AudioEngine::applySourceState` (`audio_engine.cpp:1172`): `AL_GAIN` (`:1189`), EFX `AL_LOWPASS_GAINHF`+`AL_DIRECT_FILTER` (`:1203-1218`, no-op if EFX absent) | **The entire playback path already consumes `occlusionMaterial`+`occlusionFraction`.** AX1 adds no code here; it only populates the two fields **before** this loop runs. |
| **Audio per-frame entry** | `AudioSystem::update(float dt)` (`engine/systems/audio_system.cpp:70`); per-source compose+apply loop head at `:162` (calls at `:249-253`/`:269-273`); listener synced from camera at `:82-87`; `getUpdatePhase()==PostCamera` (`audio_system.h:43`) | AX1's occlusion pass must run **after** the camera/listener is settled and **before** this compose loop reads the fields. |
| **Physics surface material** | `enum class SurfaceMaterial : uint8_t {Default=0,Stone,Wood,Metal,Cloth,Sand,Water,Grass,Dirt,Glass}` (`engine/physics/surface_material.h:32`, **10 members**, append-only); `kSurfaceMaterialCount=10` (`:49`); `surfaceMaterialLabel` (`:82`) | The physics-side material of a struck/hit body. **Distinct enum** from `AudioOcclusionMaterialPreset` (`:15-21` says so explicitly). AX1 adds the map between them (§3, S1). |
| **Body tag read** | `PhysicsWorld::getSurfaceMaterial(JPH::BodyID)` (`physics_world.h:117`); `getBodyEntity(JPH::BodyID)` (`:121`); set via `setBodyTags(id, entity, material)` (`:111`) | **MAIN-THREAD-ONLY** (`:114-116`: "goes through `BodyInterface::GetUserData`, which takes a body lock. Never call from a Jolt contact callback"). → material resolution runs on main thread, after the ray jobs finish (§4.3). |
| **Raycast** | Preferred overload `bool rayCast(origin, unitDir, maxDistance, JPH::BodyID& outBodyId, float& outHitDistance, JPH::BodyID ignoreBodyId={})` (`physics_world.h:248`; impl `physics_world.cpp:803` — a legacy 4-arg overload precedes it at `:780`; use the 6-arg). Uses `GetNarrowPhaseQuery().CastRay`. Returns hit body + world-unit distance | **Returns body id + distance only — NO hit point, NO surface normal.** Point is reconstructed `origin+dir*dist`; **the missing normal is why edge-diffraction is a non-goal** (§1). Jolt narrow-phase queries are safe to call from many threads on a non-stepping world (Jolt contract) → ray casting parallelizes on workers (§4.2). |
| **MT2 job system** | `JobSystem::parallelFor(uint32_t count, void(uint32_t begin,uint32_t end))` (`engine/core/job_system.h:94`, "AX1's primitive"); `submit` (`:87`); `wait(handle)` (`:100`); `runOnMainThread` (`:106`); `drainMainThreadQueue` (`:112`); `isSynchronous()` (`:118`, returns true when the scheduler is absent — a config `numWorkers==0` selects this deterministic inline mode). Owned `Engine::m_jobSystem` (`engine.h:137`), `getJobSystem()` (`engine.h:295`); frame-top drain wired at `engine.cpp:1110` | AX1 schedules N rays via `parallelFor`, `wait()`s, resolves on main. **Synchronous mode gives deterministic tests** (no threads). |
| **Frame order (thread-safety anchor)** | `engine.cpp:1341` `m_physicsWorld.update(dt, onFixedStep)` (Jolt step, **synchronous** — blocks until settled) runs **before** `engine.cpp:1360` `m_systemRegistry.updateAll(dt)` (all domain systems incl. AudioSystem) | ∴ during any domain-system phase the Jolt world is **settled and idle**. The only concurrency AX1 introduces is its own read-only ray workers → safe (§4.2). |
| **System phases** | `enum class UpdatePhase{PreUpdate=-100,Update=0,PostCamera=100,PostPhysics=200,Render=300}`; `SystemRegistry::sortByUpdatePhase()` `std::stable_sort`, within-phase = registration order (enum in `i_system.h`; sort in `system_registry.cpp`; CHANGELOG 2026-04-27 Sy1) | AX1's system shares AudioSystem's `PostCamera` phase and is **registered before it** so stable-sort runs occlusion first. |
| Settings add-pattern | `engine/core/settings.h` `AudioSettings` → `settings.cpp` toJson/fromJson/validate (+ schema-version migration) → `settings_apply.h` `AudioApplySink` → `settings_editor.h` `ApplyTargets` → `engine/editor/panels/audio_panel.*` | S5 follows this exact chain for the occlusion toggle + tunables. |
| Test pattern | `tests/test_audio_*.cpp` (25 files), `tests/test_contact_event.cpp` (physics stepping helper) | Pure-fn numeric (`EXPECT_NEAR`, `kEps=1e-4`); headless (`AudioEngine::isAvailable()` gates AL). AX1's map + fraction math are pure fns; the system is tested against a real `PhysicsWorld` in **synchronous** job mode. |

**Net:** the playback consumption path is 100% built. AX1 is *only* the geometry
driver + the physics↔audio material map + its scheduling. No changes to
`audio_occlusion.h`, `composeAudioSourceAlState`, or `applySourceState`.

---

## 1. Goals & non-goals

**Goals**

- Drive `occlusionFraction` + `occlusionMaterial` on every audible spatial source
  **from actual scene geometry** each frame, replacing the "gameplay layer sets it
  by hand" model. Closes the roadmap's "*I added a wall but the sound still goes
  through it cleanly*" gap **for untagged geometry too** (§3, `Default`→`Concrete`).
- Derive a **soft open-path fraction** (not just binary blocked/clear) by sampling
  multiple rays toward the source volume (§4.1), so partial occlusion near
  doorways/edges sounds gradual.
- Keep the cost **off the serial audio compose path**: schedule the rays across MT2
  workers (`parallelFor`) so wall-time scales with cores, not source count (§4.2).
- **60 FPS hard floor.** Per-frame ray cost is budgeted + culled + amortizable
  (§4.5, §6). Worst case is a fixed ceiling, not a spike.
- Reuse the existing pure occlusion math (`audio_occlusion.h`) and the existing
  compose→apply path **verbatim** (Rule 3). AX1 adds a driver + one small map,
  nothing more.
- Each slice = one commit + its own tests, matching the Phase 10 audio cadence.

**Non-goals (explicit, with reason)**

- **No edge diffraction / path-bending delay.** True diffraction needs the
  occluder's surface normal + edge geometry; `PhysicsWorld::rayCast` returns
  **only body id + distance, no normal** (`physics_world.cpp:803`). The multi-ray
  fan gives a *perceptually* graded fraction near edges, but not a physically
  modeled diffraction filter. A normal-returning cast overload is the future
  enabler — noted, not built (§7). Faking a diffraction delay off a normal we
  don't have would violate Rule 1.
- **No changes to the occlusion DSP.** Gain + low-pass folding stays exactly as
  `computeObstructionGain`/`computeObstructionLowPass` compute it today. AX1 feeds
  their inputs; it does not re-derive them.
- **No reverb/IR work.** That's AX2/AX3, separate doc.
- **No GPU.** Raycasting is branchy/sparse and Jolt is CPU-side (§8).
- **No per-listener multi-listener support.** Single listener = the camera, as
  today (`audio_system.cpp:82`). Split-screen is out of scope engine-wide.
- **No occlusion for 2D / UI / non-spatial sources.** `playSound2D` and Ui-bus
  sources are excluded by the spatial-source filter (§4.5).
- **No transmission *through* multiple materials** (e.g. wall+door layering). The
  nearest single blocker's material governs, matching the existing single-material
  field on the component.

---

## 2. Slice plan & order (dependency-respecting, simplest-first)

Each slice is independently testable; the runtime work lands after its foundation.
S1 is a pure function (zero runtime risk). S2 proves the end-to-end seam with the
simplest possible ray (single, main-thread). S3 adds the fraction. S4 parallelizes.
S5 exposes it. This ordering means a wall occludes sound after **S2** — the rest is
quality + performance.

| # | Slice | New / touched | Depends on | Verify |
|---|-------|---------------|-----------|--------|
| **S1** | Physics→audio material map | `engine/audio/occlusion_material_map.h` (pure `occlusionPresetForSurface(SurfaceMaterial)`) + test | — | every `SurfaceMaterial` maps to a preset; `Default`→`Concrete`; round-trip table test |
| **S2** | `AudioOcclusionSystem` skeleton — **single** center ray/source, binary fraction, **main-thread** (no jobs), temporal smoothing | `engine/systems/audio_occlusion_system.{h,cpp}`; register in `PostCamera` before `AudioSystem`; inject `PhysicsWorld&` + `Scene` + listener | S1 | wall between source↔listener → `occlusionFraction`→1 (smoothed), gain drops; clear LOS → 0; source's own body ignored; a source with **no** physics body still casts (`ignoreBodyId={}`) |
| **S3** | Multi-ray volumetric fraction + nearest-blocker material | extend system: N-ray fan toward source volume; fraction = blocked/N; material from nearest blocking body | S2 | edge/doorway → intermediate fraction; fully behind wall → 1; nearest blocker's material chosen |
| **S4** | MT2 parallelization + budget + cull | rays cast in `parallelFor` on workers (raw hit stash), materials resolved on main post-`wait`; per-frame ray budget + round-robin amortization; audibility/distance cull; inject `JobSystem&` | S3, MT2 | deterministic result in **synchronous** job mode == S3; profiling gate < budget at max sources; over-budget → round-robin (logged, no silent cap) |
| **S5** | Settings + editor toggle + debug ray viz | `settings.*`/`settings_apply.*`/`settings_editor.*`/`audio_panel.*`; `occlusionEnabled`/`occlusionRayCount`/`occlusionMaxDistance`/`occlusionSourceRadius`; optional debug line-draw | S4 | settings round-trip + schema migration; toggle off → fields frozen to 0 (unoccluded); debug draw shows rays |
| **S6** | Audit, CHANGELOG, ROADMAP flip, push | — | all | AUDIT_STANDARDS 5-tier clean; `AX1` bullet flipped ✅ (by headline/line_range — GFM bullet, no bracket id); CHANGELOG entry |

Slices commit individually; pushes batch per the public-repo cadence (pre-authorised).

---

## 3. Physics → audio material map (S1)

A single pure function bridges the two independent enums (`surface_material.h:15-21`
mandates they stay independent — physics must not depend on audio):

```cpp
// engine/audio/occlusion_material_map.h
#pragma once
#include "physics/surface_material.h"   // include root is engine/
#include "audio/audio_occlusion.h"

namespace Vestige
{
/// Maps the physics surface tag of an occluding body to the audio occlusion
/// preset used for transmission + low-pass. Untagged geometry (Default) maps to
/// Concrete — a generic solid wall — so a level's plain, un-tagged walls still
/// occlude (closes the AX1 "sound passes through my new wall" gap without
/// requiring the level designer to tag every surface first).
inline AudioOcclusionMaterialPreset occlusionPresetForSurface(SurfaceMaterial m)
{
    switch (m)
    {
    case SurfaceMaterial::Stone: return AudioOcclusionMaterialPreset::Stone;
    case SurfaceMaterial::Wood:  return AudioOcclusionMaterialPreset::Wood;
    case SurfaceMaterial::Metal: return AudioOcclusionMaterialPreset::Metal;
    case SurfaceMaterial::Glass: return AudioOcclusionMaterialPreset::Glass;
    case SurfaceMaterial::Water: return AudioOcclusionMaterialPreset::Water;
    case SurfaceMaterial::Cloth: return AudioOcclusionMaterialPreset::Cloth;
    case SurfaceMaterial::Grass: return AudioOcclusionMaterialPreset::Cloth;    // thin, porous → soft
    case SurfaceMaterial::Sand:  return AudioOcclusionMaterialPreset::Concrete; // dense, absorptive
    case SurfaceMaterial::Dirt:  return AudioOcclusionMaterialPreset::Concrete;
    case SurfaceMaterial::Default: return AudioOcclusionMaterialPreset::Concrete;
    }
    // Unreachable today (all 10 enumerators cased → -Wswitch-clean); guards a
    // future append-only SurfaceMaterial member, mapping it to the generic wall.
    return AudioOcclusionMaterialPreset::Concrete;
}
} // namespace Vestige
```

Rationale for the non-1:1 rows (the audio enum has `Concrete`/`Air` the physics
enum lacks; the physics enum has `Sand`/`Grass`/`Dirt` the audio enum lacks):
`Grass`→`Cloth` (thin porous layer lets sound through, softly); `Sand`/`Dirt`→
`Concrete` (dense earth blocks well); `Default`→`Concrete` (the important one: a
generic solid wall preset for untagged level geometry). These are perceptual
mappings, not fitted curves — no Formula Workbench data exists for "sand as an
acoustic wall," so they are hand-picked with a **Rule-6 `TODO: revisit via Formula
Workbench`** marker if measured transmission-loss data ever appears. `Air` is never
a map target (a body that blocked a ray is by definition not air).

**Verify (S1):** table test — all 10 `SurfaceMaterial` members return a valid
preset; `Default`→`Concrete`; no member returns `Air`.

---

## 4. The occlusion driver (`AudioOcclusionSystem`)

### 4.1 Ray geometry — volumetric source sampling

Per audible spatial source, cast `N = occlusionRayCount` rays **from the listener
toward N points** sampled inside a small sphere (radius `occlusionSourceRadius`,
default 0.5 m) around the source position. Ray 0 is the exact source centre; rays
1..N-1 use entries 1..N-1 of a **compile-time constant offset table** — a
Fibonacci-sphere distribution over the source sphere. The table holds exactly
`kMaxOcclusionRayCount` (= 16, §5) entries — enough for the max ray count
*including* the centre (entry 0), so ray i reads entry i for i in 0..N-1 with no
overflow; it is not per-frame RNG. Stable offsets avoid shimmer and keep tests deterministic
(the S3/S4 tests pin the chosen offsets as goldens).

**Degenerate case:** if the source and listener are coincident (`length(target −
listenerPos) == 0`, a zero-length direction), the source is treated as unoccluded
(target fraction 0, no cast) rather than normalising a zero vector — the listener is
effectively *at* the source, so there is nothing to occlude.

- `fractionBlocked = blockedRayCount / N` ∈ [0,1]. All blocked → 1 (fully
  occluded); all clear → 0 (open LOS); mixed near an edge/doorway → partial.
- Each ray: `dir = normalize(target - listenerPos)`, `maxDistance =
  length(target - listenerPos)`. A hit **before** the target distance = blocked;
  a clear cast (or hit at/after target) = open.
- `ignoreBodyId` = the source entity's own physics body if it has one (avoids a
  source occluding itself). Sources without a body pass `{}`.

Single-ray (S2) is exactly `N==1` (ray 0 only) → binary fraction. So S2 and S3
share one code path; S3 only raises the default `N` and adds the offset set.

### 4.2 Scheduling — cast on workers, resolve on main

Because the Jolt world is **settled** during `PostCamera` (§0 frame-order anchor)
and narrow-phase `CastRay` is many-thread-safe on a non-stepping world (Jolt
contract), the ray casts run on MT2 workers:

```
per frame, in AudioOcclusionSystem::update (PostCamera, before AudioSystem):
  1. main: classify + build the work list. Walk every spatial source (the same
     set step 4 uses) and stamp each with a STATUS:
       • measured — eligible this frame (spatial + audible + in-range, §4.5) AND
         within the round-robin budget → gets rays cast;
       • deferred — eligible but skipped this frame by the round-robin;
       • culled   — not eligible (non-spatial excluded upstream, inaudible, or out
         of range).
     Only **measured** sources get rays: append each one's N rays to a preallocated
     flat ray array [{origin,dir,maxDist,ignoreBody}] and record that source's
     contiguous span [rayBegin, rayEnd). N varies with occlusionRayCount, so the
     span is stored PER SOURCE — never a fixed stride. A results array [{blocked,
     JPH::BodyID hitBody, float hitDist}] holds one slot per ray at the same indices.
  2. jobs: jobSystem.parallelFor(rayArray.size(), [&](begin,end){   // count == number of rays
            for i in [begin,end): results[i] = physicsWorld.rayCast(...);  // read-only, no lock held
         });
     jobSystem.wait(handle);        // helps run the work; returns when done
     // Zero measured sources (empty scene / all culled) ⇒ rayArray.size()==0:
     // parallelFor(0,…) returns a complete handle and wait() no-ops
     // (job_system.h:92, :97-99). Step 4 still runs, releasing lingering fractions.
  3. main (measure the measured sources): for each **measured** source, fold
     results[rayBegin..rayEnd) → measured fractionBlocked; pick the nearest
     blocking body (smallest hitDist); resolve its material via
     physicsWorld.getSurfaceMaterial(body) → occlusionPresetForSurface(...)
     (MAIN-THREAD-ONLY lock, safe here). Store these as the source's *target*
     (targetFraction + targetMaterial). Round-robin-deferred sources keep their
     previous target unchanged (§4.4).
  4. main (slew ALL spatial sources): iterate the SAME spatial-source set
     AudioSystem's compose loop walks — scene->forEachEntity over entities with an
     AudioSourceComponent (audio_system.cpp:162) — so "all spatial sources" is a
     defined set, not a description. Read each source's step-1 STATUS to pick its
     target: **measured** → the target from step 3; **deferred** → HOLD the previous
     target (fraction + material) unchanged; **culled** (or occlusion disabled) →
     target fraction 0. Then slew comp.occlusionFraction toward the target and snap
     comp.occlusionMaterial (§4.4). Running this over all sources — not just the
     measured set — is what releases a source that left the eligible set instead of
     freezing it muffled.
```

The worker closure calls **only** `rayCast` (no `getSurfaceMaterial` — that body
lock stays on the main thread per its contract, `physics_world.h:114-116`). Material
resolution touches only the *nearest blocking body per source* (≤ source count
lookups), not per ray.

**Synchronous mode** (`JobSystem` with `numWorkers==0`, used by tests): `parallelFor`
runs inline on the calling thread → identical results, fully deterministic, no
threads. The S4 test asserts sync-mode output equals the S3 main-thread output.

Within-frame `wait()` is chosen over a next-frame pipeline for simplicity (Rule 2):
the work list is small (§6) and parallelized, so the stall is sub-budget. **Escape
hatch (documented, not built):** if profiling ever shows `wait()` exceeding the
budget, switch to fire-and-forget `submit` + consume-last-frame via the frame-top
drain (`engine.cpp:1110`) — 1-frame occlusion latency (~16 ms) is imperceptible
because the fraction is already temporally smoothed (§4.4). The `results` buffers
are double-buffered-ready (preallocated, swap-and-reuse) to make that switch cheap.

### 4.3 Nearest-blocker material pick

Among a source's blocked rays, the occluder is the body with the **smallest
`hitDist`** (the closest wall to the listener along any sampled path). Its
`SurfaceMaterial` → preset becomes the source's `targetMaterial`. If no ray is
blocked, the target fraction is 0 and the material is irrelevant (held — at
`fraction==0`, `computeObstructionGain` returns `openGain` regardless of material).
The target fraction + material are always set as a pair, so they can never diverge
(§4.4).

### 4.4 Temporal smoothing (every spatial source, every frame)

Ray results flip discretely (a source crosses behind a pillar → measured fraction
jumps 0→1). To avoid a click, a measurement sets a per-source *target*; the stored
`comp.occlusionFraction` slews toward it each frame:

```
comp.occlusionFraction += (target - comp.occlusionFraction)
                          * clamp(dt * kOcclusionSlewPerSec, 0, 1);
```

**The slew pass runs for every spatial source each frame — not only the ray-eligible
ones** (§4.2 step 4). The target per source is:

| Source state this frame | target fraction | target material |
|-------------------------|-----------------|-----------------|
| eligible + serviced (rays cast) | freshly measured (§4.3) | nearest-blocker preset (§4.3) |
| eligible + round-robin-deferred | **held** (last measured) | **held** (last) |
| culled (out of range / inaudible) or occlusion disabled | **0** | irrelevant (held) |

Two consequences this design guarantees:

- A **culled/disabled** source targets 0, so the slew *releases* it to unoccluded
  rather than freezing it at its last (possibly muffled) value.
- A **deferred** source holds *both* fraction target and material, so they refresh
  together at the next service — no "fraction rising on stale rays with a stale
  material" window opens. `comp.occlusionMaterial` is snapped to `targetMaterial`
  in the same pass (it is only audible once `fraction` is audibly > 0, by which
  time both moved together).

`kOcclusionSlewPerSec` (default ≈ 8 /s → ~125 ms to converge) is a tuning constant
carrying a **Rule-6 `TODO: revisit via Formula Workbench`** marker (no reference
dataset). `comp.occlusionFraction` is the only smoothing state — no parallel map.

**Field ownership + slice boundary.** With `occlusionEnabled`, the driver *owns*
`comp.occlusionFraction` every frame — a scene-serialized value is only a frame-0
seed, overwritten by the first slew (that is the point: it replaces the old
hand-set model). And **S2** ships the slew with only the two-state target
(*measured* while eligible, *0* when not) — no round-robin, no deferral, every
source serviced each frame; the *held/deferred* target row of the table above
arrives with the budget + round-robin machinery in **S4**.

### 4.5 Culling + budget (60 FPS discipline)

Only these sources get **rays** each frame (all spatial sources still get the §4.4
slew pass regardless):

- **Spatial** (3D-positioned; excludes `playSound2D` / Ui-bus sources).
- **Audible** — currently playing (has a live voice this frame) **and** a
  *pre-compose* gain estimate above a small epsilon:
  `resolveSourceGain(mixer, comp.bus, comp.volume)` (the pure 3-arg overload,
  `audio_mixer.h:132`) — `master × bus × volume` with **no** occlusion or ducking
  folded in (`comp.volume` `audio_source_component.h:33`, `comp.bus` `:49`). It must
  NOT use the *full composed* gain from `composeAudioSourceAlState`, which folds
  *this frame's* occlusion — that runs later in `AudioSystem` (§0 frame order:
  occlusion is registered *before* AudioSystem), so it does not exist yet and using
  it would be circular. A stopped or effectively-silent source is skipped.
- **In range** — within `occlusionMaxDistance` of the listener (default 40 m).
  Beyond it, occlusion is imperceptible under distance attenuation, so no rays are
  cast; the source's target becomes 0 and the §4.4 slew releases it.

Rays/frame `= measuredSources × N`, each a narrow-phase cast (sub-µs to low-µs)
spread across workers. **Budget:** a `kMaxOcclusionRaysPerFrame` ceiling, default
**512** = the pool-max product `MAX_SOURCES × kMaxOcclusionRayCount` = `32 × 16`
(`MAX_SOURCES = 32`, `audio_engine.h:48`; `kMaxOcclusionRayCount = 16`, §5); Rule-6
`TODO: revisit via Formula Workbench`. At the settings maximum the offered load exactly meets the ceiling
(`⌈512/512⌉ = 1`), so **round-robin never engages within the shipped settings
ranges** — at the default `N=8` that is 256 rays, 2× headroom. It engages only if
`MAX_SOURCES` or the ray-count cap is raised *past* 512; then each source is
re-tested every `⌈need/512⌉` frames (a 2–3 frame refresh — inaudible, occlusion
changes slowly), holding its target between services (§4.4). Engagement is **logged
once** when it starts (Rule: no silent cap).

---

## 5. Settings + editor (S5)

Following the L5 field→JSON→ApplySink→ApplyTargets→panel chain (schema-version
bump + migration):

| Setting | Type | Default | Effect |
|---------|------|---------|--------|
| `occlusionEnabled` | bool | `true` | off → no rays are cast; every source's target becomes 0 and the §4.4 slew releases it to unoccluded within the slew time (nothing left muffled) |
| `occlusionRayCount` | int `[1, kMaxOcclusionRayCount]` | `8` | N in §4.1; `1` = binary. **`kMaxOcclusionRayCount = 16`** is the single source of truth shared by the §4.1 offset-table size and the §4.5 budget product — bump it in one place |
| `occlusionMaxDistance` | float | `40.0` | cull radius (§4.5) |
| `occlusionSourceRadius` | float | `0.5` | source-volume sampling radius (§4.1) |

Editor: an **Audio panel** debug toggle to draw the cast rays (green = clear, red =
blocked) via the existing debug-line path, labelling each blocked ray with
`occlusionMaterialLabel` (`audio_occlusion.h:80`) for the picked material, for tuning. `SurfaceMaterial` assignment
on bodies already exists from AX4's editor material picker — AX1 adds no new
per-body widget.

**Verify (S5):** settings JSON round-trip + old-schema migration adds the fields at
defaults; toggling `occlusionEnabled` off mid-run releases all sources to fraction
0 within the slew time; ray-count change takes effect next frame.

---

## 6. Performance (60 FPS hard floor)

All work is main-thread-scheduled but ray-parallel. Budget per frame @ 60 FPS
(16.6 ms):

| Cost | When | Estimate | Notes |
|------|------|----------|-------|
| Build work list (cull + flatten) | main, once | O(sources), branchy | no alloc — preallocated ray/result vectors reused (swap-and-clear) |
| **Ray casts** | workers, `parallelFor` | `eligibleRays × ~cast cost`, ÷ workers | narrow-phase `CastRay`; read-only on a settled world |
| `wait()` stall | main | ≈ parallel critical path | bounded by `kMaxOcclusionRaysPerFrame` (round-robin caps it) |
| Material resolve | main, post-wait | ≤ 1 body-locked lookup **per source** (not per ray) | `getSurfaceMaterial` |
| Smooth + write fields | main, **all spatial sources** | O(spatial sources), trivial | one lerp + material snap each; runs for every spatial source (§4.4), not only the ray-eligible ones |

**Budget knobs:** `kMaxOcclusionRaysPerFrame` bounds the parallel critical path
directly; `occlusionMaxDistance` + audibility culling bound the offered load;
`occlusionRayCount` trades fraction smoothness for cost. The worst case is a **fixed
ceiling** (round-robin absorbs overflow), so per-frame occlusion cost cannot spike
with scene complexity.

**Profiling gate (project perf rule):** a Release benchmark drives the **eligible
source pool at maximum** (`MAX_SOURCES = 32` audible spatial sources, all within
range, `N=8` → the 256-ray ceiling) and asserts the occlusion pass (build +
parallel casts + wait + resolve + write) stays **≤ 0.3 ms/frame**, measured
empirically (not hand-summed). Sanity estimate: 256 short narrow-phase casts at
~1–2 µs each ≈ 0.26–0.51 ms *serial*, spread across the worker pool
(`hw_concurrency − 1` ≈ 11 threads on the Ryzen 5 5600 dev CPU) → ~tens of µs of
critical path, plus the `wait()`/resolve/slew tail. So 0.3 ms is a headroom target,
not a tight bound; it carries a Rule-6 `TODO: revisit via Formula Workbench` marker
and is the **empirical** figure the benchmark asserts (not a derived guarantee). If
the measured pass ever approaches it, lower `kMaxOcclusionRaysPerFrame` (more
round-robin) or switch to the next-frame pipeline (§4.2 escape hatch).
*(`MAX_SOURCES = 32` is read fresh from `audio_engine.h:48`; the procedural-audio
doc had logged a stale line-cite for this constant, so it is verified here, not
recalled. The benchmark is **CPU-bound** — the casts run on CPU workers, not the
GPU.)*

---

## 7. Accessibility

- **Positive spatial-awareness aid:** correct occlusion helps all players (and
  especially low-vision players relying on audio) judge whether a sound is *in the
  room* vs *behind a wall* — a cue that was previously wrong unless a designer hand-
  tagged it. It routes through the existing Sfx/spatial pipeline → honours master/
  bus volume, AX9 loudness, AX8 mono-downmix.
- **No new motion / no screen effect** → photosensitive/vestibular N/A.
- **Deterministic, not RNG-driven** → no per-frame audio shimmer that could fatigue
  sensitive listeners (the ray offsets are a fixed point set, §4.1).
- The `occlusionEnabled` toggle lets a player who finds heavy muffling disorienting
  turn it off entirely (fields release to 0).

---

## 8. CPU / GPU placement (project Rule 7)

**All CPU.** Justification against the Rule 7 heuristic:

- **Ray casts:** branchy tree/broadphase traversal against Jolt's CPU-side BVH.
  Not per-pixel/per-vertex; a GPU dispatch would need the collision geometry
  mirrored on the GPU + a readback — far costlier than a few hundred CPU casts. The
  MT2 job system is the correct parallelism vehicle (CPU worker pool). → **CPU.**
- **Fraction fold, material map, smoothing, culling:** decision/branchy/sparse —
  textbook CPU.
- **No fitted formula** → no FW curve, no CPU/GPU dual impl, **no parity test owed**
  on that axis. The only hand-tuned constants (`kOcclusionSlewPerSec`, default ray
  count/radius, the `Sand/Grass/Dirt/Default` map rows) carry Rule-6
  `TODO: revisit via Formula Workbench` markers pending reference data; none is a
  runtime approximation of a reference curve.

---

## 9. References

1. **Valve, *Steam Audio* SDK documentation — "Direct Sound" / occlusion &
   transmission model** (Apache-2.0; source readable as the algorithmic spec). The
   volumetric source-sampling occlusion (rays from listener to points on a sphere
   around the source, fraction = fraction of rays reaching it) mirrors Steam Audio's
   volumetric occlusion — the Steam Audio direct-sound model the AX1 roadmap bullet
   names.
2. **Jolt Physics v5.3.0 — `NarrowPhaseQuery::CastRay` threading contract**
   (multi-thread read safety on a non-stepping system). Vendored version pinned in
   `THIRD_PARTY_NOTICES.md:35` (MIT); the guarantee AX1's worker-cast plan rests on
   (§4.2).
3. **AX4 procedural-audio design** (`docs/phases/phase_10_procedural_audio_design.md`)
   — established `SurfaceMaterial` + Jolt body user-data packing that AX1's map
   consumes.

> **Diffraction note (Rule 13 honesty):** the AX1 roadmap bullet also names
> "diffraction edge." Steam Audio's diffraction needs occluder edge geometry +
> surface normals; `PhysicsWorld::rayCast` exposes neither (body id + distance
> only, `physics_world.cpp:803`). AX1 therefore ships *graded* occlusion (the
> multi-ray fraction approximates partial paths near edges perceptually) but **not**
> a modeled diffraction filter. A normal-returning cast overload is the clean
> future enabler — recorded as a non-goal (§1), not faked.

---

## 10. Resolved decisions

- **Dedicated `AudioOcclusionSystem`, not a hook inside `AudioSystem`** — keeps
  `AudioSystem`/`AudioEngine` free of a physics dependency, honouring the
  "geometry-blind pure layer" contract in `audio_occlusion.h:8-28`. Runs in
  `PostCamera` before `AudioSystem` via registration order (stable-sort).
- **Cast on workers, resolve materials on main** — the only design that respects
  both the Jolt query threading contract (casts parallel-safe) and the
  `getSurfaceMaterial` body-lock main-thread-only contract (`physics_world.h:114-116`).
- **Volumetric multi-ray fraction**, ray 0 = centre so `N==1` degrades to binary —
  S2 and S3 share one path; the fraction is the roadmap's "open-path fraction."
- **Deterministic fixed ray-offset set**, not per-frame RNG — stable (no shimmer),
  testable, accessibility-friendly (§7).
- **`Default`→`Concrete` in the material map** — untagged level walls occlude
  without designer effort; this is the literal roadmap gap being closed.
- **Slew every spatial source toward a per-source target every frame** (§4.4), not
  only the ray-eligible ones — a culled/disabled source targets 0 and is *released*
  to unoccluded rather than frozen muffled; a round-robin-deferred source *holds*
  both its fraction target and its material so they never diverge. State lives in
  the existing `occlusionFraction` field + a small per-source target; no parallel
  smoothing map.
- **Ray→source mapping is an explicit per-source `[rayBegin, rayEnd)` span** in the
  work list (not a fixed stride) because `occlusionRayCount` is user-tunable and
  round-robin varies which sources are cast (§4.2).
- **Within-frame `parallelFor`+`wait`, next-frame pipeline as a documented escape
  hatch** — simplest correct now; the switch is cheap because results buffers are
  preallocated/swap-ready.
- **Round-robin amortization over a hard ray-budget, logged when engaged** — bounds
  worst-case cost without a silent cap (project workaround-logging rule).
- **Edge diffraction is a non-goal** with a stated API-gap reason (no normal from
  `rayCast`), not a faked delay.
- **No Formula Workbench curve** — AX1 is geometry, not a fitted formula; the
  existing `computeObstructionGain/LowPass` already own the DSP math. Hand-tuned
  constants get Rule-6 TODO markers.

---

## 11. Cold-eyes loop log

> Per global Rule 14 + project Rule 9, this doc runs through `/cold-eyes` (fresh
> subagent, no authoring context), looped until a pass returns zero verified
> actionable findings. Each loop runs cold (no briefing on prior findings).

| Loop | Date | Verdict | Findings actioned |
|------|------|---------|-------------------|
| 1 | 2026-07-01 | NOT ready — 0 CRITICAL / 2 HIGH / 3 MEDIUM / 6 LOW·INFO (2 parallel cold reviewers: accuracy + logic) | **H1** culled/round-robin-deferred sources froze at their last (muffled) fraction — the slew was only wired for the ray-eligible set → §4.2 split into *measure eligible* (step 3) + *slew ALL spatial sources toward a per-source target* (step 4); §4.4 rebuilt around a target table (measured / held / zero). **H2** `occlusionEnabled` off said "frozen to 0" (instant) in §5 table but "within the slew time" (gradual) in the verify cell → unified to slewed release-to-0 (§5). **M1** ray→source index mapping was unspecified with a flat `parallelFor(rayCount)` → per-source `[rayBegin,rayEnd)` spans pinned (§4.2, §10). **M2** perf gate had no numeric budget → `MAX_SOURCES=32` (`audio_engine.h:48`), 256-ray ceiling, `kMaxOcclusionRaysPerFrame=512`, gate `≤0.3 ms/frame` (§4.5/§6). **M3** "material switches instantly" clashed with round-robin → deferred sources hold fraction+material together (§4.3/§4.4). **Accuracy L:** `getSurfaceMaterial` contract cite `:118-121`→`:114-116` (×3); `AL_GAIN` `:1188`→`:1189`; compose-loop `:163`→`:162` (+call sites); listener `:82-86`→`:82-87`; audio test count 18→25; `isSynchronous` wording (scheduler-absent, not a `numWorkers` field read). No CRITICAL/HIGH accuracy findings — load-bearing symbols all verified correct. |
| 2 | 2026-07-01 | NOT ready — 0 CRITICAL / 2 HIGH / 4 MEDIUM / 4 LOW·INFO (2 cold reviewers; **no Loop-1 finding resurfaced** — the fixes held). Deeper issues on a now-consistent doc. | **H2 (new)** audibility cull read "resolved gain," which does not exist at occlusion time (occlusion runs *before* AudioSystem composes it) → cull re-pinned to a *pre-compose* estimate `master × bus × comp.volume` (§4.5). **H1 (new)** S2's "smoothing" verify referenced the §4.4 target-table's deferred/culled states that only exist from S4 → §4.4 now states the slice boundary (S2 = two-state measured/0; deferred row lands in S4). **M-acc** `composeAudioSourceAlState` occlusion cite `:84-105` split into gain-fold `:84-88` + `lowPassGainHf` `:102-105`. **M1** serialized `occlusionFraction` vs driver ownership → §4.4 states the driver owns it when enabled (serialized = frame-0 seed). **M2** round-robin "2–3 frame refresh" was unreachable at the 512 default → §4.5 states it never engages at defaults (⌈256/512⌉=1). **M3** 0.3 ms gate lacked worked arithmetic → §6 shows 256 casts × ~1–2 µs ÷ ~11 workers, marks the gate empirical, and **corrects the dev box to the Ryzen 5 5600 CPU (not the RX 6600 GPU)** — the benchmark is CPU-bound. **L:** struct default `=1.0f/=0.0f`; §4.1 offset set named (Fibonacci-sphere, tests pin goldens); §3 append-only `-Wswitch` guard comment; §5 debug viz names `occlusionMaterialLabel`. |
| 3 | 2026-07-01 | NOT ready — 0 CRITICAL / 1 HIGH / 2 MEDIUM / 5 LOW·INFO (2 cold reviewers; **accuracy lane CLEAN — 0 C/H/M, all ~40 citations re-verified**; no Loop-1/2 finding resurfaced). Converging — findings now shallower. | **H1** the pre-compose cull "`master × bus × comp.volume`" didn't name a helper → an implementer could pick `getBusGain` (drops Master); re-pinned to the pure `resolveSourceGain(mixer, comp.bus, comp.volume)` (`audio_mixer.h:132`) which is exactly `master×bus×volume` with no occlusion/ducking (§4.5). **M2** §4.2-step-4 "slew ALL spatial sources" had no enumeration contract → named the set (same `scene->forEachEntity` `AudioSourceComponent` walk as `audio_system.cpp:162`). **M1** `occlusionRayCount` max is 16 but the budget math assumed N≤8 → §4.1 pins the offset table at ≥16 entries; §4.5 states 512 = the `32×16` pool-max product (round-robin never engages even at settings max). **L:** S2 verify adds the no-physics-body (`ignoreBodyId={}`) case; §4.1 adds the coincident source/listener degenerate (zero-length dir → unoccluded, no cast); §9 ref-1 wording softened ("direct-sound model," not "Volumetric mode named in the bullet"); §0 `sortByUpdatePhase` attributed to `system_registry.cpp` (enum stays `i_system.h`); §0 notes the legacy 4-arg `rayCast` at `:780`. |
| 4 | 2026-07-01 | NOT ready — 0 CRITICAL / 1 HIGH / 3 MEDIUM / 2 LOW·INFO (2 cold reviewers; **accuracy lane again CLEAN — 0 C/H, all citations re-verified**; no prior-loop finding resurfaced). Findings now wording/edge-case precision. | **H1** §4.2 step 4 distinguished *deferred* (hold) from *culled* (zero) targets but never said HOW → step 1 now stamps each spatial source with an explicit `{measured\|deferred\|culled}` STATUS the slew reads (deferred→hold, culled→0), closing the last path back to muffle-flicker. **M1 (empty scene)** zero-eligible-rays path undefined → §4.2 states `rayArray.size()==0` ⇒ skip submit+`wait` (`parallelFor(0)`→complete handle, `wait` no-ops, `job_system.h:92/97-99`); step-4 slew still runs. **M2 (budget prose)** §4.5 round-robin paragraph restated the conclusion 3× with a boundary-vs-past muddle → collapsed to one clean statement (`⌈512/512⌉=1`, never engages within shipped ranges). **M-acc** bare `engine.*`/`system_registry.cpp` cites lacked the `engine/core/` prefix (line numbers all correct) → §0 adds a one-line basename-path note. **L:** §9 ref-2 pins **Jolt v5.3.0** (`THIRD_PARTY_NOTICES.md:35`); §4.1 offset-table off-by-one clarified (≥16 entries covers N=16 incl. centre/entry 0). |
| 5 | 2026-07-01 | **CLEAN — ready to implement.** Logic lane: 0 CRITICAL / 0 HIGH / 0 MEDIUM, explicit "READY, recommend sign-off." Accuracy lane: **zero findings** — all ~40 citations independently re-verified correct (corroborates Loops 3/4). No prior-loop finding resurfaced across all 5 loops. | Two trivial LOW polish items, both fixed: **L2** §4.2 step 3 "for each *eligible* source" → "*measured*" (matches the step-1 STATUS vocabulary). **L1** introduced **`kMaxOcclusionRayCount = 16`** as the single source of truth shared by the §4.1 offset-table size, the §4.5 budget product, and the §5 settings clamp (so a future bump can't desync them). The step-1 STATUS → step-4 slew wiring (the historically fragile part) verified airtight. **Convergence reached — sign-off per the delegated gate.** |
