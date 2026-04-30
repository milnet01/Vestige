# Subsystem Specification — `engine/animation`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/animation` |
| Status | `shipped` (foundation; W12 cluster relocated to `engine/experimental/animation/`) |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (Phase 7A foundation; A-series audit fixes shipped 2026-04-27) |

---

## 1. Purpose

`engine/animation` is the CPU side of the engine's animation pipeline: it owns the data types and runtime that turn a glTF skin + clips into per-bone matrices ready for the renderer's GPU skinning, plus the orthogonal helpers — sprite-sheet animation for 2D, property tweens, easing curves, morph-target weight blending, and Inverse Kinematics (IK) solvers (analytic two-bone, look-at, foot — no Forward And Backward Reaching IK (FABRIK) or Cyclic Coordinate Descent (CCD) chains today; see §14 / §15).

It exists as its own subsystem because every one of those primitives is consumed by code in `engine/scene` (component-driven runtime), `engine/renderer` (skinning + morph deformation in shaders), `engine/utils/gltf_loader` (asset-side construction), and the `CharacterSystem` / `SpriteSystem` domain wrappers — pulling the math into any one of those subsystems would force the others to depend on it sideways.

For the engine's primary use case — first-person walkthroughs of biblical structures — animation is the path that makes the High Priest's vestments swing, the Tabernacle's veil draw, and any future non-player character (NPC) walk. It is not the primary load-bearing subsystem of an architectural walkthrough, but the engine targets broader games as a secondary use case (per CLAUDE.md), and `engine/animation` is the foundation those games stand on.

The directory was larger before the Phase 10.9 Slice 8 W12 audit relocated the unwired motion-matching, lip-sync, and facial-animation cluster to `engine/experimental/animation/` (see §15). What remains here is the production-live subset.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `Skeleton`, `Joint` — joint hierarchy + inverse bind matrices + Depth-First Search (DFS) update order | glTF parsing into these types — `engine/utils/gltf_loader` |
| `AnimationClip`, `AnimationChannel` — keyframe data; STEP / LINEAR / CUBICSPLINE interpolation | Asset I/O / disk loading — `engine/utils/gltf_loader` constructs them |
| `AnimationSampler` (free functions) — interpolate one channel at one time | Long-running clip authoring tools — N/A |
| `SkeletonAnimator` (`Component`) — playback, crossfade blend, root motion, morph weight sampling | Skin deformation in vertex shader — `assets/shaders/scene.vert` + `engine/renderer` |
| `AnimationStateMachine` — data-driven states + parameterised transitions driving a `SkeletonAnimator` | Visual state-graph editor UI — not yet built |
| `IK solvers` — analytic two-bone, look-at, foot IK | Procedural ragdoll, hand-on-prop attachment — `engine/experimental/physics/ragdoll.*` |
| `MorphTargetData` + `blendMorphPositions` / `blendMorphNormals` (CPU spec) | GPU morph SSBO upload + vertex-shader blending — `engine/renderer/mesh.{h,cpp}` (binding 3) |
| `Tween`, `TweenManager` (`Component`) — float / vec3 / vec4 / quat property animation with easing + events | Animation-curve editor — N/A |
| `EaseType` + Penner easings + `CubicBezierEase` (CSS-style control points) | Spline curve types beyond cubic bezier — `engine/utils/spline.h` |
| `SpriteAnimation` (Aseprite-compatible per-frame-duration clips) | Sprite atlas / texture binding — `engine/renderer/sprite_*` |
| Root motion extraction (delta translation + rotation per frame) | Applying delta to the entity transform — caller's policy (gameplay code or future character system) |
| Pose blending (single crossfade at a time) | Multi-track blend trees, additive layers, layered IK rigs — deferred (see §15) |
| Inverse kinematics analytic solvers | Iterative FABRIK / CCD chains — N/A; see §14 references and §15 |

## 3. Architecture

