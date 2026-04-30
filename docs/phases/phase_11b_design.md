# Phase 11B — Gameplay Features (Design Doc)

| Field | Value |
|---|---|
| Phase | 11B — Gameplay Features |
| Status | `planned` |
| Owners | milnet01 |
| Start (target) | after Phase 11A milestone landed and Phase 10.8 Camera Modes / Decal System / Post-Processing (PP) suite shipped |
| Target completion | TBD — sized post-review (rough guess from §5: 4 archetype slices, see slice plan) |
| Roadmap source | `ROADMAP.md` lines 830–1021 |
| Peer doc | `docs/phases/phase_11a_design.md` (parallel — infrastructure layer this phase consumes) |
| Render prerequisites | `docs/phases/phase_10_8_camera_modes_design.md` (approved, mid-flight); Phase 10.8 Decal System + PP suite (planned) |

---

## 1. Goal

Phase 11B turns Vestige from an architectural-walkthrough engine into a shippable game engine for three concrete archetypes — third-person survival horror (the *Dead Space* shape), action / role-playing game (RPG), and racing (arcade *Burnout 3 / Revenge* and simulation *Gran Turismo* both supported). It does this by adding the gameplay-facing surfaces — combat / weapon / health / inventory / save / hazard / vehicle / horror-polish / replay-playback — on top of the runtime primitives Phase 11A delivered (camera shake, screen flash, save compression, replay recording, behaviour trees, AI perception) and the rendering surfaces Phase 10.8 delivered (camera modes, decals, post-process suite). The biblical-walkthrough use case keeps working unchanged — every gameplay component is opt-in per scene.

---

## 2. Scope

### 2.1 In scope

| Area | Surface |
|---|---|
| Combat / weapons | `WeaponComponent` (hitscan / projectile / melee), per-bone damage zones, muzzle / tracer / impact effects, aim-down-sights (ADS), upgrade-bench UI |
| Health / damage | `HealthComponent`, status effects (bleeding / burning / stun / poisoned), directional damage indicator, death + respawn flow, low-health warning |
| Inventory | `InventoryComponent` (grid + weight + stacks), pickups, consumables, equipment slots, loot tables |
| Save / checkpoint | World-state serializer, save-slot manager, checkpoint volumes, quick-save / quick-load (consumes 11A `writeCompressedChunk`) |
| Environmental hazards | `HazardZoneComponent` (fire / electric / toxic / vacuum / radiation), interactive hazards (exploding barrels, panels, vents) |
| Vehicle physics | `VehicleComponent` (Jolt `WheeledVehicleController` wrap), tyre / drivetrain / aero models, two-tier damage, crash camera, vehicle camera presets, force-feedback (FFB) shim |
| Arcade racing layer | Boost meter, takedowns, traffic AI, game-mode primitives, signature-takedown record |
| Sim racing layer | Driving assists, tyre wear + thermal, fuel, pit stops, garage UI, licence tests, opponent racing-line AI, track authoring, lap timing, photo mode |
| Horror polish | Kinesis component, stasis-as-weapon, zero-G traversal, world-space UI pathway, diegetic UI, RIG health spine, encounter AI behaviour set, scare-marker, chapter system, alt-fire, scarcity-tuning helper |
| Replay (user-facing) | Playback transport, ghost overlay, replay-camera, editor panel, MP4 export hook (consumes Phase 12 ffmpeg integration) |

### 2.2 Out of scope

| Item | Where it lives |
|---|---|
| Camera shake / screen flash subsystems | Phase 11A (Phase 11B *consumes* — does not re-implement) |
| Save-file zstd compression primitive | Phase 11A — Phase 11B fills the chunks |
| Replay recording / determinism contract / `ReplayRecorder` / `ReplayPlayer` | Phase 11A — Phase 11B is the *playback / editor / export* surface only |
| Behaviour tree runtime + AI perception primitives | Phase 11A — Phase 11B authors *trees* and *perception consumers*, not the runtime |
| Camera modes (first-person / third-person / isometric / top-down / cinematic / 1st↔3rd toggle) | Phase 10.8 — vehicle / horror cameras are *presets* layered over the existing mode system |
| Decal authoring + render pipeline | Phase 10.8 — Phase 11B uses the presets (blood splatter / bullet hole / scorch) |
| PP suite (vignette, distortion, blur, chromatic aberration, desaturation, depth-of-field) | Phase 10.8 — Phase 11B *triggers* presets via runtime flags |
| ffmpeg integration | Phase 12 — Phase 11B replay editor is the first consumer |
| Networking / multiplayer / co-op networking primitives | Phase 20 — co-op horror only sketched as a deferred surface |
| AI Director pacing / cutscene / dialogue authoring | Phase 16 — Phase 11B encounters reuse 11A behaviour-tree runtime, not 16's higher-level layers |
| Editor camera (`EditorCamera`) changes | unchanged from Phase 10.8 |

### 2.3 Honest scope notes

- **Physics-vehicle baseline.** Jolt ships `VehicleConstraint` + `WheeledVehicleController`; Vestige already integrates Jolt (see `engine/physics/physics_world.{h,cpp}`). Phase 11B wraps these in a `VehicleComponent` rather than implementing a vehicle solver from scratch.
- **Tyre model.** Pacejka "Magic Formula" coefficients are public reference data; the **Formula Workbench** (`tools/formula_workbench/`, mandatory per CLAUDE.md Rule 6) is the authoring surface — coefficient tables ship as `formula_library` presets, not hand-coded magic constants.
- **Force-feedback (FFB) shim.** GLFW exposes joystick *input* but not haptic *output*. The shim in this phase plans the *binding* (`InputDevice::SteeringWheel` enum, axis names, button mapping through existing `InputBindings`); the actual FFB output backend (Simple DirectMedia Layer 2 (SDL2) haptic / libuinput) is a follow-up slice flagged as deferred in §12.
- **Co-op horror.** Phase 11B records the per-scene authoring needs (split inventory, divergent perspectives, revive) but does not ship them — they unblock once Phase 20 networking lands.
- **Diegetic UI.** Requires a *new* `UIElement::worldProjection` surface in `engine/ui/ui_in_world.{h,cpp}`; today only `engine/ui/ui_world_projection.{h,cpp}` and `engine/ui/ui_world_label.{h,cpp}` exist (rendering-side world-space text labels). Bone-socket binding is unbuilt today and lands as the *first* horror-polish slice (gates the diegetic-UI bullets that follow).

