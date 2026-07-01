# Phase 10 — Procedural / Material-Aware Audio (Design Doc)

**Status:** ✅ Signed off for implementation (2026-06-30). Cold-eyes looped to clean
(6 loops; sign-off delegated per the session standing instruction — Loop 6 returned
zero CRITICAL/HIGH/MEDIUM/LOW, two non-actionable INFO notes only). See the
Cold-eyes loop log at §15.
**Scope (user-selected "Bundle B — full AX4 + foundation"):** material-aware
**footstep**, **weapon-impact**, and **object-collision** audio synthesised from
the *surface material* + *impact velocity* instead of hand-authored per-surface
clips. Because the trigger plumbing AX4 implies does **not exist yet**, this
bundle also builds the enabling foundation:

- **S1** Formula Workbench `audio` template category (closes ROADMAP `[3D_E-0022]`)
  — so the impact/footstep curves are authored/fitted/exported through the
  Workbench (project Rule 6), not hand-coded.
- **S2** rigid-body **surface-material** tag + enum (none exists today).
- **S3** Jolt **`ContactListener` + collision-event bus** (only *stubs* exist; the
  scripting layer is already waiting on this — S8 bonus-unblocks it).
- **S4–S5** the **procedural synthesis core** (modal + PhISEM) + variation layer.
- **S6** footstep emission; **S7** impact/collision emission; **S8** scripting wire-up;
  **S9** settings/editor; **S10** audit/parity/changelog/roadmap.

**Roadmap items covered:** `AX4. Procedural footstep / impact audio` (§`audio-system`);
the **Sound material interactions** GFM bullet in §`phase-9c-new-domain-systems-foundations-shipped`
("footstep sounds derived from physics material types — deferred to Phase 10");
and `[3D_E-0022]` FW audio category (§`audio-dsp-formula-coverage…`). **Bonus
unblock:** scripting `OnCollisionEnter` / `OnCollisionExit` (stubs today).

> **Note on the Sound-material bullet's locator (cold-eyes C1):** it has **no
> bracket ID in the file** — it is a GFM checkbox whose Ants *synthetic* id is
> `9zpnclbt6d`. Synthetic ids are **not flip-locatable** (`roadmap_log` refuses
> them with `synthetic_id_not_locatable`). S10 therefore flips it by **`headline`
> / `line_range`** locator, not by `9zpnclbt6d`. Likewise `[3D_E-0022]` is a
> digit-leading bracket ID that this project's tooling cannot resolve via `id=`
> lookup (logged separately as an Ants-MCP bug) — S10 **annotates** it by headline
> (substantially delivered; K-weighting sub-item deferred — §9), it is not flipped ✅.

**References:** see §13 (Cook *Real Sound Synthesis* / PhISM; Lloyd et al. I3D 2011;
Turchet & Nordahl footstep synthesis; ISO 9613-1 / ITU-R BS.1770 for the seed FW
templates). All algorithms are textbook/published — no proprietary code.

**Contents:** §0 what exists · §1 goals/non-goals · §2 slice plan & order ·
§3 surface-material model · §4 collision-event bus · §5 synthesis core (modal+PhISEM) ·
§6 material sound banks (JSON) · §7 footstep emission · §8 impact/collision emission ·
§9 Formula Workbench audio category · §10 performance · §11 accessibility ·
§12 CPU/GPU placement · §13 references · §14 resolved decisions · §15 cold-eyes log.

---

## 0. What already exists (reality check, verified 2026-06-30)

Every claim below was read out of current source this session (global Rule 13 — no
recall). File:line citations are the authoritative anchor for the implementer.

| Subsystem | Where | Notes relevant to this bundle |
|-----------|-------|-------------------------------|
| One-shot play API | `engine/audio/audio_engine.h:230` `playSound(filePath,pos,vol,loop,bus,priority)`; `:242`/`:260` `playSoundSpatial` (+velocity overload); `:276` `playSound2D` | All return an OpenAL source id (`unsigned`, 0 on fail). Impl `audio_engine.cpp:579/630/675`. **Pitch is *not* a `playSound*` arg** — it is only uploaded via `applySourceState` from `AudioSourceAlState.pitch` (`audio_engine.cpp:1097`, `alSourcef(src,AL_PITCH,…)`). |
| Buffer cache + loudness fold | `audio_engine.cpp:611` (`volume *= loudnessMakeupForPath(...)`), `loadBuffer` measures LUFS; `loudnessMakeupForPath` keys on a **file path** (`audio_engine.cpp:475`) | New synth voices reuse `acquireSource(priority)` (`audio_engine.h:203`) + the bus/gain pipeline. **AX9 LUFS makeup does NOT apply to synth voices** — a generated buffer has no path key, so `loudnessMakeupForPath` returns unity. Synth loudness is governed by `impactLoudnessGain` (§9) + the bus/gain pipeline instead — by design (Loop-5 M). |
| Bus / gain | `engine/audio/audio_mixer.h:66` `AudioBus{Master,Music,Voice,Sfx,Ambient,Ui}`; `:132` `resolveSourceGain` (+`:145` 4-arg duck) | Footsteps/impacts play on **`Sfx`**. Final gain = master×bus×sourceVolume×clamp01(duck). Pure fn. |
| ECS audio path | `engine/audio/audio_source_component.h:24` (`AudioSourceComponent`, `.pitch` at `:52`); `engine/audio/audio_source_state.h:122` `composeAudioSourceAlState(...)`; `engine/systems/audio_system.cpp:70` `update`, `:227/:233` autoPlay, `:250/:269` compose+apply | Gameplay/script one-shots call `playSound*` **directly** and stay "untracked-by-entity" (`audio_system.cpp:212-215`) — caller owns lifetime. **This is the emission hook** for footsteps/impacts (no new component needed for the sound itself). |
| **Physics surface material** | **DOES NOT EXIST** | `RigidBody` (`engine/physics/rigid_body.h:44`) has only scalar `friction=0.5` (`:56`) + `restitution=0.3` (`:57`). No surface enum, no material id, no Jolt body user-data accessor exposed. **"foot hit STONE vs WOOD" is unknowable today** → S2 adds it. |
| **Collision / contact events** | **STUBS ONLY** | No Jolt `ContactListener` registered (`physics_world.cpp` never sets one). Scripting `OnCollisionEnter`/`Exit` are `"(Stub) … pending"` (`engine/scripting/event_nodes.cpp:197-223`, `scripting_system.cpp:452-463`, `script_compiler.cpp:139`). → S3 builds the real bus. |
| Raycast (exists) | `engine/physics/physics_world.h:204` `rayCast(origin,dir,outBodyId,outFraction)`; **`:224`** preferred `rayCast(origin,unitDir,maxDistance,outBodyId,outHitDistance,ignoreBodyId=)` (the `ignoreBodyId` param is at `:227`); `:230` `sphereCast` | Returns the **hit `JPH::BodyID`** → S7 weapon-impact = ray hit → body → material. (S6 footstep does **not** use a ray — it reads the `CharacterVirtual` ground body, §7.) |
| Character controller is `CharacterVirtual` | `engine/physics/physics_character_controller.h:98` `JPH::Ref<JPH::CharacterVirtual> m_character`; `:80/:83/:77` `isOnGround`/`isOnSteepGround`/`getLinearVelocity` | **Not a broadphase body** (no `BodyID`). S6 reads the surface under the foot from `CharacterVirtual::GetGroundBodyID()` → `getSurfaceMaterial` — no raycast needed. |
| Character ground state | `engine/physics/physics_character_controller.h:80` `isOnGround()`, `:83` `isOnSteepGround()`, `:77` `getLinearVelocity()`; owned by `CharacterSystem` (`engine/systems/character_system.h:22`) | `character_system.cpp` emits **no audio / no footstep / no ground-transition** today (grep empty). → S6 derives foot-plant from `isOnGround()` edge + horizontal speed + stride accumulator. |
| Variation precedent | `engine/audio/audio_ambient.h:110` `RandomOneShotScheduler`, `:132` `tickRandomOneShot(scheduler,dt,sampleFn)` | **Timing-only, ambient bed.** No pitch/gain jitter, no round-robin for SFX (grep empty). The **injectable-uniform-`[0,1]`-sample** pattern (deterministic in tests, `std::uniform_real_distribution` in engine) is the precedent S5 mirrors. |
| Formula→engine pipeline | `tools/formula_workbench/workbench.h:83` `exportFormula`; `engine/formula/codegen_cpp.h:32` `generateFunction`, `:39` `generateHeader` (coeffs inlined as `float` literals — "zero runtime overhead"); `engine/formula/codegen_glsl.*` | **Category is a freeform string** (`FormulaDefinition.category`, e.g. `physics_templates.cpp:105` `def.category="wind"`). Adding `audio` = a new `engine/formula/audio_templates.cpp` mirroring `physics_templates.cpp`. Library: `engine/formula/formula_library.h:37` `findByCategory`. |
| FW audio gap (tracked) | `ROADMAP.md:179-196` `### Audio-DSP formula coverage`; `[3D_E-0022]` planned | 14 existing categories, **no `audio`**. Seeds named: ISO 9613-1 absorption poly, ITU-R BS.1770 K-weighting. → S1 closes it and adds the procedural-audio curves. |
| Occlusion-material pattern | `engine/audio/audio_occlusion.h:44` `AudioOcclusionMaterialPreset{Air,Cloth,Wood,Glass,Stone,Concrete,Metal,Water}` (**8 members**, verified `:46-53`), `:75` `occlusionMaterialFor`, `:80` label; carried on `audio_source_component.h:90` | **Only existing surface-type→audio map.** S2's `SurfaceMaterial` enum mirrors this *shape* (dense stable int + `…For(enum)` table + label) but is a **different member set** (adds Sand/Grass/Dirt for footing surfaces; the two are mapped, not equal — see §3). |
| Settings add-pattern (L5) | `engine/core/settings.h` `AudioSettings`; `settings.cpp` toJson/fromJson/validate; `settings_apply.h` `AudioApplySink`; `settings_editor.h` `ApplyTargets`; `engine/editor/panels/audio_panel.*` | Field → JSON → ApplySink → ApplyTargets → panel, with schema-version migration. S9 follows it for the procedural-audio toggle/volumes. |
| Test pattern | `tests/test_audio_*.cpp` (18 files) | Pure-fn numeric (`EXPECT_NEAR`, `kEps=1e-4`); headless (`AudioEngine::isAvailable()` gates AL calls); mock `ApplySink`. Synth/PhISEM tested as **pure PCM-producing functions** with **injected RNG** (deterministic). |

