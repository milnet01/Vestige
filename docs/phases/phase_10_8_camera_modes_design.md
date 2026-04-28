# Phase 10.8 — Camera Modes (Design Doc)

**Status:** Approved 2026-04-23 — all §6 Q1–Q8 accepted as doc-recommended (M3 architecture, extract first-person, W1 sphereCast API, 150 ms transition, F1 mode-owned FOV, bundled CinematicTake, single-bool opt-out, slice order CM1→CM2→CM3→CM4→CM5→CM6/CM7→CM8). Implementation proceeds.
**Roadmap item:** `ROADMAP.md` Phase 10.8 § Camera Modes (6 bullets — mode system, 1st-person, 3rd-person, toggle, isometric, top-down, cinematic).
**Scope:** Land a switchable camera-mode architecture that lifts the existing single-active `CameraComponent` pattern into a composable rig capable of first-person, third-person, isometric, top-down, and cinematic (spline-based) modes, plus a runtime 1st↔3rd toggle. Does **not** include Camera Shake (that's Phase 11A) or Decal/Post-Process sibling bullets inside Phase 10.8 (those get their own design docs).

---

## 1. Why this doc exists

Phase 10.8 was carved out of Phase 10's tail bullets so the *Phase 11B* gameplay features that depend on camera modes stop being latent dependencies (ROADMAP reorg commit `efe4a45`). The three Phase 11B consumers already cite Phase 10.8 by name:

- **Vehicle Physics & Racing** — bumper / hood / chase / cockpit / cinematic crash cameras need selectable modes.
- **Horror Action Polish** — the Dead Space archetype is *fixed* over-the-shoulder third-person; we need a per-scene lock that disables the 1st↔3rd toggle.
- **Combat** — melee strike cams + ranged aiming in third-person need a working third-person rig with orbit + wall-clip avoidance.

And Phase 11A's **Camera Shake** system explicitly composes *on top of* Camera Mode output (ROADMAP: "Composes with Phase 10.8 Camera Modes — shake is applied post-mode, so first-person / third-person / cinematic / vehicle modes all get shake without per-mode code"). Mode output has to be a well-defined thing before shake can cleanly layer on it.

Today Vestige has:
- **One** `CameraComponent` active per scene (`Scene::setActiveCamera()`).
- A first-person control scheme baked into editor / demo scene wiring (WASD + mouse look).
- A `Camera2DComponent` with its own smooth-follow + deadzone + bounds clamp for 2D games.
- Catmull-Rom splines + `SplinePath` already shipped.
- No rig, no orbit, no wall-probe, no mode switch, no runtime toggle.

This doc specifies:
1. The camera-mode architecture shape (single-manager-+-modifier-stack vs. priority-queue vs. ECS-activation — research-cited in §8).
2. Each of the five modes: inputs, outputs, parameters, edge cases.
3. The runtime 1st↔3rd toggle: lerp vs. snap, reducedMotion coupling, per-scene opt-out.
4. Third-person wall-probe: Jolt sphere-cast vs. ray-cluster approximation.
5. Cinematic mode's relationship to the existing `SplinePath`.
6. CPU/GPU placement (Rule 12).
7. The slice plan + tests + approval gate.

---

## 2. Current state (inventory)

Surveyed 2026-04-23.

### 2.1 Camera foundations

| Piece | Location | State |
|---|---|---|
| `CameraComponent` | `engine/scene/camera_component.h:30–98` | 3D camera: `fov`, `nearPlane`, `farPlane`, `orthoSize`, `projectionType`, view/projection matrix getters. Holds an internal `Camera` for back-compat with renderer APIs. |
| `Camera2DComponent` | `engine/scene/camera_2d_component.h:29–89` | 2D orthographic smooth-follow with deadzone + bounds clamp. Separate from 3D path. |
| `Scene::setActiveCamera` / `getActiveCamera` | `engine/scene/scene.h:147–150` + `scene.cpp:113–120` | Single active camera pointer. No priority queue, no list of candidates. |
| `EditorCamera` | `engine/editor/editor_camera.{h,cpp}` | Editor-specific fly-camera; distinct from runtime `CameraComponent`. Out of scope for Phase 10.8 (editor uses its own camera by design). |
| `CatmullRomSpline` | `engine/utils/catmull_rom_spline.h:15–` | Shipped. `evaluate(t)` returns `vec3`; `evaluateTangent(t)` returns `vec3`. No FOV or lookAt tracks. |
| `SplinePath` | `engine/environment/spline_path.h` | Shipped. Authoring + sampling wrapper around `CatmullRomSpline`. |
| `InputActionMap` | `engine/input/input_bindings.h:99–` | Rebindable action map — any Phase 10.8 toggle binding goes here, not hard-coded. |
| `AccessibilitySettings.reducedMotion` | `engine/core/settings.h:261` | Present. Phase 10.7 wired it through the photosensitive path; we'll read it here too. |
| `PhysicsWorld::rayCast` | `engine/physics/physics_world.h:175–184` | Ray-only. Jolt-backed. No sphere-cast exposed yet. |

### 2.2 Gaps

- **No rig abstraction.** The current "mode" is implicit in how the scene drives a single camera's transform. There is no component type describing "what *kind* of camera this is and how does it update each frame."
- **No orbit / third-person math.** No boom-arm, no pitch/yaw state on a follow target, no collision probe.
- **No toggle API.** No input action bound to camera mode switching; no runtime blend.
- **No cinematic-mode glue.** `SplinePath` exists but nothing drives a camera along it with FOV control.
- **No `sphereCast` on `PhysicsWorld`.** Third-person wall-probe needs one (or a ray-cluster fallback).

---

## 3. CPU / GPU placement (CLAUDE.md Rule 12)

**All camera-mode work runs on the CPU.** The decision table per Rule 12:

| Work | Scale | Branching | Decision |
|---|---|---|---|
| Per-mode update (read target transform + input, write view matrix) | 1 camera / frame | Minimal — per-mode switch | **CPU.** Single-digit µs; nothing to gain by dispatching. |
| Orbit math (quat compose, boom length) | 1 camera / frame | Scalar | **CPU.** |
| Wall-probe sphere-cast | 1 cast / frame (third-person only) | Branching on hit | **CPU / physics thread.** Jolt runs the cast; we read the result. |
| Spline evaluation | 1 evaluate / frame (cinematic only) | Branchless | **CPU.** The spline API already exists as a CPU type. |
| View / projection matrix assembly | 1 camera / frame | None | **CPU.** Existing `CameraComponent::getViewMatrix()`/`getProjectionMatrix()` path. |
| Lerp during mode transition | ≤2 matrices / frame for ≤~200 ms | None | **CPU.** |

Per the heuristic: per-item GPU work requires scale (thousands / millions) + pure arithmetic + packable layout. Camera modes have scale=1, branching, and scattered inputs. GPU offload would add PCIe transfer + driver overhead for no measurable win. All consumers (renderer.cpp, shadow map builder, shadow frustum) already consume a single `mat4` per frame — no upstream change to the data path.

**One future-GPU candidate noted for audit:** if we later build "multiple live cameras with per-camera renders" (e.g. split-screen, minimap), the *dispatch* of multiple render passes moves work around, but each camera's mode update still stays scalar-CPU. Out of scope for 10.8.

---

## 4. Design

### 4.1 Architecture choice

Research (§8) shows three viable patterns:

| Pattern | Example | Pros | Cons |
|---|---|---|---|
| **M1. Priority-queue / virtual-camera** | Unity Cinemachine | Declarative; any number of cameras live in the scene, highest-priority wins; blends free. | Needs a global `Brain` and a priority store. Overkill for Vestige's single-active model. |
| **M2. Single-manager + modifier stack** | UE5 `APlayerCameraManager` + `UCameraModifier` | Clear composition; shake / lens effects layer cleanly. | Requires a manager singleton and a modifier registry per-player. |
| **M3. ECS activation (flip `isActive`)** | Bevy | Fits ECS-style component model; no new global. Cheap. | No built-in blending — must hand-roll transition lerp. |
| **M4. Base camera + rig-with-behaviours** | O3DE | Composition over inheritance; selectable behaviours per rig. | More types; more concept surface. |

**Recommendation: M3 + explicit blend utility.** Reasons:
- Vestige already uses `Scene::setActiveCamera(CameraComponent*)` — M3 slots directly into this. We add one new component type (`CameraMode`) that drives the active camera's transform per frame.
- The blend-during-transition concern from M3 is handled by a single `CameraBlender` utility (pure function: `blend(from, to, t) -> ViewOutput`), not a global system. Transitions happen only during the 1st↔3rd toggle, which is infrequent; we don't need Cinemachine's always-on blend machinery.
- Shake (Phase 11A) composes *on top of* `ViewOutput` as an additive offset — matches the research footgun #1 advice (shake after mode output, stripped before the next frame's mode calculation).

**Rejected alternatives:**
- M1 adds a priority store and a brain object for ~2 users (the 1st↔3rd toggle + per-scene cinematic-cam-picks-itself-up). Not worth the global surface.
- M2 introduces a manager singleton that doesn't fit the component-driven scene graph.
- M4 is close to what I'm proposing but adds a rig+behaviour distinction. We can get there incrementally if a mode grows multiple sub-behaviours; not needed on day one.

### 4.2 `CameraMode` component family

```cpp
namespace Vestige
{

enum class CameraModeType : uint8_t
{
    FirstPerson,
    ThirdPerson,
    Isometric,
    TopDown,
    Cinematic
};

/// Output of a mode's per-frame update. Consumed by CameraComponent::syncFromMode().
struct CameraViewOutput
{
    glm::vec3 position;      // world-space camera position
    glm::quat orientation;   // world-space camera orientation
    float     fov;           // vertical FOV (perspective) — orthoHalfHeight packed here for ortho modes
    ProjectionType projection;
    // Ortho-specific (ignored for perspective):
    float     orthoSize;
};

/// Per-entity camera-mode behaviour. Attach alongside CameraComponent.
/// Only one CameraMode should be active per scene (enforced by Scene).
class CameraMode : public Component
{
public:
    virtual ~CameraMode() = default;

    /// Per-frame update — reads target / input / spline as appropriate for the
    /// concrete mode and returns the view output. Pure-function contract:
    /// same inputs + same game state ⇒ same output (deterministic, testable).
    virtual CameraViewOutput computeOutput(const CameraInputs& inputs, float dt) = 0;

    virtual CameraModeType type() const = 0;
    virtual std::unique_ptr<Component> clone() const override = 0;
};

} // namespace Vestige
```

`CameraInputs` is a small struct carrying what any mode might need: the target entity's transform (for follow modes), raw input state (for first-person look), the `InputActionMap` (for rebindable toggles), and the current `PhysicsWorld*` (for third-person wall-probe). Passing a struct keeps the `Component::update(dt)` signature intact and makes modes testable without a full scene.

### 4.3 Each mode

**`FirstPersonCameraMode`**
- Inputs: mouse-look delta, WASD (drives entity's Transform via separate input system — mode only consumes final transform for view).
- Output: `position = entity.transform.position + eyeOffset`; `orientation = entity.transform.orientation`; `fov = authored`.
- Parameters: `glm::vec3 eyeOffset = (0, 1.7, 0)` (human eye height default), `float fov = 75.0f`.
- Edge cases: clamp pitch to ±89° to avoid gimbal flip (already industry standard).

**`ThirdPersonCameraMode`**
- Inputs: target entity handle, mouse-look delta (drives orbit yaw/pitch), `PhysicsWorld*` for wall-probe.
- Output: camera positioned on a boom arm behind/above target, looking at target's shoulder.
- Parameters: `boomLength = 3.5`, `pitchClamp = (-45°, +80°)`, `targetOffset = (0, 1.5, 0)` (shoulder height), `sphereRadius = 0.25` (probe).
- Wall-probe: spring-arm pattern. Cast a sphere from `target + targetOffset` along the boom direction out to `boomLength`. If hit at fraction `f`, camera sits at `target + targetOffset + boomDir * (f * boomLength - skinOffset)` where `skinOffset = 0.1` prevents near-plane clipping through the wall.
- Smoothing: critically-damped spring on the boom length (0.15 s time constant) so wall-retraction doesn't pop.

**`IsometricCameraMode`**
- Inputs: target entity (follow point), optional smooth-follow offset.
- Output: orthographic projection; fixed 30° elevation + 45° azimuth (the 2:1-pixel standard — research §8).
- Parameters: `orthoHalfHeight = 8.0`, `elevationDeg = 30.0`, `azimuthDeg = 45.0`, `distance = 20.0` (just for near-plane math; ortho cameras don't care about Z beyond clipping).
- Click-to-move integration: camera publishes `screenToWorldRay(mouseX, mouseY)`; input/gameplay systems consume it. **Not** a camera-mode responsibility.

**`TopDownCameraMode`**
- Inputs: target entity, height.
- Output: orthographic projection; camera directly overhead, looking straight down.
- Parameters: `orthoHalfHeight = 10.0`, `height = 30.0`, optional `headingDeg = 0` for map-aligned North.

**`CinematicCameraMode`**
- Inputs: `const SplinePath*` (position curve), `const FovTrack*` (separate 1-D scalar track for FOV), `float t` (0..1 parameterised along spline length).
- Output: `position = spline.evaluate(t)`; `forward = spline.evaluateTangent(t)` (optionally overridden by a `LookAtTarget` on the cinematic data); `fov = fovTrack.sample(t)`.
- Parameters: `playbackSpeed` (units/sec along arc length), `loopMode` (once / loop / ping-pong).
- `FovTrack` is a new lightweight struct — just `std::vector<{t, fov}>` with linear interpolation. Piece of work in its own right but ~60 LOC.

### 4.4 Runtime 1st ↔ 3rd toggle

Per the research citations (§8.2), modern convention is lerped transition, ~150–300 ms. ROADMAP already specifies 150 ms.

**Toggle flow:**
1. Toggle input fires (`InputActionMap` action `"camera.toggleFirstThird"`, default binding `V`, rebindable).
2. Scene checks `m_activeCameraModeLocked` — if true (per-scene opt-out, e.g. Dead Space archetype), ignore the input and optionally emit a subtle failure sound. The lock is a scene-authored `bool m_firstThirdToggleAllowed = true;` that biblical walkthroughs / horror levels can flip off.
3. If allowed, schedule a transition: record `from = currentMode->computeOutput()`, pick `to` = the other mode's entity (entity graph carries both `FirstPersonCameraMode` and `ThirdPersonCameraMode` as siblings — inactive one doesn't tick), set `m_transitionT = 0.0f, m_transitionDuration = 0.15s`.
4. Each frame during transition: `view = CameraBlender::blend(from, to_current, t)` where `to_current` is the *target mode's* current output (tracks the live camera target, not a stale snapshot). Lerp position, slerp orientation, lerp FOV.
5. When `t >= 1`, swap `Scene::setActiveCamera()` to the target mode's camera entity and clear transition state.

**Accessibility coupling:** if `settings.accessibility.reducedMotion == true`, skip the lerp (set `m_transitionDuration = 0`). Snaps instantly. Consistent with `reducedMotion` semantics elsewhere.

**What about vehicle / combat / dialogue?** Each scene can raise `m_firstThirdToggleAllowed = false` for specific states — the *scene* owns the gating; the *mode* just respects it. Vehicle entry can flip the lock off; dialogue system can flip it off during cutscenes.

### 4.5 Blending utility

```cpp
struct CameraBlender
{
    static CameraViewOutput blend(const CameraViewOutput& from,
                                  const CameraViewOutput& to,
                                  float t) // 0..1
    {
        CameraViewOutput out;
        out.position     = glm::mix(from.position, to.position, t);
        out.orientation  = glm::slerp(from.orientation, to.orientation, t);
        out.fov          = glm::mix(from.fov, to.fov, t);
        out.orthoSize    = glm::mix(from.orthoSize, to.orthoSize, t);
        out.projection   = (t < 0.5f) ? from.projection : to.projection;
        return out;
    }
};
```

Projection-type switches (perspective ↔ ortho) snap at `t = 0.5` — a proper perspective/ortho interpolation is possible but visually weird; discrete switch is the common game-engine choice. 1st↔3rd toggle never crosses projection types (both perspective), so this only affects hypothetical isometric↔first-person transitions, which aren't part of Phase 10.8 scope.

### 4.6 Shake composition (coordination with Phase 11A)

Per research footgun #1: shake must apply **after** mode output and **must not mutate the authoritative state**. The Phase 10.8 contract is:

1. `CameraMode::computeOutput()` produces a pristine `CameraViewOutput`.
2. `CameraComponent` stores that output *and* the shake-offset separately: `m_baseView` + `m_shakeOffset`.
3. For rendering, `getViewMatrix()` returns `view(m_baseView + m_shakeOffset)` — offset is added only at render time.
4. Each frame, `m_shakeOffset` is **reset to zero** *before* Phase 11A's `CameraShakeSystem` writes the next frame's offset. No accumulation.

This keeps Phase 10.8 shake-aware without having to implement shake yet. Phase 11A just writes to `m_shakeOffset`.

### 4.7 Wall-probe — sphere-cast vs. ray cluster

Jolt supports sphere-cast natively (`PhysicsSystem::NarrowPhaseQuery::CollideShape` or `CastShape`). The existing `PhysicsWorld::rayCast` wrapper doesn't expose it. Two implementation paths:

- **W1. Extend `PhysicsWorld` with `sphereCast(origin, direction, radius, …)`.** ~50 LOC; exposes proper geometry via Jolt's `CastShape`. Clean.
- **W2. Approximate with a 5-ray cluster** (centre + 4 edges of a square perpendicular to direction at sphere-diameter spacing). ~20 LOC; uses existing `rayCast`. Approximate — misses ledge corners.

**Recommendation: W1.** The third-person camera is the canonical sphere-cast consumer, and plenty of future consumers (bullet "overlap cone" hit detection, grenade throws, character controller capsule queries) will want a proper shape-cast API. Build it once, use it many times.

---

## 5. Slicing + commit plan

Each slice is a review-sized commit with tests + CHANGELOG entry. Slices are ordered by dependency:

| Slice | Rough LOC | Commits | Depends on | Notes |
|---|---|---|---|---|
| **CM1** — `CameraMode` base + `CameraViewOutput` + `CameraInputs` structs | ~120 | 1 | — | Pure data-model. Header-only component interface; no behaviour yet. Tests: `clone` round-trip, `CameraViewOutput` equality. |
| **CM2** — `FirstPersonCameraMode` (extract existing first-person wiring into the mode) | ~180 | 1 | CM1 | Preserves current demo-scene behaviour — regression test asserts pre/post view-matrix equality on scripted inputs. |
| **CM3** — `PhysicsWorld::sphereCast` API | ~100 | 1 | — | Parallel-trackable with CM1/CM2. Tests: hit / no-hit / edge-grazing. |
| **CM4** — `ThirdPersonCameraMode` + spring-arm wall-probe | ~250 | 1 | CM1 + CM3 | Spring-arm with boom-length smoothing. Tests: orbit at various yaw/pitch, wall retraction on mock `PhysicsWorld`. |
| **CM5** — 1st↔3rd toggle + `CameraBlender` + `reducedMotion` coupling | ~200 | 1 | CM2 + CM4 | Toggle input action, lerp transition, per-scene opt-out flag. Tests: blend determinism, reducedMotion snap, opt-out gate. |
| **CM6** — `IsometricCameraMode` + `TopDownCameraMode` | ~200 | 1 | CM1 | Orthographic modes. Tests: fixed elevation/azimuth, ortho projection correctness, smooth-follow deadzone reuse from `Camera2DComponent`. |
| **CM7** — `FovTrack` + `CinematicCameraMode` | ~220 | 1 | CM1 + existing `SplinePath` | Consumes existing spline. Tests: `t=0`/`t=1` endpoints, FOV interpolation, tangent-follow vs. explicit lookAt. |
| **CM8** — Editor integration (mode inspector, mode-switch preview) | ~180 | 1 | CM2–CM7 | Per-entity mode picker in Inspector; preview toggle in viewport. Tests: Inspector round-trip serialisation. |

**Total:** 8 slices, ~1450 LOC. Slices CM1 / CM3 / CM6 can ship in parallel if desired. The critical path is **CM1 → CM2 → CM4 → CM5**; CM7/CM8 light up afterwards without blocking 11A.

---

## 6. Open questions (blocking approval)

1. **Architecture: M3 (ECS activation) + `CameraBlender` utility?** Alternatives: M1 Cinemachine-style priority queue, M2 UE5-style manager + modifier stack, M4 O3DE-style rig + behaviours. Doc recommends M3 — least global surface, fits existing `setActiveCamera()` model. Confirm.

2. **First-person mode refactor — extract existing camera-drive code into `FirstPersonCameraMode`, or leave existing code in place and build the new mode separately?** Doc recommends *extract* — avoids two first-person codepaths drifting. Preserved by a regression test (view-matrix equality against a scripted-input replay). Confirm.

3. **Wall-probe — W1 (new `sphereCast` API on `PhysicsWorld`) or W2 (5-ray cluster approximation)?** Doc recommends W1 — proper geometry, future consumers (bullet cone, grenade throws) will want it. Confirm.

4. **Transition duration — 150 ms (per ROADMAP) or 200 ms (mid-industry)?** Research shows 100 ms (Cinemachine default) to ~1 s (modded Skyrim) range; most modern games sit around 150–250 ms. Recommend 150 ms as the ROADMAP baseline with a `settings.gameplay.cameraTransitionSec` override (default 0.15). Confirm.

5. **Per-mode vs. per-camera FOV authority — who owns the authoritative FOV?** Option F1: each mode carries its own `fov` and writes it to `CameraViewOutput`; `CameraComponent.fov` becomes a display-only field. Option F2: `CameraComponent.fov` is authoritative; modes optionally *override* it (nullable). Doc recommends **F1** — makes modes truly self-contained and means the cinematic FOV track can freely override without fighting a static field. Confirm.

6. **Cinematic input shape — `CinematicCameraMode` holds `SplinePath*` + `FovTrack*`, or a higher-level `CinematicTake` struct bundling (spline, FOV, lookAt, duration, ease)?** Doc recommends the bundled `CinematicTake` — mirrors Unity Timeline and UE5 Sequencer takes. Confirm.

7. **Per-scene 1st↔3rd opt-out — scene carries a `bool m_firstThirdToggleAllowed = true` field, or a more general `CameraGameplayLock` struct with multiple flags (toggle-allowed, mode-override, etc.)?** Doc recommends the single bool for Phase 10.8; promote to a struct if/when we find a second lock flag needs to exist. Confirm (YAGNI principle).

8. **Slice order — CM1 → CM2 → CM3 (parallel) → CM4 → CM5 → CM6/CM7 (parallel) → CM8.** Alternative: front-load CM6 (isometric/top-down) because it's cheapest and ships a demoable new mode fastest. Confirm.

---

## 7. Non-goals (explicitly out of scope)

- **Camera Shake.** Phase 11A. This phase only guarantees the composition contract (§4.6).
- **Post-process stack per-mode.** Phase 11B / Phase 13 concern. Vestige's single-active-camera model means the post-FX stack naturally follows the live camera; per-mode overrides are a separate design.
- **Split-screen / multiple live cameras.** Not on the roadmap; distinct architectural decision if ever needed.
- **Mouse-sensitivity curves / aim-assist.** Those are input-system concerns, not camera-mode concerns.
- **Editor camera (`EditorCamera`) migration.** The editor uses its own fly-camera by design; unchanged.
- **Vehicle camera modes.** Phase 11B — vehicle-specific variants (bumper, hood, chase, cockpit) land when vehicle physics does. Cinematic crash camera is also Phase 11B.
- **3rd-person orbit in vehicles.** Deferred to Phase 11B vehicle code.
- **Advanced cinematic features** — per-take ease curves, camera dolly physics, rack focus, lens distortion. Phase 13 advanced rendering.
- **Mobile / gamepad stick deadzone tuning for orbit.** Touch up when we care about console ports.

---

## 8. Research citations

Full research notes in the Camera Modes research agent transcript (2026-04-23).

**Architecture patterns.**
- Unity Cinemachine — camera control & transitions: https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/concept-camera-control-transitions.html
- Cinemachine 3 changelog (Impulse `ApplyAfter` rationale): https://docs.unity3d.com/Packages/com.unity.cinemachine@3.0/changelog/CHANGELOG.html
- CinemachineBrain API: https://docs.unity3d.com/Packages/com.unity.cinemachine@3.0/api/Unity.Cinemachine.CinemachineBrain.html
- Unreal Engine 5 — Cameras in UE: https://dev.epicgames.com/documentation/en-us/unreal-engine/cameras-in-unreal-engine
- Camera Management in UE5 (walledgarden blog): https://jiahaoli.org/blog/2023/04/22/camera-management-in-unreal-engine-5-introduction/
- UE Spring Arm docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-spring-arm-components?application_version=4.27
- Godot SpringArm3D tutorial: https://docs.godotengine.org/en/latest/tutorials/3d/spring_arm.html
- Bevy Cameras (DeepWiki): https://deepwiki.com/bevyengine/bevy/5.2-camera-and-view-management
- Bevy Cameras (Tainted Coders): https://taintedcoders.com/bevy/cameras
- Bevy screen-shake example (compositional reference): https://bevy.org/examples/camera/2d-screen-shake/
- O3DE Camera Rig Component: https://docs.o3de.org/docs/user-guide/components/reference/camera/camera-rig/
- Flax Cameras manual: https://docs.flaxengine.com/manual/graphics/cameras/index.html

**1st↔3rd toggle convention.**
- Skyrim transition-speed modding thread (tunable via `fMouseWheelZoomSpeed`, widely modded to ~1 s): https://forums.nexusmods.com/topic/701984-how-do-you-speed-up-the-transition-from-3rd-person-to-1st-person/ — *3-second default reported on forums; not confirmed in official Bethesda docs.*
- Minecraft Third-person view (F5 cycle, instant snap): https://minecraft.wiki/w/Third-person_view
- GTA V camera-perspective docs (rebindable + "independent camera modes" vehicle/foot option): https://support.rockstargames.com/articles/360001491747/Changing-Camera-Perspective-to-1st-Person-in-GTAV
- Dead Space Remake accessibility review (fixed over-the-shoulder camera, moved farther back from original): https://caniplaythat.com/2023/03/06/dead-space-remake-accessibility-review/ — *"design pillar" framing is inference from the review, not a direct EA Motive quote.*

**Isometric / top-down.**
- Pikuma — Isometric projection in games: https://pikuma.com/blog/isometric-projection-in-games
- Wikipedia — Isometric video game graphics: https://en.wikipedia.org/wiki/Isometric_video_game_graphics

**Cinematic / spline.**
- Lighthouse3D — Catmull-Rom splines: https://www.lighthouse3d.com/tutorials/maths/catmull-rom-spline/
- CodeProject — Overhauser/Catmull-Rom for camera animation: https://www.codeproject.com/articles/Overhauser-Catmull-Rom-Splines-for-Camera-Animatio

**Shake composition footgun (referenced in §4.6).**
- Squirrel Eiserloh, GDC 2016 — Math for Game Programmers: Juicing Your Cameras (PDF): http://www.mathforgameprogrammers.com/gdc2016/GDC2016_Eiserloh_Squirrel_JuicingYourCameras.pdf

---

## 9. Approval checklist

- [x] §6 Q1 — Architecture: M3 (ECS activation) + `CameraBlender` utility — approved 2026-04-23
- [x] §6 Q2 — First-person refactor: extract into `FirstPersonCameraMode` — approved 2026-04-23
- [x] §6 Q3 — Wall-probe: W1 (new `sphereCast`) — approved 2026-04-23
- [x] §6 Q4 — Transition duration: 150 ms default + `settings.gameplay.cameraTransitionSec` override — approved 2026-04-23
- [x] §6 Q5 — FOV authority: F1 (mode-owned) — approved 2026-04-23
- [x] §6 Q6 — Cinematic input: `CinematicTake` bundle — approved 2026-04-23
- [x] §6 Q7 — Per-scene opt-out: single bool (YAGNI) — approved 2026-04-23
- [x] §6 Q8 — Slice order: CM1 → CM2 → CM3 (parallel) → CM4 → CM5 → CM6/CM7 (parallel) → CM8 — approved 2026-04-23
- [ ] ROADMAP.md amended if scope changes from approval (no scope changes)
- [ ] CHANGELOG.md Unreleased entry prepared once slicing starts
