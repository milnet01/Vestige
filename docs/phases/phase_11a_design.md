# Phase 11A — Gameplay Infrastructure (Design Doc)

| Field           | Value                                                                         |
|-----------------|-------------------------------------------------------------------------------|
| Phase           | 11A — Gameplay Infrastructure                                                 |
| Status          | `planned` (not yet started)                                                   |
| Owners          | milnet01                                                                      |
| Start date      | TBD — pending user review of this doc                                         |
| Target date     | TBD — set on approval, sized below in §5                                      |
| Roadmap section | `ROADMAP.md` Phase 11A (lines ~742–830)                                       |
| Depends on      | Phase 10.7 (photosensitive clamps shipped) · Phase 10.8 (camera modes shipped)|
| Feeds into      | Phase 11B (gameplay features) · Phase 12 (asset packaging shares zstd)        |

---

## 1. Goal

Phase 11A lands the runtime primitives that every Phase 11B gameplay bullet — combat, vehicle / racing, horror polish, traffic AI, save/checkpoint, replay — will *consume*. The phase's job is not to ship a game-shaped feature; it is to land six tested infrastructure subsystems whose absence would force every Phase 11B slice to invent its own ad-hoc version. At end of phase the engine has: a Camera Shake System composing on top of Phase 10.8's `CameraMode` output, a Screen Flash System threading the photosensitive clamp at upload time, a `zstd`-backed compressed-chunk primitive shared with Phase 12 asset packaging, replay recording / playback infrastructure built around the existing fixed-timestep physics tick, a behavior-tree runtime expressive enough to drive enemy / traffic / opponent NPCs, and an AI perception system (sight + hearing + alert states) that those behavior trees query. Each piece is independently testable, ships with a CHANGELOG entry and tests, and lights up Phase 11B without further infrastructure debt.

---

## 2. Scope

| In scope                                                                                            | Out of scope                                                                                       |
|-----------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `CameraShakeComponent` + shake-type taxonomy + `CameraComponent::m_shakeOffset` write-path          | Per-game tuning of shake parameters for specific weapons / vehicles (Phase 11B per-feature work)   |
| Full-screen flash overlay pass (post-tonemap, pre-UI) + `FlashEnvelope` authoring                   | Per-game flash colour palettes / chapter-wipe sequences (Phase 11B narrative)                      |
| `zstd` integration via `FetchContent` + `writeCompressedChunk` / `readCompressedChunk` primitives   | Save-file *content schema* (which entities / which fields persist) — Phase 11B save system         |
| `ReplayRecorder` / `ReplayPlayer` with input-recording mode + state-snapshot fallback + `.vreplay` | Replay UI (scrubber, speed control, MP4 export) — Phase 11B                                        |
| Behavior-tree runtime: composites / decorators / leaves + tick-based evaluation                     | Behavior-tree visual editor (drag-and-drop graph) — Phase 11A ships data + headless ticking only   |
| AI perception: vision cone + LOS raycast + hearing stimulus bus + alert FSM                         | Per-game enemy archetypes (Necromorph, traffic car, racing AI) — Phase 11B                         |
| Photosensitive clamps wired into shake + flash at upload sites                                      | Re-tuning `PhotosensitiveLimits` defaults — that's a Phase 10.7 concern                            |
| Determinism contract documentation + per-scene `replay_safe` flag + CI parity test                  | Network-replicated replays / multiplayer rollback — out of roadmap                                 |

ROADMAP §Phase 11A lists a *Behavior Tree Editor* as a bullet inside the BT runtime block. This doc treats the editor as an open question (§12 Q5) — runtime ticking is load-bearing for Phase 11B; visual editing is convenience that can land in Phase 11B-as-it-needs-it or as a 11A-tail slice based on user review.

---

## 3. Architecture overview

Six new subsystems, three new top-level engine directories, two new public headers per subsystem on average. Cross-references to existing engine specs are inline.

| Subsystem               | New directory               | Touches existing                                                                                          |
|-------------------------|-----------------------------|-----------------------------------------------------------------------------------------------------------|
| Camera Shake            | `engine/scene/` (component) | `engine/scene/camera_component.h` (already has `m_shakeOffset` slot per Phase 10.8 §4.6); reads `engine/accessibility/photosensitive_safety.h::clampShakeAmplitude` |
| Screen Flash            | `engine/renderer/`          | `engine/renderer/renderer.cpp` composite pass (overlay between tonemap + UI); reads `clampFlashAlpha`     |
| Save-file compression   | `engine/utils/compression/` (new) | `CMakeLists.txt` (`FetchContent` block); `engine/io/` for chunk-format header (new file `chunk_io.h`) |
| Replay recording        | `engine/replay/` (new)      | `engine/input/input_bindings.h` (per-tick input snapshot); `engine/physics/physics_world.h` (fixed tick) |
| Behavior tree runtime   | `engine/ai/behavior_tree/` (new under existing `engine/ai/` skeleton) | `engine/scripting/blackboard.h` reuse (existing scripting blackboard fits BT keys cleanly) |
| AI perception           | `engine/ai/perception/` (new under `engine/ai/`) | `engine/physics/physics_world.h::rayCast` for LOS + Phase 10.8's `sphereCast` for hearing-radius queries |

### 3.1 Camera Shake — composition contract

Phase 10.8 already specified the contract for shake (`docs/phases/phase_10_8_camera_modes_design.md` §4.6): mode produces a pristine `CameraViewOutput`; `CameraComponent` stores `m_baseView + m_shakeOffset` separately; render reads the sum; offset is reset to zero each frame *before* the shake system writes the next frame's offset. Phase 11A is the system that finally writes that offset.

