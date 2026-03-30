# Phase 7B Research: Animation Blending, State Machine, Root Motion, CUBICSPLINE

**Date:** 2026-03-30
**Scope:** Phase 7B adds crossfade blending, a data-driven animation state machine, root motion extraction, and CUBICSPLINE interpolation on top of the Phase 7A skeletal animation system.

---

## Table of Contents

1. [Crossfade Blending](#1-crossfade-blending)
2. [Additive Blending](#2-additive-blending)
3. [Layered Per-Bone Blending](#3-layered-per-bone-blending)
4. [Animation State Machine](#4-animation-state-machine)
5. [Root Motion](#5-root-motion)
6. [CUBICSPLINE Interpolation](#6-cubicspline-interpolation)
7. [Performance Considerations](#7-performance-considerations)
8. [Recommended Implementation Order](#8-recommended-implementation-order)
9. [Sources](#9-sources)

---

## 1. Crossfade Blending

### 1.1 Concept

Crossfade blending smoothly transitions between two animation clips over a short duration (typically 0.15--0.3 seconds). During the crossfade, both clips are sampled independently and the resulting per-bone local-space transforms are blended using a weight that ramps from 0.0 to 1.0.

**Critical rule:** Blending must happen in **local bone space** (each bone's transform relative to its parent), not in world/model space. Blending global transforms breaks hierarchical relationships -- e.g., a blended arm position would not follow the blended shoulder position correctly.

### 1.2 Per-Bone Blend Operation

For two poses A and B with blend factor `t` (0.0 = fully A, 1.0 = fully B):

| Component   | Blend Operation |
|-------------|-----------------|
| Translation | `lerp(A.t, B.t, t)` |
| Rotation    | `slerp(A.r, B.r, t)` — spherical linear interpolation on quaternions |
| Scale       | `lerp(A.s, B.s, t)` |

The blend factor advances linearly each frame:
```
blendFactor += deltaTime / crossfadeDuration;
blendFactor = clamp(blendFactor, 0.0, 1.0);
```

When `blendFactor` reaches 1.0, the crossfade is complete and the source clip is discarded.

### 1.3 Synchronized vs. Frozen Crossfade

Two approaches to time management during a crossfade:

- **Frozen (simple):** The outgoing clip continues from its current time; the incoming clip starts from time 0. Both advance at their own speed. Good enough for most transitions.
- **Synchronized:** Both clips use **normalized time** (0.0–1.0 based on clip duration) so they stay in phase. Essential for locomotion cycles (walk → run) to keep feet synchronized and avoid foot sliding.

**Recommendation for Vestige Phase 7B:** Start with frozen crossfade. Add synchronized mode as an option on transitions that need it (locomotion blending).

### 1.4 Practical Architecture (ozz-animation model)

ozz-animation's `BlendingJob` provides a proven architecture:

1. **Input:** An array of `Layer` objects, each containing:
   - A **weight** (0.0–1.0) controlling influence
   - A **local-space pose** (arrays of translation, rotation, scale per joint) from the sampling stage
2. **Fallback:** The skeleton's **rest/bind pose** is used when accumulated weights fall below a threshold (avoids zero-weight degenerate output).
3. **Output:** A single blended local-space pose.

This separates sampling (reading keyframes) from blending (combining poses), keeping each stage simple and testable.

**Source:** [ozz-animation blending sample](https://guillaumeblanc.github.io/ozz-animation/samples/blend/); [ozz-animation BlendingJob header](https://github.com/guillaumeblanc/ozz-animation/blob/master/include/ozz/animation/runtime/blending_job.h)

---

## 2. Additive Blending

### 2.1 Concept

Additive blending superimposes a **delta animation** on top of a base pose. Unlike override blending (which interpolates between two full poses), additive blending adds differences. This is used for secondary motions: breathing overlays, hit reactions, aiming offsets, head tilts.

### 2.2 Computing the Additive Delta

The additive pose is the **difference** between the clip's current frame and a **reference frame** (typically frame 0 — the "neutral" pose):

```cpp
// Per bone:
delta.translation = clip[bone].translation - reference[bone].translation;
delta.rotation    = inverse(reference[bone].rotation) * clip[bone].rotation;
delta.scale       = clip[bone].scale / reference[bone].scale;
```

### 2.3 Applying to a Base Pose

```cpp
final.translation = base.translation + weight * delta.translation;
final.rotation    = base.rotation * slerp(identity, delta.rotation, weight);
final.scale       = base.scale * lerp(vec3(1), delta.scale, weight);
```

The `weight` parameter (0.0–1.0) controls how strongly the additive layer is applied.

### 2.4 Phase 7B Scope

Additive blending is **deferred** to a later phase. Crossfade (override) blending is the priority for Phase 7B — it covers the most common use case (transitioning between idle/walk/run). Additive can be layered on top later without changing the blending architecture.

**Source:** Phase 7 Research §5.2; [ozz-animation additive sample](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/additive/sample_additive.cc)

---

## 3. Layered Per-Bone Blending

### 3.1 Concept

Different animations applied to different parts of the skeleton. Classic example: upper body plays a "shooting" animation while lower body plays "running."

### 3.2 Bone Mask

A per-bone weight array (0.0–1.0) determines how much of each layer applies:

```cpp
for (each bone) {
    float mask = boneMask[bone];  // 0.0 = use base, 1.0 = use overlay
    final[bone] = lerp(basePose[bone], overlayPose[bone], mask);
}
```

The mask typically transitions smoothly across 2–3 bones near the spine to avoid a visible seam. ozz-animation supports this through optional `joint_weights` buffers per layer.

### 3.3 Phase 7B Scope

Per-bone masking is **deferred** along with additive blending. The blending architecture in Phase 7B will support an array of layers with weights, making it trivial to add bone masks later by multiplying each layer's weight with a per-joint mask.

**Source:** [ozz-animation partial blend sample](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/partial_blend/sample_partial_blend.cc); [PlayCanvas Anim Layer Masks](https://blog.playcanvas.com/anim-layer-masks-and-blending/); [Unreal Engine Layered Blend Per Bone](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-layered-animations-in-unreal-engine)

---

## 4. Animation State Machine

### 4.1 FSM Architecture

The standard game engine approach uses a **finite state machine** (FSM) where:
- **States** represent animation clips (or blend trees in advanced systems)
- **Transitions** define how/when to switch between states
- **Parameters** are named variables (floats, bools, triggers) that drive transition conditions

```
[Idle] ---(speed > 0.1)---> [Walk] ---(speed > 3.0)---> [Run]
  ^                            |                            |
  +----(speed < 0.1)-----------+----(speed < 0.1)-----------+
```

### 4.2 Data Structures

Based on research across Unity, Unreal, Flax, and Game Programming Patterns:

```cpp
struct AnimState {
    std::string name;
    int clipIndex;          // Index into SkeletonAnimator::m_clips
    float playbackSpeed;
    bool loop;
};

struct AnimTransition {
    int fromState;          // -1 = "any state" (wildcard)
    int toState;
    float crossfadeDuration;  // seconds
    float exitTime;            // minimum normalized time before transition can fire (0.0–1.0, 0 = immediate)
    // Conditions: evaluated each frame
    std::vector<TransitionCondition> conditions;
};

struct TransitionCondition {
    std::string paramName;
    enum CompareOp { GREATER, LESS, EQUAL, NOT_EQUAL };
    CompareOp op;
    float threshold;       // For float params
    // For bool params: threshold 1.0 = true, 0.0 = false
};
```

### 4.3 State Machine Update Loop

Each frame:
1. Evaluate transitions from current state (ordered by priority)
2. If a transition's conditions are all met (and exit time is reached), begin crossfade to the target state
3. During crossfade: sample both source and target clips, blend with ramping weight
4. When crossfade completes: discard source state, target becomes current

### 4.4 Transition Interruption

If a new transition fires while a crossfade is already in progress:
- **Simple approach (Phase 7B):** The current blended pose becomes the "source" snapshot, and a new crossfade begins toward the new target. This avoids blending 3+ clips simultaneously.
- **Advanced approach (later):** Allow multi-way crossfades with accumulated weights.

### 4.5 Hierarchical State Machines

Game Programming Patterns describes three extensions beyond basic FSMs:
1. **Hierarchical states** — substates inherit behavior from superstates (via class hierarchy)
2. **Pushdown automata** — state stack allows pushing/popping (good for interruptions like firing)
3. **Concurrent state machines** — separate FSMs for independent concerns (body movement + upper body action)

**Phase 7B scope:** Implement a flat (non-hierarchical) FSM. The architecture should be extensible to hierarchical/concurrent later, but we don't need that complexity yet.

### 4.6 Data-Driven Design

The state machine should be configurable at runtime (not hardcoded). Parameters are set from game code:
```cpp
animator.setParam("speed", 3.5f);
animator.setParam("isGrounded", true);
```

The state machine evaluates these parameters against transition conditions each frame. This enables the editor to modify animation behavior without recompilation.

**Source:** [Game Programming Patterns — State](https://gameprogrammingpatterns.com/state.html); [GameDev.net — Animation State Machine Transitions](https://www.gamedev.net/forums/topic/684913-animation-state-machine-transitions-implementation/5325402/); [Unity Animation State Machine](https://docs.unity3d.com/Manual/AnimationStateMachines.html); [Flax Engine State Machines](https://docs.flaxengine.com/manual/animation/anim-graph/state-machine.html)

---

## 5. Root Motion

### 5.1 Concept

Root motion extracts locomotion data (translation and/or rotation) from the root bone of an animation clip and applies it to the entity's transform. Instead of game code controlling movement and animation "playing on top," the animation drives the entity. This eliminates foot sliding because the visual movement exactly matches the animation.

### 5.2 Root Bone Convention

Skeletons are typically rooted at the **hip/pelvis** bone. For root motion to work, there are two conventions:
1. **Dedicated root bone at origin** — A "motion" bone at ground level below the hips drives locomotion. This is the standard for game assets (Mixamo, UE Mannequin).
2. **Hip bone as root** — The hip bone itself encodes the motion. More common in mocap data and older rigs.

Vestige should support both: the user designates which bone is the "root motion bone" (defaults to joint index 0).

### 5.3 Extraction Algorithm

Each frame during animation update:

```cpp
// Save previous root transform
glm::vec3 prevRootPos = m_localTranslations[rootBoneIndex];
glm::quat prevRootRot = m_localRotations[rootBoneIndex];

// Sample animation (populates m_localTranslations/Rotations)
sampleAllChannels(currentTime);

// Compute delta
glm::vec3 deltaPos = m_localTranslations[rootBoneIndex] - prevRootPos;
glm::quat deltaRot = glm::inverse(prevRootRot) * m_localRotations[rootBoneIndex];

// Zero out root bone's motion so the skeleton stays centered
m_localTranslations[rootBoneIndex] = vec3(0, prevRootPos.y, 0); // Keep Y for vertical motion
m_localRotations[rootBoneIndex] = identity; // Or keep original if only extracting position

// Store delta for the entity system to consume
m_rootMotionDelta = { deltaPos, deltaRot };
```

### 5.4 Application Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| **Ignore** | Root motion discarded. Game code controls movement. | Flying cameras, non-physical movement |
| **Apply to Transform** | Delta added directly to entity position/rotation. | Simple NPCs, cinematic sequences |
| **Apply to Controller** | Delta forwarded to character controller for collision-aware movement. | Player characters, physics-based NPCs |

### 5.5 Loop Boundary Handling

When an animation loops, there's a discontinuity between the last frame's root position and the first frame's root position. This must be handled:
- Compute the delta from the last sampled time to the clip end, then add the delta from clip start to the new current time.
- Unity distributes the start/end pose difference across the animation's 0–100% range to prevent discontinuities ("Loop Pose").

**Phase 7B approach:** Detect the loop wraparound in the time advancement and compute two deltas (end-of-clip + start-of-clip) to produce a smooth total delta.

### 5.6 Root Motion During Blending

When crossfading between two clips that both have root motion:
- Extract root motion delta from both clips independently
- Blend the deltas with the same crossfade weight used for the pose blend

This naturally produces a smooth transition between two movement speeds/directions.

**Source:** [Unity Root Motion](https://docs.unity3d.com/Manual/RootMotion.html); [Unreal Engine Root Motion](https://dev.epicgames.com/documentation/en-us/unreal-engine/root-motion-in-unreal-engine); [ezEngine Root Motion](https://ezengine.net/pages/docs/animation/skeletal-animation/root-motion.html); Phase 7 Research §7

---

## 6. CUBICSPLINE Interpolation

### 6.1 glTF Specification

CUBICSPLINE interpolation uses a **Cubic Hermite spline** with per-keyframe tangent vectors. The output accessor contains **three elements per keyframe** stored as triplets:

```
[in-tangent_k, value_k, out-tangent_k]
```

For N keyframes, the output accessor has 3N elements.

### 6.2 The Hermite Spline Formula

Between keyframes k and k+1:

```
t_d = t_{k+1} - t_k                        // delta time between keyframes
s   = (currentTime - t_k) / t_d             // normalized [0,1] parameter

p(s) = (2s³ - 3s² + 1) * v_k               // previous value
     + (s³ - 2s² + s)   * t_d * b_k         // previous out-tangent (scaled by deltaTime)
     + (-2s³ + 3s²)     * v_{k+1}           // next value
     + (s³ - s²)         * t_d * a_{k+1}     // next in-tangent (scaled by deltaTime)
```

Where:
- `v_k` = value at keyframe k (the middle element of the triplet)
- `b_k` = out-tangent at keyframe k (the third element of keyframe k's triplet)
- `a_{k+1}` = in-tangent at keyframe k+1 (the first element of keyframe k+1's triplet)
- `t_d` = time delta between keyframes k and k+1

### 6.3 Critical Implementation Details

1. **Tangent scaling:** The tangents stored in glTF are **normalized** (velocity-based, independent of keyframe spacing). They MUST be multiplied by `t_d` when evaluating the spline. This is explicit in the specification (Appendix C).

2. **Asymmetric tangents:** glTF supports different in-tangent and out-tangent per keyframe, allowing asymmetric curves. The out-tangent of keyframe k controls the curve leaving k; the in-tangent of keyframe k+1 controls the curve arriving at k+1.

3. **Quaternion normalization:** For rotation channels with CUBICSPLINE, the resulting quaternion MUST be normalized after interpolation to maintain unit length. The Hermite formula does not preserve quaternion unit length.

4. **Data layout for parsing:** For a vec3 channel (translation/scale) with N keyframes, the output accessor has 3N × 3 = 9N floats. For a vec4 channel (rotation) with N keyframes, the output accessor has 3N × 4 = 12N floats.

### 6.4 Implementation in animation_sampler

The existing `sampleVec3()` and `sampleQuat()` functions need a CUBICSPLINE branch alongside the existing STEP and LINEAR branches. The keyframe lookup (`findKeyframe`) and time normalization (`computeT`) remain the same — only the interpolation formula changes.

```cpp
// CUBICSPLINE branch for vec3:
float s = computeT(ts, i, time);
float s2 = s * s;
float s3 = s2 * s;
float td = ts[i+1] - ts[i];  // delta time

// Read triplets: in-tangent, value, out-tangent (3 elements per keyframe)
vec3 vk   = readVec3(vals, i * 3 + 1);       // value at k
vec3 bk   = readVec3(vals, i * 3 + 2);       // out-tangent at k
vec3 vk1  = readVec3(vals, (i+1) * 3 + 1);   // value at k+1
vec3 ak1  = readVec3(vals, (i+1) * 3 + 0);   // in-tangent at k+1

return (2*s3 - 3*s2 + 1) * vk
     + (s3 - 2*s2 + s)   * td * bk
     + (-2*s3 + 3*s2)    * vk1
     + (s3 - s2)          * td * ak1;
```

**Source:** [glTF 2.0 Specification, Appendix C](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html); [Khronos glTF Tutorials — Animations](https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_007_Animations.md); [glTF Issue #2552 — Cubic spline formula clarifications](https://github.com/KhronosGroup/glTF/issues/2552); [glTF Issue #2008 — CUBICSPLINE for rotations](https://github.com/KhronosGroup/glTF/issues/2008)

---

## 7. Performance Considerations

### 7.1 CPU Cost of Blending

Per-bone blending cost:
- 1 quaternion SLERP (rotation) — the most expensive operation per bone
- 2 vector LERPs (translation + scale)
- No matrix multiplications during blending (matrices are computed once from the final blended TRS)

For a crossfade between 2 clips on a 64-bone skeleton: ~64 SLERPs + ~128 LERPs per frame. This is negligible on modern CPUs (< 0.05 ms).

### 7.2 Sampling Cost

Each clip being blended requires full sampling (one `findKeyframe` + interpolation per channel). For a crossfade:
- Source clip: ~192 channels sampled (64 bones × 3 TRS channels)
- Target clip: ~192 channels sampled
- Total: ~384 channel samples per frame

With binary search keyframe lookup: O(log K) per channel where K is keyframes. With cached last-index optimization: O(1) amortized.

### 7.3 State Machine Overhead

Transition condition evaluation is trivially cheap — just float comparisons. The state machine's overhead per frame is negligible compared to sampling and blending.

### 7.4 Root Motion Cost

Root motion extraction adds one vec3 subtraction and one quaternion inverse-multiply per frame — negligible.

### 7.5 CUBICSPLINE Cost

Slightly more expensive than LINEAR (more multiplications in the Hermite formula), but amortized over ~192 channels it's still < 0.1 ms. No concern for the 60 FPS target.

### 7.6 Memory Budget

Additional per-animator memory for blending:
- Second pose buffer for crossfade: 64 joints × (vec3 + quat + vec3) = 64 × 40 bytes = 2.5 KB
- State machine data: < 1 KB per animator (states, transitions, parameters)
- Root motion state: 28 bytes (vec3 + quat)

Total additional: ~4 KB per animated entity — trivial.

**Source:** [GameDev.net — Skeletal Animation Optimization](https://www.gamedev.net/tutorials/programming/graphics/skeletal-animation-optimization-tips-and-tricks-r3988/); [Unreal Engine Animation Performance Tips](https://dev.epicgames.com/community/learning/knowledge-base/xBZp/unreal-engine-performance-tips-tricks-animation)

---

## 8. Recommended Implementation Order

Based on dependencies and incremental testability:

1. **CUBICSPLINE interpolation** — Extend `sampleVec3()` and `sampleQuat()`. Pure addition, no existing code changes. Unit-testable immediately.
2. **Crossfade blending** — Add a second pose buffer and blend logic to `SkeletonAnimator`. Requires extracting the "sample all channels" logic into a reusable function that writes to a specified pose buffer.
3. **Animation state machine** — New class `AnimationStateMachine` that owns states, transitions, and parameters. Drives `SkeletonAnimator` crossfade methods. Data-driven and testable.
4. **Root motion** — Extraction logic in `SkeletonAnimator`, consumed by `FirstPersonController` or entity movement systems. Requires a "root motion bone" designation.

Each step produces a testable, visually verifiable result.

---

## 9. Sources

- [ozz-animation — Open source C++ skeletal animation library](https://guillaumeblanc.github.io/ozz-animation/)
- [ozz-animation BlendingJob header](https://github.com/guillaumeblanc/ozz-animation/blob/master/include/ozz/animation/runtime/blending_job.h)
- [ozz-animation blending sample](https://guillaumeblanc.github.io/ozz-animation/samples/blend/)
- [ozz-animation partial blend sample](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/partial_blend/sample_partial_blend.cc)
- [ozz-animation additive sample](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/additive/sample_additive.cc)
- [Game Programming Patterns — State](https://gameprogrammingpatterns.com/state.html)
- [GameDev.net — Animation State Machine Transitions](https://www.gamedev.net/forums/topic/684913-animation-state-machine-transitions-implementation/5325402/)
- [GameDev.net — Skeletal Animation Optimization](https://www.gamedev.net/tutorials/programming/graphics/skeletal-animation-optimization-tips-and-tricks-r3988/)
- [Unity — Animation State Machine](https://docs.unity3d.com/Manual/AnimationStateMachines.html)
- [Unity — Root Motion](https://docs.unity3d.com/Manual/RootMotion.html)
- [Unreal Engine — Root Motion](https://dev.epicgames.com/documentation/en-us/unreal-engine/root-motion-in-unreal-engine)
- [Unreal Engine — Using Layered Animations](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-layered-animations-in-unreal-engine)
- [Unreal Engine — Animation Performance Tips](https://dev.epicgames.com/community/learning/knowledge-base/xBZp/unreal-engine-performance-tips-tricks-animation)
- [ezEngine — Root Motion](https://ezengine.net/pages/docs/animation/skeletal-animation/root-motion.html)
- [Flax Engine — State Machines](https://docs.flaxengine.com/manual/animation/anim-graph/state-machine.html)
- [PlayCanvas — Anim Layer Masks and Blending](https://blog.playcanvas.com/anim-layer-masks-and-blending/)
- [Bevy — Animation Layered Blend Per Bone](https://github.com/bevyengine/bevy/issues/14395)
- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [Khronos glTF Tutorials — Animations](https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_007_Animations.md)
- [glTF Issue #2552 — Cubic spline formula clarifications](https://github.com/KhronosGroup/glTF/issues/2552)
- [glTF Issue #2008 — CUBICSPLINE for rotations](https://github.com/KhronosGroup/glTF/issues/2008)
- [glTF Issue #1411 — Asymmetric tangents in cubic spline](https://github.com/KhronosGroup/glTF/issues/1411)
- [Animation Blending Techniques — oboe.com](https://oboe.com/learn/advanced-game-animation-techniques-r4t7pv/animation-blending-techniques-advanced-game-animation-techniques-3)
- [LinkedIn — How to implement animation blending](https://www.linkedin.com/advice/3/how-do-you-implement-animation-blending-transitions-game)
- [LearnOpenGL — Skeletal Animation](https://learnopengl.com/Guest-Articles/2020/Skeletal-Animation)
- [vlad.website — Game Engine Skeletal Animation](https://vlad.website/game-engine-skeletal-animation/)
- Phase 7 Research document (docs/PHASE7_RESEARCH.md) §5–§7
