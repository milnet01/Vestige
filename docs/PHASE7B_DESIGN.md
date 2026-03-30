# Phase 7B Design: Animation Blending, State Machine, Root Motion, CUBICSPLINE

**Date:** 2026-03-30
**Prerequisite:** Phase 7A (skeletal animation) — complete
**Research:** docs/PHASE7B_RESEARCH.md

---

## Table of Contents

1. [Scope](#1-scope)
2. [CUBICSPLINE Interpolation](#2-cubicspline-interpolation)
3. [Crossfade Blending](#3-crossfade-blending)
4. [Animation State Machine](#4-animation-state-machine)
5. [Root Motion](#5-root-motion)
6. [Implementation Steps](#6-implementation-steps)
7. [Files Modified and Created](#7-files-modified-and-created)
8. [Testing Plan](#8-testing-plan)
9. [Deferred Items](#9-deferred-items)

---

## 1. Scope

Phase 7B adds four features to the existing Phase 7A skeletal animation system:

| Feature | New Files | Modified Files |
|---------|-----------|----------------|
| CUBICSPLINE interpolation | — | `animation_sampler.cpp`, `gltf_loader.cpp` |
| Crossfade blending | — | `skeleton_animator.h/.cpp` |
| Animation state machine | `animation_state_machine.h/.cpp` | `skeleton_animator.h/.cpp` |
| Root motion | — | `skeleton_animator.h/.cpp`, `first_person_controller.cpp` |

**Deferred to later phases:** Additive blending, per-bone masking, blend trees, animation events.

---

## 2. CUBICSPLINE Interpolation

### 2.1 Changes to animation_sampler.cpp

Add a `CUBICSPLINE` branch to both `sampleVec3()` and `sampleQuat()`. The existing helper functions (`findKeyframe`, `computeT`) remain unchanged.

New helper functions needed:

```cpp
/// @brief Reads a vec3 from CUBICSPLINE data (3 elements per keyframe: in-tangent, value, out-tangent).
/// @param tripletIndex Which of the three elements: 0=in-tangent, 1=value, 2=out-tangent
static glm::vec3 readVec3Cubic(const std::vector<float>& values, int keyIndex, int tripletIndex)
{
    // Each keyframe has 3 vec3s (9 floats): [in-tangent(3), value(3), out-tangent(3)]
    size_t base = static_cast<size_t>(keyIndex) * 9 + static_cast<size_t>(tripletIndex) * 3;
    return glm::vec3(values[base], values[base + 1], values[base + 2]);
}

/// @brief Reads a quat from CUBICSPLINE data (3 elements per keyframe: in-tangent, value, out-tangent).
static glm::quat readQuatCubic(const std::vector<float>& values, int keyIndex, int tripletIndex)
{
    // Each keyframe has 3 vec4s (12 floats): [in-tangent(4), value(4), out-tangent(4)]
    size_t base = static_cast<size_t>(keyIndex) * 12 + static_cast<size_t>(tripletIndex) * 4;
    return glm::quat(values[base + 3], values[base], values[base + 1], values[base + 2]);
}
```

Hermite evaluation for vec3:

```cpp
case AnimInterpolation::CUBICSPLINE:
{
    float s = computeT(ts, i, time);
    float s2 = s * s;
    float s3 = s2 * s;
    float td = ts[i + 1] - ts[i];

    glm::vec3 vk  = readVec3Cubic(vals, i, 1);       // value at k
    glm::vec3 bk  = readVec3Cubic(vals, i, 2);       // out-tangent at k
    glm::vec3 vk1 = readVec3Cubic(vals, i + 1, 1);   // value at k+1
    glm::vec3 ak1 = readVec3Cubic(vals, i + 1, 0);   // in-tangent at k+1

    return (2.0f*s3 - 3.0f*s2 + 1.0f) * vk
         + (s3 - 2.0f*s2 + s)          * td * bk
         + (-2.0f*s3 + 3.0f*s2)        * vk1
         + (s3 - s2)                    * td * ak1;
}
```

For quaternions: same formula, but **normalize** the result.

### 2.2 Changes to gltf_loader

The glTF loader already parses the interpolation mode string into `AnimInterpolation`. Need to verify it handles `"CUBICSPLINE"` correctly and reads the 3x-sized output accessor data without truncation. The existing code stores all output floats in `channel.values` — for CUBICSPLINE, this will be 3x as many values as LINEAR, which is correct as long as the loader reads the full accessor.

### 2.3 Boundary Behavior

- Before first keyframe: return `readVec3Cubic(vals, 0, 1)` (the value, not tangent)
- After last keyframe: return `readVec3Cubic(vals, lastKey, 1)`
- Single keyframe: return the value element

---

## 3. Crossfade Blending

### 3.1 Architecture

The `SkeletonAnimator` gains a **second pose buffer** and crossfade state. During a crossfade, both clips are sampled into separate buffers, then blended per-bone into the primary buffer before `computeBoneMatrices()`.

### 3.2 New Members in SkeletonAnimator

```cpp
// Crossfade state
bool m_crossfading = false;
float m_crossfadeTime = 0.0f;      // Current elapsed crossfade time
float m_crossfadeDuration = 0.0f;  // Total crossfade duration (seconds)

// Source clip state (the clip being faded out)
int m_sourceClipIndex = -1;
float m_sourceTime = 0.0f;

// Second pose buffer (for the outgoing clip during crossfade)
std::vector<glm::vec3> m_sourceTranslations;
std::vector<glm::quat> m_sourceRotations;
std::vector<glm::vec3> m_sourceScales;
```

### 3.3 New Public Methods

```cpp
/// @brief Crossfade from the current clip to a new clip.
/// @param clipName Name of the target clip.
/// @param duration Crossfade duration in seconds (0 = instant switch).
void crossfadeTo(const std::string& clipName, float duration);

/// @brief Crossfade by clip index.
void crossfadeToIndex(int index, float duration);

/// @brief Returns true if currently crossfading between two clips.
bool isCrossfading() const;
```

### 3.4 Updated update() Logic

```cpp
void SkeletonAnimator::update(float deltaTime)
{
    if (!m_playing || m_paused || !m_skeleton) return;

    if (m_crossfading)
    {
        // Advance crossfade timer
        m_crossfadeTime += deltaTime;
        float blendFactor = glm::clamp(m_crossfadeTime / m_crossfadeDuration, 0.0f, 1.0f);

        // Sample source (outgoing) clip into source buffers
        advanceAndSample(m_sourceClipIndex, m_sourceTime, deltaTime,
                         m_sourceTranslations, m_sourceRotations, m_sourceScales);

        // Sample target (incoming) clip into primary buffers
        advanceAndSample(m_activeClipIndex, m_currentTime, deltaTime,
                         m_localTranslations, m_localRotations, m_localScales);

        // Blend: source → target using blendFactor
        for (int j = 0; j < m_skeleton->getJointCount(); ++j)
        {
            size_t idx = static_cast<size_t>(j);
            m_localTranslations[idx] = glm::mix(m_sourceTranslations[idx],
                                                  m_localTranslations[idx], blendFactor);
            m_localRotations[idx] = glm::slerp(m_sourceRotations[idx],
                                                m_localRotations[idx], blendFactor);
            m_localScales[idx] = glm::mix(m_sourceScales[idx],
                                           m_localScales[idx], blendFactor);
        }

        // Crossfade complete?
        if (blendFactor >= 1.0f)
        {
            m_crossfading = false;
            m_sourceClipIndex = -1;
        }
    }
    else
    {
        // Single clip playback (existing logic)
        advanceAndSample(m_activeClipIndex, m_currentTime, deltaTime,
                         m_localTranslations, m_localRotations, m_localScales);
    }

    computeBoneMatrices();
}
```

### 3.5 Internal Helper: advanceAndSample()

Extracts the existing time-advancement and channel-sampling logic into a reusable private method:

```cpp
/// @brief Advance a clip's time and sample all channels into the given pose buffers.
void advanceAndSample(int clipIndex, float& time, float deltaTime,
                      std::vector<glm::vec3>& translations,
                      std::vector<glm::quat>& rotations,
                      std::vector<glm::vec3>& scales);
```

This refactoring lets us sample two different clips into two different buffers without duplicating code.

### 3.6 Transition Interruption

If `crossfadeTo()` is called while already crossfading, the current blended pose is "frozen" into the source buffer and a new crossfade begins:

```cpp
void SkeletonAnimator::crossfadeToIndex(int index, float duration)
{
    if (m_crossfading)
    {
        // Snapshot current blended pose as new source
        m_sourceTranslations = m_localTranslations;
        m_sourceRotations = m_localRotations;
        m_sourceScales = m_localScales;
        m_sourceClipIndex = -1;  // "frozen pose" — no clip to advance
    }
    else if (m_activeClipIndex >= 0)
    {
        // Normal: current clip becomes source
        m_sourceTranslations = m_localTranslations;
        m_sourceRotations = m_localRotations;
        m_sourceScales = m_localScales;
        m_sourceClipIndex = m_activeClipIndex;
        m_sourceTime = m_currentTime;
    }

    m_activeClipIndex = index;
    m_currentTime = 0.0f;
    m_crossfading = (duration > 0.0f);
    m_crossfadeTime = 0.0f;
    m_crossfadeDuration = duration;
    m_playing = true;
    m_paused = false;
}
```

---

## 4. Animation State Machine

### 4.1 New Class: AnimationStateMachine

A data-driven FSM that evaluates parameter-based conditions and drives crossfade transitions on a `SkeletonAnimator`.

### 4.2 Data Types

```cpp
/// @brief Comparison operators for transition conditions.
enum class AnimCompareOp
{
    GREATER,      ///< param > threshold
    LESS,         ///< param < threshold
    GREATER_EQ,   ///< param >= threshold
    LESS_EQ,      ///< param <= threshold
    EQUAL,        ///< param == threshold (float: abs diff < epsilon)
    NOT_EQUAL     ///< param != threshold
};

/// @brief A condition that must be satisfied for a transition to fire.
struct AnimTransitionCondition
{
    std::string paramName;
    AnimCompareOp op = AnimCompareOp::GREATER;
    float threshold = 0.0f;
};

/// @brief A state in the animation state machine.
struct AnimState
{
    std::string name;
    int clipIndex = -1;       ///< Index into SkeletonAnimator::m_clips
    float playbackSpeed = 1.0f;
    bool loop = true;
};

/// @brief A transition between two states.
struct AnimTransition
{
    int fromState = -1;       ///< Source state index (-1 = "any state")
    int toState = -1;         ///< Target state index
    float crossfadeDuration = 0.2f;  ///< Seconds
    float exitTime = 0.0f;   ///< Minimum normalized time (0-1) in source before transition can fire
    std::vector<AnimTransitionCondition> conditions;  ///< ALL must be true
};
```

### 4.3 AnimationStateMachine Class

```cpp
class AnimationStateMachine
{
public:
    // --- Configuration (call before use) ---
    int addState(const AnimState& state);
    void addTransition(const AnimTransition& transition);

    // --- Parameters (set from game code each frame) ---
    void setFloat(const std::string& name, float value);
    void setBool(const std::string& name, bool value);
    void setTrigger(const std::string& name);  // One-shot: auto-resets after consumed
    float getFloat(const std::string& name) const;
    bool getBool(const std::string& name) const;

    // --- Runtime ---
    void start(SkeletonAnimator& animator);  // Enter initial state (index 0)
    void update(SkeletonAnimator& animator, float deltaTime);

    // --- Query ---
    int getCurrentStateIndex() const;
    const std::string& getCurrentStateName() const;

private:
    bool evaluateCondition(const AnimTransitionCondition& cond) const;
    bool evaluateTransition(const AnimTransition& transition,
                            const SkeletonAnimator& animator) const;

    std::vector<AnimState> m_states;
    std::vector<AnimTransition> m_transitions;
    std::unordered_map<std::string, float> m_params;  // floats and bools (bool = 0.0/1.0)
    std::unordered_map<std::string, bool> m_triggers;
    int m_currentState = -1;
};
```

### 4.4 Update Logic

```cpp
void AnimationStateMachine::update(SkeletonAnimator& animator, float deltaTime)
{
    if (m_currentState < 0) return;

    // Evaluate transitions (from current state + "any state" transitions)
    for (const auto& transition : m_transitions)
    {
        if (transition.fromState != m_currentState && transition.fromState != -1)
            continue;

        if (!evaluateTransition(transition, animator))
            continue;

        // Transition fires
        m_currentState = transition.toState;
        const auto& targetState = m_states[m_currentState];
        animator.setSpeed(targetState.playbackSpeed);
        animator.setLooping(targetState.loop);
        animator.crossfadeToIndex(targetState.clipIndex, transition.crossfadeDuration);

        // Consume triggers
        for (const auto& cond : transition.conditions)
        {
            auto it = m_triggers.find(cond.paramName);
            if (it != m_triggers.end())
                it->second = false;
        }
        break;  // Only fire one transition per frame
    }
}
```

### 4.5 Exit Time Evaluation

```cpp
bool AnimationStateMachine::evaluateTransition(const AnimTransition& transition,
                                                const SkeletonAnimator& animator) const
{
    // Check exit time
    if (transition.exitTime > 0.0f)
    {
        int activeClip = animator.getActiveClipIndex();
        if (activeClip >= 0)
        {
            float duration = animator.getClip(activeClip)->getDuration();
            float normalized = (duration > 0.0f) ? animator.getCurrentTime() / duration : 1.0f;
            if (normalized < transition.exitTime)
                return false;
        }
    }

    // Check all conditions
    for (const auto& cond : transition.conditions)
    {
        if (!evaluateCondition(cond))
            return false;
    }

    return true;
}
```

### 4.6 Trigger Parameters

Triggers are one-shot booleans that auto-reset after being consumed by a transition. Useful for events like "jump" or "attack" that should fire once and not repeat.

```cpp
void AnimationStateMachine::setTrigger(const std::string& name)
{
    m_triggers[name] = true;
    m_params[name] = 1.0f;  // Also expose as float for condition evaluation
}
```

When a transition consumes a trigger, it resets `m_triggers[name] = false` and `m_params[name] = 0.0f`.

---

## 5. Root Motion

### 5.1 Configuration

```cpp
// New members in SkeletonAnimator:
enum class RootMotionMode { IGNORE, APPLY_TO_TRANSFORM, APPLY_TO_CONTROLLER };

RootMotionMode m_rootMotionMode = RootMotionMode::IGNORE;
int m_rootMotionBone = 0;         // Joint index (default: first joint, usually hips)

// Per-frame root motion delta
glm::vec3 m_rootMotionDeltaPos = glm::vec3(0.0f);
glm::quat m_rootMotionDeltaRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

// Previous frame's root bone transform (for delta computation)
glm::vec3 m_prevRootPos = glm::vec3(0.0f);
glm::quat m_prevRootRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
```

### 5.2 New Public Methods

```cpp
/// @brief Enables root motion extraction from the specified bone.
void setRootMotionMode(RootMotionMode mode);
void setRootMotionBone(int jointIndex);

/// @brief Gets the root motion delta accumulated this frame.
/// The entity system should consume this and apply it to the entity transform.
glm::vec3 getRootMotionDeltaPosition() const;
glm::quat getRootMotionDeltaRotation() const;
```

### 5.3 Extraction in update()

After sampling (and blending if crossfading), before `computeBoneMatrices()`:

```cpp
if (m_rootMotionMode != RootMotionMode::IGNORE)
{
    size_t ri = static_cast<size_t>(m_rootMotionBone);

    // Compute delta from previous frame
    m_rootMotionDeltaPos = m_localTranslations[ri] - m_prevRootPos;
    m_rootMotionDeltaRot = glm::inverse(m_prevRootRot) * m_localRotations[ri];

    // Save current as previous for next frame
    m_prevRootPos = m_localTranslations[ri];
    m_prevRootRot = m_localRotations[ri];

    // Zero out the root bone's horizontal motion (keep vertical for jumps)
    m_localTranslations[ri].x = 0.0f;
    m_localTranslations[ri].z = 0.0f;
    m_localRotations[ri] = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}
```

### 5.4 Loop Boundary

When the animation wraps around (time resets from near-end to near-start), the delta would be incorrect. Detection:

```cpp
// In advanceAndSample, detect loop wrap:
bool looped = false;
if (m_looping && newTime < oldTime)
{
    looped = true;
}
```

When looped, compute two deltas:
1. From previous position to the end-of-clip root position
2. From the start-of-clip root position to the new position
3. Sum them for the total frame delta

### 5.5 Consumption by Entity System

The entity movement system (or `FirstPersonController` for the player) reads the delta each frame:

```cpp
// In entity update or controller:
if (animator->getRootMotionMode() == RootMotionMode::APPLY_TO_TRANSFORM)
{
    entity.position += animator->getRootMotionDeltaPosition();
    entity.rotation = entity.rotation * animator->getRootMotionDeltaRotation();
}
```

---

## 6. Implementation Steps

### Step 1: CUBICSPLINE Interpolation
1. Add `readVec3Cubic()` and `readQuatCubic()` helper functions to `animation_sampler.cpp`
2. Add CUBICSPLINE branch to `sampleVec3()`
3. Add CUBICSPLINE branch to `sampleQuat()` (with normalize)
4. Verify glTF loader correctly parses CUBICSPLINE output data (3x elements)
5. Write unit tests for CUBICSPLINE sampling

### Step 2: Crossfade Blending
1. Add second pose buffers and crossfade state to `SkeletonAnimator`
2. Extract sampling logic into `advanceAndSample()` private method
3. Implement `crossfadeTo()` / `crossfadeToIndex()` public methods
4. Update `update()` to handle crossfade path
5. Handle transition interruption (freeze current blend as source)
6. Update `clone()` to copy crossfade state
7. Write unit tests for crossfade timing, blend factor progression

### Step 3: Animation State Machine
1. Create `engine/animation/animation_state_machine.h` with data types
2. Create `engine/animation/animation_state_machine.cpp` with update logic
3. Implement parameter system (float, bool, trigger)
4. Implement condition evaluation
5. Implement transition selection (exit time + conditions)
6. Implement trigger consumption
7. Add to `CMakeLists.txt`
8. Write unit tests for state transitions, conditions, triggers

### Step 4: Root Motion
1. Add root motion state to `SkeletonAnimator`
2. Implement delta extraction after sampling
3. Handle loop boundary for smooth wrapping
4. Implement root bone zeroing
5. Add getter methods for delta position/rotation
6. Integrate with entity transform system
7. Write unit tests for delta computation

### Step 5: Integration and Visual Testing
1. Load a glTF model with multiple animation clips (e.g., idle + walk)
2. Configure a state machine with speed-based transitions
3. Verify crossfade is visually smooth
4. Test CUBICSPLINE with a model that uses it
5. Test root motion with a walking character

---

## 7. Files Modified and Created

### New Files
| File | Description |
|------|-------------|
| `engine/animation/animation_state_machine.h` | AnimState, AnimTransition, AnimationStateMachine class |
| `engine/animation/animation_state_machine.cpp` | State machine update, condition evaluation, transition logic |
| `tests/test_cubicspline.cpp` | Unit tests for CUBICSPLINE interpolation |
| `tests/test_crossfade.cpp` | Unit tests for crossfade blending |
| `tests/test_animation_state_machine.cpp` | Unit tests for state machine transitions |
| `tests/test_root_motion.cpp` | Unit tests for root motion delta extraction |

### Modified Files
| File | Changes |
|------|---------|
| `engine/animation/animation_sampler.cpp` | Add CUBICSPLINE branch + helper functions |
| `engine/animation/skeleton_animator.h` | Add crossfade buffers, root motion state, new public methods |
| `engine/animation/skeleton_animator.cpp` | Crossfade logic, root motion extraction, advanceAndSample() refactor |
| `engine/CMakeLists.txt` | Add `animation_state_machine.cpp` |
| `tests/CMakeLists.txt` | Add new test files |

---

## 8. Testing Plan

### Unit Tests

**CUBICSPLINE tests:**
- Known Hermite curve: verify exact values at t=0, t=0.5, t=1
- Boundary: before first keyframe, after last keyframe, single keyframe
- Quaternion: verify result is normalized
- Tangent scaling: verify deltaTime multiplication produces correct curve shape

**Crossfade tests:**
- Blend factor starts at 0, reaches 1 after duration
- At t=0: output equals source pose
- At t=duration: output equals target pose
- Mid-crossfade: output is interpolated
- Transition interruption: frozen pose becomes new source
- Zero-duration crossfade: instant switch

**State machine tests:**
- Initial state is index 0
- Transition fires when conditions met
- Transition blocked when conditions not met
- Exit time prevents premature transition
- Trigger consumed after transition fires
- "Any state" transition fires from any current state
- Multiple conditions: all must be true (AND logic)

**Root motion tests:**
- Delta position is difference between consecutive root samples
- Delta rotation is inverse(prev) * current
- Root bone zeroed after extraction
- Loop wrap produces correct accumulated delta

### Visual Tests
- Load model with idle + walk clips, crossfade between them on keypress
- Verify no popping or jarring transitions
- Test CUBICSPLINE model (many Blender-exported models use it)

---

## 9. Deferred Items

| Feature | Planned Phase | Reason for Deferral |
|---------|---------------|---------------------|
| Additive blending | Phase 7E | Requires reference frame concept; crossfade covers main use case |
| Per-bone masking | Phase 7E | Needs additive blending foundation |
| Blend trees (1D/2D) | Phase 7E | State machine with crossfade covers most needs first |
| Animation events | Phase 7C | Coupled with EventBus integration |
| Synchronized crossfade | Phase 7B+ | Add as option after basic frozen crossfade works |
| Hierarchical states | Future | Flat FSM sufficient for initial needs |