---

## 3. Architecture overview

### 3.1 Subsystem map

```
Phase 11B (this doc)
├─ engine/gameplay/
│   ├─ weapon_component.{h,cpp}              [NEW]  hitscan / projectile / melee
│   ├─ health_component.{h,cpp}              [NEW]  per-entity HP + status effects
│   ├─ status_effect.{h,cpp}                 [NEW]  bleeding / burning / stun / poisoned
│   ├─ inventory_component.{h,cpp}           [NEW]  grid storage + weight + stacks
│   ├─ pickup_component.{h,cpp}              [NEW]  world entity → inventory transfer
│   ├─ loot_table.{h,cpp}                    [NEW]  weighted random drops
│   └─ damage_event.h                        [NEW]  EventBus payload for hits + status
├─ engine/save/
│   ├─ save_writer.{h,cpp}                   [NEW]  fills 11A compressed chunks
│   ├─ save_reader.{h,cpp}                   [NEW]
│   ├─ checkpoint_volume.{h,cpp}             [NEW]  trigger volume → autosave
│   └─ save_slot_manager.{h,cpp}             [NEW]  metadata + thumbnail
├─ engine/hazard/
│   ├─ hazard_zone_component.{h,cpp}         [NEW]  fire / electric / toxic / vacuum / radiation
│   └─ interactive_hazard.{h,cpp}            [NEW]  barrel / panel / vent / collapse
├─ engine/vehicle/
│   ├─ vehicle_component.{h,cpp}             [NEW]  Jolt WheeledVehicleController wrap
│   ├─ tyre_model.{h,cpp}                    [NEW]  Pacejka long+lat (Workbench-fit)
│   ├─ drivetrain.{h,cpp}                    [NEW]  engine torque → gearbox → diff
│   ├─ aero.{h,cpp}                          [NEW]  drag + downforce + slipstream
│   ├─ vehicle_damage.{h,cpp}                [NEW]  cosmetic + functional tiers
│   ├─ vehicle_camera_preset.{h,cpp}         [NEW]  configures Phase 10.8 modes
│   ├─ arcade/
│   │   ├─ boost_meter.{h,cpp}               [NEW]  near-miss / drift / draft / airtime
│   │   ├─ takedown_classifier.{h,cpp}       [NEW]  collision-angle → score
│   │   ├─ traffic_ai.{h,cpp}                [NEW]  splined civilian routes
│   │   └─ game_mode.{h,cpp}                 [NEW]  Road Rage / Crash / Burning Lap / GP
│   └─ sim/
│       ├─ tyre_thermal.{h,cpp}              [NEW]  wear % + temp window
│       ├─ fuel_model.{h,cpp}                [NEW]
│       ├─ pit_stop.{h,cpp}                  [NEW]  service menu + time cost
│       ├─ tuning_setup.{h,cpp}              [NEW]  per-car coefficients
│       ├─ licence_test.{h,cpp}              [NEW]  scripted-challenge harness
│       ├─ opponent_ai.{h,cpp}               [NEW]  racing-line + braking-points
│       ├─ track_spline.{h,cpp}              [NEW]  banking + sectors + surface
│       ├─ lap_timer.{h,cpp}                 [NEW]
│       └─ photo_mode.{h,cpp}                [NEW]  pause + free-orbit + LUT picker
├─ engine/horror/
│   ├─ kinesis_component.{h,cpp}             [NEW]  charge + throw (uses experimental/grab_system)
│   ├─ stasis_weapon.{h,cpp}                 [NEW]  weapon-shape over experimental/stasis_system
│   ├─ zero_g_controller.{h,cpp}             [NEW]  6DOF + Magnetic-boot mode
│   ├─ rig_health_spine.{h,cpp}              [NEW]  reads HealthComponent → 5-segment glow
│   ├─ scare_marker.{h,cpp}                  [NEW]  scripted stinger trigger
│   └─ chapter_system.{h,cpp}                [NEW]  named subregion + autocheckpoint
├─ engine/replay/
│   ├─ replay_player.{h,cpp}                 [NEW]  playback transport (uses 11A recorder)
│   ├─ replay_camera.{h,cpp}                 [NEW]  free-fly orbit / dolly / spline
│   ├─ ghost_overlay.{h,cpp}                 [NEW]  translucent ghost-car render
│   └─ replay_export.{h,cpp}                 [NEW]  feeds Phase 12 ffmpeg pipeline
└─ engine/ui/
    └─ ui_in_world.{h,cpp}                   [NEW]  world-space UI bound to bone socket
                                                    (extends existing ui_world_projection)
```

### 3.2 Cross-references to existing code