```
                   glTF on disk
                        │
                        ▼
              engine/utils/gltf_loader
              (loadSkin, loadAnimations,
               loadMorphTargets)
                        │ owns
                        ▼
           ┌────────────────────────────┐
           │  Skeleton (shared_ptr)     │
           │  AnimationClip (shared_ptr)│  ◄────── engine/resource/Model
           │  MorphTargetData           │
           └────────────────────────────┘
                        │ refs
                        ▼
        ┌─────────────────────────────────────┐
        │   SkeletonAnimator (Component)      │
        │   ┌──────────────────────────────┐  │
        │   │ advance + sample (per chan)  │  │ ── animation_sampler ──┐
        │   │   ↓                          │  │                        │
        │   │ (optional crossfade blend)   │  │                        ▼
        │   │   ↓                          │  │             AnimationClip
        │   │ extractRootMotion()          │  │             (channels)
        │   │   ↓                          │  │
        │   │ computeBoneMatrices()        │  │
        │   │   (DFS via m_updateOrder)    │  │
        │   └──────────────────────────────┘  │
        │   m_boneMatrices ──┐  m_morphWts ──┐│
        └────────────────────┼───────────────┼┘
                             │               │
                             ▼               ▼
                      engine/renderer (GPU skinning + morph SSBO)

   AnimationStateMachine ─── drives ──▶ SkeletonAnimator::crossfadeToIndex

   TweenManager (Component) ─── ticks N Tweens ─── writes float/vec3/vec4/quat targets

   SpriteAnimation ─── owned by ──▶ SpriteComponent ─── SpriteSystem renders frame name

   IK solvers (free functions) ─── caller passes in joint state, gets corrected local rots ──▶
       caller writes back into SkeletonAnimator local pose (no built-in IK loop yet)
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `Joint` | struct | Name, parent index, inverse bind matrix, local bind transform. `engine/animation/skeleton.h:17` |
| `Skeleton` | class | Joint forest + DFS pre-order `m_updateOrder` (audit A1). `engine/animation/skeleton.h:26` |
| `AnimationChannel` | struct | One channel: joint index, target path (T/R/S/W), interpolation, timestamps, packed values. `engine/animation/animation_clip.h:32` |
| `AnimInterpolation` | enum | `STEP` / `LINEAR` / `CUBICSPLINE`. `engine/animation/animation_clip.h:15` |
| `AnimTargetPath` | enum | `TRANSLATION` / `ROTATION` / `SCALE` / `WEIGHTS`. `engine/animation/animation_clip.h:23` |
| `AnimationClip` | class | Named bag of channels + cached duration. `engine/animation/animation_clip.h:45` |
| `sampleVec3 / sampleQuat` | free functions | Stateless single-channel evaluators; binary-search keyframe lookup. `engine/animation/animation_sampler.h:20,26` |
| `SkeletonAnimator` | class (`Component`) | Per-entity playback driver — owns pose buffers, runs sampler, blends crossfade, walks hierarchy. `engine/animation/skeleton_animator.h:35` |
| `RootMotionMode` | enum | `IGNORE` / `APPLY_TO_TRANSFORM`. `engine/animation/skeleton_animator.h:23` |
| `AnimationStateMachine` | class | States + parameterised transitions; drives a `SkeletonAnimator` via `crossfadeToIndex`. `engine/animation/animation_state_machine.h:61` |
| `TwoBoneIKRequest / Result` + `solveTwoBoneIK` | structs + free function | Analytic two-bone IK with pole vector (audit A4). `engine/animation/ik_solver.h:19,37,47` |
| `LookAtIKRequest / Result` + `solveLookAtIK` | structs + free function | Look-at with max-angle clamp. `engine/animation/ik_solver.h:54,68,76` |
| `FootIKRequest / Result` + `solveFootIK` | structs + free function | Two-bone IK to ground point + ankle alignment + pelvis offset. `engine/animation/ik_solver.h:83,107,118` |
| `MorphTarget / MorphTargetData` | structs | Per-vertex deltas (positions / normals / tangents) + default weights. `engine/animation/morph_target.h:20,30` |
| `blendMorphPositions / blendMorphNormals` | free functions | CPU reference blend (the GPU runtime is in `engine/renderer/mesh`). `engine/animation/morph_target.h:50,63` |
| `EaseType` | enum | 32 Penner easings + `LINEAR` / `STEP`. `engine/animation/easing.h:14` |
| `evaluateEasing` | free function | `EaseType + t → eased t`. `engine/animation/easing.h:37` |
| `CubicBezierEase` | class | CSS-style cubic bezier (Newton-Raphson solve x(t)=input). `engine/animation/easing.h:46` |
| `Tween` | class | One float / vec3 / vec4 / quat property animation; builder API; events at normalised times. `engine/animation/tween.h:40` |
| `TweenManager` | class (`Component`) | Owns + ticks a vector of `Tween`s; cancels by target pointer. `engine/animation/tween.h:159` |
| `SpriteAnimation` | class | Aseprite-compatible per-frame-duration clip player; forward / reverse / ping-pong. `engine/animation/sprite_animation.h:51` |

## 4. Public API

10 public headers — past the template's 7-header threshold, so this section follows the **facade-by-header pattern**: one code block per public header showing types + headline functions, with `// see <header> for full surface` pointers where the public surface is wider than the snippet shows.

```cpp
// animation/skeleton.h
struct Joint { /* name, parentIndex, inverseBindMatrix, localBindTransform */ };
class Skeleton {
public:
    int  getJointCount() const;
    int  findJoint(const std::string& name) const;        // -1 if missing
    void buildUpdateOrder();                              // DFS pre-order; idempotent
    static constexpr int MAX_JOINTS = 128;
    std::vector<Joint> m_joints;
    std::vector<int>   m_rootJoints;
    std::vector<int>   m_updateOrder;                     // populated by buildUpdateOrder
};
```

```cpp
// animation/animation_clip.h
enum class AnimInterpolation { STEP, LINEAR, CUBICSPLINE };
enum class AnimTargetPath    { TRANSLATION, ROTATION, SCALE, WEIGHTS };
struct AnimationChannel {
    int               jointIndex = -1;     // -1 for WEIGHTS (mesh-targeted)
    AnimTargetPath    targetPath;
    AnimInterpolation interpolation;
    std::vector<float> timestamps;         // ascending, seconds
    std::vector<float> values;             // packed 3/4/(3*N) floats per key
};
class AnimationClip {
public:
    float              getDuration() const;
    const std::string& getName() const;
    void               computeDuration();
    std::string        m_name;
    std::vector<AnimationChannel> m_channels;
    float              m_duration = 0.0f;
};
```

```cpp
// animation/animation_sampler.h  — stateless
glm::vec3 sampleVec3(const AnimationChannel&, float time);   // STEP / LINEAR / CUBICSPLINE
glm::quat sampleQuat(const AnimationChannel&, float time);   // Spherical Linear Interpolation (SLERP) for LINEAR; double-cover fix for CUBICSPLINE (audit A2)
```