Per Eiserloh's GDC 2016 *trauma* model (§11 ref), shake amplitude derives from a scalar `trauma ∈ [0, 1]` that decays linearly toward zero; rendered shake is `trauma² × baseAmplitude × noise(t)`. Multiple impulses *add* into trauma (capped at 1.0). One trauma scalar per shake type so a continuous earthquake can co-exist with a one-shot weapon kick.

| Shake type   | Authored params                                  | Trauma source           | Notes                                                                              |
|--------------|--------------------------------------------------|-------------------------|------------------------------------------------------------------------------------|
| `Impulse`    | peak, decay, frequency                           | one-shot `addTrauma()`  | Weapon kick, footfall. Decay default 0.6 s.                                        |
| `Continuous` | baseAmplitude, frequency, falloffOverDistance    | env emitter             | Earthquake / vehicle rumble. No decay; lives until source removed.                 |
| `Directional`| axis, peak, decay                                | one-shot `addTrauma()`  | Recoil along a vector — yaw-only or pitch-only (3D-friendly per Eiserloh).         |
| `Trauma`     | decayPerSec, damageToTrauma curve                | health-system listener  | Dead-Space-style accumulating gore-cam. Decays continuously; damage events bump.   |

3D-only restriction (per Eiserloh): translational shake misbehaves in 3D (camera clips through walls). Rotational shake is the default in 3D; translational shake is opt-in per shake instance and clamped to 1 cm peak. (2D path inherits both — relevant for `Camera2DComponent` consumers; gated by `ProjectionType`.)

Photosensitive clamp applies at the *write* into `m_shakeOffset`: amplitude flows through `clampShakeAmplitude(amp, engine.photosensitiveEnabled(), engine.photosensitiveLimits())` before the offset is composed. Strobe-frequency content (rapid alternation) flows through `clampStrobeHz` on the `frequency` parameter.

### 3.2 Screen Flash — overlay pass

A new full-screen overlay pass slots into `Renderer::render()` between the tonemap step and the User Interface (UI) compositing step. State: a small heap (`std::vector<ActiveFlash>`, ≤ 8 simultaneous flashes — bounded to keep blend cost predictable). Per-flash record: `{ glm::vec4 colourRGBA, FlashEnvelope envelope, float elapsed, float duration }`.

`FlashEnvelope` is a 3-segment curve `{fadeIn, plateau, fadeOut}` authored via the Formula Workbench (CLAUDE.md Rule 6) — curve coefficients exported from the workbench, never hand-coded. Flash types are *named presets* over the same envelope:

| Flash type     | Default colour     | Envelope (s)         | Use                                            |
|----------------|--------------------|----------------------|------------------------------------------------|
| `Hit`          | `(1, 0.1, 0.1, α)` | (0.04, 0.06, 0.20)   | Damage taken                                   |
| `Pickup`       | `(0.3, 1, 0.4, α)` | (0.05, 0.10, 0.25)   | Item gained                                    |
| `Stasis`       | `(0.3, 0.8, 1, α)` | (0.08, 0.10, 0.30)   | Dead-Space-style stasis cast                   |
| `ScreenWipe`   | author-supplied    | (0.30, 0.60, 0.30)   | Chapter / scene transition                     |
| `DeathFade`    | `(0, 0, 0, α)`     | (0.20, 1.50, —)      | Player death — no fade-out (caller dismisses)  |

Peak alpha α flows through `clampFlashAlpha(α, engine.photosensitiveEnabled(), engine.photosensitiveLimits())` at *push time* — once at upload, not per-frame. Multiple simultaneous flashes blend additively in linear space; final composite is gamma-corrected by the existing tonemapper so additive blending stays perceptually linear.

WCAG 2.3.1 (Three Flashes or Below Threshold — see §11) is enforced by the *combination* of `clampFlashAlpha` (luminance-delta cap when safe-mode is on) plus `clampStrobeHz` for any pulse-style flash effect — this is the only place engine-side safety actively gates the WCAG metric.

### 3.3 Save-file compression — `zstd` + chunk primitive

Vendored via `FetchContent` against `facebook/zstd` (the canonical reference implementation; tag pinned to the latest stable `v1.5.x` release at the time of slice S1 — exact tag captured in CMake then). Single integration shared with Phase 12 asset packaging — both consumers `target_link_libraries(... zstd::libzstd_static)`.

Chunk-IO API (header `engine/utils/compression/chunk_io.h`):

```cpp
struct ChunkHeader
{
    uint32_t magic;           // 'VCHK' (0x4B484356 LE)
    uint16_t versionMajor;    // bumped on incompatible format change
    uint16_t versionMinor;    // bumped on additive change
    uint32_t uncompressedLen; // bytes; 0 == "stream sentinel, length unknown"
    uint32_t compressedLen;
    uint32_t crc32;           // of the *uncompressed* payload
};

[[nodiscard]] Result<void, IoError>
writeCompressedChunk(std::ostream& out, std::span<const std::byte> bytes,
                     int compressionLevel = 3);

[[nodiscard]] Result<std::vector<std::byte>, IoError>
readCompressedChunk(std::istream& in);
```

`Result<T, E>` per `CODING_STANDARDS.md` §11 (`std::expected` alias). The header is versioned so a Phase 11B-shipped save round-trips against a Phase 12 reader without coordination.

Save-file format (Phase 11A defines the *envelope*; Phase 11B fills the *payload*):

```
+------------------+
|  FileHeader      |  magic 'VSAV', engineVersion, sceneId, chunkCount
+------------------+
|  ChunkTable      |  array<{tag, offset, len}>  -- tags identify scene-state / inventory / replay-snapshot etc
+------------------+
|  ChunkHeader[0]  |
|  zstd payload[0] |
+------------------+
|  ChunkHeader[1]  |
|  ...             |
```