| Existing surface | Used by 11B for | Notes |
|---|---|---|
| `engine/physics/physics_world.{h,cpp}` (Jolt) | vehicle base, hitscan raycast, sphereCast (Phase 10.8 W1), kinesis pickup, hazard volume queries | Phase 10.8 adds `sphereCast`; weapon hitscan + grenade arcs reuse it |
| `engine/physics/physics_character_controller.{h,cpp}` | zero-G traversal — extend with `GravityMode` enum | not a new controller — extension |
| `engine/audio/audio_music.h` (`MusicStingerQueue`) | scare-marker, takedown-cam stinger, chapter intro | already shipped (`audio_music.h:144`) |
| `engine/audio/audio_ambient.{h,cpp}` | hazard ambient beds, pit-lane crowd, engine room hum | shipped |
| `engine/audio/audio_source_component.{h,cpp}` | weapon fire / impact / engine RPM-driven sources | shipped — Phase 10.7 wired bus routing |
| `engine/scene/particle_emitter.{h,cpp}` | muzzle flash, blood, impact dust, fire / electric / toxic FX | shipped |
| `engine/systems/destruction_system.{h,cpp}` | exploding barrels, vehicle cosmetic damage, *Crashbreaker* | shipped (Phase 8) |
| `engine/experimental/physics/grab_system.{h,cpp}` | Kinesis pickup leg | currently `experimental/`; promotion to non-experimental tracked as Open Q §12.2 |
| `engine/experimental/physics/stasis_system.{h,cpp}` | stasis-weapon trigger | same caveat as grab_system |
| `engine/animation/skeleton_animator.{h,cpp}` | per-bone damage zones, ragdoll death, zero-G magnetic-boot orientation snap | shipped |
| `engine/scene/camera_component.{h,cpp}` + Phase 10.8 `CameraMode` | every vehicle / horror / replay camera *preset* | Phase 10.8 ships modes; 11B authors presets |
| `engine/ui/ui_world_projection.{h,cpp}`, `engine/ui/ui_world_label.{h,cpp}` | base for world-space UI | new `ui_in_world` extends — does not replace |
| `engine/input/input_bindings.{h,cpp}` | every gameplay action (fire / reload / interact / boost / brake / handbrake / paddle-shift) | shipped — bindings rebindable from day one |
| `engine/formula/formula_library.{h,cpp}` | tyre Pacejka, drivetrain torque curves, aero `Cd / Cl / A`, status-effect DoT curves, AI braking-point tables | per CLAUDE.md Rule 6 — **mandatory** |
| `engine/utils/catmull_rom_spline.{h,cpp}` + `engine/environment/spline_path.{h,cpp}` | track centreline, traffic routes, replay-camera dolly, opponent racing line | shipped |
| `engine/accessibility/photosensitive_safety.{h,cpp}` clamp helpers | weapon muzzle flash, hazard tint pulses, boost-FOV-kick, crash-cam slow-mo replacement, hit flash, low-health screen pulse | shipped (Phase 10.7) — every visual punch goes through these |

### 3.3 Event surfaces

| Event | Producer | Consumer |
|---|---|---|
| `WeaponFiredEvent` | `WeaponComponent::fire()` | `MuzzleFlashSystem`, `AudioSystem`, AI perception (hearing) |
| `EntityDamagedEvent` | hit detection / hazard tick | `HealthComponent`, screen-flash + camera-shake (via 11A), AI alert propagation |
| `EntityDiedEvent` | `HealthComponent::onZero` | ragdoll, loot drop, AI faction state, save-state autosave |
| `ItemPickedUpEvent` | `PickupComponent::onInteract` | `InventoryComponent`, audio cue, screen-flash green pulse |
| `CheckpointReachedEvent` | `CheckpointVolume::onEnter` | `SaveWriter`, chapter system |
| `VehicleCrashEvent` | `VehicleDamage::onImpactSeverity > T` | crash-camera, takedown classifier (arcade), pit-stop trigger (sim) |
| `BoostActivatedEvent` (arcade) | `BoostMeter::onSpend` | FOV kick (Phase 10.8 PP), audio filter sweep |
| `LapCompletedEvent` (sim) | `LapTimer` | timing HUD, replay sector marker |
| `ChapterEnteredEvent` | `ChapterSystem::onEnter` | title-card prefab, autosave, audio stinger |

All events flow through the existing `EventBus` (per ARCHITECTURE.md "Subsystem + Event Bus") — no new dispatcher.

---

## 4. Steps / slices