```cpp
// animation/skeleton_animator.h  — Component
enum class RootMotionMode { IGNORE, APPLY_TO_TRANSFORM };
class SkeletonAnimator : public Component {
public:
    // Lifecycle
    void update(float dt) override;
    std::unique_ptr<Component> clone() const override;

    // Skeleton + clips (shared between instances)
    void setSkeleton(std::shared_ptr<Skeleton>);
    void addClip   (std::shared_ptr<AnimationClip>);
    int  getClipCount() const;
    const std::shared_ptr<AnimationClip>& getClip(int) const;     // returns null sp if Out Of Range (OOR)

    // Playback
    void  play       (const std::string& clipName);                // restart from 0
    void  playIndex  (int index);
    void  stop();
    void  setPaused  (bool); bool isPaused()  const;
    void  setLooping (bool); bool isLooping() const;
    void  setSpeed   (float); float getSpeed() const;
    bool  isPlaying  () const;
    float getCurrentTime() const;
    int   getActiveClipIndex() const;

    // Crossfade (single source → target; sourceClipIndex == -1 means frozen pose)
    void crossfadeTo     (const std::string& clipName, float duration);
    void crossfadeToIndex(int index, float duration);
    bool isCrossfading() const;

    // Root motion (delta accumulated in update(); caller applies)
    void          setRootMotionMode(RootMotionMode);
    RootMotionMode getRootMotionMode() const;
    void          setRootMotionBone(int);
    glm::vec3     getRootMotionDeltaPosition() const;
    glm::quat     getRootMotionDeltaRotation() const;

    // Output
    const std::vector<glm::mat4>& getBoneMatrices() const;        // one per joint, ready for GPU
    bool                          hasBones() const;

    // Morph weights (sampled from WEIGHTS channels OR set procedurally)
    const std::vector<float>& getMorphWeights() const;
    void setMorphWeight(int index, float weight);
    void setMorphTargetCount(int count);
};
```

```cpp
// animation/animation_state_machine.h
enum class AnimCompareOp { GREATER, LESS, GREATER_EQ, LESS_EQ, EQUAL, NOT_EQUAL };
struct AnimTransitionCondition { std::string paramName; AnimCompareOp op; float threshold; };
struct AnimState                { std::string name; int clipIndex; float playbackSpeed; bool loop; };
struct AnimTransition           { int fromState; int toState; float crossfadeDuration; float exitTime;
                                  std::vector<AnimTransitionCondition> conditions; };
class AnimationStateMachine {
public:
    int  addState(const AnimState&);
    void addTransition(const AnimTransition&);
    int  getStateCount() const;
    const AnimState& getState(int) const;
    void setFloat  (const std::string&, float);
    void setBool   (const std::string&, bool);   // stored as 0/1 in m_params
    void setTrigger(const std::string&);          // one-shot; consumed by the firing transition
    float getFloat (const std::string&) const;
    bool  getBool  (const std::string&) const;
    void  start    (SkeletonAnimator&);
    void  update   (SkeletonAnimator&, float deltaTime);
    int               getCurrentStateIndex() const;
    const std::string& getCurrentStateName() const;
    bool              isRunning() const;
};
```

```cpp
// animation/ik_solver.h  — analytic, stateless
TwoBoneIKResult solveTwoBoneIK(const TwoBoneIKRequest&);
LookAtIKResult  solveLookAtIK (const LookAtIKRequest&);
FootIKResult    solveFootIK   (const FootIKRequest&);   // wraps two-bone + ankle alignment
```

```cpp
// animation/morph_target.h
struct MorphTarget    { std::string name; std::vector<glm::vec3> positionDeltas, normalDeltas, tangentDeltas; };
struct MorphTargetData{ std::vector<MorphTarget> targets; std::vector<float> defaultWeights; size_t vertexCount; };
void blendMorphPositions(const MorphTargetData&, const std::vector<float>& weights,
                         const std::vector<glm::vec3>& base, std::vector<glm::vec3>& out);
void blendMorphNormals  (const MorphTargetData&, const std::vector<float>& weights,
                         const std::vector<glm::vec3>& base, std::vector<glm::vec3>& out);
```

```cpp
// animation/easing.h
enum class EaseType : uint8_t { LINEAR, STEP, EASE_IN_QUAD, ..., EASE_IN_OUT_BOUNCE, COUNT };
float       evaluateEasing(EaseType, float t);          // t in [0,1]; some types overshoot
const char* easeTypeName  (EaseType);
class CubicBezierEase {
public:
    CubicBezierEase(float x1, float y1, float x2, float y2);  // CSS cubic-bezier control points
    float evaluate(float t) const;                            // Newton-Raphson, max 8 iters
};
```

```cpp
// animation/tween.h  — Tween + TweenManager(Component)
class Tween {
public:
    enum class TargetType : uint8_t { FLOAT, VEC3, VEC4, QUAT };
    static Tween floatTween(float*,   float,             float,             float dur, EaseType=LINEAR);
    static Tween vec3Tween (glm::vec3*, const glm::vec3&, const glm::vec3&, float dur, EaseType=LINEAR);
    static Tween vec4Tween (glm::vec4*, const glm::vec4&, const glm::vec4&, float dur, EaseType=LINEAR);
    static Tween quatTween (glm::quat*, const glm::quat&, const glm::quat&, float dur, EaseType=LINEAR);  // slerp
    Tween& setPlayback (TweenPlayback);     // ONCE / LOOP / PING_PONG
    Tween& setDelay    (float seconds);
    Tween& setEase     (EaseType);
    Tween& setCustomEase(float x1,float y1,float x2,float y2);
    Tween& onComplete  (std::function<void()>);
    Tween& onLoop      (std::function<void()>);
    Tween& addEvent    (float normalisedTime, std::function<void()>);
    void  update(float dt); void pause(); void resume(); void stop(); void restart();
    bool  isFinished() const; bool isPaused() const; float getProgress() const;
};
class TweenManager : public Component {
public:
    Tween& add        (Tween);
    void   cancelTarget(void* target);
    void   cancelAll  ();
    size_t activeTweenCount() const;
};
```