Atomic write per `engine/utils/atomic_write.h` (existing) — write-to-temp + rename-on-close so a crash mid-save never corrupts the previous file.

### 3.4 Replay recording — input mode + state-snapshot fallback

Two modes share a `.vreplay` envelope; the chosen mode is per-scene (header field). The scene authoritatively flags whether it's *replay-safe via input recording* — default off; opt-in once a scene's full subsystem set has been audited deterministic. Subsystems known deterministic on day 1 of Phase 11A: physics (Jolt fixed-timestep, `-fno-fast-math` per `CODING_STANDARDS.md` §30), input bindings, scripting graph evaluation. Subsystems known *non*-deterministic: audio (driver-thread float DSP, irrelevant to replay state), particle GPU rand (irrelevant — visual-only). Subsystems pending audit: AI thread-pool dispatch order (relevant — Phase 11A AI perception ticks must be deterministic; tracked in §12 Q3).

| Mode            | Stored                                                    | Replay strategy                                                       |
|-----------------|-----------------------------------------------------------|-----------------------------------------------------------------------|
| `InputRecord`   | RNG seeds + per-tick input deltas (delta-encoded)         | Re-simulate from t=0 with stored inputs. Bit-exact given determinism. |
| `StateSnapshot` | Full transform / velocity dump every N seconds            | Interpolate between snapshots at playback. Robust to non-determinism. |

Compression: every snapshot or input-delta page flows through §3.3's `writeCompressedChunk`. Target: < 1 MB / minute for a racing game in `InputRecord` mode (per ROADMAP) — verifiable with a synthetic-input determinism harness (see §9).

`.vreplay` header carries: engine version, scene ID, mode, RNG seed at tick 0, fixed-timestep value used during recording. Mismatched fixed-timestep on playback is an error (mode-specific — fatal for `InputRecord`, warning for `StateSnapshot`).

### 3.5 Behavior-tree runtime — composites, decorators, leaves

A behavior tree (BT) is a directed tree of nodes evaluated each tick from the root, where every node returns one of `Status::Running / Success / Failure`. The *standard 4-status model* (Champandard / `gameaipro` BT Starter Kit, §11) is overkill for Phase 11A consumers; we ship the 3-status model and add `Aborted` only if a Phase 11B consumer demonstrably needs it.

| Node category | Concrete types                                                                                               |
|---------------|--------------------------------------------------------------------------------------------------------------|
| Composite     | `Sequence` (AND, short-circuit on Failure), `Selector` (OR, short-circuit on Success), `Parallel` (concurrent, success-policy + failure-policy enums) |
| Decorator     | `Inverter`, `Repeater(N \| Forever)`, `Cooldown(seconds)`, `Conditional(predicate)`, `Timeout(seconds)`      |
| Leaf — Action | concrete game actions (move-to, attack, patrol, flee) — each derives `BehaviorTreeAction` and overrides `tick()` |
| Leaf — Condition | predicate over Blackboard state (`canSeePlayer`, `healthBelow(0.3)`)                                      |

State storage: a `Blackboard` keyed by string + `std::variant<bool, int, float, glm::vec3, EntityHandle>`. Reuses the existing `engine/scripting/blackboard.h` shape — Phase 11A *does not* fork a separate BT-only blackboard; one blackboard per agent serves both scripting and BT.

Tree authoring for Phase 11A: data-driven from JSON (`assets/ai/<tree>.bt.json`) or programmatic via fluent C++ builder. Visual editor deferred per §12 Q5.

`Utility AI` enhancement (ROADMAP optional bullet): a `UtilitySelector` decorator that scores each child via a `std::function<float(Blackboard&)>` and ticks the highest-scoring one. Same node interface as `Selector`; trivial to add in slice BT4.

### 3.6 AI perception — sight + hearing + alert FSM

Modelled after Unreal Engine's *AI Perception Component* (UE 5.7 docs, §11) but distilled to the parts our consumers actually need.

| Sense       | Stimulus shape                                                | Probe                                                           |
|-------------|---------------------------------------------------------------|-----------------------------------------------------------------|
| Sight       | `{ source, position, awarenessGain }`                         | `coneAngle`, `range`, `peripheralAngle` + LOS raycast           |
| Hearing     | `{ source, position, loudness, type }`                        | distance check + sound-occlusion raycast (loudness-attenuated)  |
| Damage      | `{ source, position, amount }`                                | direct event — no probe (auto-promote to Alert)                 |

Sight uses `PhysicsWorld::rayCast` (existing, per Phase 10.8 inventory). Hearing uses one ray for occlusion; sound *type* (gunshot / footstep / explosion / door) determines base radius — table-driven, exported from Formula Workbench.

Stimuli feed an `AwarenessAccumulator` per (perceiver, source) pair — gain rises while the source is sensed, decays when sense is lost. Alert states are an FSM driven by accumulator thresholds:

```
Unaware → Suspicious → Alert → Combat → Search → Return → Unaware
```

Transitions are time-hysteresed (suspicion builds for ≥ 0.5 s before promoting; loss-of-sense holds Alert ≥ 3 s before demoting to Search) so a frame-1 missed raycast doesn't dump combat back to unaware. Alert *propagation* (alerted NPC alerts allies in radius) is a single `EventBus` event `AiAlertEvent { alerterEntity, lastKnownPosition, alertLevel }`; Phase 11A ships the event + a default propagation policy (radius-based) and lets Phase 11B per-game consumers override.

### 3.7 Cross-subsystem dependency graph

