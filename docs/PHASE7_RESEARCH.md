# Phase 7: Animation System -- Research

Research compiled 2026-03-26 for the Vestige 3D Engine.

---

## Table of Contents

1. [Skeletal Animation System](#1-skeletal-animation-system)
2. [glTF Animation Data Structures](#2-gltf-animation-data-structures)
3. [Keyframe Interpolation](#3-keyframe-interpolation)
4. [Vertex Skinning](#4-vertex-skinning)
5. [Animation Blending](#5-animation-blending)
6. [Animation State Machines](#6-animation-state-machines)
7. [Root Motion](#7-root-motion)
8. [Object and Property Animation](#8-object-and-property-animation)
9. [Inverse Kinematics](#9-inverse-kinematics)
10. [Morph Targets / Blend Shapes](#10-morph-targets--blend-shapes)
11. [Animation Retargeting](#11-animation-retargeting)
12. [Performance Considerations](#12-performance-considerations)
13. [Industry Best Practices](#13-industry-best-practices)
14. [Recommended Phasing for Vestige](#14-recommended-phasing-for-vestige)
15. [Open Source Libraries](#15-open-source-libraries)
16. [Sources](#16-sources)

---

## 1. Skeletal Animation System

### 1.1 Core Concepts

Skeletal animation attaches mesh vertices to a virtual **skeleton** -- a hierarchy of **bones** (also called **joints**). Each vertex is influenced by one or more bones with different weights, so when the skeleton moves, the mesh deforms realistically. The process of defining which bones affect which vertices is called **skinning** (or **rigging** in DCC tools).

The animation pipeline has three distinct phases:
1. **Sampling** -- Read keyframe data and interpolate to the current time to produce a local-space transform (T, R, S) per bone.
2. **Hierarchy evaluation** -- Walk the bone tree root-to-leaf, concatenating local transforms to produce global (model-space) transforms.
3. **Skinning** -- In the vertex shader, blend the global bone matrices (weighted by per-vertex bone weights) to deform each vertex.

### 1.2 How glTF Stores Skeletal Data

glTF 2.0 represents skeletons through two main constructs: **skins** and **node animations**.

#### Skins

A `skin` object contains:

| Property | Type | Description |
|----------|------|-------------|
| `joints` | `int[]` | **Required.** Array of node indices that form the skeleton. |
| `inverseBindMatrices` | `int` | Optional accessor index pointing to an array of `MAT4` values -- one per joint. |
| `skeleton` | `int` | Optional node index of the skeleton root. |

A mesh primitive references a skin. The `joints` array defines the set of nodes that act as bones. The order of joints in this array defines the **joint index** used by the `JOINTS_0` vertex attribute.

#### Inverse Bind Matrices

Each inverse bind matrix transforms a vertex from **model space** into the **local space** of the corresponding joint in its bind (rest) pose. Mathematically, if a joint's global transform in the bind pose is `G_bind`, then:

```
inverseBindMatrix = inverse(G_bind)
```

If `inverseBindMatrices` is not provided, identity matrices are assumed (meaning joints are already defined in bind pose space).

#### Skeleton Hierarchy

Bones are regular glTF nodes arranged in a parent-child hierarchy. Joint 0 is typically the root (hips). The critical ordering property is that **parent joints always appear before their children** in the hierarchy traversal, enabling sequential (non-recursive) computation of global transforms.

**Source:** glTF 2.0 Specification (registry.khronos.org/glTF); Khronos glTF Tutorials -- Skins (github.khronos.org)

### 1.3 Vertex Attributes for Skinning

glTF defines two per-vertex attributes for skinning:

| Attribute | Type | Component Types Allowed | Description |
|-----------|------|------------------------|-------------|
| `JOINTS_0` | `VEC4` | `UNSIGNED_BYTE` (max 256 joints) or `UNSIGNED_SHORT` (max 65536 joints) | Indices into the `skin.joints` array |
| `WEIGHTS_0` | `VEC4` | `FLOAT`, `UNSIGNED_BYTE` (normalized), or `UNSIGNED_SHORT` (normalized) | Blend weights (must sum to 1.0) |

Each vertex can be influenced by up to **4 bones** per attribute set. If more than 4 influences are needed, additional sets (`JOINTS_1`/`WEIGHTS_1`, etc.) can be provided, though 4 is sufficient for the vast majority of game characters.

**Vestige impact:** The current `Vertex` struct has no bone data. We need to add `ivec4 boneIds` and `vec4 boneWeights` fields, and allocate vertex attribute slots for them. The glTF loader must parse `JOINTS_0` and `WEIGHTS_0` accessors.

---

## 2. glTF Animation Data Structures

### 2.1 Animation Object

A glTF `animation` object contains two arrays:

```json
{
  "animations": [{
    "name": "Walk",
    "samplers": [ ... ],
    "channels": [ ... ]
  }]
```

### 2.2 Samplers

Each sampler defines **how** values change over time:

| Property | Description |
|----------|-------------|
| `input` | Accessor index for **timestamps** (float scalars, in seconds) |
| `output` | Accessor index for **values** at each timestamp |
| `interpolation` | `"STEP"`, `"LINEAR"`, or `"CUBICSPLINE"` |

The `input` accessor contains monotonically increasing time values. The `output` accessor contains the animated values (vec3 for translation/scale, vec4 quaternion for rotation, float array for morph weights).

### 2.3 Channels

Each channel connects a sampler to a specific node property:

| Property | Description |
|----------|-------------|
| `sampler` | Index into the animation's `samplers` array |
| `target.node` | Index of the node to animate |
| `target.path` | `"translation"`, `"rotation"`, `"scale"`, or `"weights"` |

A single animation can contain multiple channels targeting different nodes and properties. Multiple animations can target the same node (used for blending).

### 2.4 Multiple Animations Per Model

glTF supports an array of `animations`, each with its own name. This naturally maps to an animation clip library: "Idle", "Walk", "Run", "Attack", etc. A model can have dozens of clips, all stored in the same glTF file with separate timestamp/value accessors.

### 2.5 Data Layout Summary

```
animation
  ├── samplers[]
  │     ├── input  → accessor (float timestamps)
  │     ├── output → accessor (vec3/vec4/float[] values)
  │     └── interpolation ("LINEAR" | "STEP" | "CUBICSPLINE")
  └── channels[]
        ├── sampler → index into samplers[]
        └── target
              ├── node → node index
              └── path → "translation" | "rotation" | "scale" | "weights"
```

**Source:** Khronos glTF Tutorials -- Animations (github.khronos.org); glTF 2.0 Specification (registry.khronos.org)

---

## 3. Keyframe Interpolation

glTF supports three interpolation modes. All three must be implemented for full spec compliance.

### 3.1 STEP Interpolation

The simplest mode. The output value remains constant at the previous keyframe's value until the next keyframe is reached, then jumps instantly. No interpolation is performed.

```cpp
// Pseudocode
if (t < keyframes[i+1].time)
    return keyframes[i].value;
```

Use case: on/off switches, visibility toggles, snapping to discrete states.

### 3.2 LINEAR Interpolation

Standard linear interpolation between adjacent keyframes:

```
factor = (currentTime - prevTime) / (nextTime - prevTime)
```

For **translation and scale** (vec3):
```cpp
result = glm::mix(prevValue, nextValue, factor);
// Equivalent to: prevValue + factor * (nextValue - prevValue)
```

For **rotation** (quaternion), linear interpolation would produce non-unit quaternions and non-constant angular velocity. Instead, use **Spherical Linear Interpolation** (SLERP):
```cpp
result = glm::slerp(prevQuat, nextQuat, factor);
```

SLERP maintains unit length and produces smooth rotation along the shortest arc on the unit sphere. GLM provides `glm::slerp` in `<glm/gtc/quaternion.hpp>`.

### 3.3 CUBICSPLINE Interpolation

Uses Hermite spline interpolation with per-keyframe tangent vectors. The output accessor contains **three** elements per keyframe (in-tangent, value, out-tangent), so it has 3x as many elements as the input (timestamp) accessor.

**Data layout per keyframe:**

```
[in-tangent_k, value_k, out-tangent_k]
```

For N keyframes, the output accessor has 3N elements.

**The Hermite spline formula** between keyframes k and k+1:

```
t_d = t_{k+1} - t_k                      // delta time between keyframes
s   = (currentTime - t_k) / t_d           // normalized [0,1] parameter

p(s) = (2s^3 - 3s^2 + 1) * v_k           // previous value
     + (s^3 - 2s^2 + s)  * t_d * b_k     // previous out-tangent (scaled)
     + (-2s^3 + 3s^2)    * v_{k+1}        // next value
     + (s^3 - s^2)        * t_d * a_{k+1} // next in-tangent (scaled)
```

Where:
- `v_k` = value at keyframe k
- `b_k` = out-tangent at keyframe k (the third element of keyframe k's triplet)
- `a_{k+1}` = in-tangent at keyframe k+1 (the first element of keyframe k+1's triplet)
- `t_d` = time delta between keyframes k and k+1

**Critical detail:** The tangents stored in glTF are **normalized** (independent of keyframe spacing). They must be multiplied by `t_d` (the delta time) when evaluating the spline. This is explicit in the specification.

For **quaternion** cubic spline output, the result must be **normalized** after interpolation to maintain unit length.

**Source:** glTF 2.0 Specification, Appendix C; Cubic Hermite Spline (Wikipedia); Khronos glTF Tutorials -- Animations

### 3.4 Keyframe Lookup Optimization

Naively searching through all keyframes each frame is O(n) per channel. Since animations are sampled sequentially (time only moves forward during playback), **cache the last keyframe index** and search forward from there. This gives O(1) amortized lookup per channel.

```cpp
// Cache lastKeyIndex per channel
while (lastKeyIndex + 1 < keyCount && timestamps[lastKeyIndex + 1] <= currentTime)
{
    lastKeyIndex++;
}
```

When the animation loops, reset `lastKeyIndex` to 0.

---

## 4. Vertex Skinning

### 4.1 The Skinning Equation

The glTF specification defines the joint matrix for joint `j` as:

```
jointMatrix[j] = globalTransform(jointNode[j]) * inverseBindMatrix[j]
```

The skinned vertex position is computed as:

```
skinnedPosition = (w.x * jointMatrix[joints.x]
                 + w.y * jointMatrix[joints.y]
                 + w.z * jointMatrix[joints.z]
                 + w.w * jointMatrix[joints.w]) * vertexPosition
```

Where `joints` is the `JOINTS_0` ivec4 (bone indices) and `w` is the `WEIGHTS_0` vec4 (blend weights) for that vertex.

**Important note from the glTF spec:** "The transform of the node that the mesh/skin is attached to is ignored for skinning purposes." The mesh node's own transform does NOT apply -- skinning uses only the joint matrices.

### 4.2 Global Transform Computation

For each joint, the global transform is computed by walking the hierarchy from root to leaf:

```
globalTransform[root] = localTransform[root]
globalTransform[child] = globalTransform[parent] * localTransform[child]
```

Where `localTransform` is either the animated TRS values (if the joint has an animation channel at the current time) or the node's default TRS values (if no animation targets that joint).

**Bone ordering guarantee:** If joints are stored such that `parent_index < child_index` (which glTF typically provides), global transforms can be computed in a single forward pass without recursion:

```cpp
for (int j = 0; j < jointCount; ++j)
{
    if (parentIndex[j] >= 0)
        globalTransform[j] = globalTransform[parentIndex[j]] * localTransform[j];
    else
        globalTransform[j] = localTransform[j];

    // Final matrix sent to shader:
    finalMatrix[j] = globalTransform[j] * inverseBindMatrix[j];
}
```

### 4.3 GPU Skinning (Vertex Shader Approach)

The standard and recommended approach. Bone matrices are uploaded to the GPU, and the vertex shader computes the skinned position. This is fast because:
- Vertex data stays in VRAM (uploaded once at load time)
- Only the bone matrices change per frame (a few KB)
- The GPU's parallel architecture processes thousands of vertices simultaneously

**Vertex struct extension for Vestige:**

```cpp
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::ivec4 boneIds;      // NEW: up to 4 bone indices (-1 = unused)
    glm::vec4 boneWeights;   // NEW: corresponding weights
};
```

**Vertex attribute setup:**

```cpp
// Bone IDs (integer attribute -- must use glVertexAttribIPointer)
glEnableVertexAttribArray(6);
glVertexAttribIPointer(6, 4, GL_INT, sizeof(Vertex),
    (void*)offsetof(Vertex, boneIds));

// Bone weights (float attribute)
glEnableVertexAttribArray(7);
glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
    (void*)offsetof(Vertex, boneWeights));
```

**Note on `glVertexAttribIPointer`:** Bone IDs are integer indices -- they must use the `I` variant. Using `glVertexAttribPointer` for integers silently converts them to floats, causing incorrect bone lookups. This is a common bug.

**Vertex shader (GLSL 4.50):**

```glsl
#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
// ... other attributes ...
layout(location = 6) in ivec4 aBoneIds;
layout(location = 7) in vec4  aBoneWeights;

const int MAX_BONES = 128;
uniform mat4 uBoneMatrices[MAX_BONES];
uniform bool uHasBones;

void main()
{
    vec4 worldPos;

    if (uHasBones)
    {
        mat4 skinMatrix = mat4(0.0);
        for (int i = 0; i < 4; i++)
        {
            if (aBoneIds[i] >= 0 && aBoneIds[i] < MAX_BONES)
            {
                skinMatrix += aBoneWeights[i] * uBoneMatrices[aBoneIds[i]];
            }
        }
        worldPos = uModel * skinMatrix * vec4(aPosition, 1.0);
    }
    else
    {
        worldPos = uModel * vec4(aPosition, 1.0);
    }

    gl_Position = uProjection * uView * worldPos;
}
```

**Normal transformation:** Normals must also be skinned, using `vec4(aNormal, 0.0)` to exclude translation, then re-normalized:

```glsl
vec3 skinnedNormal = vec3(0.0);
for (int i = 0; i < 4; i++)
{
    if (aBoneIds[i] >= 0)
        skinnedNormal += aBoneWeights[i]
            * mat3(uBoneMatrices[aBoneIds[i]]) * aNormal;
}
skinnedNormal = normalize(skinnedNormal);
```

### 4.4 CPU Skinning (Alternative)

Transforms vertices on the CPU, re-uploading the entire vertex buffer each frame. This is simpler to debug but much slower for large meshes. Only useful for:
- Debug visualization of skinning
- Physics collision mesh updates
- Very small meshes (< 100 vertices)

**Recommendation for Vestige:** Use GPU skinning exclusively. CPU skinning is not worth the complexity of maintaining two code paths.

**Source:** LearnOpenGL -- Skeletal Animation; lisyarus blog -- glTF Animation; OGLDev Tutorial 38

---

## 5. Animation Blending

### 5.1 Crossfade Blending

The most basic form of blending: smoothly transition from one animation to another over a short duration (typically 0.15--0.3 seconds).

```cpp
// During crossfade:
blendFactor += deltaTime / crossfadeDuration;  // 0.0 → 1.0
blendFactor = clamp(blendFactor, 0.0f, 1.0f);

for (each bone)
{
    localTransform[bone] = lerp(poseA[bone], poseB[bone], blendFactor);
    // For rotations: slerp(quatA[bone], quatB[bone], blendFactor);
}
```

When `blendFactor` reaches 1.0, animation A is fully replaced by animation B.

**Key principle:** Blending must happen in **local bone space** (each bone's transform relative to its parent), not world space. Blending in world space produces incorrect results because the hierarchical parent-child relationships are lost.

### 5.2 Additive Blending

Superimposes a delta animation on top of a base animation. Used for secondary motions (breathing, hit reactions, aiming offsets) that should combine with whatever base animation is playing.

The additive pose is computed as the **difference** between the additive clip's current frame and its reference frame (typically frame 0):

```cpp
// Compute additive delta:
additiveDelta[bone].translation = additiveClip[bone].translation
                                - referenceFrame[bone].translation;
additiveDelta[bone].rotation    = inverse(referenceFrame[bone].rotation)
                                * additiveClip[bone].rotation;
additiveDelta[bone].scale       = additiveClip[bone].scale
                                / referenceFrame[bone].scale;

// Apply to base pose:
final[bone].translation = base[bone].translation + weight * additiveDelta[bone].translation;
final[bone].rotation    = base[bone].rotation * slerp(identity, additiveDelta[bone].rotation, weight);
final[bone].scale       = base[bone].scale * lerp(vec3(1), additiveDelta[bone].scale, weight);
```

### 5.3 Layered Blending (Per-Bone Masking)

Applies different animations to different parts of the skeleton. For example, the upper body plays a "shooting" animation while the lower body plays a "running" animation.

Implementation uses a **bone mask** -- a per-bone weight (0.0--1.0) that determines how much of each layer's pose to apply:

```cpp
for (each bone)
{
    float maskWeight = boneMask[bone];  // 0.0 = use base, 1.0 = use overlay
    final[bone] = lerp(basePose[bone], overlayPose[bone], maskWeight);
}
```

The mask typically transitions smoothly across 2--3 bones around the spine to avoid a visible seam between upper and lower body animations.

### 5.4 Blend Operation on TRS Components

When blending two poses A and B with factor `t`:

| Component | Blend Operation |
|-----------|----------------|
| Translation | `lerp(A.t, B.t, t)` = `A.t + t * (B.t - A.t)` |
| Rotation | `slerp(A.r, B.r, t)` -- spherical linear interpolation on quaternions |
| Scale | `lerp(A.s, B.s, t)` = `A.s + t * (B.s - A.s)` |

**Source:** ozz-animation samples (blend, additive, partial blend); C++ Game Animation Programming (O'Reilly); Unreal Engine AnimGraph documentation

---

## 6. Animation State Machines

### 6.1 Finite State Machine (FSM) Approach

The most common pattern. States represent animation clips (or blend trees), and transitions define how to switch between them:

```
[Idle] ---(speed > 0.1)---> [Walk] ---(speed > 3.0)---> [Run]
  ^                            |                            |
  |                            |                            |
  +----(speed < 0.1)-----------+----(speed < 0.1)-----------+
```

Each transition specifies:
- **Condition** -- boolean/threshold expression on parameters (speed, isGrounded, etc.)
- **Crossfade duration** -- how long to blend between the two states
- **Exit time** (optional) -- minimum time before a transition can fire

**Data structure:**

```cpp
struct AnimationState
{
    std::string name;
    int clipIndex;             // or a blend tree
    float playbackSpeed;
    bool loop;
};

struct AnimationTransition
{
    int fromState;
    int toState;
    float crossfadeDuration;
    std::function<bool()> condition;
};

class AnimationStateMachine
{
    std::vector<AnimationState> m_states;
    std::vector<AnimationTransition> m_transitions;
    int m_currentState;
    int m_nextState;          // -1 if not transitioning
    float m_transitionTimer;
};
```

### 6.2 Blend Trees

A blend tree produces a pose by blending multiple clips based on continuous parameters rather than discrete states. Common types:

- **1D Blend:** Interpolate between 2+ clips based on a single parameter (e.g., speed: idle/walk/run).
- **2D Blend:** Interpolate based on two parameters (e.g., direction X/Y: forward/backward/strafe).

Blend trees can be nested inside state machine states, creating a hierarchical system:

```
StateMachine
  ├── State: Locomotion → BlendTree1D(speed: idle, walk, run)
  ├── State: Jump → SingleClip(jump_anim)
  └── State: Falling → SingleClip(fall_anim)
```

### 6.3 Practical Recommendation for Vestige

Start with a simple state machine (Phase 7A). Blend trees can be added later as a state type. The state machine should be data-driven (loaded from a configuration file, not hardcoded), enabling the editor to modify animation behavior without recompilation.

**Source:** Game Programming Patterns -- State (gameprogrammingpatterns.com); Unity AnimationStateMachines; Godot AnimationTree

---

## 7. Root Motion

### 7.1 Concept

Root motion is translation/rotation baked into the root bone of an animation clip. Instead of the game code controlling character movement and the animation just "playing on top," the animation itself drives the character's position. This produces more realistic locomotion because the feet match the movement speed exactly.

### 7.2 Extraction Methods

1. **Dedicated root bone:** A "motion" bone at the hierarchy root contains only the locomotion delta. The animation system reads the root bone's delta transform each frame and applies it to the game object's position/rotation. The root bone's contribution is then zeroed out of the skeleton pose.

2. **Foot contact analysis:** If no dedicated root bone exists, the system can estimate root motion by analyzing foot contact points. More complex and less common.

### 7.3 Application Modes

| Mode | Description |
|------|-------------|
| **Ignore** | Root motion is discarded. Game code controls movement. |
| **Apply to Transform** | Root motion delta is added directly to the entity position. |
| **Send to Character Controller** | Root motion is forwarded to a physics-based character controller for collision-aware movement. |

### 7.4 Implementation

Each frame during animation sampling:

```cpp
// Extract root bone delta this frame
glm::vec3 rootDeltaPos = currentRootPos - previousRootPos;
glm::quat rootDeltaRot = glm::inverse(previousRootRot) * currentRootRot;

// Apply to entity (mode: Apply to Transform)
entity.transform.position += rootDeltaPos;
entity.transform.rotation = entity.transform.rotation * rootDeltaRot;

// Zero out root bone's transform so skeleton pose is centered
localTransform[rootBone].translation = vec3(0);
localTransform[rootBone].rotation = identity;
```

**Source:** Unity Root Motion documentation; Unreal Engine Root Motion; ezEngine Root Motion; ozz-animation motion extraction sample

---

## 8. Object and Property Animation

### 8.1 glTF Node Animations (TRS Channels)

glTF node animations are not limited to skeleton bones. Any node can be animated via translation, rotation, and scale channels. This enables:
- Camera fly-throughs
- Door opening/closing
- Platform movement
- Light color/intensity changes (via extensions)

Vestige already has `ModelNode` with TRS data. Adding animation simply means sampling the appropriate channel at the current time and overriding the node's local transform.

### 8.2 Property Animation / Tweening System

A general-purpose system for interpolating any numeric value over time. Independent of skeletal animation but uses the same mathematical foundations.

**Core design:**

```cpp
struct PropertyTween
{
    float* target;         // pointer to the value being animated
    float startValue;
    float endValue;
    float duration;
    float elapsed;
    EasingFunction easing;
    std::function<void()> onComplete;  // callback when finished
};

class TweenManager
{
    std::vector<PropertyTween> m_activeTweens;

    void update(float deltaTime)
    {
        for (auto& tween : m_activeTweens)
        {
            tween.elapsed += deltaTime;
            float t = clamp(tween.elapsed / tween.duration, 0.0f, 1.0f);
            float easedT = tween.easing(t);
            *tween.target = lerp(tween.startValue, tween.endValue, easedT);
        }
        // Remove completed tweens...
    }
};
```

### 8.3 Easing Functions

Easing functions map a linear parameter `t` (0..1) to a curved parameter, controlling acceleration/deceleration. The standard set (Robert Penner's easing functions) includes:

| Category | Functions |
|----------|-----------|
| Linear | `t` |
| Quadratic | `easeInQuad`, `easeOutQuad`, `easeInOutQuad` |
| Cubic | `easeInCubic`, `easeOutCubic`, `easeInOutCubic` |
| Sine | `easeInSine`, `easeOutSine`, `easeInOutSine` |
| Exponential | `easeInExpo`, `easeOutExpo`, `easeInOutExpo` |
| Elastic | `easeInElastic`, `easeOutElastic`, `easeInOutElastic` |
| Bounce | `easeInBounce`, `easeOutBounce`, `easeInOutBounce` |
| Back | `easeInBack`, `easeOutBack`, `easeInOutBack` |

Example implementations (header-only, pure functions):

```cpp
inline float easeInQuad(float t) { return t * t; }
inline float easeOutQuad(float t) { return t * (2.0f - t); }
inline float easeInOutQuad(float t)
{
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

inline float easeOutElastic(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t - 0.075f) * TWO_PI / 0.3f) + 1.0f;
}
```

### 8.4 Animation Events / Callbacks

Animation events fire user-defined callbacks at specific timestamps within an animation clip. Common uses:
- Play footstep sound at foot-contact frames
- Spawn particles (dust, sparks) at impact frames
- Enable/disable hitbox during attack window
- Trigger screen shake

**Design pattern:**

```cpp
struct AnimationEvent
{
    float time;                        // timestamp within the clip
    std::string eventName;             // e.g., "footstep_left", "attack_start"
    std::unordered_map<std::string, std::string> params;  // optional key-value data
};

// During animation sampling, check if any events fall between previousTime and currentTime
for (const auto& event : clip.events)
{
    if (event.time > previousTime && event.time <= currentTime)
    {
        eventBus.publish(AnimationEventFired{entity, event.eventName, event.params});
    }
}
```

This integrates naturally with Vestige's existing EventBus architecture.

**Source:** Robert Penner's Easing Functions (robertpenner.com); AHEasing C library (github.com/warrenm/AHEasing); Tweeny C++ library (github.com/mobius3/tweeny); Unreal Engine Animation Notifies

---

## 9. Inverse Kinematics

### 9.1 Two-Bone IK (Analytical Solution)

The most common IK in games, used for arms and legs. Given a chain of two bones (e.g., upper arm + forearm), compute joint rotations to reach a target position.

**Mathematical basis: Law of Cosines.**

Given bone lengths `a` (upper) and `b` (lower), and target distance `c` from root to target:

```
// Clamp target distance to reachable range
c = clamp(length(target - root), epsilon, a + b - epsilon);

// Elbow angle via law of cosines:
cos(elbowAngle) = (a^2 + b^2 - c^2) / (2 * a * b)

// Shoulder angle:
cos(shoulderAngle) = (a^2 + c^2 - b^2) / (2 * a * c)
```

**Full 3D implementation** (from Daniel Holden / The Orange Duck):

```cpp
void twoBoneIK(
    vec3 a, vec3 b, vec3 c,    // current joint positions (root, mid, end)
    vec3 target, float eps,     // target position, small epsilon
    quat a_globalRot, quat b_globalRot,  // current global rotations
    quat& a_localRot, quat& b_localRot)  // output: modified local rotations
{
    float lab = length(b - a);   // upper bone length
    float lcb = length(b - c);   // lower bone length
    float lat = clamp(length(target - a), eps, lab + lcb - eps);

    // Current angles
    float ac_ab_0 = acos(clamp(dot(normalize(c - a), normalize(b - a)), -1, 1));
    float ba_bc_0 = acos(clamp(dot(normalize(a - b), normalize(c - b)), -1, 1));
    float ac_at_0 = acos(clamp(dot(normalize(c - a), normalize(target - a)), -1, 1));

    // Desired angles (law of cosines)
    float ac_ab_1 = acos(clamp((lcb*lcb - lab*lab - lat*lat) / (-2*lab*lat), -1, 1));
    float ba_bc_1 = acos(clamp((lat*lat - lab*lab - lcb*lcb) / (-2*lab*lcb), -1, 1));

    // Rotation axes
    vec3 axis0 = normalize(cross(c - a, b - a));   // bend plane normal
    vec3 axis1 = normalize(cross(c - a, target - a));  // swing axis

    // Compute local rotation deltas
    quat r0 = angleAxis(ac_ab_1 - ac_ab_0, inverse(a_globalRot) * axis0);  // extend/contract
    quat r1 = angleAxis(ba_bc_1 - ba_bc_0, inverse(b_globalRot) * axis0);  // elbow bend
    quat r2 = angleAxis(ac_at_0, inverse(a_globalRot) * axis1);            // swing to target

    a_localRot = a_localRot * r0 * r2;
    b_localRot = b_localRot * r1;
}
```

**Pole vector:** When the chain is nearly fully extended, the cross product `cross(c - a, b - a)` becomes numerically unstable. Use a pole vector (e.g., the knee's forward direction) as a fallback axis:

```cpp
vec3 axis0 = normalize(cross(c - a, poleTarget));
```

### 9.2 FABRIK (Forward And Backward Reaching Inverse Kinematics)

FABRIK is a heuristic iterative solver that works on multi-bone chains. Instead of dealing with joint angles, it repositions joint positions directly by locating points on lines. It is significantly faster than Jacobian-based methods and produces natural-looking results.

**Algorithm:**

```
Input: Joint positions p[0..n], target T, tolerance ε
Output: Updated joint positions

1. Store original root position: rootPos = p[0]
2. Repeat until converged or max iterations:

   // BACKWARD pass (end-effector → root):
   a. p[n] = T  (move end effector to target)
   b. For i = n-1 down to 0:
      - d = distance(p[i+1], p[i])
      - boneLen = original distance between joints i and i+1
      - λ = boneLen / d
      - p[i] = lerp(p[i+1], p[i], λ)   // place p[i] at boneLen from p[i+1]

   // FORWARD pass (root → end-effector):
   c. p[0] = rootPos  (restore root to original position)
   d. For i = 0 to n-1:
      - d = distance(p[i], p[i+1])
      - boneLen = original distance between joints i and i+1
      - λ = boneLen / d
      - p[i+1] = lerp(p[i], p[i+1], λ)  // place p[i+1] at boneLen from p[i]

3. Check convergence: if distance(p[n], T) < ε, done.
```

**Properties:**
- Converges in 3--10 iterations typically
- O(n) per iteration (n = number of joints)
- No matrix operations -- just point repositioning
- Easy to add joint constraints (angle limits per joint)
- Supports multi-chain hierarchies (e.g., spine → both arms)

### 9.3 CCD (Cyclic Coordinate Descent)

CCD iterates from the end bone toward the root, rotating each joint to align the end effector with the target.

```
For each iteration:
  For j = end_bone to root:
    - Compute vector from joint j to end effector
    - Compute vector from joint j to target
    - Rotate joint j to align these vectors (using cross product for axis, dot for angle)
```

**Pros:** Simple, fast, computationally cheap.
**Cons:** Can produce unnatural poses (over-rotation of joints near the end), may oscillate, and does not converge as smoothly as FABRIK. CCD tends to curl the chain like a tentacle rather than producing natural limb motion.

### 9.4 Foot Placement IK

The most common practical IK application in games. Prevents feet from floating above or sinking below terrain.

**Pipeline:**

1. **Raycast** from each foot bone position downward to find terrain height.
2. **Compute hip offset:** Lower the hip bone by the largest negative foot offset (keeps both feet grounded).
3. **Apply two-bone IK** to each leg, targeting the terrain contact point.
4. **Align foot rotation** to terrain slope normal.

```cpp
void updateFootIK(Entity& entity, Terrain& terrain)
{
    vec3 leftFootPos = getGlobalBonePosition(entity, "LeftFoot");
    vec3 rightFootPos = getGlobalBonePosition(entity, "RightFoot");

    float leftGroundY = terrain.getHeightAt(leftFootPos.x, leftFootPos.z);
    float rightGroundY = terrain.getHeightAt(rightFootPos.x, rightFootPos.z);

    float leftOffset = leftGroundY - leftFootPos.y;
    float rightOffset = rightGroundY - rightFootPos.y;

    // Lower hips by the largest negative offset
    float hipDrop = min(leftOffset, rightOffset);
    entity.skeleton.bones["Hips"].translation.y += hipDrop;

    // IK each leg to ground contact
    twoBoneIK(hipPos, kneePos, leftFootPos, vec3(leftFootPos.x, leftGroundY, leftFootPos.z), ...);
    twoBoneIK(hipPos, kneePos, rightFootPos, vec3(rightFootPos.x, rightGroundY, rightFootPos.z), ...);
}
```

### 9.5 Look-At Constraint

Rotates a bone (typically head or eyes) to face a target point. Implemented as a simple rotation computation:

```cpp
void lookAt(Bone& bone, vec3 boneWorldPos, vec3 targetWorldPos, vec3 upVector, float weight)
{
    vec3 forward = normalize(targetWorldPos - boneWorldPos);
    quat desiredRotation = quatLookAt(forward, upVector);
    bone.globalRotation = slerp(bone.globalRotation, desiredRotation, weight);
}
```

The `weight` parameter (0--1) allows partial look-at, which looks more natural than snapping. Typical values: 0.5--0.8 for head tracking, 1.0 for mechanical turrets.

**Source:** The Orange Duck -- Simple Two Joint IK; FABRIK paper (Aristidou & Lasenby); ozz-animation IK samples; Gamasutra -- Inverse Kinematics for Foot Placement

---

## 10. Morph Targets / Blend Shapes

### 10.1 Concept

Morph targets are an alternative deformation technique to skeletal skinning. Instead of bones, the mesh stores **complete alternate vertex positions** (and optionally normals/tangents) for each expression or shape. The final vertex is computed as:

```
finalPos = basePos + weight[0] * (target0Pos - basePos) + weight[1] * (target1Pos - basePos) + ...
```

Or equivalently (since glTF stores deltas):

```
finalPos = basePos + weight[0] * delta0 + weight[1] * delta1 + ...
```

### 10.2 glTF Morph Target Format

In glTF, morph targets are defined per mesh primitive in the `targets` array. Each target is an object mapping attribute names to accessor indices containing **displacement vectors** (deltas from the base mesh):

```json
{
  "primitives": [{
    "attributes": { "POSITION": 0, "NORMAL": 1 },
    "targets": [
      { "POSITION": 2, "NORMAL": 3 },
      { "POSITION": 4, "NORMAL": 5 }
    ]
  }]
}
```

Weights are stored either on the mesh or animated via the `"weights"` channel path. The glTF spec allows up to 8 morph targets to be active simultaneously, though implementations may support more.

### 10.3 Vertex Shader Implementation

Morph target deltas are uploaded as additional vertex attributes or via a buffer/texture:

```glsl
// Simple approach with extra attributes (limited to a few targets):
layout(location = 8) in vec3 aMorphDelta0;
layout(location = 9) in vec3 aMorphDelta1;

uniform float uMorphWeights[MAX_MORPH_TARGETS];

vec3 morphedPos = aPosition
    + uMorphWeights[0] * aMorphDelta0
    + uMorphWeights[1] * aMorphDelta1;
```

For models with many morph targets, a **texture buffer** or **SSBO** is preferable since vertex attribute slots are limited (typically 16 in OpenGL).

### 10.4 Use Cases

- **Facial expressions** (smile, frown, blink, phonemes for lip-sync)
- **Corrective shapes** (fix skinning artifacts at extreme joint angles)
- **Body variation** (muscular, thin, etc.)
- **Damage states** (dents, cracks)

### 10.5 Priority for Vestige

Morph targets are lower priority than skeletal animation for an architectural walkthrough engine. Implement skeletal animation first. Morph targets can be added later if character facial animation is needed.

**Source:** Anton's OpenGL Tutorials -- Blend Shapes; glTF 2.0 Specification -- Morph Targets; Khronos glTF Sample Renderer -- Skinning and Morphing

---

## 11. Animation Retargeting

### 11.1 Concept

Animation retargeting transfers an animation created for one skeleton to a different skeleton with different proportions or structure. This allows sharing animation libraries across multiple character models.

### 11.2 Techniques

**Bone Mapping:** Define correspondence between source and target skeleton bones. Bones with matching names or roles are mapped automatically; others require manual assignment.

**Translation Retargeting Modes:**

| Mode | Description |
|------|-------------|
| Animation | Use translation directly from animation data (source proportions). |
| Skeleton | Use target skeleton's bind pose translation (preserves target proportions). |
| AnimationScaled | Scale animation translation by ratio of target/source bone lengths. |

**Rotation Transfer:** Rotations transfer directly since they are relative to parent bones. This is why local-space rotations are the standard storage format.

### 11.3 Offline vs Runtime

**Offline retargeting:** Creates new animation clips for the target skeleton at import time. Simple, no runtime cost, but produces duplicate data.

**Runtime retargeting:** Computes retargeted poses each frame. No duplicate data, but adds per-frame cost. Wicked Engine implements this approach.

### 11.4 Priority for Vestige

Retargeting is an advanced feature. Not needed until Vestige has multiple character models sharing animations. Defer to a later phase.

**Source:** Unreal Engine Animation Retargeting documentation; Wicked Engine Animation Retargeting blog post; Godot Retargeting 3D Skeletons documentation

---

## 12. Performance Considerations

### 12.1 Bone Matrix Upload Strategies

The bone matrices (one `mat4` per joint) must be sent to the GPU each frame. Several approaches exist:

| Method | Size Limit | Performance | Notes |
|--------|-----------|-------------|-------|
| **Uniform array** (`uniform mat4[]`) | ~64--256 bones (depends on `GL_MAX_VERTEX_UNIFORM_COMPONENTS`) | Fast for small counts | Simplest. OpenGL guarantees >= 1024 components (= 64 mat4). Modern GPUs report 4096 (= 256 mat4). |
| **UBO** (Uniform Buffer Object) | ~256 bones (16 KB typical, 64 KB max guaranteed) | Fast | Uses `std140` layout. 256 mat4 = 16 KB. Good default choice. |
| **SSBO** (Shader Storage Buffer Object) | Essentially unlimited (128 MB guaranteed) | Fast on modern hardware | Uses `std430` layout (tighter packing). Supports read/write. Best for crowd rendering with shared bone buffers. |
| **Texture Buffer** (`samplerBuffer`) | Very large | Fast random access | Data accessed via `texelFetch`. Slightly more complex API. |

**Recommendation for Vestige:** Start with a **uniform array** of `mat4[128]`. This handles all realistic character skeletons (most game characters use 30--70 bones, complex ones up to ~100). If crowd rendering is needed later, switch to SSBO.

**Vestige's target GPU (RX 6600, RDNA2)** reports `GL_MAX_VERTEX_UNIFORM_COMPONENTS` >= 4096, allowing up to 256 mat4 in a uniform array. This is more than sufficient.

### 12.2 mat4x3 Optimization

Since bone transforms are affine (the bottom row is always `0 0 0 1`), storing and uploading `mat4x3` instead of `mat4` saves 25% bandwidth:

```glsl
uniform mat4x3 uBoneMatrices[128];  // 48 bytes each vs 64

vec4 skinnedPos = vec4(uBoneMatrices[boneId] * vec4(pos, 1.0), 1.0);
```

This reduces upload size from 8 KB to 6 KB for 128 bones. Worth doing but not critical at Vestige's scale.

### 12.3 Dual Quaternion Skinning vs Linear Blend Skinning

**Linear Blend Skinning (LBS)** is the standard approach described in Section 4. It blends transformation matrices, which can cause **volume loss** (mesh collapse) at joints, especially shoulders with extreme rotations. This is called the "candy wrapper" artifact.

**Dual Quaternion Skinning (DQS)** blends dual quaternions instead of matrices. A dual quaternion (8 floats) represents a rigid transformation (rotation + translation). Blending dual quaternions and normalizing the result guarantees the output is also a rigid transformation, eliminating volume loss.

**DQS formula:**

```
dq_blended = normalize(w0 * dq0 + w1 * dq1 + w2 * dq2 + w3 * dq3)
```

**Critical implementation detail:** Before blending, check that all quaternion real parts have the same sign (shortest-path test):

```glsl
if (dot(dq[i].real, dq[0].real) < 0.0)
    dq[i] = -dq[i];  // flip to ensure shortest path
```

**Tradeoffs:**

| Aspect | LBS | DQS |
|--------|-----|-----|
| Speed | Baseline | ~Same (negligible overhead) |
| Volume preservation | Poor at extreme angles | Excellent |
| Artifacts | Candy wrapper at shoulders/wrists | Can bulge at knees/elbows |
| Implementation complexity | Simple | Moderate (dual quaternion math) |
| Industry adoption | Universal standard | Second standard, used in Unreal/Unity |

**Recommendation for Vestige:** Implement LBS first (it is the glTF standard). Add DQS as an optional enhancement later. Most architectural models will not have extreme joint bending.

### 12.4 Compute Shader Skinning

Instead of skinning in the vertex shader, a compute shader can pre-skin all vertices into an output buffer. The regular vertex shader then reads pre-skinned vertices with no bone math.

**Advantages:**
- Skinning happens **once per mesh per frame**, not once per render pass. A mesh rendered in shadow pass, Z-prepass, and main pass would be skinned 3x in the vertex shader approach but only 1x with compute.
- Can generate velocity vectors for motion blur by loading previous frame's positions.
- Thread groups can share bone matrices via shared memory (LDS), reducing VRAM reads.

**Disadvantages:**
- Requires an additional output vertex buffer (2x VRAM for skinned meshes).
- More complex pipeline setup.
- For single-pass rendering (Vestige's current forward renderer), the vertex shader approach is simpler and equally fast.

**Recommendation for Vestige:** Use vertex shader skinning. Compute shader skinning is only worthwhile with multi-pass rendering (deferred + shadow + reflection passes) and large numbers of animated characters.

### 12.5 Animation LOD

Reduce animation cost for distant characters:

| LOD Level | Distance | Technique |
|-----------|----------|-----------|
| LOD 0 | < 20m | Full skeleton, full sample rate |
| LOD 1 | 20--50m | Full skeleton, half sample rate (sample every 2 frames, interpolate) |
| LOD 2 | 50--100m | Reduced skeleton (remove leaf bones like fingers), quarter sample rate |
| LOD 3 | > 100m | No animation update (freeze at last pose), or simple procedural sway |

**Leaf bone removal:** The only safe optimization for skeleton LODs is removing **leaf bones** (bones at chain ends with no children), such as individual finger bones, toe bones, or facial bones. These can be removed by "baking" their bind pose into the parent bone's inverse bind matrix.

### 12.6 SIMD and Data-Oriented Optimization

For evaluating hundreds of animated characters, a data-oriented layout (Structure of Arrays) enables SIMD processing:

```cpp
// Array of Structures (typical):
struct Bone { vec3 translation; quat rotation; vec3 scale; };
Bone bones[128];

// Structure of Arrays (SIMD-friendly):
struct BonesSoA
{
    float tx[128], ty[128], tz[128];      // translations
    float qx[128], qy[128], qz[128], qw[128];  // rotations
    float sx[128], sy[128], sz[128];      // scales
};
```

ozz-animation uses SoA layout with SSE/NEON intrinsics for maximum throughput. For Vestige's initial implementation, AoS is simpler and sufficient for a handful of characters. SoA optimization should be considered if performance becomes an issue with many animated characters.

**Source:** lisyarus blog -- glTF Animation (buffer strategy comparison); Wicked Engine -- Compute Shader Skinning; OpenGL Wiki -- SSBO; GameDev.net -- Skeletal Animation Optimization; Kavan et al. -- Skinning with Dual Quaternions (2007)

---

## 13. Industry Best Practices

### 13.1 How Major Engines Handle Animation

**Unity:**
- Mecanim system: visual state machine editor ("Animator Controller")
- AnimationClip stores keyframe data, Animator component drives playback
- Avatar system for humanoid retargeting (auto-maps bones by name/position)
- GPU skinning by default, CPU fallback available
- Maximum 4 bones per vertex
- Root motion support built into Animator

**Unreal Engine:**
- AnimGraph: node-based visual programming for animation logic
- Animation Blueprint: per-character animation state machine
- Blend Spaces: 1D/2D blend trees
- Animation Notifies: event system for sound, particles, etc.
- Control Rig: procedural animation and IK
- Typically uses CPU skinning for physics interaction, GPU for rendering
- Root motion with multiple application modes

**Godot:**
- AnimationPlayer node: timeline-based, can animate any property
- AnimationTree: state machine + blend tree hybrid
- Skeleton3D node: bone hierarchy
- AnimationMixer for blending
- Currently less mature than Unity/Unreal for skeletal animation

### 13.2 Common Pitfalls

1. **Coordinate space confusion.** Mixing up local bone space, model space, and world space is the #1 source of bugs. Every matrix operation must be clear about which space it transforms from and to. Label your variables: `localTransform`, `globalTransform`, `bindPoseInverse`, `finalMatrix`.

2. **Blending in world space.** Animation blending must happen in **local bone space** (relative to parent). Blending global transforms produces incorrect hierarchical motion -- e.g., an additive torso bend would not affect the arms.

3. **Forgetting to normalize quaternions.** After SLERP or any quaternion arithmetic, always normalize. Accumulated floating-point error can cause non-unit quaternions, leading to scaling artifacts.

4. **Ignoring the mesh node transform.** The glTF spec explicitly states the mesh node's transform is ignored for skinned meshes. Applying it produces doubled transformations.

5. **Using `glVertexAttribPointer` for bone IDs.** Bone IDs are integers. You must use `glVertexAttribIPointer` (the `I` variant). The non-I variant silently converts to float, causing incorrect bone lookups.

6. **Not clamping `acos` inputs.** Floating-point imprecision can produce values slightly outside [-1, 1], causing `acos` to return NaN. Always clamp: `acos(clamp(x, -1.0f, 1.0f))`.

7. **Linear keyframe search.** Scanning all keyframes each frame is O(n). Cache the last keyframe index per channel -- animation time only moves forward during normal playback.

8. **Uploading all bone matrices even for static meshes.** Use a `uHasBones` uniform or shader variant to skip bone math for non-animated meshes. Alternatively, use separate shader programs.

9. **Root transform contamination.** If root motion extraction is not handled, the character may drift away from its entity position as the animation plays.

10. **Mesa AMD driver note (Vestige-specific):** All declared GLSL samplers must have valid textures bound at draw time, even if unused in the current code path. This also applies to bone matrix arrays if using sampler-based approaches.

### 13.3 Testing Strategy

- **Unit tests:** Interpolation functions (LERP, SLERP, cubic spline), bone hierarchy traversal, skinning matrix computation.
- **Visual tests:** Load glTF models with known animations (Khronos sample models: `RiggedSimple`, `RiggedFigure`, `CesiumMan`, `Fox`, `BrainStem`). Compare against reference renderers (Khronos glTF Sample Viewer, Blender).
- **Performance tests:** Measure frame time with 1, 10, 50 animated characters. Target: 60 FPS with 10+ animated characters.

**Source:** Unity Animation documentation; Unreal Engine Animation documentation; Godot AnimationTree documentation; vlad.website -- Game Engine Skeletal Animation

---

## 14. Recommended Phasing for Vestige

Based on the research above, here is the recommended implementation order. Each sub-phase builds on the previous one and produces a testable, visually verifiable result.

### Phase 7A: Core Skeletal Animation (Foundation)

**Goal:** Load and play a single glTF animation on a skinned mesh.

1. Extend `Vertex` struct with `boneIds` (ivec4) and `boneWeights` (vec4).
2. Extend `Mesh::upload()` to set up bone vertex attributes (locations 6, 7).
3. Extend `GltfLoader` to parse `skin` objects (joints array, inverse bind matrices).
4. Extend `GltfLoader` to parse `JOINTS_0` and `WEIGHTS_0` vertex attributes.
5. Extend `GltfLoader` to parse `animation` objects (samplers, channels).
6. Create `Skeleton` class: stores joint hierarchy, inverse bind matrices, current global transforms.
7. Create `AnimationClip` class: stores sampler data (timestamps, values, interpolation mode).
8. Create `AnimationSampler`: evaluates a clip at a given time (LINEAR + STEP interpolation).
9. Create `Animator` component: drives playback (play, pause, loop, speed), computes final bone matrices.
10. Modify vertex shader: add bone matrix uniform array, compute skinned position/normal.
11. Add `uHasBones` uniform to skip bone math for static meshes.
12. **Visual test:** Load `CesiumMan.glb` or `Fox.glb` from Khronos sample models.

### Phase 7B: Interpolation and Blending

**Goal:** Full glTF interpolation support + crossfade transitions.

1. Implement CUBICSPLINE interpolation (Hermite formula with tangents).
2. Implement crossfade blending between two clips.
3. Create `AnimationStateMachine` class (data-driven states + transitions).
4. Add animation event system (callback at timestamp).
5. **Visual test:** Crossfade between idle and walk animations.

### Phase 7C: Node Animation and Property Tweening

**Goal:** Animate any scene node, not just skeletons.

1. Implement glTF node TRS animation (camera paths, door movement, etc.).
2. Create `TweenManager` with easing functions.
3. Integrate animation events with EventBus.
4. **Visual test:** Animate a door opening with easing.

### Phase 7D: Inverse Kinematics

**Goal:** Procedural animation adjustments.

1. Implement two-bone IK solver (analytical, for arms/legs).
2. Implement look-at constraint (head tracking).
3. Implement foot IK on terrain (raycast + hip adjustment + two-bone IK).
4. **Visual test:** Character standing on uneven terrain with feet properly grounded.

### Phase 7E: Advanced Features (Future)

- Additive blending
- Layered per-bone blending
- Blend trees (1D, 2D)
- FABRIK multi-bone IK
- Morph targets / blend shapes
- Dual quaternion skinning
- Animation retargeting
- Root motion extraction
- Animation LOD
- Compute shader skinning

---

## 15. Open Source Libraries

### 15.1 ozz-animation

**Repository:** https://github.com/guillaumeblanc/ozz-animation
**License:** MIT
**Language:** C++11

The gold standard open-source skeletal animation library. Features:
- Runtime sampling, blending (standard, partial, additive)
- Two-bone IK, look-at IK, foot IK
- Motion extraction
- SIMD-optimized (SSE, NEON) with SoA data layout
- glTF, FBX, Collada import toolchain
- Data-oriented, cache-friendly design
- 21+ interactive samples with source code

**Relevance to Vestige:** Excellent reference implementation. However, ozz-animation uses its own binary format and requires its offline toolchain. Integrating it would mean either using ozz as a runtime dependency (adds ~200KB) or studying its architecture and implementing key algorithms natively. The latter approach is recommended for learning purposes and to maintain Vestige's minimal dependency philosophy.

### 15.2 Khronos glTF Sample Models

**Repository:** https://github.com/KhronosGroup/glTF-Sample-Models
**License:** Various (mostly CC-BY 4.0)

Essential test assets:
- `RiggedSimple` -- Minimal skinned mesh (2 bones)
- `RiggedFigure` -- Simple humanoid skeleton
- `CesiumMan` -- Walking humanoid (good first test)
- `Fox` -- Quadruped with multiple animations
- `BrainStem` -- Complex skeleton with many bones
- `AnimatedMorphCube` -- Morph target test
- `AnimatedMorphSphere` -- Morph target with multiple weights

### 15.3 tinygltf (Already Used by Vestige)

Vestige already uses tinygltf for glTF loading. tinygltf parses all animation and skin data into its data structures (`tinygltf::Animation`, `tinygltf::Skin`, etc.). The animation implementation only needs to read from these parsed structures -- no additional parsing library is needed.

### 15.4 Other Notable References

- **LearnOpenGL Skeletal Animation tutorial** -- Complete OpenGL+Assimp implementation walkthrough.
- **lisyarus blog -- glTF Animation** -- Detailed analysis of glTF skinning with buffer upload strategy comparison.
- **"Hands-On C++ Game Animation Programming"** (Gabor Szauer, Packt) -- Book covering the full animation pipeline from scratch in C++.
- **"C++ Game Animation Programming, 2nd Edition"** (Michael Dunsky & Gabor Szauer, Packt) -- Updated edition with blend tree and state machine coverage.

---

## 16. Sources

### Specifications and Official Documentation
- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) -- Khronos Group
- [glTF Tutorials -- Skins](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_020_Skins.html) -- Khronos Group
- [glTF Tutorials -- Animations](https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_007_Animations.md) -- Khronos Group
- [OpenGL Wiki -- Skeletal Animation](https://www.khronos.org/opengl/wiki/Skeletal_Animation) -- Khronos Group
- [OpenGL Wiki -- Shader Storage Buffer Object](https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object) -- Khronos Group

### Tutorials and Implementation Guides
- [LearnOpenGL -- Skeletal Animation](https://learnopengl.com/Guest-Articles/2020/Skeletal-Animation) -- Joey de Vries
- [OGLDev Tutorial 38 -- Skeletal Animation with Assimp](https://ogldev.org/www/tutorial38/tutorial38.html) -- Etay Meiri
- [Skeletal Animation in glTF](https://lisyarus.github.io/blog/posts/gltf-animation.html) -- lisyarus blog
- [Game Engine: How I Implemented Skeletal Animation](https://vlad.website/game-engine-skeletal-animation/) -- Vlad
- [GPU Skinning of MD5 Models in OpenGL](https://www.3dgep.com/gpu-skinning-of-md5-models-in-opengl-and-cg/) -- 3D Game Engine Programming
- [Skeletal Animation and GPU Skinning](https://yadiyasheng.medium.com/skeletal-animation-and-gpu-skinning-c99b30eb2ca2) -- Yadi (Medium)
- [Skinned Mesh Animations in the Vertex Shader](https://amengol.github.io/animation_programming/skinned-mesh-animations-in-the-vertex-shader/) -- amengol
- [Advanced OpenGL Animation Technique](https://www.freecodecamp.org/news/advanced-opengl-animation-technique-skeletal-animations/) -- freeCodeCamp
- [glTF Animation and Transform Composition](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Advanced_Topics/GLTF_Animation.html) -- Vulkan Documentation Project
- [Animating Blend Shapes](https://antongerdelan.net/opengl/blend_shapes.html) -- Anton's OpenGL Tutorials

### Animation Blending and State Machines
- [ozz-animation Blend Sample](https://guillaumeblanc.github.io/ozz-animation/samples/blend/) -- Guillaume Blanc
- [ozz-animation Additive Blending](https://guillaumeblanc.github.io/ozz-animation/samples/additive/) -- Guillaume Blanc
- [Animation Blending Techniques](https://oboe.com/learn/advanced-game-animation-techniques-r4t7pv/animation-blending-techniques-mpkhx5) -- Advanced Game Animation Techniques
- [State -- Game Programming Patterns](https://gameprogrammingpatterns.com/state.html) -- Robert Nystrom
- [Unity -- Animation State Machines](https://docs.unity3d.com/Manual/AnimationStateMachines.html) -- Unity Technologies
- [Godot AnimationTree State Machine](https://godotengine.org/article/godot-gets-new-animation-tree-state-machine/) -- Godot Engine

### Inverse Kinematics
- [Simple Two Joint IK](https://theorangeduck.com/page/simple-two-joint) -- Daniel Holden (The Orange Duck)
- [FABRIK: A Fast, Iterative Solver for Inverse Kinematics](https://andreasaristidou.com/FABRIK) -- Andreas Aristidou
- [ozz-animation Two-Bone IK](https://guillaumeblanc.github.io/ozz-animation/samples/two_bone_ik/) -- Guillaume Blanc
- [ozz-animation Foot IK](https://github.com/guillaumeblanc/ozz-animation/blob/master/samples/foot_ik/sample_foot_ik.cc) -- Guillaume Blanc
- [CCD Inverse Kinematics](https://rodolphe-vaillant.fr/entry/114/cyclic-coordonate-descent-inverse-kynematic-ccd-ik) -- Rodolphe Vaillant
- [Inverse Kinematics for Foot Placement](https://www.gamedeveloper.com/programming/inverse-kinematics-two-joints-for-foot-placement) -- Gamasutra
- [Foot IK and Terrain Adaptation](https://palospublishing.com/foot-ik-and-terrain-adaptation/) -- Palos Publishing

### Dual Quaternion Skinning
- [Skinning with Dual Quaternions](https://users.cs.utah.edu/~ladislav/kavan07skinning/kavan07skinning.html) -- Kavan et al. (2007)
- [Dual Quaternion Skinning Tutorial and C++ Code](http://rodolphe-vaillant.fr/?e=29) -- Rodolphe Vaillant

### Root Motion and Retargeting
- [Unity Root Motion](https://docs.unity3d.com/6000.3/Documentation/Manual/RootMotion.html) -- Unity Technologies
- [Unreal Engine Root Motion](https://dev.epicgames.com/documentation/en-us/unreal-engine/root-motion-in-unreal-engine) -- Epic Games
- [ezEngine Root Motion](https://ezengine.net/pages/docs/animation/skeletal-animation/root-motion.html) -- ezEngine
- [ozz-animation Motion Extraction](https://guillaumeblanc.github.io/ozz-animation/samples/motion_extraction/) -- Guillaume Blanc
- [Animation Retargeting -- Wicked Engine](https://wickedengine.net/2022/09/animation-retargeting/) -- Turanszki Janos
- [Animation Retargeting in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-retargeting-in-unreal-engine) -- Epic Games

### Performance and Optimization
- [Skinning in a Compute Shader](https://wickedengine.net/2017/09/skinning-in-compute-shader/comment-page-1/) -- Wicked Engine
- [Skeletal Animation Optimization Tips and Tricks](https://www.gamedev.net/tutorials/programming/graphics/skeletal-animation-optimization-tips-and-tricks-r3988/) -- GameDev.net
- [Uniform Buffers vs Texture Buffers: The 2015 Edition](https://www.yosoygames.com.ar/wp/2015/01/uniform-buffers-vs-texture-buffers-the-2015-edition/) -- Yosoygames
- [Simplygon Bone Reduction](https://simplygon.com/features/bonereduction) -- Simplygon

### Animation Events and Tweening
- [Animation Notifies in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-notifies-in-unreal-engine) -- Epic Games
- [Unity -- Add an Animation Event](https://docs.unity3d.com/Manual/script-AnimationWindowEvent.html) -- Unity Technologies
- [Robert Penner's Easing Functions](https://robertpenner.com/easing/) -- Robert Penner
- [Tweeny: A Modern C++ Tweening Library](https://github.com/mobius3/tweeny) -- mobius3
- [AHEasing: Easing Functions for C/C++](https://github.com/warrenm/AHEasing) -- Warren Moore
- [Bezier Curve as Easing Function in C++](https://asawicki.info/news_1790_bezier_curve_as_easing_function_in_c) -- Adam Sawicki

### Libraries
- [ozz-animation](https://guillaumeblanc.github.io/ozz-animation/) -- Guillaume Blanc (MIT License)
- [glTF Sample Models](https://github.com/KhronosGroup/glTF-Sample-Models) -- Khronos Group

### Books
- "Hands-On C++ Game Animation Programming" -- Gabor Szauer (Packt, 2020)
- "C++ Game Animation Programming, 2nd Edition" -- Michael Dunsky & Gabor Szauer (Packt, 2023)