```cpp
// animation/sprite_animation.h
struct SpriteAnimationFrame { std::string name; float durationMs = 100.0f; };
enum class SpriteAnimationDirection { Forward, Reverse, PingPong };
struct SpriteAnimationClip  { std::string name; std::vector<SpriteAnimationFrame> frames;
                              SpriteAnimationDirection direction = Forward; bool loop = true; };
class SpriteAnimation {
public:
    void        addClip(SpriteAnimationClip);
    std::size_t clipCount() const;
    const SpriteAnimationClip* findClip(const std::string&) const;
    void  play(const std::string& clipName);                  // resets to first frame
    void  stop();
    void  tick(float deltaTimeSeconds);                       // no-op when stopped
    const std::string& currentFrameName() const;
    int   currentFrameIndex() const;                          // -1 if none
    float currentFrameElapsedMs() const;
    bool  isPlaying() const;
    bool  advancedLastTick() const;                           // cleared next tick
};
```

**Non-obvious contract details:**

- **Time and angles are in radians and seconds at every API boundary** (CODING_STANDARDS §27). `AnimationChannel::timestamps` are seconds; `LookAtIKRequest::maxAngle` is radians; `SpriteAnimationFrame::durationMs` is the deliberate exception (Aseprite's exporter writes ms — converting on import would lose the round-trip property).
- **`Skeleton::buildUpdateOrder()` MUST be called after `m_joints` and `m_rootJoints` are populated.** glTF's `skin.joints` array is not required to be parent-before-child; iterating storage order produced silent corruption pre-A1. `gltf_loader::loadSkin` calls it; hand-built skeletons in tests must call it themselves.
- **`SkeletonAnimator` shares its `Skeleton` and `AnimationClip`s via `std::shared_ptr` across instances.** Only the per-instance pose buffers (`m_localTranslations`, `m_globalTransforms`, `m_boneMatrices`, `m_morphWeights`, source crossfade buffers) are owned by the animator. `clone()` deep-copies the pointer state (skeleton + clips remain shared), allocates fresh pose buffers, resets `m_currentTime` to 0.
- **`crossfadeTo` keeps advancing the source clip during the blend** — the source's own playback time progresses by `dt * speed`, so if you crossfade out of a walk the source walk keeps walking until the blend completes. Setting `m_sourceClipIndex = -1` (special case) freezes the source pose buffers instead.
- **`AnimationStateMachine::update` evaluates one transition per frame.** Transitions are checked in declaration order; first one whose conditions are all true (AND logic) and whose `exitTime` constraint passes fires. `setTrigger` is consumed when a transition that references the trigger fires.
- **Root motion delta is computed AFTER sampling/blending and BEFORE bone-matrix computation.** `extractRootMotion` zeroes the root bone's X/Z translation and rotation in `m_localTranslations[rootIdx]` so the resulting bone matrices keep the mesh centred; the caller is responsible for applying the delta to the entity transform (currently no built-in path — the field is exposed via `getRootMotion*`). Y translation is preserved for jumps / stairs.
- **WEIGHTS channels have `jointIndex == -1`** (they target the mesh, not a joint). Sampler in `SkeletonAnimator::advanceAndSample` interpolates them inline and writes into `m_morphWeights` — no separate API call required.
- **`Tween` is move-only.** `TweenManager::add` takes by value (move-construction), returns a reference into its internal vector. The reference can dangle if more tweens are added subsequently and the vector reallocates — chain configuration calls in one statement (`mgr.add(t).setDelay(0.5f).onComplete(...)`).
- **`evaluateEasing` for `EASE_*_ELASTIC` / `EASE_*_BACK` deliberately overshoots [0,1].** Callers that animate a clamped-range target must clamp themselves.
- **`SpriteAnimation::play(name)` always resets to the first frame**, even if `name` is the currently-playing clip — call `isPlayingClip(name)` first if you want to preserve playback state across identical transitions (e.g. running into a transition that re-asserts "run").
- **IK solvers are stateless and analytic.** They do not mutate the input request; they return a result struct. Caller wires the corrected `localRot` back into `SkeletonAnimator::m_localRotations` itself — there is no built-in `applyIK` step in the animator's update loop.

**Stability:** the facade above is semver-respecting for `v0.x`. `SkeletonAnimator` exposes its pose-buffer members and `m_clips` as public for the loader's convenience (a smell — the renderer reads `getBoneMatrices()` only, but `gltf_loader` mutates `m_clips` via `addClip`); migrating to a friend-class or builder is on the open-questions list. Audit-driven changes since `v0.1.0` (A1 / A2 / A4 / A5) were behaviour-preserving for correct inputs and behaviour-changing for inputs that previously hit the bugs.

## 5. Data Flow

**Asset → runtime, one-time per model:**

1. `engine/utils/gltf_loader::loadSkin` populates `Skeleton::m_joints` + `m_rootJoints`, then calls `buildUpdateOrder()` (audit A1).
2. `loadAnimations` walks every glTF animation, builds `AnimationChannel`s with timestamps + packed values, calls `AnimationClip::computeDuration()`.
3. `loadMorphTargets` populates `MorphTargetData`.
4. The owning `Model` (`engine/resource/model.h`) holds `shared_ptr<Skeleton>` + `vector<shared_ptr<AnimationClip>>` + `MorphTargetData`.
5. Scene instantiates an entity, attaches a `SkeletonAnimator` component, calls `setSkeleton(skeleton)` → allocates pose buffers from bind pose; `addClip(clip)` for each.

**Per-frame, per `SkeletonAnimator` (`update(dt)`):**

1. Early-out if not playing / paused / no skeleton / no active clip — also clears `m_rootMotionDelta*`.
2. **Single-clip path:** `advanceAndSample(activeClip, m_currentTime, dt, m_localTranslations, m_localRotations, m_localScales)`. WEIGHTS channels write into `m_morphWeights` inline.
3. **Crossfade path:** `advanceAndSample` the source clip into source buffers, then the target clip into primary buffers, then per-bone blend `mix(src, tgt, blendFactor)` for T/S and `slerp` for R. When `blendFactor >= 1` the crossfade ends.
4. Non-looping clip past its end → `m_playing = false`.
5. `extractRootMotion()` — store delta from prev → current root pose; zero root X/Z translation + rotation in the local pose so the mesh stays centred.
6. `computeBoneMatrices()` — iterate `m_skeleton->m_updateOrder` (DFS pre-order, audit A1); each joint's global = parent's global × local Translation/Rotation/Scale (TRS); bone matrix = global × inverse-bind.

**State machine driving an animator (`update(animator, dt)`):**

1. Walk transitions; first one with `fromState ∈ {currentState, ANY}` whose `evaluateTransition` returns true fires.
2. `evaluateTransition`: `exitTime` clamp on source clip's normalised time, then AND of all conditions (`evaluateCondition` reads `m_params` / `m_triggers`).
3. On fire: `animator.setSpeed/setLooping`, `animator.crossfadeToIndex(targetState.clipIndex, transition.crossfadeDuration)`, consume any triggers referenced by the conditions.
4. Only one transition per frame.

**Renderer consumption:** `engine/renderer` reads `SkeletonAnimator::getBoneMatrices()` (max 128, `Skeleton::MAX_JOINTS`) and uploads to a bone-matrix Shader Storage Buffer Object (SSBO); reads `getMorphWeights()` and uploads via uniform array (`u_morphWeights[i]`) plus binds the morph SSBO at binding 3. On Mesa, an always-bound dummy SSBO at binding 3 satisfies driver validation when the mesh has no morphs (per project memory `feedback_mesa_sampler_binding`).

**Sprite animation drive:** `SpriteComponent::update(dt)` → `animation->tick(dt)` → renderer reads `currentFrameName()` to look up the atlas entry. No SSBO involved.

**Tween drive:** `Scene::update(dt)` walks the component tree; `TweenManager::update(dt)` ticks every owned `Tween`, removes finished ones, fires `onComplete`/`onLoop`/event callbacks at normalised-time crossings.

**Exception path:** asset version drift surfaces at load time, not runtime — `gltf_loader` rejects malformed clips before they reach `engine/animation`. At runtime the sampler tolerates empty channels (returns identity / zero), out-of-range joint indices (skips), and zero-duration clips (early-out in `advanceAndSample`). No exceptions cross the API boundary.

## 6. CPU / GPU placement

Per CLAUDE.md Rule 7. Animation is a CPU/GPU split: pose evaluation and bone-matrix derivation run on the CPU; deformation runs on the GPU.

| Workload | Placement | Reason |
|----------|-----------|--------|
| Per-channel keyframe interpolation (`sampleVec3` / `sampleQuat`) | CPU (main thread) | Branching per interpolation type; binary-search lookup; per-bone, not per-vertex. Cost scales with `O(joints × channels)` ≈ low hundreds, not vertex count. |
| Crossfade pose blend (per-bone slerp / lerp) | CPU (main thread) | Same reason — per-bone, ≤ 128 joints. |
| Joint-hierarchy walk + bone-matrix multiply | CPU (main thread) | Sparse / pointer-chasing through parent indices; sequential dependency (parent before child). Suits CPU; not data-parallel without restructuring. |
| Linear blend skinning (per-vertex deformation) | **GPU** (vertex shader, `assets/shaders/scene.vert`) | Per-vertex × bones-per-vertex; the textbook GPU workload. |
| Morph-target deformation (per-vertex weighted delta sum) | **GPU** (vertex shader; deltas in SSBO at binding 3, weights as uniform array up to `MAX_MORPH_TARGETS = 8`) | Per-vertex × targets; data-parallel; same heuristic as skinning. |
| `blendMorphPositions / blendMorphNormals` (CPU spec) | CPU | **Reference / spec only.** Used by `tests/test_morph_target.cpp` to pin the expected blend semantics; NOT called per-frame in production. The runtime path is the GPU vertex shader. |
| IK solvers | CPU (main thread, on-demand) | Branching, sparse, per-character (not per-vertex). |
| Sprite animation tick | CPU (main thread) | Scalar bookkeeping; one frame per sprite per frame. |
| Tween tick | CPU (main thread) | Scalar property writes; finished-tween removal is a sparse decision. |

**Dual implementations:** the CPU `blendMorphPositions / blendMorphNormals` and the GPU vertex-shader morph blend in `assets/shaders/scene.vert` are paired CPU spec / GPU runtime; the parity test is `tests/test_morph_target_pipeline.cpp` (the math equivalence of the spec is also pinned by `tests/test_morph_target.cpp`). Skeletal skinning has no CPU mirror in `engine/animation` — the CPU side stops at `getBoneMatrices()`; the vertex shader does the per-vertex multiply.

## 7. Threading model

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** | All of `engine/animation`. `Scene::update` walks the component tree on the main thread, calling `Component::update(dt)` on each `SkeletonAnimator` / `TweenManager` / `SpriteAnimation`. State machines and IK solvers are also main-thread. | None — no internal locks. |
| **Worker threads** | None of `engine/animation` is currently invoked from worker threads. | N/A |

**Main-thread-only by contract.** No internal mutex, no atomic. Animation has no `std::shared_ptr` race because per-instance buffers (`m_local*`, `m_boneMatrices`, `m_morphWeights`) are never read on a thread other than the one running `update`. The shared `Skeleton` and `AnimationClip` are read-only after asset load.

**Worker-thread sampling — flagged but not implemented.** Per-character pose evaluation (steps 1–5 of §5's per-frame flow, ending at `m_boneMatrices`) is embarrassingly parallel across animators (no cross-instance state). A future job-system migration could move every animator's pose evaluation to a worker pool fan-out and join before the renderer's bone-matrix upload. Tracked as Open Q3 in §15 — the public API does not promise thread-safety today and downstream callers must not assume it.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms/frame. `engine/animation` is per-character work; the budget scales with the number of animated entities in scene. Architectural-walkthrough scenes (the engine's primary use case) typically have 0–4 animated characters; broader-game scenes may have dozens.

Not yet measured — will be filled by the Phase 11 audit (concretely, end of Phase 10.9 cycle); tracked as Open Q5 in §15.

Tentative target shape (will replace once measured):

- `SkeletonAnimator::update` per character (≤ 64 joints, single-clip): target < 0.05 ms.
- `SkeletonAnimator::update` per character (crossfade): target < 0.10 ms.
- `AnimationStateMachine::update` per character: target < 0.01 ms.
- `Tween::update`: target < 0.001 ms each; manager hot path budget < 0.1 ms total.
- `SpriteAnimation::tick`: target < 0.005 ms each.
- IK solver call (one of two-bone / look-at / foot): target < 0.01 ms each.

**Profiler markers / capture points:** `engine/animation` does not currently emit `glPushDebugGroup` markers (no GPU work in this subsystem). The relevant downstream site is the renderer's bone-matrix-upload pass at `engine/renderer/renderer.cpp:1500-1510`, but **no `glPushDebugGroup` is emitted there today** — this matches the renderer spec's §15 Q2 "GPU debug markers not yet wired" debt. When that debt closes, the proposed marker name for this site is `scene_pass_skinned_upload`. For CPU-side capture under Tracy / `PerformanceProfiler`, the registry-level `SystemMetrics` table catches the time spent inside each system's `update` — animation work currently rolls up under `Scene` (component-tree update) rather than a dedicated system, which is part of why §15-Q4 calls for a wrapper system.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap. `std::vector` for pose buffers, channels, weights; `std::shared_ptr` for skeletons + clips (shared across instances); `std::unordered_map` in state machine + sprite animation. No arena, no per-frame transient allocator. |
| Per-instance peak | A single `SkeletonAnimator` with 64 joints: ~16 KB pose buffers (T/R/S × prev + current + global + bone-matrices). Doubled during crossfade. Morph weights bounded by `Mesh::MAX_MORPH_TARGETS = 8` × 4 bytes. |
| Shared peak | `Skeleton` ~10 KB (128 joints × ~80 bytes); `AnimationClip` size dominated by keyframe data — typical humanoid clip ~50 KB–1 MB depending on duration and channel density. Morph data scales with vertex count × target count: a 5k-vert head with 8 targets ≈ 480 KB of position deltas alone. |
| Ownership | `Skeleton` + `AnimationClip` + `MorphTargetData` owned by `Model` in `engine/resource`; `SkeletonAnimator` holds `shared_ptr` references. `Tween` / `TweenManager` / `SpriteAnimation` own their internal state outright. |
| Lifetimes | Asset-resident: scene-load lifetime (until model unloaded). Per-instance: entity lifetime. Reusable pose buffers retain capacity across frames once `setSkeleton` has resized them — no re-alloc on the hot path. |

`std::vector::resize` happens at `setSkeleton` time and at the lazy growth in WEIGHTS-channel sampling; both are init-path allocations. `clone()` allocates fresh per-instance buffers via `initializeBuffers()` (one-time cost).

## 10. Error handling

Per CODING_STANDARDS §11. No exceptions in steady-state.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| `SkeletonAnimator::update` with no skeleton / no active clip | Silent no-op; `m_rootMotionDelta*` cleared | Caller must `setSkeleton` + `play(...)` first. |
| `play(name)` with unknown clip name | Silent (loop falls through; `m_activeClipIndex` unchanged) | Caller checks `getActiveClipIndex()` to confirm; an optional `bool play(...)` return is on the open list (Q2). |
| `playIndex(out-of-range)` / `getClip(out-of-range)` | Silent (early return) / null `shared_ptr` | Caller validates against `getClipCount()`. |
| Joint index out of skeleton range in a channel | Channel skipped during sampling | Asset-level error; `gltf_loader` should have caught it. |
| Zero-duration clip | Early-out in `advanceAndSample`; pose buffers unchanged | Caller treats as "still in bind pose". |
| Empty channel timestamps / values | `sampleVec3` returns zero, `sampleQuat` returns identity | Authoring error; surfaces as "T-pose for that bone". |
| Asset version drift (glTF schema bumps) | Caught at `gltf_loader` (out of scope here) | Loader returns failure; engine substitutes a placeholder model. |
| Mismatched bone count between clip's joint indices and the bound skeleton | Channels with `jointIndex >= jointCount` skipped | Author retargets the clip; runtime keeps running with bind pose for affected bones. |
| `Skeleton::buildUpdateOrder` finds an orphan / cycle | Orphan appended at tail in storage order; debug `assert` on parent-precedes-child invariant | Author fixes the skeleton; release build muddles through. |
| `IK::solveTwoBoneIK` target unreachable | `result.reached = false`; arm extends fully toward target | Caller checks `reached`; foot IK exposes `pelvisOffset` as the standard "lower the body to compensate" path. |
| `Tween` advanced after `stop()` | No-op (`m_finished` flag short-circuits) | None needed. |
| `TweenManager::add` causing internal vector reallocation | Returned reference dangles | Caller chains config calls in one expression; documented in §4 contract. |
| `SpriteAnimation::play` on a clip with zero frames | Silent no-op | Authoring error. |
| OOM during pose-buffer resize | `std::bad_alloc` propagates | App aborts (CODING_STANDARDS §11). |

`Result<T,E>` / `std::expected` not yet used here — the subsystem predates the codebase-wide policy. Migration tracked as Open Q6.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Sampler interpolation (STEP / LINEAR / CUBICSPLINE; quaternion double-cover audit A2) | `tests/test_animation_sampler.cpp` | Public API contract + audit regression |
| Generic curve evaluation | `tests/test_animation_curve.cpp` | Public API contract |
| State machine transitions, triggers, exit-time | `tests/test_animation_state_machine.cpp` | Public API contract |
| Skeleton joint hierarchy + DFS update order (audit A1) | `tests/test_skeleton.cpp` | Public API contract + audit regression |
| `SkeletonAnimator` playback, crossfade, bone-matrix correctness, shuffled-vs-sorted equivalence | `tests/test_skeleton_animator.cpp` | Public API contract + audit regression |
| Root motion delta extraction | `tests/test_root_motion.cpp` | Public API contract |
| IK solvers (two-bone reach + pole vector A4, look-at clamp, foot IK + pelvis offset) | `tests/test_ik_solver.cpp` | Public API contract + audit regression |
| Morph target CPU blend (positions + normals) | `tests/test_morph_target.cpp` | Public API contract |
| Morph target GPU pipeline parity (CPU spec ↔ GPU vert shader) | `tests/test_morph_target_pipeline.cpp` | Cross-subsystem CPU/GPU parity |
| Easing curves (Penner + cubic bezier Newton-Raphson) | `tests/test_easing.cpp` | Public API contract |
| `Tween` + `TweenManager` (factory, easing, events, loop / ping-pong, cancel-by-target) | `tests/test_tween.cpp` | Public API contract |
| `SpriteAnimation` (forward / reverse / ping-pong, frame timing, `advancedLastTick`) | `tests/test_sprite_animation.cpp` | Public API contract |
| Motion-vector overlay's `prevWorldMatrix` for skinned objects | `tests/test_motion_overlay_prev_world.cpp` | Cross-subsystem regression |

**Adding a test for `engine/animation`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use the public types directly without an `Engine` instance — every primitive in this subsystem is unit-testable headlessly. Hand-built skeletons must call `buildUpdateOrder()` after populating joints. Deterministic seeding for randomness via fixed seeds inline (no engine-wide random helper currently consumed by these tests).

**Coverage gap:** the CPU side is well-covered; the GPU side (the vertex-shader skinning + morph blend) is exercised via the visual-test runner (`engine/testing/visual_test_runner.h`) and the morph-target pipeline integration test, not headless unit tests. There is no headless unit test that verifies `getBoneMatrices()` upload survives the SSBO round-trip — that surfaces only at visual smoke time.

## 12. Accessibility

`engine/animation` produces no user-facing pixels or sound directly, but it gates several photosensitive / reduced-motion concerns downstream:

- **Reduced motion (`Settings::accessibility.reducedMotion`)** — when enabled, downstream consumers should reduce or disable camera-shake, screen-flash, and exaggerated tween-driven UI motion. The setting flows through `engine/core` apply-sinks; `engine/animation` itself reads no settings, but `Tween` / `TweenManager` are the most common animation-of-UI primitive and any UI built atop them must respect the flag (consume via the renderer accessibility sink, gate the `mgr.add(t)` call). `SkeletonAnimator` playback speed is rebindable via `setSpeed` if a future "slow down characters" toggle is added.
- **Photosensitive caps (`PhotosensitiveSafetyWire`)** — flash and strobe limits live in `engine/accessibility/photosensitive_safety.h`. `Tween` event callbacks driving full-screen flashes must run through the photosensitive store, not write the screen directly.
- **Captions / subtitles** — facial / lip-sync animation is the natural pair for spoken dialogue; the cluster lives in `engine/experimental/animation/` (W12, see §15) and is not currently wired. When it activates, captions must be the primary information channel and lip-sync the supplement (per memory `user_accessibility`).

`engine/animation` itself has no input, no UI rendering, and no audio.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/scene/component.h` | engine subsystem | `SkeletonAnimator` and `TweenManager` are `Component` subclasses. |
| `<glm/glm.hpp>`, `<glm/gtc/quaternion.hpp>` | external | Math primitives — `vec3`, `mat4`, `quat`, `slerp`, `mix`. |
| `<cmath>`, `<algorithm>`, `<vector>`, `<unordered_map>`, `<memory>`, `<string>`, `<functional>`, `<cstdint>` | std | Standard library — public-header includes (the `<cstring>` / `<cassert>` uses are `.cpp`-only and not part of the public API surface). |

**Direction:** `engine/animation` is depended on by `engine/scene` (component update tree), `engine/renderer/mesh` (morph data + bone matrices), `engine/utils/gltf_loader` (asset construction), `engine/resource/model` (asset bundle), `engine/editor/panels/model_viewer_panel` (preview UI), and the `engine/experimental/animation/*` cluster (which `#include`s production headers — the *forbidden* direction is `engine/animation` → `engine/experimental/animation`, not the other way around). `engine/animation` does **not** depend on `engine/renderer`, `engine/audio`, `engine/physics`, or `engine/core` — keeping the include graph one-way is what makes the subsystem unit-testable headlessly without an `Engine` instance.

## 14. References

External / current (≤ 1 year old where possible):

- Daniel Holden. *Inertialization* (2024–2025, ongoing series) — canonical write-up of inertialization-as-blend-replacement; pole-vector / forward-kinematics conventions used in the IK audit. <https://theorangeduck.com/page/inertialization>
- Saeed Ghorbani. *Interactive Inverse Kinematics: CCD, FABRIK, and Jacobian Transpose* (2025). Comparison of analytical vs. iterative chain solvers; informs the choice to ship analytic two-bone first. <https://saeed1262.github.io/blog/2025/inverse-kinematics-models/>
- Andreas Aristidou et al. *FABRIK: A fast, iterative solver for the Inverse Kinematics problem.* — reference for any future N-bone chain support. <https://andreasaristidou.com/FABRIK>
- Epic Games. *Game Animation Sample Project — UE 5.7 updates* (2025). Current best-practice motion-matching + chooser pattern; informs the experimental cluster's reactivation criteria. <https://www.unrealengine.com/tech-blog/explore-the-updates-to-the-game-animation-sample-project-in-ue-5-7>
- Khronos Group. *glTF 2.0 specification — Animation and Skinning* (DeepWiki summary, 2025). Authoritative interpolation modes (STEP / LINEAR / CUBICSPLINE), JOINTS_n / WEIGHTS_n attributes, inverse-bind matrices. <https://deepwiki.com/facebook/glTF/2.5-animation-and-skinning>
- KhronosGroup. *glTF Tutorials — Simple Skin* (current). Used in cross-checking `loadSkin` semantics. <https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_019_SimpleSkin.md>
- Guillaume Blanc. *ozz-animation — open-source C++ skeletal animation library and toolset* (2025). Reference architecture for runtime sampling + blending; the engine deliberately ships a smaller surface than ozz but the conceptual split (skeleton ↔ clip ↔ sampler ↔ animator) matches.  <https://guillaumeblanc.github.io/ozz-animation/>
- David Bizzocchi (Bungie). *Inertialization: High-Performance Animation Transitions in Gears of War.* GDC. — origin of the technique; the relocated `engine/experimental/animation/inertialization.{h,cpp}` is a port. <https://www.youtube.com/watch?v=BYyv4KTegJI>
- Daniel Holden. *Dead Blending* (2025) — natural successor / alternative framing for blending; informs §15 thinking about the next blend implementation. <https://theorangeduck.com/page/dead-blending>
- Robert Penner. *Easing Equations* (canonical, embedded into `easing.cpp`).
- W3C / CSS Working Group. *cubic-bezier(x1, y1, x2, y2)* — the four-control-point form `CubicBezierEase` matches.

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §17 (CPU/GPU), §18 (public API), §27 (units).
- `ARCHITECTURE.md` (subsystem map; component update flow).
- `ROADMAP.md` Phase 7 (foundation), Phase 10.9 Slice 6 (A1–A5 audit fixes), Phase 10.9 Slice 8 W12 (zombie relocation).
- `docs/phases/phase_07a_design.md`, `phase_07b_design.md`, `phase_07c_design.md`, `phase_07d_design.md` — original design.
- `engine/experimental/animation/README.md` — the relocated cluster's status doc.

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Multi-track blend tree / additive layer support — current animator does single-clip + single crossfade. Layered IK on animation, additive run-on-top-of-walk, and partial-body masks are unimplemented. | `milnet01` | post-MIT release (Phase 12) |
| 2 | `SkeletonAnimator::play(name)` returns void — caller can't tell whether the name resolved. Either return `bool`, log a warning on miss, or both. | `milnet01` | Phase 11 entry |
| 3 | Worker-thread sampling — pose evaluation across animators is embarrassingly parallel; no current `update` is dispatched off-main-thread. Decide whether to job-system fan-out or document main-thread-only normatively. | `milnet01` | Phase 11 entry (decision) / post-MIT (impl) |
| 4 | No dedicated `AnimationSystem` (`ISystem`) — the per-frame work rolls up under `Scene`'s component update. A wrapper would surface metrics in the `SystemRegistry` table and pair naturally with `CharacterSystem`'s "owns animation" comment in `engine/systems/character_system.h:21`. | `milnet01` | Phase 11 entry |
| 5 | §8 performance numbers are placeholders. Need a Tracy capture across the standard demo + a populated-NPC scene. | `milnet01` | Phase 11 audit (concrete: end of Phase 10.9 cycle) |
| 6 | No `Result<T, E>` / `std::expected` adoption — silent no-ops on bad inputs predate the codebase-wide policy. Migration on the broader engine debt list. | `milnet01` | post-MIT release (Phase 12) |
| 7 | `engine/experimental/animation/` cluster (motion-matching, lip-sync, facial, eye, inertialization) — relocated in W12 but not yet activated. Future activation requires building real consumers (character controller, phoneme pipeline, rigged head); the README at the relocated path documents the recipe. **Not a `engine/animation` open question per se** — listed here so the spec doesn't read as if those features are in this directory. | `milnet01` | triage (depends on biblical-game phase scoping) |
| 8 | `SkeletonAnimator` exposes pose-buffer member access (`m_clips`, `m_localTranslations`) as public for the loader's convenience. Migrating the loader to a friend or a builder would re-encapsulate. | `milnet01` | post-MIT release (Phase 12) |
| 9 | Root motion `APPLY_TO_TRANSFORM` mode — the delta is computed and exposed, but no built-in path applies it to the entity. Currently the caller is responsible; documenting that explicitly OR shipping a default applier is open. | `milnet01` | Phase 11 entry |
| 10 | No CCD or FABRIK chain solver — analytic two-bone is sufficient for arms / legs. A general N-bone solver (tails, spines, finger chains) is out of scope for v0.x; FABRIK is the recommended starting point per §14 references. | `milnet01` | post-MIT release (Phase 12) |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/animation` foundation since Phase 7, post Phase 10.9 Slice 6 audit fixes (A1 / A2 / A4 / A5) and Slice 8 W12 zombie relocation. |