---

## 1. Goals & non-goals

**Goals**

- Replace hand-authored per-surface clips with **synthesis from (surface material,
  impact velocity)** for footsteps, weapon-impacts, and object-collisions. Target
  ~10× footstep/impact asset-budget cut (ship tiny JSON banks, not WAV sets).
- Build the **minimum** missing foundation to make that possible: surface-material
  tags (S2) and a real collision-event bus (S3) — nothing speculative beyond it.
- Route **all** numeric design (impact→loudness, impact→pitch, aggregate event
  rate) through the **Formula Workbench `audio` category** (project Rule 6), with a
  CPU↔runtime **parity test** pinning the exported approximation (Rule 7).
- Stay inside the **60 FPS hard floor**. Audio + the new systems run on the main
  thread; per-frame cost is budgeted in §10. Synthesis is **per-strike, sparse**
  (a few events/sec), not per-frame.
- Each slice = one commit + its own tests, matching the Phase 10 audio cadence.

**Non-goals (explicit, with reason)**

- **No continuous/looping synthesis** (engine hum, wind, friction-drag tones). This
  bundle is **event-driven impacts only**. Sliding/scraping friction audio is a
  noted follow-up (Turchet's "brushing" model) — not in scope.
- **No reverb/occlusion changes.** Synthesised one-shots pass through the *existing*
  Sfx bus → existing reverb/occlusion/air-absorption/LOD untouched.
- **No physics-thread audio.** OpenAL calls stay main-thread; the contact listener
  only **enqueues** (§4).
- **No GPU DSP.** All synthesis is CPU (§12) — sparse, cheap, and branchy.
- **No new music/AI-Director coupling** (that's AX10/AX14).
- **3D physics only.** The collision-event work (S3) targets the Jolt 3D world;
  the engine has no general 2D rigid-body physics (only `collider_2d_component`
  trigger flags, no solver), so 2D collision audio is **N/A**, not deferred.

---

## 2. Slice plan & order (dependency-respecting, simplest-first)

Order is chosen so each slice is independently testable and the runtime audio
work lands **after** its foundation. S1 is pure-tool (no engine-runtime risk) and
ships first so the synth core consumes Workbench-fit curves from day one.

| # | Slice | New / touched | Depends on | Verify |
|---|-------|---------------|-----------|--------|
| **S1** | FW `audio` category + procedural-audio curves (`[3D_E-0022]`) | `engine/formula/audio_templates.{h,cpp}`, library registration, export → `engine/audio/procedural/audio_curves_generated.h` | — | reference-harness regression locks; parity test vs reference values |
| **S2** | Surface-material tag + enum | `engine/physics/surface_material.h`, `rigid_body.h` (+field), `physics_world.{h,cpp}` (set/get via Jolt body user-data) | — | set→get round-trip; default = `Default`; user-data pack/unpack |
| **S3** | Collision-event bus | `physics_world.cpp` (register `ContactListener`), `engine/physics/contact_event.h`, thread-safe pending queue + main-thread drain → EventBus `CollisionEvent` | S2 | two bodies collide → 1 event w/ bodies+point+normal-velocity; impulse-threshold filter; per-pair throttle |
| **S4** | Synthesis core (modal + PhISEM) | `engine/audio/procedural/modal_synth.{h,cpp}`, `phisem.{h,cpp}` | S1 | modal sum = finite, monotonically-decaying PCM; PhISEM grain count ≈ rate×dur; deterministic w/ injected RNG |
| **S5** | Material banks + variation + play glue | `engine/audio/procedural/material_sound_bank.{h,cpp}`, `assets/audio/synthesis/footstep_modal.json`, synth-buffer ring on `AudioEngine` | S4 | JSON parse → bank; round-robin + pitch/gain jitter deterministic; `playSynth(mat, approachSpeed, pos, envelopeScale)` returns a live source |
| **S6** | Footstep emission | `engine/systems/footstep_system.{h,cpp}` (or extend `CharacterSystem`) | S2,S5 | stride accumulator fires at expected cadence vs speed; landing edge → louder strike; ground-body→material lookup (`GetGroundBodyID`, no ray) |
| **S7** | Impact / collision emission | subscribe to S3 bus; weapon-impact via raycast-hit path | S3,S5 | collision above threshold → 1 impact at correct material+loudness; sub-threshold silent; weapon ray hit → impact |
| **S8** | Scripting `OnCollisionEnter/Exit` wire-up (bonus) | `event_nodes.cpp`, `scripting_system.cpp`, `script_compiler.cpp` | S3 | script node fires on `CollisionEvent`; enter/exit pairing |
| **S9** | Settings + editor | `settings.*`, `settings_apply.*`, `settings_editor.*`, audio panel; surface-material assign widget + debug panel | S5,S6,S7 | settings round-trip + schema migration; toggle mutes emission; editor assigns material |
| **S10** | Audit, parity, CHANGELOG, ROADMAP flips, push | — | all | AUDIT_STANDARDS 5-tier clean; parity green; AX4 + the Sound-material GFM bullet flipped ✅; `[3D_E-0022]` **annotated** (substantially delivered; K-weighting deferred — §9) |

Slices commit individually; pushes batch per the public-repo cadence (pre-authorised).

---

## 3. Surface-material model (S2)

A **dense, stable enum** mirroring the occlusion-preset pattern
(`audio_occlusion.h:44`):

```cpp
// engine/physics/surface_material.h
enum class SurfaceMaterial : std::uint8_t
{
    Default = 0,   // unknown / untagged → generic dull thud bank
    Stone, Wood, Metal, Cloth, Sand, Water, Grass, Dirt, Glass
};                  // append-only; persisted as the int value
```

**Where it lives at runtime.** Stored in the **Jolt body user-data** (`uint64`),
which is **entirely unused today** (verified: `createStaticBody`/`createDynamicBody`/
`createKinematicBody` at `physics_world.cpp:212/228/252` never set `mUserData` — so
there is no collision with an existing entity tag, but equally **no BodyID→Entity
map exists yet**). Rationale for user-data over a side map: it is O(1), travels
with the body, and is **safe to read inside the contact callback** (which runs on
Jolt job threads — a side `std::unordered_map` mutated on the main thread would
not be).

**Packed layout** (resolves cold-eyes C2 + C3 — the scripting bridge S8 needs an
entity id, not just a material):

```
user-data (uint64):  [63..40 reserved(0) | 39..8 entityId (u32) | 7..0 SurfaceMaterial (u8)]
                      ^^^^^^^^^^^^^^^^^^^^ deliberate headroom (e.g. a future flags byte); zeroed for now
```

**Entity handle is `uint32_t`, NOT the `Entity` class (cold-eyes C-1).** `Entity`
(`engine/scene/entity.h:72`) is a heavyweight, **non-copyable** scene-graph object
whose numeric id is `uint32_t getId()` (`entity.h:81`, "never 0"). It cannot be
stored by value in a POD or compared to `0`. The packed user-data and both event
structs therefore carry a plain entity **id**:

```cpp
using EntityId = std::uint32_t;   // 0 == none (Entity::getId() is never 0)
```

`RigidBody` gains a `SurfaceMaterial surfaceMaterial = SurfaceMaterial::Default;`
authoring field (`rigid_body.h`). **Set post-create, not via a new `create*Body`
signature** (the three creators stay untouched — C2). The bridge call site is
**`RigidBody::createBody` (`rigid_body.cpp:138-144`)** — a `RigidBody` is a
`Component`, so it reaches its owner via `getOwner()->getId()`; right after the
`PhysicsWorld::create*Body` call it invokes the new
`PhysicsWorld::setBodyTags(bodyId, entityId, material)` →
`bodyInterface.SetUserData(bodyId, pack(entityId, material))`. Bodies created
outside a `RigidBody` (e.g. the static world geometry at `engine/core/engine.cpp:1903/1909`)
get entity id **0** — S8 fires their collision events with a null entity (explicit,
not faked). Accessors:

```cpp
void            PhysicsWorld::setBodyTags(JPH::BodyID, EntityId, SurfaceMaterial); // main thread (uses BodyInterface)
SurfaceMaterial PhysicsWorld::getSurfaceMaterial(JPH::BodyID) const; // MAIN THREAD ONLY — Default if unset/invalid
EntityId        PhysicsWorld::getBodyEntity(JPH::BodyID) const;      // MAIN THREAD ONLY — 0 == none
```

**Two read paths — do not cross them (cold-eyes C1).** The `JPH::BodyID` accessors
above go through `BodyInterface::GetUserData(BodyID)`, which **takes a body lock** —
legal on the main thread (editor inspector, debug panel), **illegal inside a
contact callback** (Jolt: "this callback is called when all bodies are locked, so
don't use any locking functions"). Inside the contact callback (§4) you have the
already-locked `const Body&` refs, so you read user-data **directly and lock-free**
via `inBody.GetUserData()` and unpack with the same `unpackEntity`/`unpackMaterial`
helpers. The §3 `BodyID` accessors are the main-thread convenience wrappers; the
callback never calls them.

**Setting it (M3).** The post-create `setBodyTags` (main thread, via
`BodyInterface::SetUserData`) is used because `RigidBody::createBody` runs after the
`PhysicsWorld::create*Body` call returns only a `BodyID`. Considered-and-noted
alternative: set `BodyCreationSettings.mUserData` *before* `CreateAndAddBody` inside
the three creators — fewer calls, but it would change their signatures (rejected per
C2) and the static-geometry path (`engine.cpp:1903`) needs the post-create setter
regardless. One setter path is simpler than two.

**Acoustic parameters do not live here** — the enum is only an index; the *sound*
is the bank JSON keyed by it (§6), keeping physics and audio decoupled.

**`SurfaceMaterial` (physics) vs `AudioOcclusionMaterialPreset` (audio) (M2).**
They overlap but are not equal — occlusion describes *what a wall is made of for
sound to pass through*; `SurfaceMaterial` describes *what a floor/object is made of
when struck*. A small `occlusionPresetFor(SurfaceMaterial)` helper maps the shared
members (Stone/Wood/Metal/Glass/Water/Cloth) for any future code that wants both;
the two enums stay independent so each can add members the other doesn't need
(`SurfaceMaterial` adds Sand/Grass/Dirt; occlusion has Concrete/Air).

---

## 4. Collision-event bus (S3)

> **As-built note (S3, 2026-06-30).** The shipped listener is simpler than the
> Enter-path below in two deliberate ways, decided during implementation:
> (1) the **min-speed threshold is applied by the S7 audio subscriber, not in
> the listener** — the bus publishes every contact onset (with `approachSpeed`)
> so a sub-threshold touch still raises a *scripting* enter (S8) while playing
> no sound; (2) **`OnContactPersisted` is ignored and there is no per-pair
> throttle** — one Enter per `OnContactAdded`, one Exit per `OnContactRemoved`.
> Jolt guarantees a matching removal for every add, so Enter/Exit stay balanced
> for the scripting nodes (a thresholded Enter with an un-thresholded Exit would
> not be). Emission is naturally bounded by the contact-*onset* rate rather than
> the per-frame contact count, so the throttle is unnecessary; per-frame synth
> cost is still bounded by the S7 budget. See `engine/physics/contact_event.h`.

**The single biggest missing piece.** Register one `ContactListener` via
`PhysicsSystem::SetContactListener`. **Jolt 5.3.0 contract (verified against the
pinned FetchContent `ContactListener.h:66-116`):**

- Callbacks run **on multiple job threads simultaneously**; the header states you
  are **"only allowed to read from the bodies and can't change physics state"** —
  so reading body velocity + user-data inside the callback is *explicitly
  sanctioned* (resolves the H-2/H-3 thread-safety questions). The listener must
  **not** call `BodyInterface` or mutate state.
- `OnContactAdded(const Body& b1, const Body& b2, const ContactManifold& m, ContactSettings&)`
  and `OnContactPersisted(…)` get **both body refs + the manifold** (normal +
  contact points). **Coordinate space matters:** the `ContactManifold` stores its
  points *relative to `mBaseOffset`*, while `Body::GetPointVelocity` takes a
  **world-space** point — so use `p = manifold.GetWorldSpaceContactPointOn1(0)` (the
  manifold also carries a multi-point array; the **first/deepest point** is used,
  not an average — sufficient for a loudness proxy) and `normal =
  manifold.mWorldSpaceNormal`. Then `approachSpeed = |dot(b1.GetPointVelocity(p) −
  b2.GetPointVelocity(p), normal)|`, read from the **already-locked `Body&` refs**,
  never via a `BodyInterface` accessor (§3 C1). Material/entity likewise come from
  `b1.GetUserData()` / `b2.GetUserData()` directly (lock-free). *(Implementer:
  confirm `GetWorldSpaceContactPointOn1` against the Jolt 5.3.0 `ContactManifold`.)*
  - **`EstimateCollisionResponse` considered (H1):** the Jolt header's own
    `OnContactAdded` comment recommends `EstimateCollisionResponse` to derive a
    resolved impulse for impact-sound volume. We **deliberately use the cheaper
    pre-resolution velocity proxy** instead (Rule 5): it needs no solver pass, the
    callback stays minimal, and perceptual loudness maps fine from approach velocity
    (loudness JND ≈ 1 dB). Revisit only if a material needs true impulse.
  - *(Implementer: confirm the exact `Body::GetPointVelocity` / `GetUserData`
    spellings against the Jolt 5.3.0 headers — the FetchContent source is ephemeral;
    standard accessors, but verify, same posture the quick-wins doc took for OpenAL
    tokens.)*
- **`OnContactRemoved(const SubShapeIDPair&)` gets NO body access at all** (header
  lines 107-116: the bodies "may have been removed and destroyed"). So **Exit
  events cannot carry point/velocity/material** — only the cached entity pair.

**Enter path (OnContactAdded/Persisted):** compute `approachSpeed`, apply the
**min-speed threshold**, read each body's `SurfaceMaterial`/`EntityId` from the
`Body&` refs' user-data, and enqueue a `PendingContact` **and** record
`pairCache[SubShapeIDPair] = {entityA, entityB}` (the `SubShapeIDPair` is hashable —
`GetHash()`, verified). **Exit path (OnContactRemoved):** look up + erase the cached
pair and enqueue a `PendingContact{phase=Exit}` carrying only the entity ids.

**The enqueue lock is the listener's OWN `std::mutex` (H2)** — guarding only the
pending vector + `pairCache`, both private to the listener. This is *not* a Jolt
body lock: Jolt's "don't use any locking functions" rule forbids `BodyInterface` /
`BodyLockRead` calls inside the callback, which this design never makes. A plain
`std::mutex` around the listener's own data is legal and standard. Held for the
push only (microseconds), uncontended in the common case.

```cpp
// engine/physics/contact_event.h
// (a) POD enqueued on a job thread — NOT an EventBus event:
struct PendingContact
{
    EntityId        entityA, entityB;   // from user-data (0 == none)
    glm::vec3       point;              // Enter only (zero on Exit)
    glm::vec3       normal;             // Enter only
    float           approachSpeed;      // Enter only, m/s
    SurfaceMaterial matA, matB;         // Enter only
    enum class Phase { Enter, Exit } phase;
};

// (b) the bus payload — MUST inherit Event (verified engine/core/event.h:12):
struct CollisionEvent : public Vestige::Event
{
    EntityId        entityA, entityB;
    glm::vec3       point, normal;      // valid only when isEnter
    float           approachSpeed;      // valid only when isEnter
    SurfaceMaterial matA, matB;         // valid only when isEnter
    bool            isEnter;            // drives S8 enter/exit nodes; audio uses Enter only
};
```

**Main-thread drain.** `PhysicsWorld::update` (post-`Update`) swaps the pending
buffer out under the lock and, for each, publishes a `CollisionEvent` on the
existing **EventBus** after a **per-body-pair throttle** (suppress duplicate Enter
contacts within a short window so a resting/jittering pair doesn't machine-gun
events). **EventBus `publish<T>` is synchronous** (`event_bus.h:79`), so the audio
subscriber (S7) synthesises *inside* the drain loop on the main thread — the
per-frame synth budget (§10) is therefore enforced by the **subscriber**, which
queues overflow rather than synthesising unboundedly. Audio (S7) and scripting (S8)
are ordinary EventBus subscribers — neither runs in the Jolt callback.

**Why `approachSpeed` not impulse:** impulse needs the solved manifold (post-step,
per-substep accumulation); approach normal velocity is available at
`OnContactAdded`, stable, cheap, and perceptually adequate (Lloyd et al. drive
excitation energy from impact velocity; loudness JND ≈ 1 dB, §9). **Two
simplifications named per Rule 5:** (1) post-solve impulse is not used; (2) **mass
is dropped** — true impact energy is ½·m·v², but a velocity-only proxy is used, so
a heavy-slow and a light-fast contact of equal approach speed sound equally loud.
Both are deliberate and acceptable for perceptual loudness; revisit only if a
specific material (e.g. a massive door) sounds wrong.

**Throttle + budget (anti-machine-gun, named per project Rule 5):** (1) at most
**one event per body-pair per `kContactThrottleSeconds` (≈0.05 s)** — the
last-fire-time is stored on the **same `pairCache` entry** (keyed by `SubShapeIDPair`),
so it is created on Enter and **erased on Exit**, never growing unbounded over a
session; (2) synthesis
is bounded by a per-frame **sample budget** `kSynthModeSamplesPerFrame` (§10), not a
raw event count — strikes beyond the budget are **queued to the next frame**
(bounded ring, ≤`kMaxSynthQueueFrames`≈3 ≈ 50 ms). **Only burst-overflow strikes
wait at all** — the common ≤4-strikes/frame path synthesises same-frame with zero
queue latency. A queued strike in a many-simultaneous-impact burst (a collapsing
stack) incurs ≤50 ms; for a percussive onset that is at the edge of perceptible
(ideal is <30 ms), but acceptable because such bursts blend into a rattle where
exact per-impact timing isn't discernible. A strike still queued past the bound is
dropped (counted in a debug stat). These are
explicit clamps, logged in CHANGELOG, not hidden.

---

## 5. Synthesis core (S4) — modal (solid) + PhISEM (aggregate)

> **As-built note (S5, 2026-07-01).** Five deliberate implementation choices,
> none changing the design's contracts:
> (1) **Buffer ring → source-keyed map.** The "32-buffer ring, one per source
> slot" is realised as a lazy `source-id → AL buffer` map on `AudioEngine`
> (`m_synthBuffers`): a buffer is generated on a slot's first synth use and
> refilled per strike. This satisfies all three of the ring's contracts — ≤32
> buffers, no per-strike `alGenBuffers`, never overwrite a buffer attached to a
> playing source (because `acquireSource` only returns a slot whose prior buffer
> was already detached via `AL_BUFFER,0`) — without a literal ring cursor + the
> `AL_PLAYING`-skip scan §5 sketched. Simpler, same invariant.
> (2) **Runtime curves live in `audio/procedural/audio_curves.h`** (not a
> `_generated.h` — there is no codegen tool). It is the hand-transcribed
> closed-form of the FW `audio` category, pinned to `audio_templates.cpp` by
> `tests/test_audio_curves_parity.cpp` so the two cannot silently diverge
> (Rule 6 source-of-truth + Rule 7 parity).
> (3) **PhISEM grain-rate composition** (the design left it unpinned): the bank's
> per-material `eventRateHz` is scaled by
> `aggregateEventRate(speed) / aggregateEventRate(1.5)`, i.e. it is the rate at
> the FW curve's *declared default input* (1.5 m/s, asserted by the parity test)
> and rises with contact speed. One documented anchor, no loose constant.
> (4) **`roundRobin` variant-selector dropped.** Per-strike RNG pitch/gain jitter
> already makes consecutive footsteps differ, and there is no pre-baked variant
> set to round-robin among (synthesis is fresh each strike). `jitterPitch` /
> `jitterGain` (§5c) are kept and tested. Add round-robin if S6 ever caches
> variants.
> (5) **`water.pitchGlide` is parsed-and-ignored** — the §5a/§5b synth core (S4,
> already shipped) has no per-grain glide parameter. The key stays in the asset
> for forward-compat; consuming it is a synth-core change, deferred (TODO).

Two algorithmic models, selected per material by the bank (§6). Both are **pure
functions** that fill an `int16` PCM span; **runtime, per strike**, synthesised
into a **round-robin buffer ring** on `AudioEngine`. **Ring sizing + reclaim (L2 /
M-3):** the ring holds **`MAX_SOURCES` (32)** buffers — one per slot in the existing
OpenAL source pool (verified `MAX_SOURCES = 32`, `audio_engine.h:45`). Since at most
32 sources can play at once, a 32-buffer ring can never be forced to overwrite a
buffer still attached to a playing source; the reclaim rule additionally skips any
slot whose previously-attached source is still `AL_PLAYING`
(`alGetSourcei(.., AL_SOURCE_STATE)`) and falls back to `acquireSource`'s own
priority eviction if all 32 are busy. No per-strike OpenAL buffer allocation —
buffers are allocated once at init.

### 5a. Modal synthesis — solid surfaces (stone, wood, metal, glass)

A struck solid rings at a few resonant modes. Each **mode** = a damped sinusoid
`(f_i Hz, decay d_i 1/s, gain g_i)`. Conceptually the strike is
`s(t)=Σ_i g_i·e^(−d_i·t)·sin(2π f_i·t)` — but it is **NOT** evaluated that way
(a per-sample `sin`+`exp` per mode would cost single-digit ms/strike). Each mode
is realised as a **two-pole resonator recurrence** (the standard modal-synth form),
so the inner loop is **multiply-adds only**:

```
per mode, computed ONCE per strike:  r = e^(−d_i/SR),   c = 2·r·cos(2π f_i/SR)
excitation (M-2):                     x[0] = energyGain,  x[n>0] = 0   (a unit impulse)
                                      y[−1] = y[−2] = 0                (zero initial state)
per sample (impulse-driven):          y_i[n] = c·y_i[n−1] − r²·y_i[n−2] + x[n]   // 2 mul + 1 add (+ impulse only at n=0)
strike PCM:                           s[n] = Σ_i g_i · y_i[n]
```

The `+ x[n]` input term is essential — without it (and with zero initial state)
the recurrence stays at zero forever (the silent-resonator trap). A single impulse
at `n=0` drives each two-pole section into its modal impulse response
`∝ g_i·r^n·sin(2π f_i·n/SR)`. The two transcendentals (`e`, `cos`) are evaluated
**once per mode per strike** (a few dozen total), never per sample.

`SR` is the synth sample rate **`kSynthSampleRate = 48000` Hz** (matches the engine
audio rate, `audio_music_stream.h:50`). Every §10 sample count derives from it:
0.35 s → 16 800 samples, 0.18 s → 8 640 samples. Buffers are generated at this rate
and uploaded as 16-bit mono.

- **Excitation by impact energy:** overall amplitude scales with
  `impactLoudnessGain(approachSpeed)` (§9, FW-fit, raw m/s in); higher-energy
  strikes tilt the per-mode `g_i` toward higher modes (brightness) and apply
  `impactPitchScale(approachSpeed)` to the synth sample rate / `c` term.
- **Variation (no two strikes identical):** per-strike, perturb each `g_i` and
  `d_i` by a small RNG factor (±few %) plus a global pitch/gain jitter (§5c).
  Variation source is an **injected `[0,1]` uniform** (deterministic in tests; the
  engine uses one generator per `FootstepSystem`/emitter — §5c/L4).
- **Mode + duration caps (hard, named per Rule 5):** **≤6 modes**, **≤0.35 s**
  `durSec` at the synth sample rate. Metal — the busiest, naturally ~0.5 s — is
  **clamped to 0.35 s / 4 modes** (a slightly shorter ring; audibly minor, and the
  clamp is what keeps a single worst-case strike inside one frame's budget, §10).
  Typical footstep/impact = 2–4 modes, 0.12–0.2 s.
- **Cost (honest, post-recurrence, ~3 ns per mode·sample — see §10 derivation):**
  the **6-mode figure is the hard ceiling, not a shipped bank** — no bank in §6 uses
  6 modes; the busiest shipped bank is **metal at 4 modes × 16.8k ≈ 6.7·10⁴
  mode·samples ≈ ~0.20 ms**. Budgeting against the 6-mode ceiling (1.0·10⁵
  mode·samples ≈ **~0.30 ms**) therefore leaves headroom over every real strike.
  *Typical* footstep = 3 modes × 8.6k (0.18 s) ≈ 2.6·10⁴ mode·samples ≈ **~80 µs**.
  The §10 per-frame budget bounds the sum across simultaneous strikes; even one
  ceiling strike fits within one frame.
- **Authoring:** mode banks are **data** (§6 JSON), seeded from Cook *RSS* tables
  and refined by ear; only the *velocity→param curves* go through the Workbench
  (Rule 6) — modal frequency tables are reference data in JSON.

### 5b. PhISEM — aggregate surfaces (sand, gravel/dirt, grass, water splash)

Aggregates are many micro-impacts of small particles — Cook's **PhISEM** (the
stochastic-event half of the PhISM family; the modal §5a is the PhISAM half).
Model: a **stochastic sequence of grains** over the strike duration —

- grain **onset times** from a Poisson process at rate `aggregateEventRate(energy)`
  (FW curve); grain **energy** decays over the event (sound-system "energy" leaks
  per grain, à la Cook's shaker model);
- each grain = a short filtered-noise burst (a 1–2 pole resonator ping at the
  material's centre frequency) — reusing the modal one-mode kernel from 5a;
- **variation** is intrinsic (the RNG drives the Poisson process); same injected
  uniform for determinism.

`water` uses PhISEM with a higher centre frequency + a short pitch-glide per grain
(droplet "plink"); `cloth` is a very short, low-energy PhISEM burst (soft).

### 5c. Variation layer (S5)

Mirrors `RandomOneShotScheduler`'s injectable-uniform contract:

```cpp
// deterministic: caller injects sample() returning [0,1)
float jitterPitch(float base, float spreadCents, std::function<float()> sample);
float jitterGain (float base, float spreadDb,    std::function<float()> sample);
int   roundRobin (int n, int& cursor); // advance, never repeat immediately
```

Round-robin selects among the ring's recent variants so consecutive footsteps
differ; pitch jitter ±~50 cents, gain jitter ±~2 dB (data-driven per bank).

**Determinism (L4):** the `sample()` source is injected. Tests pass a fixed
sequence (byte-stable PCM-shape assertions). The engine gives **each emitter its
own `std::mt19937`** (one per `FootstepSystem`; impact emission uses the
`AudioEngine`'s) seeded from a fixed session seed, so a replay re-synthesises
identically. No shared global RNG (no cross-emitter contention or order-dependence).

---

## 6. Material sound banks (S5) — `assets/audio/synthesis/footstep_modal.json`

One JSON file, one entry per `SurfaceMaterial`, declaring the model + params. This
is the **~10× asset saving**: a few KB of JSON replaces a WAV set per surface.

```jsonc
{
  "version": 1,
  "materials": {
    "stone":  { "model": "modal",  "modes": [ {"f":380,"d":34,"g":1.0}, {"f":920,"d":48,"g":0.6}, {"f":1840,"d":70,"g":0.3} ],
                "durSec": 0.18, "pitchJitterCents": 40, "gainJitterDb": 2.0 },
    "wood":   { "model": "modal",  "modes": [ {"f":210,"d":22,"g":1.0}, {"f":540,"d":30,"g":0.5} ], "durSec": 0.16, "pitchJitterCents": 50, "gainJitterDb": 2.5 },
    "metal":  { "model": "modal",  "modes": [ {"f":640,"d":8,"g":1.0}, {"f":1700,"d":11,"g":0.8}, {"f":3100,"d":16,"g":0.5}, {"f":5200,"d":22,"g":0.25} ], "durSec": 0.35, "pitchJitterCents": 30, "gainJitterDb": 1.5 },
    "sand":   { "model": "phisem", "centreHz": 1800, "qual": 2.0, "eventRateHz": 1200, "durSec": 0.12, "energyDecay": 18 },
    "water":  { "model": "phisem", "centreHz": 2600, "qual": 4.0, "eventRateHz": 700,  "durSec": 0.2,  "energyDecay": 10, "pitchGlide": -0.4 },
    "grass":  { "model": "phisem", "centreHz": 1400, "qual": 1.5, "eventRateHz": 900,  "durSec": 0.14, "energyDecay": 20 },
    "cloth":  { "model": "phisem", "centreHz": 900,  "qual": 1.0, "eventRateHz": 500,  "durSec": 0.08, "energyDecay": 30 },
    "default":{ "model": "modal",  "modes": [ {"f":300,"d":40,"g":1.0} ], "durSec": 0.12, "pitchJitterCents": 40, "gainJitterDb": 2.0 }
  }
}
```

Numbers above are **seed values** (illustrative, from the cited reference tables) —
refined during S4/S5 by ear and locked by the test's deterministic-PCM checks
(shape, not exact bytes). `dirt`/`glass` map to `grass`/`stone`-like entries or
their own rows. `durSec` is clamped to the §5a synth cap (≤0.35 s). Footsteps and
impacts **share the bank**; the difference is the `envelopeScale` argument on
`playSynth` (M-4, §8): footsteps pass 1.0 (the bank `durSec`), impacts pass a value
>1 (a longer ring, still clamped to the cap). No separate impact bank is needed.

---

## 7. Footstep emission (S6)

A small `FootstepSystem` (new) reading the existing character controller — **no
physics-core change**:

1. Each frame, for the player (and any NPC with a controller), read
   `isOnGround()` + horizontal speed (from `getLinearVelocity()`).
2. **Cadence:** accumulate horizontal distance; when it exceeds a speed-scaled
   **stride length**, fire a footstep and reset. (Distance-based, not time-based →
   cadence tracks speed naturally; walk vs run falls out.)
3. **Surface under foot — from the controller's ground body, NOT a raycast (Loop-4
   correction).** The controller is a `JPH::CharacterVirtual` (verified
   `physics_character_controller.h:98`), which is **not a broadphase body** — so a
   downward raycast would need no self-exclude (nothing to self-hit) *and* would
   duplicate work the controller already does. `CharacterVirtual` tracks its ground
   contact every `update`; expose `getGroundBodyId()` (wrapping
   `CharacterVirtual::GetGroundBodyID()`) on `PhysicsCharacterController` and read
   `getSurfaceMaterial(groundBodyId)` (§3, main-thread accessor — fine here, the
   footstep system runs on the main thread). When not on ground the ground-body id
   is invalid → no footstep. No raycast, no `ignoreBodyId`, no ray-length tuning —
   strictly simpler and exactly the surface the foot rests on. *(Implementer:
   confirm `CharacterVirtual::GetGroundBodyID()` spelling against Jolt 5.3.0 — it is
   a standard `CharacterBase` accessor; a sibling `GetGroundMaterial()` also exists
   if a `PhysicsMaterial*` path is ever preferred over the user-data lookup.)*
4. **Landing edge:** `isOnGround()` false→true with downward speed → a single
   louder strike (energy from landing vertical speed) instead of a normal step.
5. Play via `AudioEngine::playSynth(material, footSpeed, footPosition, /*envelopeScale*/1.0f)`
   (§5/§8); loudness from `impactLoudnessGain(footSpeed)`. A landing strike (step 4)
   passes the landing vertical speed and a slightly larger `envelopeScale`.

Gated by a settings toggle (§9). NPC footsteps respect the per-source LOD ladder
(AX5) already shipped — distant NPCs synthesise less often / not at all.

> **S6 as-built (2026-07-01, commit pending):** shipped as a standalone
> `FootstepSystem` (registered ISystem, `isForceActive()` like `AudioSystem`),
> *not* an extension of `CharacterSystem` — the registry slot keeps it
> self-contained and update-ordered after the controller (the main loop steps
> the controller before `updateAll`). Deviations / pinned choices:
> 1. **`getGroundBodyId()` confirmed** — wraps `CharacterVirtual::GetGroundBodyID()`
>    (Jolt 5.3.0 `CharacterBase.h:115`, returns `JPH::BodyID`). Invalid when
>    airborne → `getSurfaceMaterial` already maps that to `Default`, so a
>    stale/removed ground (Jolt's documented caveat) is safe with no extra guard.
> 2. **Cadence** is distance-based with a *speed-scaled* stride
>    `stride = kStrideBaseMeters + kStridePerSpeed·speed` (running lengthens the
>    stride, so cadence rises but saturates). The crossing **remainder carries
>    forward** so cadence doesn't drift. Below `kMinFootstepSpeedMps` the
>    accumulator is bled to zero (no banked distance while standing). All
>    non-curve constants carry the Rule-6 `TODO: revisit via Formula Workbench`
>    marker (§9 L-4).
> 3. **Landing** uses the *peak* airborne descent speed (tracked each airborne
>    frame, read on the airborne→ground edge — the controller has already zeroed
>    vertical velocity by the grounded frame) and a `1.3` envelopeScale (between
>    a footstep's `1.0` and an impact's `1.5`); it resets the walk cadence.
> 4. **No per-emitter RNG** on the system — per-strike pitch/gain jitter (§5c)
>    already happens inside `playSynth` via the engine's `m_synthRng`, so the
>    footstep system holds no RNG of its own. The pure cadence/landing logic
>    (`StrideAccumulator`, `strideLengthMeters`, `landingTriggered`) is split out
>    engine-free and unit-tested (`test_footstep_system.cpp`); the thin
>    physics→audio glue is verified by read (it reuses S2/S3-tested accessors).

---

## 8. Impact / collision emission (S7) + the play entry point

**New `AudioEngine` entry point** (the one place synthesis meets the source pool):

```cpp
unsigned AudioEngine::playSynth(SurfaceMaterial mat, float approachSpeed /* m/s */,
                                const glm::vec3& pos,
                                float envelopeScale = 1.0f,   // 1.0 = footstep; >1 = longer impact ring (M-4)
                                AudioBus bus = AudioBus::Sfx,
                                SoundPriority prio = SoundPriority::Normal);
```

**One energy domain end-to-end (cold-eyes H1/H2):** the boundary unit is **raw
approach speed in m/s** — the same unit the §9 FW curves take. `playSynth` passes
`approachSpeed` straight into `impactLoudnessGain(speed)` and
`impactPitchScale(speed)`; the speed→[0,1] normalisation lives **inside the fitted
curve** (its reference dataset defines the full-scale-loudness speed), so there is
**no loose `kRefSpeed` constant** — it is absorbed into the Workbench fit (Rule 6),
not hand-coded. `playSynth` then: picks the bank (§6) → synthesises into the next
ring buffer (§5) with speed-scaled loudness/pitch + variation → `acquireSource(prio)`
→ attach buffer → position/gain via the **existing** `resolveSourceGain` →
`alSourcePlay`. (No AX9 LUFS makeup fold — synth buffers have no path key, §0;
amplitude is the `impactLoudnessGain` curve baked into the synthesised samples.)
LOD/occlusion/air-absorption all apply unchanged — it is an ordinary positional
one-shot after synthesis.

**Object-collision audio (S7):** subscribe to the S3 `CollisionEvent` bus. On each
event above the §4 min-speed threshold, choose the **harder** of `matA`/`matB`
(priority Metal>Stone>Glass>Wood>Dirt>Grass>Sand>Cloth>Water) for the dominant
timbre and `playSynth(mat, approachSpeed, point, /*envelopeScale*/1.5f)` (impacts
ring longer than footsteps). The per-pair throttle (§4) prevents rattling.

**Default-vs-Default policy (M3) — AUDIO-only (L-3):** if **both** bodies are
`SurfaceMaterial::Default` (untagged), the collision emits **no synth** — otherwise
an unauthored scene full of untagged boxes would thud on every contact. A single
body tagged non-Default emits using that material (the Default side is the softer
partner). **This gate is in the S7 audio subscriber, not the bus** — the
`CollisionEvent` still publishes for every (throttled) contact, so the S8 scripting
`OnCollisionEnter`/`Exit` nodes still fire on untagged bodies (scripts often care
about untagged geometry). A project setting `proceduralAudio.emitUntaggedCollisions`
(default **off**, §9-settings) can force-enable Default↔Default audio for debugging.
Stated explicitly per Rule 5 (a deliberate gate, not hidden silence).

**Weapon-impact audio (S7):** a weapon hit is already a `rayCast` in gameplay; on
hit, read the hit body's material and `playSynth(mat, hitSpeed, hitPoint, 1.5f)`.
`hitSpeed` = projectile/swing speed where known; for a hitscan weapon (no travel
speed) it is `kHitscanReferenceSpeed` — a hand-picked m/s mapping to a firm impact,
carrying `// TODO: revisit via Formula Workbench if reference data emerges` (L2,
Rule 6). No new plumbing — same entry point.

**Scripting unblock (S8):** the same `CollisionEvent` bus feeds the stubbed
`OnCollisionEnter`/`OnCollisionExit` nodes (`event_nodes.cpp:197-223`). Enter =
first `OnContactAdded` for a pair; Exit = `OnContactRemoved`. This is a **bonus**
that falls out of S3 for ~free and clears three "pending" stubs. **Note (Loop-5 L):**
the existing node pins include `contactPoint`/`normal` (`VEC3`) and `otherEntity`;
on **Enter** all are populated, but on **Exit** Jolt gives no body access (§4), so
the Exit event carries only the entity ids and the point/normal pins **read zero**.
Script authors should treat point/normal as valid on Enter only.

> **S7 as-built (2026-07-01, commit pending):** shipped as a standalone
> force-active `ImpactAudioSystem` (registered ISystem, mirrors FootstepSystem)
> that subscribes to the `CollisionEvent` bus in `initialize()` and synthesises
> inside that synchronous dispatch; `update()` is a no-op (purely event-driven),
> `shutdown()` unsubscribes. Deviations / pinned choices:
> 1. **Whole S7 policy is one pure function** `decideImpact(CollisionEvent,
>    emitUntaggedCollisions) → {play, material}` + `harderMaterial` /
>    `materialHardnessRank` (Metal=9…Water=1, **Default=0** so a single tagged
>    body always wins the timbre). Unit-tested engine-free
>    (`test_impact_audio_system.cpp`, 10 tests); the thin subscribe→playSynth
>    glue is verified by read (reuses S3/S5-tested paths). The min-speed gate
>    (`kMinImpactSpeed`, shared with the bus) + Default↔Default suppression both
>    live in this subscriber, not the bus — so scripting (S8) still sees every
>    contact. Impact `envelopeScale = 1.5` (Rule-6 TODO marker).
> 2. **No per-frame synth budget / overflow queue** (design §4/§10). Dropped for
>    the same reason S3 dropped the per-pair throttle: emission is bounded by the
>    contact-*onset* rate (one Enter per pair at first contact, then silence) and
>    the 32-voice pool, not the per-frame contact count — so unbounded per-frame
>    synthesis is not a realistic path. If a pathological many-simultaneous-onset
>    burst (a collapsing stack landing on one frame) ever spikes a frame, a
>    per-frame strike-count cap is the cheap fix; deferred until measured
>    (Rule 5: named, not hidden).
> 3. **Weapon-impact path is not built** — there is **no weapon / gameplay
>    raycast system in the engine** today (grep empty), so there is nothing to
>    hook. It needs **no new plumbing**: a future weapon system calls the same
>    public `AudioEngine::playSynth(mat, hitSpeed, hitPoint, 1.5f)` on a ray hit
>    (`kHitscanReferenceSpeed` for a hitscan). Building a weapon system now would
>    be speculative scaffolding (Rule 2), so it is left as the documented
>    one-call integration point for when weapons land.

---

## 9. Formula Workbench audio category (S1) — closes `[3D_E-0022]`

A new `engine/formula/audio_templates.cpp` registers `FormulaDefinition`s with
`category = "audio"` (freeform-string category — verified `physics_templates.cpp:105`),
exactly mirroring `physics_templates.cpp`. The Workbench template browser then
lists an **Audio** group; each template carries a reference dataset + a
reference-harness regression lock, exportable to C++/GLSL like every other domain.

**Templates this bundle needs (authored/fit/exported, not hand-coded — Rule 6):**

| Template | Shape | Drives |
|----------|-------|--------|
| `impactLoudnessGain` | `approachSpeed (m/s) → linear gain [0,1]` | strike amplitude (§5/§8) |
| `impactPitchScale` | `approachSpeed → pitch multiplier (≈0.85–1.25)` | strike pitch (§5/§8) |
| `aggregateEventRate` | `energy → Poisson grain rate (Hz)` | PhISEM density (§5b) |

**Seed templates that also close `[3D_E-0022]`** (already-shipped curves the
roadmap item names, now brought into the Workbench so they're fit/validated/
exportable, with the AX6 `TODO: revisit via Formula Workbench` finally actionable):

| Template | Shape | Notes |
|----------|-------|-------|
| `iso9613AbsorptionHF` | `α(f≈4kHz, T, h) → dB/m → linear HF gain` | cheap polynomial fit, target ≤0.5 dB abs err over T∈[−20,50]°C, h∈[10,100]% — pins/replaces AX6's hand-coded form behind a **parity test** (Rule 7). This one closes AX6's standing `TODO: revisit via Formula Workbench`. |

> **`[3D_E-0022]` scope note (cold-eyes M-5):** the roadmap item *also* lists a
> BS.1770 K-weighting seed, but **AX9 loudness ships via libebur128**
> (`engine/audio/audio_loudness.cpp`), not hand-coded K-weighting coefficients — so
> there is no engine code to "bring under validation," and a `bs1770KWeighting`
> template would be a **new, unused curve**. It is therefore **deliberately out of
> this bundle's scope** (adding a dead curve fails Rule 2 / Rule 6). `[3D_E-0022]`
> is therefore **substantially delivered** — the `audio` category exists + the three
> procedural curves + `iso9613AbsorptionHF` (which closes AX6's TODO) — but its named
> **K-weighting sub-item is explicitly deferred**, not delivered. So S10 does **not**
> flip `[3D_E-0022]` to fully shipped ✅; it **annotates** the bullet (`roadmap_log`)
> with what shipped and that K-weighting is deferred pending the engine ever
> hand-coding K-weighting (e.g. a real-time bus meter). Deferred openly, not dropped
> silently (Loop-5 L).

**Engine integration shape:** `CodegenCpp::generateHeader(audioDefs, FULL)` emits
`engine/audio/procedural/audio_curves_generated.h` with each curve as an inline
`float` function, coefficients baked as compile-time literals (zero runtime
overhead, `codegen_cpp.h:39`). The synth core (§5) `#include`s it. A **parity test**
(Rule 7) pins each exported function against the Workbench reference dataset
(`EXPECT_NEAR`, the established `physics_templates` discipline). Only the
velocity/energy *curves* are Workbench-fit; the modal **frequency tables** are
reference data and live in the bank JSON (§6) — stated to avoid the false
expectation that every constant is FW-fit.

**Non-curve tuning constants (cold-eyes L-4, Rule 6).** A handful of values are
*tuning*, not curves fit to reference data — `kContactThrottleSeconds`,
`kMaxSynthQueueFrames`, `kSynthModeSamplesPerFrame`, the per-bank `pitchJitterCents` /
`gainJitterDb`, and the §8 material-hardness priority order. Rule 6 permits
hand-coding "when no reference data exists," and none of these has a published
reference dataset (they are latency/ergonomics knobs). Each is therefore hand-coded
**with a `// TODO: revisit via Formula Workbench if reference data emerges` comment**
at its definition (Rule 6's required marker) and named as a clamp in §4/§10 (Rule 5)
— not silently magic. They are explicitly **out of Workbench scope**, stated rather
than assumed.

---

## 10. Performance (60 FPS hard floor)

Audio + the new systems are **main-thread** today. Budget, per frame at 60 FPS
(16.6 ms):

**Synthesis is the only non-trivial new cost**, and it is bounded by a per-frame
**sample budget** (not an event count), so the worst case is a fixed ceiling
regardless of how many contacts fire.

| Cost | When | Estimate | Notes |
|------|------|----------|-------|
| Contact callback (vel + threshold + user-data read + enqueue) | physics threads, per contact | ~tens of ns each | no alloc beyond a `vector` push; no audio/EventBus there |
| Contact drain + publish | main thread, post-step | O(events) branchy | cheap; throttled per pair (§4) |
| **Per-strike synthesis (resonator)** | main thread | typical **~80 µs** (3 modes, 0.18 s); worst single **~0.3 ms** (6 modes, 0.35 s, capped) | recurrence, no per-sample `sin`/`exp` (§5a) |
| **Per-frame synth total** | main thread | **bounded by `kSynthModeSamplesPerFrame`** | see budget below |
| Footstep ground-body read | main thread, per stride (~1–2/sec/char) | one cheap accessor | `CharacterVirtual::GetGroundBodyID()` + user-data unpack; no ray |
| **Per-strike fixed costs** | main thread, per strike | ~µs each | `alBufferData` upload (≤16.8k×2 B = the 0.35 s cap), `acquireSource` priority scan, `alSourcePlay`, and the ≤32-slot `AL_PLAYING` ring-reclaim scan — **per strike, not per mode·sample**. ~4 typical strikes/frame ⇒ 4× these; each is a handful of µs, but they are *not* in the synth sample budget |

**Budget derivation (consistent units, cold-eyes H-1).** Take the resonator inner
loop at **~5 mul-adds per mode·sample** (2 for the recurrence, 1 for the gain
accumulate, ~2 for state/int16 housekeeping) and a conservative scalar throughput
of **~0.6 ns/op** → **~3 ns per mode·sample**. The per-frame synthesis budget is
`kSynthModeSamplesPerFrame = 1.2·10⁵ mode·samples` →

```
1.2·10⁵ mode·samples × 3 ns  ≈  0.36 ms/frame   (the synthesis ceiling)
```

That budget fits **one worst-case strike** (6 modes × 16.8k ≈ 1.0·10⁵ mode·samples,
~0.30 ms — so a single capped strike always completes in its frame) **or ~4 typical
strikes** (3 modes × 8.6k ≈ 2.6·10⁴ each). Realistic offered load is far lower
(footsteps ~2/s; collisions sparse and per-pair throttled). **Bursts beyond the
budget queue to the next frame** (§4, ≤`kMaxSynthQueueFrames`≈3), so per-frame synth
cost is a hard ceiling that cannot spike. No per-frame allocation: the buffer ring
is pre-allocated (§5) and the pending-contact vector reuses capacity (swap-and-clear).

**Profiling gate (project perf rule):** a Release benchmark drives a sustained
**offered load that exceeds the budget** (≥8 strikes/frame, to exercise the queue)
and asserts the combined audio + footstep + contact-drain main-thread update stays
**< 0.5 ms/frame** at the **32-voice pool maximum** (`MAX_SOURCES`) — synthesis
≤0.36 ms + the per-strike fixed costs (upload / acquire / play / reclaim) + the
contact-drain/footstep work, all measured **empirically** by the benchmark (not
hand-summed). The gate measures the *budgeted* path (the queue absorbs the rest),
not an unbounded burst, mirroring the AX-bundle benchmark. If the fixed-cost tail
ever pushes it over 0.5 ms, lower `kSynthModeSamplesPerFrame` (more queueing) — the
synthesis budget is the tunable knob.

---

## 11. Accessibility

- **Spatial-awareness aid:** material-distinct footstep/impact cues help low-vision
  players localise surfaces and events. Cues route through the existing Sfx bus →
  honour master/SFX volume, AX9 loudness normalisation, and AX8 mono-downmix
  (mono output already collapses HRTF/surround for single-ear users).
- **Caption hook (reuse, not new):** significant impacts can feed the existing
  subtitle/caption system (`engine/ui/subtitle.h`) as `[stone impact]`-style
  captions on its **`SubtitleCategory::SoundCue`** channel (the enum value is in
  `engine/ui/subtitle.h:40`) — a thin, optional
  emitter; no new UI. Caveat (verified): subtitle visibility is a **single global
  `setEnabled`/`isEnabled`**; there is **no dedicated non-speech toggle** distinct
  from dialogue. So this routes behind the global caption toggle; a *granular*
  "sound captions" toggle (independent of dialogue) is **explicit optional S9 work**,
  not assumed to exist. If even the `SoundCue` channel proves unfit, the hook is
  deferred and noted — not faked.
- **Photosensitive / vestibular:** N/A (audio-only; no screen effect).
- **No new motion.** Footstep cadence is audio; it does not add camera bob.

---

## 12. CPU / GPU placement (project Rule 7)

**All CPU.** Justification against the Rule 7 heuristic:

- **Synthesis (modal/PhISEM):** per-sample math, but **per-strike and sparse**
  (event-driven, a few/sec), not per-pixel/per-frame. A GPU dispatch per strike
  would cost more in latency + readback than it saves; OpenAL needs the PCM on the
  CPU side to upload to a buffer. → **CPU.**
- **Collision-event handling, material lookup, footstep cadence:** branching,
  sparse, decision/state — textbook CPU per the heuristic.
- **FW curves:** scalar, evaluated a few times per strike — CPU; exported as inline
  C++ (a GLSL twin is available from the same `FormulaDefinition` if a future GPU
  audio path ever wants it — dual-impl is free here but **not built** now; no parity
  burden incurred).

No dual CPU/GPU implementation is introduced, so no parity test is owed on that
axis. The only parity test owed is **FW-exported curve vs FW reference dataset**
(§9), per Rule 7's "fitted approximation pinned to the exact reference."

---

## 13. References

1. **P. R. Cook, *Real Sound Synthesis for Interactive Applications*** (A K Peters,
   2002) — the **PhISM** family: its modal/additive method (PhISAM) for struck
   solids (§5a) and its stochastic-event method (**PhISEM**) for aggregate
   particle/shaker sounds (maracas, "shoveling gravel", §5b). The AX4 bullet's
   named reference.
2. **B. Lloyd, N. Raghuvanshi, N. Govindaraju, "Sound Synthesis for Impact Sounds
   in Video Games," *I3D 2011*** — modal model = bank of damped sinusoids derived
   from a recording; runtime variation; shipped in *Crackdown II*; less memory than
   prerecorded clips; velocity→excitation. (Loudness JND ≈ 1 dB informs the
   velocity→loudness mapping.)
3. **L. Turchet & R. Nordahl et al., "Footstep sounds synthesis…," *Applied
   Acoustics* (2016)** — exciter (shoe) × resonator (floor); **solid** vs
   **aggregate** surface taxonomy; ground-reaction-force → amplitude. Basis for the
   §5a/§5b split.
4. **ISO 9613-1** atmospheric absorption (seed FW template, §9; ties off AX6's
   `TODO: revisit via Formula Workbench`).
5. **ITU-R BS.1770 / EBU R128** K-weighting (seed FW template, §9; AX9 coeffs).

> **Reference substitution note:** the ROADMAP AX4 bullet names Cook + *Stam's
> "Stable Fluids for sound."* Stam's stable-fluids work is a fluid-simulation method
> ill-suited to rigid-body impact/footstep synthesis, so this doc keeps Cook and
> adds the directly-applicable **Lloyd et al. (impact)** and **Turchet & Nordahl
> (footstep)** instead. A deliberate, better-fit substitution — noted for the audit
> trail (Rule 13).

---

## 14. Resolved decisions

- **Synthesise per-strike into a fixed buffer ring** (not pre-baked pools, not a
  real-time OpenAL DSP voice). Per-strike matches the roadmap "runtime synth,"
  gives unlimited variation, and is 60 FPS-safe because strikes are sparse; the
  ring bounds memory and avoids per-strike allocation. Pre-baking is a noted future
  optimisation if profiling ever demands it.
- **Two-pole resonator recurrence, not per-sample `sin`/`exp`** — the only
  implementation that makes the synthesis cost fit the 60 FPS budget (§5a/§10).
- **`approachSpeed` (relative normal velocity), not solved impulse, and
  mass-independent** — available at `OnContactAdded`, perceptually adequate; both
  simplifications named (§4).
- **One energy domain = raw m/s** end-to-end; the speed→[0,1] normalisation lives
  inside the FW-fitted curve, so there is **no loose `kRefSpeed` constant** (§8/§9).
- **Jolt body user-data packs `[entityId(u32) | SurfaceMaterial(u8)]`**, not a side
  map — reading it in the contact callback is sanctioned by the Jolt 5.3.0 contract
  (`ContactListener.h:66`, read-only body access), and it supplies the entity id S8
  needs (§3). Set post-create via `SetUserData` at `RigidBody::createBody`; the three
  `create*Body` signatures stay untouched. The entity **id is `uint32_t`**, not the
  non-copyable `Entity` class (cold-eyes C-1).
- **Exit events use an add-time `SubShapeIDPair→entities` cache** because Jolt's
  `OnContactRemoved` has no body access at all (`ContactListener.h:107-116`); Exit
  carries only the entity pair, no point/velocity. Audio uses Enter only (§4).
- **Resonator is impulse-driven** (`+x[n]` input term, zero initial state) — without
  it the recurrence is silent (§5a, cold-eyes M-2).
- **Default↔Default collisions suppressed by default** so untagged scenes don't
  thud (§8); a debug setting can force-enable.
- **Curves via Workbench, frequency tables via JSON** — only the velocity/energy
  curves are FW-fit; modal mode tables are reference data in the bank (§6/§9).
- **One emission entry point `playSynth`** reused by footsteps, collisions, and
  weapon-impacts (Rule 3 reuse-before-rewrite); collision + footstep + weapon
  paths differ only in *how energy + material are sourced*, not in playback.
- **Scripting collision nodes unblocked as a side effect** of S3, not as separate
  design surface (§8).

---

## 15. Cold-eyes loop log

> Per global Rule 14 + project Rule 9, this doc runs through `/cold-eyes` (fresh
> subagent, no authoring context), looped until a pass returns zero verified
> actionable findings. Each loop runs cold (no briefing on prior findings).

| Loop | Date | Verdict | Findings actioned |
|------|------|---------|-------------------|
| 1 | 2026-06-30 | NOT ready — 3 CRITICAL / 4 HIGH / 5 MEDIUM / 5 LOW | **C1** synthetic-id `9zpnclbt6d` / digit-leading `[3D_E-0022]` aren't flip-locatable → S10 flips both by headline/line_range (intro note). **C2** user-data set post-create via `SetUserData`, three `create*Body` sigs untouched (§3). **C3** user-data packs entityId+material so S8 has an entity (§3). **H1/H2** one energy domain = raw m/s; `kRefSpeed` folded into the FW curve, removed (§8/§9/§14). **H3** bus payload is `CollisionEvent : public Event`; `PendingContact` POD kept distinct (§4). **H4** two-pole resonator recurrence (no per-sample `sin`/`exp`); cost re-derived (§5a). **M1** §10 rebuilt on a per-frame sample budget + overflow queue, honest numbers. **M2** occlusion enum corrected to 8 members + SurfaceMaterial↔occlusion mapping (§0/§3). **M3** Default↔Default suppression policy (§8). **M4** footstep ray uses `ignoreBodyId` (§7). **M5** caption toggle is global; granular toggle = explicit S9 work (§11). **L1** 2D = N/A non-goal (§1). **L2** ring reclaim checks `AL_PLAYING`, sized ≥ max voices (§5). **L3** mass-drop named (§4). **L4** per-emitter RNG (§5c). **L5/INFO** PhISM/PhISAM/PhISEM terminology fixed (§5/§13). |
| 2 | 2026-06-30 | NOT ready — 1 CRITICAL / 3 HIGH / 5 MEDIUM / 4 LOW (no Loop-1 finding resurfaced) | **C-1** `Entity` is a non-copyable class → all id fields/returns now `EntityId = uint32_t` (§3/§4/§14). **H-1** §10 budget math was wrong ~3× → rederived at a consistent ~3 ns/mode·sample; caps tightened to ≤6 modes/≤0.35 s so one worst strike fits the 0.36 ms budget; gate at the 32-voice pool max (§5a/§10). **H-2/H-3** Jolt callback thread-safety + velocity-availability **verified** against `ContactListener.h:66` (read-only sanctioned) and cited; `GetPointVelocity`/`GetUserData` flagged for implementer spelling-confirm (§4). **(new, found while verifying H-2)** `OnContactRemoved` has no body access → Exit events use an add-time pair→entity cache (§4). **M-1** named `RigidBody::createBody` as the bridge + engine.cpp static-body null-entity path (§3). **M-2** resonator was silent → added `+x[n]` impulse term (§5a). **M-3** ring sized to `MAX_SOURCES`=32, reconciled with pool + benchmark (§5/§10). **M-4** `playSynth` gains `envelopeScale` for footstep-vs-impact (§6/§7/§8). **M-5** dropped the dead `bs1770KWeighting` template (AX9 uses libebur128) (§9). **L-1** rayCast cite 226→224. **L-2** single-`ignoreBodyId` limitation noted (§7). **L-3** Default↔Default suppression is audio-only; scripting bus still fires (§8). **L-4** non-curve tuning constants carry the Rule-6 `TODO` marker (§9). **I-2** synchronous `publish` → subscriber enforces the synth budget (§4). |
| 3 | 2026-06-30 | NOT ready — 1 CRITICAL / 2 HIGH / 5 MEDIUM / 5 LOW (no Loop-2 finding resurfaced; math/data-flow/architecture verified sound) | **C1** the contact callback must read user-data from the lock-free `Body&` ref, not the `BodyID` accessor (which locks → deadlock); §3/§4 now split the two paths explicitly. **H1** acknowledged Jolt's `EstimateCollisionResponse` and stated why the cheaper velocity proxy is chosen (§4). **H2** separated the listener's own `std::mutex` from Jolt's body locks (§4). **M2** `MAX_SOURCES` line 40→**45** (verified; the quick-wins doc's cite was stale). **M3** noted `mUserData`-at-creation as the considered-and-rejected alternative (§3). **M4** clarified 6 modes = hard ceiling, metal (4) = real worst case, budget has headroom (§5a). **M5** `kSynthSamplesPerFrame`→`kSynthModeSamplesPerFrame` (true unit). **L1** softened the burst-latency claim (only overflow waits; <30 ms ideal) (§4). **L2** named `kHitscanReferenceSpeed` + Rule-6 TODO (§8). **L3** specified the footstep ray length (§7). **L4/L5** reserved-bits intent + `getId` `:81` cite. **M1 REJECTED** — reviewer claimed `SoundCue` isn't in `subtitle.h`; verified it **is** (`subtitle.h:40`), citation kept. |
| 4 | 2026-06-30 | **0 CRITICAL / 0 HIGH** / 3 MEDIUM / 3 LOW (citations + Jolt contract verified excellent; no Loop-3 finding resurfaced) | **M** stated `kSynthSampleRate = 48000` — every §10 number derives from it (§5a). **M** pinned the manifold contact-point coordinate space (`GetWorldSpaceContactPointOn1(0)`, world not base-offset-relative; first/deepest point) (§4). **M** added the per-strike fixed-cost line (upload/acquire/play/reclaim, measured empirically) (§10). **L (real bug)** the controller is a `CharacterVirtual` with **no `BodyID`**, so `ignoreBodyId` was unimplementable → §7 now reads `CharacterVirtual::GetGroundBodyID()` (no raycast, simpler); §0 updated. **L** `alBufferData` bound 24k→16.8k (matches the 0.35 s cap). **L** fixed the `playSynth` call drift in the §2 verify cell. **INFO** throttle last-fire-time shares the `pairCache` lifetime (no unbounded growth). |
| 5 | 2026-06-30 | **0 CRITICAL / 0 HIGH** / 1 MEDIUM / 3 LOW (citations + Jolt contract + resonator math + §10 arithmetic all independently re-verified correct; no Loop-4 finding resurfaced) | **M** AX9 LUFS makeup is a **no-op for synth voices** (no path key → unity); §0/§8 corrected — synth loudness is `impactLoudnessGain` + bus/gain, not AX9. **L** purged leftover "raycast" language in §2/§10 (footsteps use `GetGroundBodyID`, not a ray). **L** `[3D_E-0022]` is **annotated not flipped** — substantially delivered but K-weighting sub-item deferred (§9/§2/intro). **L** noted Exit events feed zeroed point/normal pins (§8 S8). **INFO** reserved bits / implementer-confirm flags — no action. |
| 6 | 2026-06-30 | **CLEAN — ready to implement.** 0 CRITICAL / 0 HIGH / 0 MEDIUM / 0 LOW; 2 non-actionable INFO. Every claim re-verified true against current source + the pinned Jolt 5.3.0 headers; no prior-loop finding resurfaced. | **INFO** added the §13 reference-substitution note (Cook/Lloyd/Turchet over the roadmap's "Stable Fluids"). **INFO** `kSynthSampleRate=48000` anchored to `audio_music_stream.h:50` — value correct, affects no number. No design change. **Convergence reached** — sign-off per the delegated gate. |