```
                     +-----------------------+
                     |  PhotosensitiveLimits |   (Phase 10.7, shipped)
                     |  + clamp helpers      |
                     +-----------+-----------+
                                 |
                +----------------+----------------+
                |                                 |
        +-------v-------+                  +------v--------+
        | Camera Shake  |                  | Screen Flash  |
        | (this phase)  |                  | (this phase)  |
        +-------+-------+                  +------+--------+
                |                                 |
                v                                 v
        CameraComponent                     Renderer overlay pass
        (Phase 10.8 m_shakeOffset)          (Phase 10.8 post-process slot)


        +-----------------+
        | zstd FetchContent| ──┬── Save-file chunk-IO  ──→ Phase 11B save system
        +-----------------+   └── Replay record/playback ──→ Phase 11B replay UI
                                  (this phase)


        +---------------------+      +---------------------+
        | Behavior-tree       | <----| AI Perception        |
        | runtime (this phase)|      | (sight / hearing FSM)|
        +---------------------+      +---------------------+
                ^                              ^
                |  (Blackboard reuse)          |  (rayCast / sphereCast)
        engine/scripting/blackboard.h    engine/physics/physics_world.h
```

---

## 4. Steps / slices

Each slice lands as a review-sized commit with tests + CHANGELOG entry. Slices are ordered by dependency. Independent slices (CS1 ‖ FL1 ‖ ZS1) can ship in parallel.

| Slice  | Scope                                                                                                            | Rough LOC | Depends on             |
|--------|------------------------------------------------------------------------------------------------------------------|-----------|------------------------|
| CS1    | `CameraShakeComponent` data model + `ShakeType` enum + Formula-Workbench-exported envelope JSON                  | 180       | —                      |
| CS2    | Per-frame integrator: composes trauma → offset; writes `CameraComponent::m_shakeOffset`; clamp at write          | 220       | CS1                    |
| CS3    | Hookups: weapon-kick listener, footfall listener, damage→trauma listener (default propagator)                    | 140       | CS2                    |
| CS4    | Editor preview slider in camera inspector (one slider per shake type — debug-tune button)                        | 120       | CS2                    |
| FL1    | `FlashSystem` + `ActiveFlash` struct + Formula-Workbench-exported envelope coefficients                          | 160       | —                      |
| FL2    | Renderer overlay pass slot; full-screen quad shader; clamp at push                                               | 200       | FL1, existing renderer |
| FL3    | Flash-type presets (`Hit / Pickup / Stasis / ScreenWipe / DeathFade`) + EventBus listeners for each              | 140       | FL2                    |
| ZS1    | `FetchContent` for `zstd`; CMake link; vendor sanity test                                                        | 80        | —                      |
| ZS2    | `chunk_io.h` API + `writeCompressedChunk` / `readCompressedChunk` impl + CRC + atomic-write integration          | 220       | ZS1                    |
| RP1    | `.vreplay` envelope writer/reader (header + chunk table)                                                         | 180       | ZS2                    |
| RP2    | `ReplayRecorder` (InputRecord mode) — per-tick input snapshot, delta encoding                                    | 240       | RP1                    |
| RP3    | `ReplayPlayer` (InputRecord mode) — re-simulate with stored inputs                                               | 200       | RP2                    |
| RP4    | StateSnapshot mode (recorder + player; periodic transform dumps + interpolation)                                 | 260       | RP1                    |
| RP5    | Determinism harness CI test (record N=600 ticks, replay, position-epsilon assert)                                | 160       | RP3                    |
| BT1    | `BehaviorTreeNode` base + `Status` enum + tick contract                                                          | 120       | —                      |
| BT2    | Composites: `Sequence`, `Selector`, `Parallel`                                                                   | 200       | BT1                    |
| BT3    | Decorators: `Inverter`, `Repeater`, `Cooldown`, `Conditional`, `Timeout`                                         | 220       | BT1                    |
| BT4    | Leaf actions / conditions API + JSON loader + fluent C++ builder                                                 | 280       | BT2, BT3               |
| BT5    | Blackboard reuse glue (BT keys → existing scripting Blackboard) + concrete demo tree                             | 160       | BT4                    |
| AP1    | `AiPerceiverComponent` + sight cone + LOS raycast                                                                | 200       | —                      |
| AP2    | Hearing stimulus bus (EventBus `AiSoundEvent`) + occlusion raycast + per-type radius table                       | 220       | AP1                    |
| AP3    | `AwarenessAccumulator` + Alert FSM + propagation event                                                           | 240       | AP1, AP2               |
| AP4    | BT integration — `CanSeePlayer`, `LastKnownPosition`, `IsAlertLevel(...)` condition leaves                       | 140       | AP3, BT5               |

**Total:** ~24 slices, ~4280 LOC. Critical path: ZS1 → ZS2 → RP1 → RP2 → RP3 → RP5 (replay determinism harness is the gating CI piece). CS / FL / BT / AP families ship in parallel after their respective dependency-zero slice.

Recommended commit order (one path through the DAG): ZS1, CS1, FL1, BT1, AP1 (parallel-trackable) → ZS2, CS2, FL2, BT2, BT3, AP2 → CS3, FL3, BT4, AP3 → BT5, AP4 → RP1 → RP2, RP4 → RP3 → RP5 → CS4. Tail slice CS4 (editor preview) is non-blocking and can land after AP4.

Milestone (per ROADMAP):

> Camera shake drives view-matrix offsets for any camera mode, screen flashes upload through the photosensitive clamp, save files round-trip through compressed chunks, replays record and play back deterministically, and the BT runtime can evaluate `Sequence(Patrol, CheckPlayerVisible, Attack)` on a scripted NPC using perception data.