Eight ordered slices, grouped by archetype. Slices A–E unblock the action / RPG archetype; F unblocks horror polish; G–H unblock racing. Each slice is review-sized (rough LOC in the table is the design-time estimate, not a contract — final size emerges from the slice's own design pass).

| # | Slice | Depends on | Outcome |
|---|---|---|---|
| **A** | Combat + Health + Damage events | Phase 10.8 PP, Phase 11A camera shake / screen flash | A scripted demo: fire weapon, hit dummy with per-bone damage, dummy bleeds + dies, screen flashes red on hit, low-health audio cue triggers |
| **B** | Inventory + Pickups + Equipment | Slice A (consumables hook into Health) | Drop a health-pack prefab, walk over, prompt-press, inventory updates, use → heal |
| **C** | Save / Checkpoint | Phase 11A `writeCompressedChunk`; Slices A+B (state to serialize) | Quick-save F5, quit, relaunch, quick-load F9, world identical (entity positions, health, inventory, doors) |
| **D** | Environmental hazards | Slice A (damage path) | Step into fire zone → take burning DoT, see fire particles + screen tint; barrel explodes on shot → AoE damage |
| **E** | Replay playback (user-facing) | Phase 11A `ReplayRecorder` / `ReplayPlayer` infra; Phase 10.8 cinematic camera | Load `.vreplay`, scrub timeline, swap to free-orbit replay-camera, play back at 0.25–4× |
| **F** | Horror polish | Slices A+B+C; new `UIElement::worldProjection` is *first sub-slice within F* (gates F2..F11) | Dismember + stasis + kinesis + zero-G traversal demo with diegetic RIG health spine, no screen-space HUD |
| **G** | Vehicle core + arcade racing | Slices A (damage) + C (save); Phase 10.8 cameras | Drive a tuned car on a splined track, near-miss / drift / draft / airtime fill boost, takedowns score, traffic AI behaves |
| **H** | Sim racing additions + photo mode | Slice G; Slice E (replay infra for ghost) | Tyre wear degrades grip, fuel drops on throttle, pit-stop service menu, ghost overlay, photo mode with aperture/shutter/LUT |

**Critical path:** A → B → C unblocks the generic-action archetype. F is independent of G/H. G ships before H (sim builds on arcade).

**Slice ordering rationale.** Combat / health is the largest blast-radius surface and gates everything else with a damage path. Save lands third because it's the smallest add per system already-extant — and because slice D / G / H all need state to serialize. Horror polish (F) is intentionally not first: it requires the world-space UI surface that doesn't exist today, and the survival-horror archetype is the easiest to demo *after* generic combat works.

---

## 5. CPU / GPU placement (per CLAUDE.md Rule 7)

Decision per workload, applying the project heuristic — *per-pixel / per-vertex / per-particle / per-froxel → GPU; branching / sparse / decision / I/O → CPU.*

| Workload | Placement | Reason |
|---|---|---|
| Weapon fire decision (which weapon, ammo check, fire-rate gate) | CPU | sparse + branching; ≤handful of fire events / frame |
| Hitscan raycast (Jolt) | CPU (physics thread) | Jolt-side; same path as Phase 10.8 wall-probe sphereCast |
| Per-bone damage-zone lookup | CPU | scalar — check which zone the hit point falls in given the skeleton pose |
| Projectile flight (grenade / bolt) | CPU (physics thread) | Jolt rigid body; ≤dozens / frame typical |
| Status-effect tick (bleeding / burning DoT) | CPU | per-entity scalar; bounded by entity count |
| Muzzle / impact / blood particle systems | GPU (compute + draw) | per-particle work; reuses existing GPU particle path (`engine/scene/gpu_particle_emitter.{h,cpp}`) |
| Decal placement (bullet hole / blood splatter) | CPU sparse + GPU render | Phase 10.8 owns the pipeline; 11B is just trigger sites |
| Inventory tick (drag-drop UI hover) | CPU | UI / event-driven |
| Loot-table sample on entity death | CPU | one weighted-random draw per kill |
| Save-state serialize | CPU | I/O + compression (zstd via 11A) |
| Hazard-volume entity overlap query | CPU (physics thread) | broadphase via Jolt; results consumed scalar-side |
| Hazard ambient particle emit | GPU | reuses GPU particle path |
| Vehicle suspension step | CPU (physics thread) | 4 wheels × 1 vehicle × tick — Jolt `WheeledVehicleController` runs scalar |
| Tyre Pacejka evaluation (long + lat slip → force) | CPU | 4 wheels × tick — branching on compound + temp; Workbench-authored coefficient tables |
| Drivetrain torque chain | CPU | scalar — engine RPM → gear → diff → wheel torque |
| Aero drag + downforce + slipstream | CPU | one drag force per vehicle per tick |
| Tyre thermal model (sim) | CPU | per-wheel scalar; Workbench-fit |
| Fuel consumption (sim) | CPU | scalar |
| Boost-meter accumulation (arcade) | CPU | event-driven (near-miss raycast triggers, drift threshold checks) |
| Takedown classifier | CPU | one collision event → angle / speed branch |
| Traffic AI tick | CPU | spline-follow scalar per civilian car (bounded by `maxTrafficCount`) |
| Opponent racing-line AI tick | CPU | spline-walk + decision tree per opponent |
| Lap / sector timing | CPU | scalar |
| Replay playback scrub | CPU | snapshot decode + interpolate |
| Replay-camera path (free-orbit / dolly / spline) | CPU | scalar |
| Ghost-car render | GPU (existing skinned mesh path) | re-uses character draw with translucency override |
| MP4 export (Phase 12) | CPU | offline; ffmpeg subprocess |
| Photo Mode aperture / DoF | GPU | already on GPU as part of Phase 10.8 PP suite — Photo Mode just unlocks runtime sliders |
| RIG health-spine glow shader | GPU (frag) | per-pixel intensity from `HealthComponent::pct` uniform |
| Diegetic UI quad render | GPU (existing 2D batch with world-space transform) | per-vertex transform + per-pixel sampling |
| Scan-line / chromatic-flicker on holographic panels | GPU (frag) | per-pixel UV warp + sampler — Phase 10.8 PP chromatic-aberration source effect |

**No dual CPU+GPU implementations are introduced this phase.** Tyre / drivetrain / aero stay CPU because the per-vehicle scale (1 to ~32 cars) and branching profile fail the Rule-7 GPU heuristic. If a future archetype demands hundreds of vehicles (large open-world traffic) a GPU traffic-AI pass becomes a candidate — explicitly out of scope here.

---

## 6. Performance budget

The 60 frames-per-second (FPS) hard requirement (CLAUDE.md) gives a 16.6 ms / frame budget. Phase 11B's per-frame slice targets:

| Subsystem | Target | Rationale |
|---|---|---|
| Combat (per-frame fire-rate gates + status-effect tick) | < 0.20 ms | bounded by ≤200 active combatants × scalar tick |
| Health-component update | < 0.05 ms | regen + status DoT — pure arithmetic per entity |
| Inventory update | negligible (UI event-driven) | only ticks when inventory UI is open |
| Save-state serialize (one-shot, manual save) | < 80 ms (one-shot, off the per-frame budget) | budgeted as a stutter the player asked for; auto-save runs on a worker thread to keep the frame clean |
| Hazard-volume tick | < 0.10 ms | broadphase overlap + per-entity DoT |
| Vehicle physics step (1 player car + ≤7 AI / traffic) | < 1.0 ms | Jolt is the cost; scalar Pacejka per wheel adds ~0.05 ms × 32 wheels |
| Vehicle physics step (Burnout city scene) | **conditional on Q5 (vehicle count ceiling):** < 2.5 ms at 32 vehicles, < 1.25 ms at 16 vehicles. Pick the right cell once Q5 is decided. Upper bound for the demo target — exceeding triggers a vehicle-LOD strategy spike. |
| Boost / takedown / traffic AI tick (arcade) | < 0.30 ms | event-driven + spline walks |
| Tyre thermal + fuel + lap timer (sim) | < 0.10 ms | scalar |
| Opponent AI tick (sim, ≤7 cars) | < 0.20 ms | racing-line walk + decision tree |
| Horror — RIG spine glow shader | negligible | per-pixel cost folded into existing PP composite |
| Horror — diegetic UI quads (≤16 active) | < 0.25 ms | reuses existing 2D batch path |
| Replay playback decode + interp | < 0.30 ms | bounded to keep playback at game-tick rate |
| Photo Mode | game-paused (budget irrelevant) | photo mode pauses simulation by spec |

**Headroom.** Combined steady-state Phase 11B per-frame additions stay under ~5 ms in the worst-case scene (32-vehicle arcade with full AI), leaving the rest of the 16.6 ms for renderer + audio + scripts. Individual subsystem budgets are *upper bounds*; routine cases run far below. Numbers are design-time estimates — measurements will land per-slice and replace the targets in the slice's own retrospective.

**Profiler markers.** Each subsystem registers a `glPushDebugGroup` label per CODING_STANDARDS §29 (`gameplay/combat`, `gameplay/inventory`, `vehicle/physics`, `vehicle/ai`, `horror/diegetic_ui`, `replay/playback`).

---

## 7. Accessibility

Phase 11B touches user-facing input, audio, and visual surfaces heavily — every subsystem below carries an accessibility constraint, and every visual punch routes through the Phase 10.7 `PhotosensitiveLimits` clamps before reaching the screen.

### 7.1 Visual

| Surface | Constraint | Source |
|---|---|---|
| Hit-flash overlay (red, on damage taken) | peak alpha clamped via `clampFlashAlpha` (Phase 11A consumer) | photosensitive |
| Boost-meter activation FOV kick | duration / magnitude clamped via `PhotosensitiveLimits.shakeAmplitudeScale`-equivalent + `reducedMotion` skip | photosensitive + `reducedMotion` |
| Crash-cam slow-mo (arcade) | when `accessibility.reducedMotion` is on, slow-mo replaced with instant cut (per ROADMAP line 948) | `reducedMotion` |
| Status-effect screen distortion (poison / blur / chromatic aberration) | passes through Phase 10.8 PP suite which honours photosensitive caps | photosensitive |
| Low-health desaturation + heartbeat pulse | desaturation amount + pulse Hz clamped by `clampStrobeHz` | photosensitive |
| Diegetic UI as the *only* HUD path (horror) | accessibility fallback: `Settings.gameplay.diegeticUiOnly = false` re-enables a screen-space HUD for partially-sighted players | first-class accessibility surface |
| RIG health-spine peripheral glow (first-person) | colour-blind friendly palette (full-bright not red-on-dark) — uses accent palette `{0.784, 0.604, 0.243}` (warm amber) the partially-sighted user already prefers per Phase 10.7 narrator-style decision | colour-blind |
| Crosshair / reticle | scalable + 4 colour presets (white / yellow / cyan / amber); never red-only | `Settings.accessibility.crosshairColor` |
| Damage indicator (directional) | duplicates direction with audio panning + arrow icon; never colour-only | duplicate channel |

### 7.2 Audio

| Surface | Constraint |
|---|---|
| Weapon fire / impact / footstep audio | feeds AI hearing; routed through `audio_source_component` bus (Phase 10.7) so master / SFX / voice gain settings apply |
| Engine RPM + skid-chirp + backfire (vehicle) | crossfade through existing `audio_music` primitives; SFX bus respected |
| Subtitles for any spoken dialogue | mandatory caption-map entry per Phase 10.7 §4.2 caption-map schema; sound-cue captions for `[engine roar]` / `[gunshot]` etc. (not just dialogue) |
| Low-health heartbeat | distinct from rhythm-game / dance audio — has a `[low health]` sound-cue caption when subtitles enabled |
| Music-stinger queue scares (`MusicStingerQueue`) | volume capped by photosensitive surrogate (`max stinger gain`) when safe-mode is on — surface is the same volume slider |

### 7.3 Input

| Constraint | Where |
|---|---|
| Every gameplay action (fire / reload / interact / boost / brake / handbrake / paddle-shift / kinesis / stasis / zero-G thrust) is rebindable through `engine/input/input_bindings.h` from day one | every component using `InputActionMap` |
| No quick-time events (QTE) requiring sub-200ms reaction | spec rule for the encounter / scare authoring path |
| No time-pressure puzzles | spec rule (matches CODING_STANDARDS / a11y guidance) |
| Gamepad + keyboard parity for every action | every component spec |
| Steering-wheel + paddle-shift exposed via `InputDevice::SteeringWheel` enum so users with motor-control aids can use a wheel without losing menu / pause access | `input_bindings.h` enum extension |
| Hold-to-interact has a "tap to interact" accessibility toggle | `Settings.accessibility.holdToTap` |
| Vehicle driving assists (sim) — every assist (Anti-lock Braking System (ABS), Traction Control System (TCS), Electronic Stability Control (ESC), active steering, braking-line overlay) is a 4-step slider (Off / Weak / Standard / Strong), not binary; "Pro" preset turns everything off | `tuning_setup.h` |

### 7.4 Motion

| Surface | Behaviour when `reducedMotion = true` |
|---|---|
| Crash-cam slow-motion (arcade) | replaced with instant cut |
| Camera-shake from weapon recoil / impact | amplitude scaled by `shakeAmplitudeScale` (Phase 10.7 clamp); when `reducedMotion` is on, additionally clamped to a fixed low ceiling |
| Boost FOV kick | duration shortened to a single frame settle (no oscillation) |
| Photo-mode camera-fly while paused | viewport stays static unless the user moves it (no auto-orbit) |
| Diegetic UI scan-line / flicker | flicker speed clamped via `clampStrobeHz` |
| Death-fade overlay | `reducedMotion = true` → cross-cut instead of fade |

### 7.5 Cognitive load

| Surface | Constraint |
|---|---|
| Inventory / save-slot / vehicle-tuning UIs | all use `engine/ui/ui_accessible.{h,cpp}` focus-ring + screen-reader-friendly tab order (already shipped) |
| Tutorial prompts (interact / pickup / weapon-swap) | persistent, not flash-once; can be disabled per `Settings.accessibility.persistentPrompts` |
| Combat HUD readouts (ammo / health) — when *not* in horror diegetic-UI mode | high-contrast pair (foreground / background passes WCAG AA 4.5:1 contrast at the smallest text size) |

---

## 8. Testing strategy

### 8.1 Unit-test coverage per slice

| Slice | Test files (planned) |
|---|---|
| A | `test_weapon_component.cpp`, `test_health_component.cpp`, `test_status_effect.cpp`, `test_damage_event_routing.cpp`, `test_per_bone_damage_zone.cpp` |
| B | `test_inventory_component.cpp`, `test_pickup_interact.cpp`, `test_consumable_use.cpp`, `test_loot_table_distribution.cpp` |
| C | `test_save_writer_round_trip.cpp`, `test_save_reader_version_mismatch.cpp`, `test_checkpoint_volume_autosave.cpp`, `test_save_slot_metadata.cpp` |
| D | `test_hazard_zone_dot.cpp`, `test_interactive_barrel_chain_explosion.cpp`, `test_vacuum_zone_object_pull.cpp` |
| E | `test_replay_player_scrub.cpp`, `test_replay_camera_dolly.cpp`, `test_ghost_overlay_render.cpp` |
| F | `test_kinesis_grab_throw.cpp`, `test_stasis_weapon_ammo_pool.cpp`, `test_zero_g_gravity_mode_switch.cpp`, `test_rig_spine_glow_threshold.cpp`, `test_chapter_autocheckpoint.cpp`, `test_world_space_ui_bone_socket.cpp` |
| G | `test_vehicle_component_basic.cpp`, `test_tyre_model_pacejka.cpp` (against published coefficient tables — research-citation §10), `test_drivetrain_gear_shift.cpp`, `test_aero_drag_quadratic.cpp`, `test_boost_meter_near_miss.cpp`, `test_takedown_classifier_angles.cpp`, `test_traffic_ai_lane_change.cpp` |
| H | `test_tyre_thermal_window.cpp`, `test_fuel_consumption_throttle.cpp`, `test_pit_stop_service_time.cpp`, `test_lap_timer_sectors.cpp`, `test_opponent_ai_braking_point.cpp`, `test_photo_mode_aperture.cpp` |

### 8.2 Integration / behaviour harnesses

- **Damage round-trip harness** — fire scripted weapon at scripted dummy, assert `EntityDamagedEvent` fires with correct bone + amount, `HealthComponent` decreases, screen-flash and shake events queue at the 11A consumer.
- **Save round-trip harness** — populate a scene, save, mutate state, load, assert deep equality on entity / scene / player state.
- **Replay determinism harness (consumer)** — extends Phase 11A's recorder/player CI check with playback-side scrubs; assert frame-N state matches recorded snapshot to epsilon.
- **Tyre-coefficient parity test** — load published Pacejka coefficient table for a reference tyre (`Pacejka94 — Magic Formula slick`), evaluate at known slip-angle points, assert force values match within 1% tolerance.
- **Opponent-AI scenario harness** — scripted track segments (car ahead braking late on straight, overtaking opportunity on inside line); assert the AI takes the documented decision (commit / abort / follow) deterministically.
- **Boost-meter scenario harness** — simulate near-miss / drift / draft / airtime sequences; assert meter-fill within tuning tolerance.

### 8.3 Visual regression

Hooked into the existing `engine/testing/visual_test_runner.{h,cpp}`. New scenes per archetype:
- `tests/visual/horror_diegetic_demo.scene` — RIG spine + ammo display + holographic door panel
- `tests/visual/arcade_takedown_demo.scene` — boost-active collision → takedown cam
- `tests/visual/sim_pit_stop_demo.scene` — pit-lane entry + service menu
- `tests/visual/replay_ghost_demo.scene` — ghost car overlay alongside live car

### 8.4 Coverage gaps acknowledged

- **Force-feedback (FFB) output.** No CI hardware emulation for steering-wheel haptics. The shim's binding side is unit-tested; the actual haptic-driver call sites are visual-only at first, gated as deferred per §12.5.
- **Co-op horror.** Untestable until Phase 20 networking lands; the per-scene authoring schema gets a *schema* test (parse the flag, no runtime path).
- **Photo Mode export pixel-exact.** Output PNG bit-equality across drivers is famously fragile — the test asserts dimensions + format + LUT name, not pixel hashes.

---

## 9. Dependencies

### 9.1 Engine subsystems (existing, consumed)

| Subsystem | Why |
|---|---|
| `engine/core/` (`Engine`, `EventBus`, `SystemRegistry`) | every gameplay component plugs in |
| `engine/scene/` (`Scene`, `Entity`, `Component`, `CameraComponent`) | component model + camera surfaces |
| `engine/physics/` (Jolt) | hitscan, projectile, sphereCast, vehicle constraint, hazard overlap |
| `engine/animation/` | per-bone damage, ragdoll, magnetic-boot orientation snap |
| `engine/audio/` (`audio_source_component`, `audio_music`, `audio_ambient`) | weapon SFX, engine sounds, scares |
| `engine/ui/` (`ui_accessible`, `ui_world_projection`, `ui_world_label`, plus new `ui_in_world`) | inventory / save-slot / tuning / diegetic |
| `engine/input/` (`InputActionMap`, `input_bindings`) | rebindable actions |
| `engine/formula/` (`formula_library`, `formula`) | tyre Pacejka, torque curves, aero, status DoT — Workbench is the authoring path |
| `engine/utils/catmull_rom_spline`, `engine/environment/spline_path` | track / traffic / replay-camera |
| `engine/scene/particle_emitter`, `engine/scene/gpu_particle_emitter` | muzzle / impact / hazard FX |
| `engine/systems/destruction_system` | barrels, vehicle damage, Crashbreaker |
| `engine/accessibility/photosensitive_safety` | every visual punch passes through |
| `engine/experimental/physics/grab_system` | Kinesis core (promotion to non-experimental tracked in §12.2) |
| `engine/experimental/physics/stasis_system` | stasis-as-weapon core |
| `engine/testing/visual_test_runner` | per-archetype visual regression scenes |

### 9.2 Engine subsystems (new — built in 11B)

Per §3.1 — `engine/gameplay/`, `engine/save/`, `engine/hazard/`, `engine/vehicle/`, `engine/horror/`, `engine/replay/`, plus the `engine/ui/ui_in_world.{h,cpp}` extension.

### 9.3 Engine subsystems (consumed but not yet shipped — blocking)

| Surface | Owning phase | Phase 11B behaviour if missing |
|---|---|---|
| `CameraShakeSystem` | Phase 11A | every weapon recoil / impact / crash-cam triggers degrade silently — slice A waits for 11A |
| `ScreenFlashSystem` | Phase 11A | hit-flash / pickup-flash / death-fade are no-ops — slice A waits |
| `writeCompressedChunk` / `readCompressedChunk` | Phase 11A | save / load fall back to uncompressed JSON dev-only — slice C waits |
| `ReplayRecorder` / `ReplayPlayer` | Phase 11A | slice E entirely blocked |
| Behaviour-tree runtime | Phase 11A | enemy / opponent / traffic AI implemented as state machines (downgrade) until 11A lands |
| AI perception | Phase 11A | every AI consumer downgrades to spherical-aggro (range-only) until 11A lands |
| Camera modes (1st / 3rd / iso / topdown / cinematic) | Phase 10.8 | vehicle / horror / replay cameras can't compose — slices E/F/G/H all block |
| Decal System | Phase 10.8 | blood / bullet-hole / scorch-mark presets degrade to particle-only — slice A's hit-feedback degrades |
| PP suite (vignette / distortion / blur / chromatic / desaturation) | Phase 10.8 | status-effect screen distortion + low-health desaturation + boost FOV kick degrade |
| ffmpeg integration | Phase 12 | replay MP4 export blocked — slice E's editor panel ships everything except the export button |

### 9.4 External libraries

| Library | Usage | Notes |
|---|---|---|
| GLFW | input | already integrated |
| GLM | math | already integrated |
| Jolt Physics | vehicle / hitscan / hazard / grab / kinesis | already integrated; vehicle wraps `WheeledVehicleController` |
| zstd | save / replay compression | vendored by Phase 11A — no separate dep |
| ImGui | editor inventory / save-slot / tuning panels | already integrated |
| stb_image | save-slot thumbnails | already integrated |
| Possibly SDL2 (haptic) or libuinput | force-feedback shim | **deferred** — see §12.5 |

---

## 10. References

Web research sources, all dated within the last twelve months. Acronyms first-used per SPEC_TEMPLATE "Style conventions".

### 10.1 Vehicle physics

- Jolt Physics — `WheeledVehicleController` documentation: https://jrouwe.github.io/JoltPhysics/class_wheeled_vehicle_controller.html
- Jolt Physics — `VehicleConstraint` source / sample (`Samples/Vehicle/`): https://github.com/jrouwe/JoltPhysics/tree/master/Samples/Vehicle
- *Pacejka Magic Formula* primer (TNO Automotive, public reference): https://www.tno.nl/en/sustainable/safe-traffic-vehicles/safe-vehicles-vehicle-development/pacejka-tyre-models/

### 10.2 Arcade racing — Burnout 3 / Revenge

- Eurogamer — *Burnout 3: Takedown* retrospective on the boost-from-risk mechanic and takedown classifier (Criterion's design pillar): https://www.eurogamer.net/burnout-3-takedown-retrospective
- Digital Foundry — *Burnout: Revenge* technical retrospective: https://www.eurogamer.net/digitalfoundry-burnout-revenge-retrospective

### 10.3 Sim racing — *Gran Turismo*

- Hans B. Pacejka — *Tyre and Vehicle Dynamics* (3rd ed., Butterworth-Heinemann) — canonical engineering reference for the Magic Formula. The §8.2 parity test loads against the published coefficient ranges from this book, not against any single dev-diary citation.
- TNO Automotive — *MF-Tyre / MF-Swift* documentation (commercial implementation of the Pacejka model, public spec PDFs): https://www.tno.nl/en/sustainable/safe-clean-mobility/road-vehicle-engineering/road-vehicle-modelling/mf-tyre-mf-swift/
- Polygon — *Gran Turismo 7* update notes archive (tyre / fuel / penalty model tweaks 2024–2025): https://www.polygon.com/gran-turismo-7 — used as a pop-press cross-check for "what the user community feels," not as an engineering source.

### 10.4 Survival horror — Dead Space archetype

- Eurogamer — *Dead Space Remake* design / accessibility review (RIG, diegetic UI, kinesis, stasis): https://www.eurogamer.net/dead-space-remake-review
- Can I Play That? — *Dead Space Remake* accessibility review (over-the-shoulder camera fixed, diegetic-UI fallback considerations): https://caniplaythat.com/2023/03/06/dead-space-remake-accessibility-review/

### 10.5 Inventory / RPG patterns

- Game Developer (Gamasutra archive) — *Resident Evil* / *Dead Space* tetris-grid inventory design retrospective: https://www.gamedeveloper.com/design/inventory-design-resident-evil-dead-space
- Unity — *Open Project: Inventory* sample (recent reference for grid + drag-drop patterns): https://github.com/UnityTechnologies/open-project-1

### 10.6 Save / checkpoint

- Microsoft Game Stack — *Modernizing Save Games on Xbox* (2024): https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/system/overviews/connected-storage/connected-storage-overview
- Facebook zstd — format spec + tuning guide (used here via 11A integration): https://github.com/facebook/zstd

### 10.7 Replay

- Forza Motorsport blog index (Turn 10) — *index, not an archival anchor*; cite the specific post URL once a replay-design article from 2024+ is identified during slice E1 implementation: https://forza.net/news
- Polyphony Digital news index (GT Sport / GT 7 official press archive) — *index, not an archival anchor*; same caveat as Forza: pin a specific article URL during slice E1: https://www.gran-turismo.com/world/news/

### 10.8 Behaviour trees and AI perception (consumed from Phase 11A)

- Phase 11A primary references stand. Citations to specific Bobby Anguelov / Game AI Pro talks are deliberately not duplicated here — see `phase_11a_design.md` §10 for the BT + perception canonical references.

### 10.9 Force-feedback / steering wheels

- SDL2 haptic API documentation: https://wiki.libsdl.org/SDL2/CategoryForceFeedback
- libuinput documentation (`man 4 uinput`): https://www.kernel.org/doc/html/latest/input/uinput.html

### 10.10 Accessibility (gameplay-specific)

- Game Accessibility Guidelines — combat / inventory / motion sections: https://gameaccessibilityguidelines.com/
- Xbox Accessibility Guideline 118 (photosensitivity): https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/118

---

## 11. Open questions (blocking approval)

Numbered for review-comment reference. Each maps back to a ROADMAP §11B item the doc has flagged as not-yet-decided.

1. **Slice ordering — A → B → C → D → E → F → G → H?** Alternative: front-load horror polish (F) for early demo at the cost of longer time-to-generic-action. Doc recommends combat-first because the damage path gates everything else.

2. **Kinesis / stasis — promote `engine/experimental/physics/grab_system` and `stasis_system` to non-experimental as part of slice F1, or keep them in `experimental/` and have horror code consume them across the boundary?** Doc recommends *promote* — a shipping Phase 11B feature shouldn't reach into `experimental/`. Promotion has its own (small) audit pass. Confirm.

3. ~~**World-space UI surface (`UIElement::worldProjection`) — extend existing or sibling?**~~ **DECIDED — sibling `ui_in_world.{h,cpp}`** (committed in §3.1 + §2.3). Rationale: `ui_world_projection` and `ui_world_label` are pure projection helpers; `ui_in_world` adds bone-socket binding + world-anchored composition logic, a different concern. Two-file split keeps each header narrow.

4. **Diegetic UI default — for the horror archetype, ship with diegetic-only HUD ON by default and the screen-space fallback as the accessibility opt-in, or the inverse?** ROADMAP says "no HUD is drawn in screen-space for this archetype". Accessibility says the opt-in must be friction-free for partially-sighted users. Doc recommends *diegetic on by default, fallback in `Settings.accessibility.diegeticUiOnly = false`*. Confirm.

5. **Vehicle physics scale ceiling — what's the worst-case vehicle count for the arcade Burnout demo scene (drives the < 2.5 ms per-frame budget in §6)?** Doc currently assumes 32 vehicles. Burnout 3 averaged 8 racers + ~8 traffic = 16. Going to 32 is comfortable headroom; halving to 16 frees ~1.25 ms for richer particles / decals. Confirm 32 vs 16 vs other.

6. **Force-feedback (FFB) backend — defer entirely to a follow-up slice (binding-only this phase), or pick SDL2-haptic-on-Linux + raw-input-on-Windows now?** ROADMAP line 950 already flags FFB as "deferred to a follow-up slice but the binding enum goes in day one". Doc concurs. Confirm explicit deferral so review knows.

7. **Sim opponent AI rubber-banding — disable by default for sim per ROADMAP line 967, or expose as a setting starting at "Off"?** Doc recommends setting at "Off" (matches arcade-versus-sim toggle); the setting is per-game, not per-tuning-preset. Confirm.

8. **Replay export — trigger MP4 export inside Phase 11B (blocked on Phase 12 ffmpeg integration), or land replay-editor + offline-capture-to-PNG-sequence in 11B and add the MP4 stitch in Phase 12?** Doc recommends *PNG-sequence in 11B*, *MP4 stitch in Phase 12* — keeps slice E unblocked and has the user-facing affordance ready for Phase 12 to add the final encoder pass. Confirm.

9. **Chapter system vs save system — does the chapter-entered autocheckpoint write a *new* save slot, or overwrite the existing autosave slot?** Doc recommends *overwrite the autosave slot* (manual saves stay distinct). Confirm.

10. **Loot-table specification format — JSON authored alongside scenes, or a dedicated `assets/loot/<table>.lut` schema?** Doc recommends JSON for parity with `assets/captions.json` (Phase 10.7). Confirm.

11. **Boost-meter risk weights — Workbench-fit per game, or ship a default set of arcade weights and let games override?** Doc recommends *ship defaults that match Burnout 3 reference behaviour, expose for per-game override*. Confirm.

12. **Photo Mode pause semantics — pause the entire `Engine::update`, or pause only physics + AI and let particles / shaders keep running?** Doc recommends *pause physics + AI, leave shaders / particles ticking* so the photographed frame stays visually alive (matches GT 7). Confirm.

13. **Co-op horror — record the per-scene authoring fields in 11B (split-inventory flag, divergent-perspective markers) or wait until Phase 20 networking?** Doc recommends *record the schema fields now, no runtime use until Phase 20* — saves a schema migration later. Confirm.

14. **Status-effect catalogue extensibility — fixed enum (bleeding / burning / stun / poisoned) or registered list with per-effect callbacks?** Doc recommends *registered list* — games will invent new effects (zero-G nausea, radiation sickness, freeze) and a fixed enum forces engine forks. Confirm.

15. **Per-bone damage zones — bone-name strings (cheap, brittle to skeleton renames) or socket-id integers (robust, requires a per-skeleton zone-table asset)?** Doc recommends *socket-id integers* with a `damage_zone_table.json` per skeleton. Confirm.

---

## 12. Non-goals (explicitly out of scope)

- **Networking / dedicated-server infrastructure** — Phase 20.
- **AI Director pacing layer (dynamic difficulty, encounter cadence)** — Phase 16.
- **Cutscene editor / dialogue timeline** — Phase 16.
- **Dedicated mobile-touch input** — Phase 12 / later port concern.
- **VR support for horror archetype** — separate later phase.
- **Procedural-generation of tracks / dungeons** — separate later phase.
- **Marketplace / mod-pipeline for inventory items / weapons** — Phase 12+.
- **Dynamic music layering beyond stinger queue** — covered by `audio_music`'s existing crossfade; richer layering is its own design.
- **Frame-perfect input-buffer combat (fighting-game bullet)** — different archetype than the three we're shipping; future phase.
- **Achievement / trophy hooks** — Phase 12 (Steam / store integration).

---

## 13. Change log

| Date | Author | Change |
|---|---|---|
| 2026-04-28 | parent agent (planning) | initial planned-status draft for review |

---

## 14. Review checklist (gate to implementation)

- [ ] §11 Q1 — slice ordering — sign-off
- [ ] §11 Q2 — `experimental/grab_system` + `stasis_system` promotion — sign-off
- [ ] §11 Q3 — world-space UI surface placement — sign-off
- [ ] §11 Q4 — diegetic-UI default + a11y fallback — sign-off
- [ ] §11 Q5 — vehicle scale ceiling — sign-off
- [ ] §11 Q6 — force-feedback explicit deferral — sign-off
- [ ] §11 Q7 — sim rubber-banding default — sign-off
- [ ] §11 Q8 — replay export split (PNG in 11B / MP4 in 12) — sign-off
- [ ] §11 Q9 — chapter autocheckpoint vs autosave slot — sign-off
- [ ] §11 Q10 — loot-table format — sign-off
- [ ] §11 Q11 — boost-meter weights authoring — sign-off
- [ ] §11 Q12 — photo-mode pause semantics — sign-off
- [ ] §11 Q13 — co-op horror schema-fields-now — sign-off
- [ ] §11 Q14 — status-effect extensibility — sign-off
- [ ] §11 Q15 — per-bone damage zone keying — sign-off
- [ ] Cross-reference into `docs/phases/phase_11a_design.md` once that doc lands (parallel agent)
- [ ] ROADMAP.md amended only if scope changes from approval
- [ ] CHANGELOG.md `Unreleased` entry prepared once first slice begins
