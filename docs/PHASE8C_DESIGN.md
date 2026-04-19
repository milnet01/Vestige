# Phase 8C Design: Constraints and Joints

**Date:** 2026-03-31
**Phase:** 8C
**Status:** Implemented. `engine/physics/physics_constraint.{h,cpp}` wraps Jolt's HINGE / FIXED / DISTANCE / POINT / SLIDER constraints. This document is retained as the original design record.
**Depends on:** Phase 8A (Jolt integration), Phase 8B (character controller)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Research Summary](#2-research-summary)
3. [Architecture](#3-architecture)
4. [Constraint Types](#4-constraint-types)
5. [API Design](#5-api-design)
6. [Interaction System](#6-interaction-system)
7. [Breaking Constraints](#7-breaking-constraints)
8. [Editor Integration](#8-editor-integration)
9. [Demo Scene](#9-demo-scene)
10. [Implementation Steps](#10-implementation-steps)
11. [File Structure](#11-file-structure)
12. [Performance Considerations](#12-performance-considerations)
13. [Testing Strategy](#13-testing-strategy)

---

## 1. Overview

Phase 8C adds constraint/joint support to the Vestige physics system. Constraints connect two rigid bodies (or one body to the world) and restrict their relative motion. This enables interactive architectural elements: doors that swing on hinges, hanging lamps on chains, sliding gates, and welded compound objects.

### Target Use Cases

| Use Case | Constraint Type | Example |
|----------|----------------|---------|
| Doors and gates | Hinge | Tabernacle entrance, Temple doors |
| Hanging lamps/censers | Point + Distance | Menorah, incense censer |
| Sliding panels | Slider | Sliding gates, drawbridges |
| Welded compound objects | Fixed | Multi-piece furniture |
| Chains and ropes | Distance (chained) | Lamp chains, curtain rods |
| Breakable objects | Any + break threshold | Pottery, barricades |

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Constraint wrapper | `PhysicsConstraint` class (not a Component) | Constraints are between bodies, not on entities. PhysicsWorld owns lifecycle. |
| Entity integration | `ConstraintComponent` on one entity, references another | Fits existing ECS pattern. Component configures, PhysicsConstraint executes. |
| World attachment | `Body::sFixedToWorld` sentinel | All Jolt constraints are two-body; this is the standard approach for world pins. |
| Spring behavior | Jolt `SpringSettings` on distance/hinge limits | Built-in, physically correct, configurable frequency/damping. |
| Breaking | Lambda monitoring post-step | Jolt has no built-in break force; impulse checking is the documented approach. |
| Interaction | Raycast + impulse/torque | Simple, no new input abstractions needed. |

---

## 2. Research Summary

### Jolt Constraint System

Jolt provides 13 constraint types, all following a **Settings -> Create -> AddConstraint** pattern:

```cpp
HingeConstraintSettings settings;
settings.mSpace = EConstraintSpace::WorldSpace;
settings.mPoint1 = settings.mPoint2 = hingePos;
settings.mHingeAxis1 = settings.mHingeAxis2 = Vec3::sAxisY();
settings.mNormalAxis1 = settings.mNormalAxis2 = Vec3::sAxisX();

TwoBodyConstraint* constraint = settings.Create(body1, body2);
physicsSystem->AddConstraint(constraint);
```

Key facts:
- Constraints are ref-counted (`JPH::Ref<>`)
- `Body::sFixedToWorld` pins a body to world space (infinite mass sentinel)
- Bodies do NOT track their constraints -- we must remove constraints before destroying bodies
- Constraint properties can be modified at runtime but NOT during `PhysicsSystem::Update()`
- `SpringSettings` controls soft limits (frequency in Hz, damping ratio 0-1+)
- `MotorSettings` wraps spring settings + force/torque limits for driven joints
- Constraint impulses (`GetTotalLambda*()`) enable breaking detection

### Constraints We Will Implement

| Type | DOF Removed | Jolt Class | Use In Engine |
|------|-------------|------------|---------------|
| **Hinge** | 5 | `HingeConstraint` | Doors, gates, lids |
| **Fixed** | 6 (all) | `FixedConstraint` | Welding objects, compound bodies |
| **Distance** | 1 | `DistanceConstraint` | Ropes, chains, spring connections |
| **Point** | 3 (translation) | `PointConstraint` | Ball-and-socket, hanging objects |
| **Slider** | 5 | `SliderConstraint` | Sliding doors, lifts, pistons |

Deferred for later phases: ConeConstraint, SwingTwistConstraint (ragdolls), SixDOFConstraint, GearConstraint, RackAndPinionConstraint, PulleyConstraint, PathConstraint.

### Sources

- [Jolt Physics GitHub](https://github.com/jrouwe/JoltPhysics) -- constraint headers, samples
- [Jolt Architecture.md](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md) -- constraint lifecycle, breaking
- [Jolt API Docs v5.0](https://jrouwe.github.io/JoltPhysicsDocs/5.0.0/index.html) -- class references
- [ezEngine Jolt Constraints](https://ezengine.net/pages/docs/physics/jolt/constraints/jolt-constraints.html) -- practical patterns

---

## 3. Architecture

### 3.1 Class Hierarchy

```
PhysicsWorld (owns constraints)
  ├── addConstraint(settings, bodyA, bodyB) → ConstraintHandle
  ├── removeConstraint(handle)
  ├── getConstraint(handle)
  └── checkBreakableConstraints(deltaTime)

PhysicsConstraint (wraps Jolt constraint)
  ├── Stores JPH::Ref<TwoBodyConstraint>
  ├── Stores constraint type, break threshold
  ├── Exposes type-specific getters/setters
  └── Tracks body IDs for cleanup

ConstraintComponent : Component (on entity)
  ├── Configuration (type, axis, limits, spring, motor)
  ├── References target entity (or world)
  ├── Creates PhysicsConstraint via PhysicsWorld on init
  └── Exposes properties to Inspector panel
```

### 3.2 Ownership and Lifecycle

1. **ConstraintComponent** is created on an entity in the editor or code
2. When the component is activated (both entities have RigidBody components), it calls `PhysicsWorld::addConstraint()`
3. PhysicsWorld creates the Jolt constraint, stores it, returns a handle
4. ConstraintComponent stores the handle for later modification/removal
5. On ConstraintComponent destruction, it calls `PhysicsWorld::removeConstraint(handle)`
6. PhysicsWorld removes the Jolt constraint from the physics system

**Critical rule:** Constraints must be removed before their referenced bodies are destroyed. PhysicsWorld enforces this by removing all constraints referencing a body in `destroyBody()`.

### 3.3 Handle System

Constraints are identified by a lightweight handle (uint32_t generation + index) rather than raw pointers, preventing dangling references:

```cpp
struct ConstraintHandle
{
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;
    bool isValid() const { return index != UINT32_MAX; }
};
```

---

## 4. Constraint Types

### 4.1 Hinge Constraint

Rotation around a single axis. Primary use: doors.

**Parameters:**
- `pivotPoint` (vec3) -- world-space hinge location
- `hingeAxis` (vec3) -- rotation axis (typically Y for vertical doors)
- `normalAxis` (vec3) -- perpendicular reference axis
- `limitsMin` (float) -- minimum angle in degrees (default: -180)
- `limitsMax` (float) -- maximum angle in degrees (default: 180)
- `limitSpring` (SpringSettings) -- soft limits (frequency=0 means hard)
- `motorEnabled` (bool) -- whether motor is active
- `motorMode` (velocity/position) -- motor drive mode
- `motorTarget` (float) -- target velocity (rad/s) or angle (rad)
- `maxFrictionTorque` (float) -- resistance without motor (default: 0)

**Door example (Tabernacle entrance):**
```
pivotPoint: left edge of door frame
hingeAxis: (0, 1, 0)  -- Y-up
limitsMin: -120 degrees (open inward)
limitsMax: 0 degrees (closed position)
maxFrictionTorque: 2.0 Nm (slight resistance)
```

### 4.2 Fixed Constraint

Welds two bodies together. All 6 DOF removed.

**Parameters:**
- `autoDetectPoint` (bool) -- auto-calculate from body positions (default: true)
- `pivotPoint` (vec3) -- explicit world-space attachment point

### 4.3 Distance Constraint

Maintains distance between two attachment points. Can act as a spring.

**Parameters:**
- `attachPointA` (vec3) -- attachment on body A
- `attachPointB` (vec3) -- attachment on body B
- `minDistance` (float) -- minimum allowed distance (-1 = auto from positions)
- `maxDistance` (float) -- maximum allowed distance (-1 = auto from positions)
- `springFrequency` (float) -- Hz, 0 = rigid rod (default: 0)
- `springDamping` (float) -- damping ratio 0-1+ (default: 0)

**Hanging lamp example:**
```
attachPointA: ceiling hook position
attachPointB: top of lamp body
minDistance: 0
maxDistance: 2.0m (chain length)
springFrequency: 1.0 Hz (gentle swing)
springDamping: 0.3 (some damping, allows oscillation)
```

### 4.4 Point Constraint

Ball-and-socket joint. Translation locked, rotation free.

**Parameters:**
- `pivotPoint` (vec3) -- world-space pivot

**Use case:** Hanging objects that can swing in all directions (censers, pendulums).

### 4.5 Slider Constraint

Translation along one axis. Rotation locked.

**Parameters:**
- `pivotPoint` (vec3) -- world-space constraint origin
- `slideAxis` (vec3) -- direction of allowed translation
- `limitsMin` (float) -- minimum slide distance in meters
- `limitsMax` (float) -- maximum slide distance in meters
- `limitSpring` (SpringSettings) -- soft limits
- `motorEnabled` (bool)
- `motorMode` (velocity/position)
- `motorTarget` (float) -- target velocity (m/s) or position (m)
- `maxFrictionForce` (float) -- sliding resistance

---

## 5. API Design

### 5.1 PhysicsConstraint

```cpp
/// Wrapper around a Jolt TwoBodyConstraint.
class PhysicsConstraint
{
public:
    enum class Type : uint8_t
    {
        HINGE,
        FIXED,
        DISTANCE,
        POINT,
        SLIDER
    };

    // Identification
    Type getType() const;
    ConstraintHandle getHandle() const;
    JPH::BodyID getBodyA() const;
    JPH::BodyID getBodyB() const;

    // Enabled state
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Breaking
    void setBreakForce(float force);     // 0 = unbreakable (default)
    float getBreakForce() const;
    float getCurrentForce() const;        // Force applied last step

    // Type-specific access (returns nullptr if wrong type)
    JPH::HingeConstraint* asHinge();
    JPH::FixedConstraint* asFixed();
    JPH::DistanceConstraint* asDistance();
    JPH::PointConstraint* asPoint();
    JPH::SliderConstraint* asSlider();

private:
    JPH::Ref<JPH::TwoBodyConstraint> m_constraint;
    ConstraintHandle m_handle;
    Type m_type;
    JPH::BodyID m_bodyA;
    JPH::BodyID m_bodyB;
    float m_breakForce = 0.0f;

    friend class PhysicsWorld;
};
```

### 5.2 PhysicsWorld Extensions

```cpp
// Add to PhysicsWorld public API:

/// Create a hinge constraint between two bodies (or body + world).
/// Pass JPH::BodyID() for bodyA to attach bodyB to the world.
ConstraintHandle addHingeConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pivotPoint,
    const glm::vec3& hingeAxis,
    const glm::vec3& normalAxis,
    float limitsMinDeg = -180.0f,
    float limitsMaxDeg = 180.0f);

/// Create a fixed constraint welding two bodies together.
ConstraintHandle addFixedConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB);

/// Create a distance constraint between attachment points.
ConstraintHandle addDistanceConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pointA, const glm::vec3& pointB,
    float minDist = -1.0f, float maxDist = -1.0f);

/// Create a point (ball-and-socket) constraint.
ConstraintHandle addPointConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pivotPoint);

/// Create a slider constraint along an axis.
ConstraintHandle addSliderConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pivotPoint,
    const glm::vec3& slideAxis,
    float limitsMin = -FLT_MAX, float limitsMax = FLT_MAX);

/// Access a constraint by handle.
PhysicsConstraint* getConstraint(ConstraintHandle handle);

/// Remove a constraint.
void removeConstraint(ConstraintHandle handle);

/// Remove all constraints referencing a body (called by destroyBody).
void removeConstraintsForBody(JPH::BodyID bodyId);

/// Check breakable constraints and disable those exceeding their threshold.
/// Call after update() each frame.
void checkBreakableConstraints(float deltaTime);
```

### 5.3 ConstraintComponent

```cpp
/// Component that creates and manages a physics constraint on an entity.
/// The owning entity must have a RigidBody. The target can be another entity
/// (with RigidBody) or the world (targetEntity == nullptr).
class ConstraintComponent : public Component
{
public:
    // Configuration (set before activation)
    PhysicsConstraint::Type constraintType = PhysicsConstraint::Type::HINGE;

    // Target
    Entity* targetEntity = nullptr;  // nullptr = attach to world

    // Common
    glm::vec3 pivotPoint = glm::vec3(0.0f);   // World-space
    float breakForce = 0.0f;                    // 0 = unbreakable

    // Hinge-specific
    glm::vec3 hingeAxis = glm::vec3(0, 1, 0);
    glm::vec3 normalAxis = glm::vec3(1, 0, 0);
    float hingeLimitsMin = -180.0f;             // Degrees
    float hingeLimitsMax = 180.0f;
    float hingeFriction = 0.0f;                 // Nm

    // Distance-specific
    glm::vec3 attachPointA = glm::vec3(0.0f);
    glm::vec3 attachPointB = glm::vec3(0.0f);
    float distanceMin = -1.0f;                  // -1 = auto
    float distanceMax = -1.0f;
    float springFrequency = 0.0f;               // Hz, 0 = rigid
    float springDamping = 0.0f;

    // Slider-specific
    glm::vec3 slideAxis = glm::vec3(1, 0, 0);
    float sliderLimitsMin = -1.0f;              // Meters
    float sliderLimitsMax = 1.0f;

    // Motor (hinge or slider)
    bool motorEnabled = false;
    bool motorIsVelocity = true;                // true=velocity, false=position
    float motorTarget = 0.0f;                   // rad/s or rad (hinge), m/s or m (slider)
    float motorMaxForce = FLT_MAX;

    // Component interface
    void activate(PhysicsWorld& world);
    void deactivate(PhysicsWorld& world);
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    // Runtime access
    ConstraintHandle getHandle() const { return m_handle; }
    bool isActive() const { return m_handle.isValid(); }

private:
    ConstraintHandle m_handle;
    PhysicsWorld* m_world = nullptr;
};
```

---

## 6. Interaction System

Players interact with constrained dynamic objects via raycasting.

### 6.1 Raycast from Camera

```
Player looks at door → press E → raycast from camera center
  → hit dynamic body with constraint?
    → apply impulse in camera forward direction
```

### 6.2 Implementation

Add to engine main loop (on E key press):

```cpp
void Engine::handleInteraction()
{
    // Cast ray from camera center, 3m range
    glm::vec3 origin = m_camera->getPosition();
    glm::vec3 dir = m_camera->getForward();

    JPH::RayCast ray(toJolt(origin), toJolt(dir * 3.0f));
    JPH::RayCastResult result;

    if (m_physicsWorld.rayCast(ray, result))
    {
        JPH::BodyID hitBody = result.mBodyID;

        // Only interact with dynamic bodies
        if (m_physicsWorld.getBodyMotionType(hitBody) == JPH::EMotionType::Dynamic)
        {
            // Apply impulse at hit point
            glm::vec3 hitPoint = origin + dir * result.mFraction * 3.0f;
            glm::vec3 impulse = dir * INTERACTION_FORCE;
            m_physicsWorld.applyImpulseAtPoint(hitBody, impulse, hitPoint);
        }
    }
}
```

### 6.3 PhysicsWorld Raycast Addition

```cpp
/// Cast a ray and return the closest hit.
bool rayCast(const glm::vec3& origin, const glm::vec3& direction,
             float maxDistance, RayCastResult& outResult);

/// Apply an impulse at a specific world-space point on a body.
void applyImpulseAtPoint(JPH::BodyID bodyId,
                          const glm::vec3& impulse,
                          const glm::vec3& worldPoint);

/// Get motion type of a body (static/dynamic/kinematic).
JPH::EMotionType getBodyMotionType(JPH::BodyID bodyId) const;
```

---

## 7. Breaking Constraints

### 7.1 Lambda Monitoring

After each physics step, check constraint impulses:

```cpp
void PhysicsWorld::checkBreakableConstraints(float deltaTime)
{
    for (auto& [handle, constraint] : m_constraints)
    {
        if (constraint.getBreakForce() <= 0.0f) continue;  // Unbreakable
        if (!constraint.isEnabled()) continue;

        float impulse = 0.0f;
        switch (constraint.getType())
        {
            case PhysicsConstraint::Type::HINGE:
                impulse = toGlm(constraint.asHinge()->GetTotalLambdaPosition()).length();
                break;
            case PhysicsConstraint::Type::FIXED:
                impulse = toGlm(constraint.asFixed()->GetTotalLambdaPosition()).length();
                break;
            case PhysicsConstraint::Type::DISTANCE:
                impulse = std::abs(constraint.asDistance()->GetTotalLambdaPosition());
                break;
            // ... other types
        }

        float force = impulse / deltaTime;
        constraint.setCurrentForce(force);

        if (force > constraint.getBreakForce())
        {
            constraint.setEnabled(false);  // Disable, don't remove (cheaper)
        }
    }
}
```

### 7.2 Break Events

When a constraint breaks, emit an event on the EventBus:

```cpp
struct ConstraintBrokenEvent
{
    ConstraintHandle handle;
    float breakForce;
    float actualForce;
};
```

This allows gameplay code to react (play sound, spawn particles, etc.).

---

## 8. Editor Integration

### 8.1 Inspector Panel

Add constraint properties to the inspector when a ConstraintComponent is selected:

- **Type dropdown:** Hinge, Fixed, Distance, Point, Slider
- **Target entity:** Entity picker (or "World" option)
- **Pivot point:** Vec3 editor with gizmo
- **Type-specific parameters:** Shown/hidden based on selected type
  - Hinge: axis, limits, friction sliders
  - Distance: min/max, spring frequency/damping
  - Slider: axis, limits
- **Motor section:** Enable toggle, velocity/position mode, target, max force
- **Break force:** 0 = unbreakable, slider for threshold
- **Debug draw:** Toggle to visualize constraint in scene

### 8.2 Debug Visualization

Extend `PhysicsDebugDraw` to render constraints:
- **Hinge:** Arc showing angle limits, line along hinge axis
- **Distance:** Line between attachment points (green = in range, red = at limit)
- **Point:** Cross at pivot point
- **Slider:** Line along slide axis with limit markers
- **Fixed:** X symbol at attachment point
- **Broken:** Red dashed line

---

## 9. Demo Scene

After implementation, add interactive objects to the current scene to demonstrate constraints:

1. **Hinged door:** Dynamic box (2m x 2.5m x 0.1m) with hinge at left edge, angle limits [-120, 0], slight friction. Player pushes with E key.

2. **Hanging lamp:** Dynamic sphere on a chain of 3 distance constraints, attached to ceiling. Player can push to swing.

3. **Sliding crate:** Dynamic box on a slider constraint, limited to 2m travel. Player pushes along track.

4. **Breakable shelf:** Two dynamic boxes connected by fixed constraint with break force 50N. Player can push hard enough to break.

5. **Swinging sign:** Dynamic box (sign) with point constraint at top center. Swings freely in all directions.

---

## 10. Implementation Steps

### Step 1: PhysicsConstraint class
- Create `engine/physics/physics_constraint.h/.cpp`
- `ConstraintHandle` struct
- `PhysicsConstraint` class wrapping Jolt constraint
- Type enum, handle, body IDs, break force, current force
- Type-safe accessors (`asHinge()`, `asFixed()`, etc.)

### Step 2: PhysicsWorld constraint management
- Add constraint storage (`std::unordered_map<uint32_t, PhysicsConstraint>`)
- Handle generation counter
- `addHingeConstraint()`, `addFixedConstraint()`, `addDistanceConstraint()`, `addPointConstraint()`, `addSliderConstraint()`
- `removeConstraint()`, `removeConstraintsForBody()`, `getConstraint()`
- `checkBreakableConstraints()`
- Update `destroyBody()` to remove associated constraints first

### Step 3: Raycast and impulse API
- Add `rayCast()` to PhysicsWorld using Jolt's narrow phase query
- Add `applyImpulseAtPoint()` using body interface
- Add `getBodyMotionType()` helper

### Step 4: ConstraintComponent
- Create `engine/physics/constraint_component.h/.cpp`
- Configuration properties for all constraint types
- `activate()` / `deactivate()` methods
- Motor control helpers

### Step 5: Engine integration
- E key interaction handler in engine main loop
- Call `checkBreakableConstraints()` after physics update
- Debug draw extension for constraints

### Step 6: Inspector panel
- Constraint type dropdown
- Type-specific parameter sections
- Motor controls
- Break force slider

### Step 7: Unit tests
- Test each constraint type creation and behavior
- Test breaking constraints
- Test removal and cleanup
- Test raycast API

### Step 8: Demo scene objects
- Add interactive constrained objects to demonstrate functionality

---

## 11. File Structure

```
engine/physics/
    physics_constraint.h        # NEW - PhysicsConstraint, ConstraintHandle
    physics_constraint.cpp      # NEW
    constraint_component.h      # NEW - ConstraintComponent
    constraint_component.cpp    # NEW
    physics_world.h             # MODIFIED - add constraint management + raycast
    physics_world.cpp           # MODIFIED
    physics_debug.h             # MODIFIED - constraint visualization
    physics_debug.cpp           # MODIFIED
engine/core/
    engine.cpp                  # MODIFIED - E key interaction
engine/editor/panels/
    inspector_panel.h           # MODIFIED - constraint UI
    inspector_panel.cpp         # MODIFIED
tests/
    test_physics_constraint.cpp # NEW - constraint unit tests
```

---

## 12. Performance Considerations

- **Solver iterations:** Default 10 velocity / 2 position is sufficient for doors and simple chains. No per-constraint override needed.
- **Constraint count:** Architectural scenes will have ~10-50 constraints. This is trivial for Jolt (designed for thousands).
- **Breaking check:** O(n) iteration over active constraints each frame. With <50 constraints, this is negligible.
- **Raycast:** Single narrow-phase ray per E-key press. Zero cost when not interacting.
- **Debug draw:** Only active when toggled. Lines are batched with existing debug draw system.
- **Island splitting:** Constraints form islands. A chain of 10 links = 1 island. Jolt handles this efficiently with automatic large-island splitting.

**Spring frequency guidelines (at 60 Hz sim):**
- Max safe frequency: 30 Hz (half sim rate)
- Stiff spring: 20 Hz
- Soft spring: 2 Hz
- Very soft: 0.5 Hz

---

## 13. Testing Strategy

### Unit Tests

| Test | What It Verifies |
|------|-----------------|
| `HingeCreationAndLimits` | Hinge constraint created, angle stays within limits after simulation |
| `HingeMotorVelocity` | Motor drives rotation at target velocity |
| `HingeMotorPosition` | Motor drives to target angle |
| `FixedConstraintWeld` | Two bodies move as one unit |
| `DistanceConstraintRigid` | Bodies maintain fixed distance |
| `DistanceConstraintSpring` | Bodies oscillate with spring settings |
| `PointConstraintSwing` | Body swings freely around pivot |
| `SliderConstraintLimits` | Body slides within limit range |
| `SliderMotor` | Motor drives to target position/velocity |
| `BreakableConstraint` | Constraint disables when force exceeds threshold |
| `WorldAttachment` | Body constrained to world (no bodyA) stays in place |
| `RemoveConstraint` | Constraint removed cleanly, bodies become independent |
| `DestroyBodyRemovesConstraints` | Destroying a body auto-removes its constraints |
| `HandleValidation` | Invalid/stale handles return nullptr |
| `RayCastHitsBody` | Raycast finds dynamic body |
| `ImpulseAtPoint` | Impulse applied at offset creates torque |

### Visual Tests

1. Push a hinged door open with E key -- it swings, hits angle limit, bounces back
2. Push a hanging lamp -- it swings and gradually settles
3. Push a crate along its slider track -- stops at limits
4. Hit a breakable object hard enough -- constraint breaks, pieces fall apart
5. Verify debug draw shows constraint shapes correctly