The §9 testing strategy is deliberately structured so the milestone is verifiable mechanically — each clause maps to a named test.

---

## 5. CPU / GPU placement (CLAUDE.md Rule 7)

| Workload                                                            | Placement              | Reason                                                                                                                                                              |
|---------------------------------------------------------------------|------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Trauma decay + noise sampling for camera shake                      | CPU (main thread)      | One scalar per shake instance per frame; bounded ≤ low tens of instances. GPU dispatch overhead exceeds the work.                                                  |
| Shake offset compose into `m_shakeOffset`                           | CPU                    | One vec3 + one quat per camera per frame.                                                                                                                           |
| Flash-overlay shader (sample full-screen target, blend per-flash)   | GPU (frag shader)      | Per-pixel work, per Rule 7 heuristic. Single full-screen quad.                                                                                                      |
| Flash-state tick (envelope advance, removal of finished flashes)    | CPU                    | Bounded ≤ 8 active flashes; trivial branching.                                                                                                                      |
| zstd compress / decompress                                          | CPU (worker thread for save; main thread for in-memory chunks during replay tick) | zstd is a CPU library. Worker-thread offload during save avoids 60-FPS hitch; replay ticks stay on the deterministic thread for ordering. |
| CRC32 of chunk payload                                              | CPU (zstd's `XXH3` is fine, but we ship CRC32 for forward-compat with Phase 12 archive readers; use `zstd_xxhash` if measured-faster) | Bounded by I/O bandwidth, not compute. CPU sufficient. |
| Per-tick replay input snapshot + delta encode                       | CPU                    | Tens of bytes per tick. I/O-shaped work, never per-pixel.                                                                                                           |
| Replay re-simulation                                                | CPU                    | Same code path as live tick (deterministic re-execution).                                                                                                           |
| BT node ticking                                                     | CPU                    | Branching, sparse, decision-heavy — Rule 7 explicitly routes this to CPU.                                                                                           |
| BT blackboard read/write                                            | CPU                    | Sparse keyed access; no SIMD shape.                                                                                                                                 |
| AI sight LOS raycast                                                | CPU (Jolt physics thread, via `PhysicsWorld::rayCast`) | Existing API contract — Jolt runs casts on the physics thread; we read results.                                                          |
| AI hearing occlusion raycast                                        | CPU (Jolt physics thread) | Same as above.                                                                                                                                                  |
| Awareness accumulator + alert FSM                                   | CPU                    | Tens of perceiver-source pairs at most; sparse update.                                                                                                              |

**No new GPU compute dispatches are introduced.** The flash-overlay shader is the only new GPU touch — a single full-screen quad pass that already fits the existing post-process scheduling. Replay determinism *requires* CPU-only execution for any subsystem whose state participates in input-mode replay (see §3.4 — Phase 11A AI perception ticks must be deterministic; tracked in §12 Q3 — *if* AI perception ends up using a thread pool, perception must move out of the input-replay determinism set or the pool must publish ordered results).

No dual CPU+GPU implementations in this phase. No "CPU for now, move later" placeholders.

---

## 6. Performance budget

The 60 FPS hard requirement (`CLAUDE.md`) gives 16.6 ms/frame. Phase 11A's slice across all six subsystems combined targets:

| Path                                                                | Budget     | Measured      |
|---------------------------------------------------------------------|------------|---------------|
| Camera shake — full per-frame integrate + write                     | < 0.05 ms  | TBD — measure on slice CS2 land, RX 6600, demo scene with 4 active shakes |
| Screen flash — per-frame state tick (CPU)                           | < 0.05 ms  | TBD — measure on slice FL2 land                                            |
| Screen flash — overlay shader pass (GPU)                            | < 0.10 ms  | TBD — measure on slice FL2 land                                            |
| Replay recorder — per-tick input snapshot + zstd-page (amortised)   | < 0.20 ms  | TBD — measure on slice RP2 land                                            |
| Save round-trip (single chunk, ~64 KB) — *not per-frame; one-shot*  | < 5 ms     | TBD — measure on slice ZS2 land (worker-thread offload)                    |
| BT tick (single agent, 20-node tree)                                | < 0.02 ms  | TBD — measure on slice BT4 land                                            |
| AI perception tick (single perceiver, 1 sight + 1 hearing probe)    | < 0.10 ms  | TBD — measure on slice AP3 land (raycast cost dominates)                   |
| **Phase 11A total per-frame slice (worst case w/ 8 NPCs ticking)**  | **< 1.5 ms** | TBD — measure end-of-phase                                                |

Numbers are engineering targets; honesty rule per `SPEC_TEMPLATE.md` §8 — all marked `TBD — measure on <slice>` until real numbers exist. End-of-phase audit (per `CLAUDE.md` Rule 4) re-measures; any slice that misses budget gets a dedicated optimisation pass before phase close.

Profiler markers (per `CODING_STANDARDS.md` §29 — `glPushDebugGroup` for the GPU pass, named scope macros for CPU): `phase11a.shake.tick`, `phase11a.flash.tick`, `phase11a.flash.gpu_pass`, `phase11a.replay.record_tick`, `phase11a.replay.zstd_page`, `phase11a.bt.tick`, `phase11a.perception.sight`, `phase11a.perception.hearing`.

---

## 7. Accessibility

Phase 11A's user-facing surfaces and their constraints:

| Surface                       | Constraint                                                                                                                         |
|-------------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| Camera shake (visual)         | Amplitude flows through `clampShakeAmplitude` at write — safe-mode caps at `shakeAmplitudeScale × authored` (default 0.25× cap).    |
| Camera shake (frequency)      | `frequency` flows through `clampStrobeHz` — 2 Hz hard ceiling in safe mode, WCAG 2.3.1 ceiling 3 Hz unconditional.                  |
| Camera shake (motion sickness)| `Settings::accessibility::reducedMotion == true` halves baseline amplitude *and* frequency for *all* shake types — independent of the photosensitive flag, since reduced-motion users may not have photosensitivity. |
| Screen flash (luminance)      | Peak alpha α flows through `clampFlashAlpha` at push — safe-mode caps at `maxFlashAlpha = 0.25` (default).                          |
| Screen flash (frequency)      | Pulse-style flashes (any envelope with > 1 fade-in cycle within 1 s) flow through `clampStrobeHz` at the *cadence* of pushes, not just per-push α. WCAG 2.3.1 binding constraint. |
| Screen flash (colour)         | Red-channel-saturated flashes (`r > 0.8 && g < 0.3 && b < 0.3`) get an additional 0.5× α cap when safe-mode is on (red-flash threshold per WCAG 2.3.1). |
| Replay system (UX)            | `.vreplay` files are user-data — the *load* path produces a `Result<...>` error rather than crashing on corrupt data. No silent-fail.|
| Behavior tree (debug visualisation, when editor lands) | Active-node highlight uses outline + colour — never colour-only encoding (CODING_STANDARDS.md §32 / `feedback_use_superpowers` guideline). |
| AI perception (audio cues for damage / suspicion) | Detection events feed the existing `SubtitleQueue` so partially-sighted users get caption fallback for sound-only alerts. |

The shake + flash a11y wiring is the *delivery* of the deferred Phase 10.7 retrofits — Phase 10.7 §4.3 explicitly noted shake and flash subsystems didn't exist at that phase and parked the clamp helpers waiting for their consumers. Phase 11A is the consumer.

---

## 8. Testing strategy

| Concern                                              | Test file                                              | Coverage                                                                                              |
|------------------------------------------------------|--------------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| Shake trauma decay arithmetic                        | `tests/test_camera_shake_trauma.cpp`                   | Trauma squared/cubed curves; multi-instance accumulation cap at 1.0; Eiserloh model parity.           |
| Shake clamp wiring                                   | `tests/test_camera_shake_a11y.cpp`                     | `photosensitiveEnabled=true` + amplitude=1.0 + scale=0.25 → effective 0.25; reducedMotion halving.     |
| Flash envelope evaluation                            | `tests/test_flash_envelope.cpp`                        | Fade-in / plateau / fade-out segment sampling; deterministic at named timestamps.                     |
| Flash clamp wiring                                   | `tests/test_flash_a11y.cpp`                            | Push α=1.0 with safe-mode + red-channel-saturated colour → effective ≤ 0.125. WCAG-2.3.1 case.        |
| Flash overlay GPU pass                               | visual-test runner only (no unit test — GPU output)    | Coverage gap: pass exercised only via `engine/testing/visual_test_runner.h`. Document.                |
| zstd round-trip                                      | `tests/test_compression_chunk_io.cpp`                  | Random-bytes round-trip; CRC mismatch detection; oversized payload rejection.                         |
| Save-file format integrity                           | `tests/test_save_file_envelope.cpp`                    | Multi-chunk write+read; mid-write crash leaves prior file intact (atomic-write contract).             |
| Replay determinism — input mode                      | `tests/test_replay_determinism.cpp`                    | Record 600 ticks of scripted input → re-simulate → assert position / orientation / RNG-seed bit-exact.|
| Replay state-snapshot fallback                       | `tests/test_replay_state_snapshot.cpp`                 | Snapshot every 30 ticks → interpolate at playback → assert visual position drift < 1 cm.              |
| BT composite semantics                               | `tests/test_bt_composites.cpp`                         | Sequence short-circuit on Failure; Selector short-circuit on Success; Parallel policy enums.          |
| BT decorator semantics                               | `tests/test_bt_decorators.cpp`                         | Inverter, Repeater(N), Cooldown, Conditional, Timeout — each in isolation.                            |
| BT JSON load                                         | `tests/test_bt_json_load.cpp`                          | Round-trip a 12-node tree through JSON; assert structural identity.                                   |
| BT milestone integration                             | `tests/test_bt_perception_integration.cpp`             | The ROADMAP-named tree `Sequence(Patrol, CheckPlayerVisible, Attack)` runs end-to-end on a headless NPC and player. |
| AI sight cone                                        | `tests/test_ai_perception_sight.cpp`                   | Cone hits / cone misses / LOS-blocked-by-wall / peripheral-vs-central detection rate.                 |
| AI hearing                                           | `tests/test_ai_perception_hearing.cpp`                 | Distance attenuation; per-type radius lookup; occlusion through wall.                                 |
| AI alert FSM                                         | `tests/test_ai_alert_fsm.cpp`                          | Unaware → Suspicious transition latency; Alert → Search demotion ≥ 3 s; propagation event.            |

Per `CODING_STANDARDS.md` §19, every new public type gets a test. Coverage gap explicitly documented: the flash-overlay GPU pass output is not unit-testable headlessly — it goes through the visual-test runner. Same pattern as existing post-process passes.

Determinism harness (RP5) runs in CI per ROADMAP. Linux runner only — Windows-builds gate on the harness output but don't re-run it (per global rule 6, public-repo CI minutes).

---

## 9. Dependencies

### Existing engine subsystems

| Dependency                                                  | Why                                                                                                |
|-------------------------------------------------------------|----------------------------------------------------------------------------------------------------|
| `engine/scene/camera_component.h`                           | `m_shakeOffset` write-target (slot reserved by Phase 10.8 §4.6).                                   |
| `engine/scene/camera_mode.h`                                | Composition contract — shake applies post-mode (Phase 10.8 §4.6).                                  |
| `engine/accessibility/photosensitive_safety.h`              | Clamp helpers (`clampShakeAmplitude`, `clampFlashAlpha`, `clampStrobeHz`) — shipped Phase 10.7.    |
| `engine/core/engine.h::photosensitiveEnabled / Limits`      | Authoritative reader — Phase 10.7 wiring.                                                           |
| `engine/core/settings.h::accessibility.reducedMotion`       | Reduced-motion gate (Phase 10.7 wiring) — applies independently of photosensitive flag.             |
| `engine/renderer/renderer.cpp` post-process pipeline        | Flash-overlay slot between tonemap + UI compositing.                                                |
| `engine/utils/atomic_write.h`                               | Save-file atomicity — write-temp + rename.                                                          |
| `engine/utils/result.h` (or `std::expected` direct, per §11 of Coding Standards) | Error returns from chunk IO + replay load.                                       |
| `engine/physics/physics_world.h::rayCast`                   | Sight LOS raycast.                                                                                  |
| `engine/physics/physics_world.h::sphereCast`                | Hearing-radius queries (Phase 10.8 shipped this API per §4.7 of that doc).                          |
| `engine/physics/physics_world.h::fixedTimestep`             | Replay-mode determinism contract (header field stored in `.vreplay`).                               |
| `engine/scripting/blackboard.h`                             | BT key-value state store reuse.                                                                     |
| `engine/input/input_bindings.h`                             | Replay input snapshot — record from the action map, not raw GLFW state.                             |
| `engine/core/event_bus.h`                                   | `AiSoundEvent` / `AiAlertEvent` propagation; flash-trigger events; damage→trauma listener.          |
| `engine/audio/subtitle_queue.h`                             | Caption fallback for sound-only AI cues (a11y).                                                     |
| `engine/utils/logger.h`                                     | Replay format-version mismatch logs; chunk CRC failures.                                            |

### External libraries

| Dependency                | Type                | Why                                                                                                |
|---------------------------|---------------------|----------------------------------------------------------------------------------------------------|
| `facebook/zstd` (`v1.5.x`)| FetchContent        | Compression — save files + replay pages. Shared with Phase 12 asset packaging (single integration).|
| `nlohmann/json` (existing)| existing            | BT tree JSON load + caption-map round-trip.                                                         |
| `glm` (existing)          | existing            | Vec / quat math.                                                                                    |
| `Jolt` (existing)         | existing            | Raycast / sphereCast for AI perception.                                                             |

No new external library introductions beyond zstd. zstd's BSD-3 / GPLv2 dual licence is compatible with the engine's MIT direction (per project memory `project_open_source_plan.md`).

---

## 10. References

Web research, ≤ 1 year old where the technique has continuing-relevance docs and ≥ 1 year where the canonical citation predates that window. No fabricated sources — sources omitted rather than guessed.

**Camera shake — trauma model:**
- Squirrel Eiserloh, *Math for Game Programmers: Juicing Your Cameras with Math*, GDC 2016 — original trauma-model presentation. PDF: `http://www.mathforgameprogrammers.com/gdc2016/GDC2016_Eiserloh_Squirrel_JuicingYourCameras.pdf` (canonical, predates the 1-year window but is the source paper).
- Internet Archive recording of the same talk: `https://archive.org/details/GDC2016Eiserloh`.
- Bevy engine 2D screen-shake example (compositional reference): `https://bevy.org/examples/camera/2d-screen-shake/`.
- Borderline blog, *All-purpose screenshake, the right way*: `http://blog.borderline.games/tutorials/gettinghit!/trauma-based-screenshake.html`.

**Screen flash — WCAG 2.3.1:**
- W3C, *Understanding Success Criterion 2.3.1: Three Flashes or Below Threshold*: `https://www.w3.org/WAI/WCAG22/Understanding/three-flashes-or-below-threshold.html`.
- DigitalA11Y, *Understanding WCAG SC 2.3.1*: `https://www.digitala11y.com/understanding-sc-2-3-1-three-flashes-or-below-threshold/`.

**zstd integration:**
- Facebook zstd canonical repo + manual: `https://github.com/facebook/zstd` and `http://facebook.github.io/zstd/zstd_manual.html`. RFC 8878 (Zstandard format spec) referenced from there.
- OpenTTD savegame zstd integration PR (real-world game-engine integration reference): `https://github.com/OpenTTD/OpenTTD/pull/8773`.
- Factorio thread on zstd savegame compression (engineering-tradeoff discussion): `https://forums.factorio.com/viewtopic.php?t=34273`.

**Replay — determinism + fixed-timestep:**
- Glenn Fiedler, *Fix Your Timestep!*, Gaffer on Games (2004; canonical reference, pre-window): `https://gafferongames.com/post/fix_your_timestep/`.
- Jakub Tomšů, *Reliable fixed timestep & inputs* (2024–2025): `https://jakubtomsu.github.io/posts/input_in_fixed_timestep/`.
- *Instant Replay: Building a Game Engine with Reproducible Behavior*, Game Developer (Bungie engineer post-mortem on input-record replay): `https://www.gamedeveloper.com/design/instant-replay-building-a-game-engine-with-reproducible-behavior`.
- *Developing Your Own Replay System*, Game Developer: `https://www.gamedeveloper.com/programming/developing-your-own-replay-system`.

**Behavior trees:**
- Champandard & Dunstan, *The Behavior Tree Starter Kit*, Game AI Pro chapter 6 (canonical 3-status / 4-status discussion): `http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter06_The_Behavior_Tree_Starter_Kit.pdf`.
- Merrill, *Building Utility Decisions into Your Existing Behavior Tree*, Game AI Pro chapter 10: `http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter10_Building_Utility_Decisions_into_Your_Existing_Behavior_Tree.pdf`.
- libgdx `gdx-ai` BT runtime (live reference implementation): `https://github.com/libgdx/gdx-ai/wiki/Behavior-Trees`.
- *Behavior trees for AI: How they work*, Game Developer: `https://www.gamedeveloper.com/programming/behavior-trees-for-ai-how-they-work`.

**AI perception:**
- Unreal Engine 5.7 *AI Perception in Unreal Engine* docs (sight / hearing / damage stimulus shape — modelled-after reference): `https://dev.epicgames.com/documentation/en-us/unreal-engine/ai-perception-in-unreal-engine`.
- Rodney Lab, *UE5 AI Hearing C++: Unreal Engine Perception Example*: `https://rodneylab.com/unreal-engine-5-hearing-ai-c++-example/`.

---

## 11. Open questions (blocking approval)

| #  | Question                                                                                                                                                                          | Recommendation                                                                                                  |
|----|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------|
| Q1 | **Trauma decay curve — linear vs. exponential?** Eiserloh ships linear; some derivatives use `0.5 ^ (dt / halfLife)` for smoother end-of-shake. Linear is simpler + matches the reference. | Linear, per the canonical Eiserloh paper. Re-evaluate if visual review finds the linear-end pop objectionable.  |
| Q2 | **Replay default mode — `InputRecord` or `StateSnapshot` per-scene default?** `InputRecord` is smaller + bit-exact but requires the determinism audit. `StateSnapshot` works always but is larger and visually-interpolated. | `StateSnapshot` default; flip individual scenes to `InputRecord` once they pass the determinism harness.        |
| Q3 | **AI perception thread-pooling — same thread as physics, or worker-pool dispatch?** Worker-pool gives parallel perceiver evaluation but breaks input-mode replay determinism unless results are gathered in a deterministic order. | Same thread as physics for Phase 11A — keeps determinism trivially. Promote to pool in Phase 11B *if* a profile shows perception is the per-frame hotspot. |
| Q4 | **BT 3-status (`Running/Success/Failure`) vs. 4-status (+ `Aborted`)?** 4-status is needed when a Parallel composite preempts a child mid-tick and the child needs to clean up. None of our Phase 11B consumers documented as needing this. | Ship 3-status. Add `Aborted` only if a Phase 11B feature demonstrably needs it. (YAGNI per `CLAUDE.md` Rule 3.) |
| Q5 | **BT visual editor — Phase 11A or Phase 11B?** ROADMAP §Phase 11A lists it; runtime ticking is the load-bearing piece. The editor is convenience. | Defer to Phase 11B (or a 11A tail-slice) — JSON + fluent C++ builder is enough to author trees and ship the milestone. Visual editor lights up when the first BT-heavy 11B feature lands and asks for it. |
| Q6 | **Camera-shake clamp interaction with `reducedMotion`** — clamp only when `photosensitiveEnabled`, only when `reducedMotion`, or when *either* is set? They cover overlapping but distinct user populations. | Clamp on either — `reducedMotion` halves; `photosensitiveEnabled` then applies its scale on top. Two independent paths, multiplicative. |
| Q7 | **Flash blend mode — additive vs. screen-blend?** Additive is what Phase 10.7's clamp helpers assume; screen-blend (`1 - (1-a)(1-b)`) caps at 1.0 naturally. | Additive in linear space, then the existing tonemap clamps highlights — matches the photosensitive clamp's mental model (peak α is what we cap). |
| Q8 | **`.vreplay` versioning policy** — bump on every additive change, or only on incompatible? Header has both major + minor. | Major on incompatible, minor on additive. Phase 11A ships v1.0. Phase 11B replay-UI add-ons must ship v1.x, never v2.0.                          |
| Q9 | **Slice order — recommended order in §4 (ZS1/CS1/FL1/BT1/AP1 in parallel) or front-load CS+FL (camera shake + flash visible immediately)?** | Doc-recommended parallel start; CS + FL ship as a paired demo at slice CS3 + FL3 land. Front-loading sacrifices the ZS path, which gates RP1–RP5. |

---

## 12. Non-goals (explicitly out of scope)

- **Network-replicated replays / multiplayer rollback netcode.** Not on the roadmap; would need a separate determinism contract (machine-to-machine).
- **Replay MP4 export.** Phase 11B feature — Phase 11A ships the `.vreplay` envelope only.
- **Per-game enemy archetypes** (Necromorph, traffic car, racing AI). Phase 11B — those consume the BT runtime and AI perception primitives this phase ships.
- **Save-file *content schema***. Phase 11A ships the chunk-IO envelope; *what* goes in chunks is the Phase 11B save / checkpoint system.
- **Camera shake for `EditorCamera`.** Editor camera is by-design separate (per Phase 10.8 §2.1 inventory).
- **Visual BT editor / live-debug tree-overlay.** Deferred to Phase 11B per Q5.
- **AI Director / cutscene / dialogue.** Phase 16 territory — those consume the BT + perception primitives this phase ships.
- **Audio mixer effect chains (reverb, occlusion DSP).** Phase 11B / Phase 13 audio polish.

---

## 13. Change log

| Date       | Doc version | Author    | Change                                                                                              |
|------------|-------------|-----------|-----------------------------------------------------------------------------------------------------|
| 2026-04-28 | 0.1 (draft) | milnet01  | Initial design doc. Pending cold-eyes review.                                                       |
