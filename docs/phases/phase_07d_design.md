# Phase 7D Design: Inverse Kinematics

## File Structure

| File | Purpose |
|------|---------|
| `engine/animation/ik_solver.h` | IK solver classes (TwoBoneIK, LookAtIK) |
| `engine/animation/ik_solver.cpp` | Solver implementations |
| `tests/test_ik_solver.cpp` | Unit tests |

## 1. Two-Bone IK Solver

```cpp
struct TwoBoneIKRequest
{
    // Joint positions in model space
    glm::vec3 startPos;     // e.g. shoulder/hip
    glm::vec3 midPos;       // e.g. elbow/knee
    glm::vec3 endPos;       // e.g. hand/foot (effector)

    // Joint rotations in model space
    glm::quat startGlobalRot;
    glm::quat midGlobalRot;

    // Current local rotations (will be corrected)
    glm::quat startLocalRot;
    glm::quat midLocalRot;

    // Target position in model space
    glm::vec3 target;

    // Pole vector: direction the mid joint should point toward
    glm::vec3 poleVector = glm::vec3(0, 0, 1);

    // Weight [0,1]: 0 = no IK, 1 = full IK
    float weight = 1.0f;
};

struct TwoBoneIKResult
{
    glm::quat startLocalRot;  // Corrected local rotation for start joint
    glm::quat midLocalRot;    // Corrected local rotation for mid joint
    bool reached = false;     // True if target is within chain reach
};

TwoBoneIKResult solveTwoBoneIK(const TwoBoneIKRequest& req);
```

## 2. Look-At IK Solver

```cpp
struct LookAtIKRequest
{
    glm::vec3 jointPos;         // Joint position in model space
    glm::quat jointGlobalRot;   // Joint rotation in model space
    glm::quat jointLocalRot;    // Current local rotation

    glm::vec3 target;           // Look-at target in model space
    glm::vec3 forwardAxis = glm::vec3(0, 0, -1); // Joint's forward in local space

    float maxAngle = glm::radians(90.0f);  // Maximum turn angle
    float weight = 1.0f;
};

struct LookAtIKResult
{
    glm::quat localRot;  // Corrected local rotation
};

LookAtIKResult solveLookAtIK(const LookAtIKRequest& req);
```

## 3. Foot IK Helper

```cpp
struct FootIKRequest
{
    // Leg chain joint indices
    int hipJoint;
    int kneeJoint;
    int ankleJoint;

    // Ground hit data (from external raycast)
    glm::vec3 groundPosition;    // Where the foot should be
    glm::vec3 groundNormal;      // Surface normal for foot alignment

    // Pole vector for knee direction
    glm::vec3 kneeForward = glm::vec3(0, 0, 1);

    float weight = 1.0f;
};

struct FootIKResult
{
    glm::quat hipLocalRot;
    glm::quat kneeLocalRot;
    glm::quat ankleLocalRot;  // For ground alignment
    float pelvisOffset;        // How much to lower the pelvis
};
```

Foot IK is a composition: two-bone IK for the leg chain + look-at for ankle alignment. The raycast itself is external (renderer/physics provides the ground hit).

## 4. IK Blending

All solvers accept a `weight` parameter:
- `weight = 0`: returns input rotations unchanged
- `weight = 1`: returns fully IK-solved rotations
- `0 < weight < 1`: NLerp between input and solved rotations

This allows smooth IK activation/deactivation and layered application.

## 5. Testing Plan (~15 tests)

### Two-Bone IK
- Straight-line chain reaches target at various positions
- Unreachable target: chain extends toward target, `reached = false`
- Target at start position: chain folds, no crash
- Pole vector controls mid-joint direction
- Weight = 0 returns original rotations unchanged
- Weight = 0.5 produces intermediate rotation

### Look-At IK
- Joint rotates to face target
- Max angle constraint limits rotation
- Target behind joint: clamped to max angle
- Weight = 0 returns original rotation

### Foot IK
- Ankle reaches ground position
- Pelvis offset computed correctly
- Ground normal aligns ankle
