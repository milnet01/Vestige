# Phase 8 Design: Physics

**Date:** 2026-03-31
**Phase:** 8 — Physics
**Status:** Implemented. All sub-phases (8A–8G) are shipped; Phase 8 is marked COMPLETE in `ROADMAP.md`. This document is retained as the original design record.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Research Summary](#2-research-summary)
3. [Sub-Phase Breakdown](#3-sub-phase-breakdown)
4. [Phase 8A: Jolt Integration and Rigid Body Foundation](#4-phase-8a-jolt-integration-and-rigid-body-foundation)
5. [Phase 8B: Physics-Based Character Controller](#5-phase-8b-physics-based-character-controller)
6. [Phase 8C: Constraints and Joints](#6-phase-8c-constraints-and-joints)
7. [Phase 8D: Cloth Simulation (CPU XPBD)](#7-phase-8d-cloth-simulation-cpu-xpbd)
8. [Phase 8E: Cloth Polish and Scene Integration](#8-phase-8e-cloth-polish-and-scene-integration)
9. [Architecture](#9-architecture)
10. [File Structure](#10-file-structure)
11. [Performance Budget](#11-performance-budget)
12. [Testing Strategy](#12-testing-strategy)
13. [Risk Assessment](#13-risk-assessment)

---

## 1. Overview

Phase 8 adds physical simulation to the Vestige engine: rigid body dynamics via Jolt Physics, a physics-based character controller replacing the current AABB system, constraint/joint support for interactive objects, and cloth simulation for curtains and fabric.

### Milestone (from ROADMAP)

> The Tabernacle's linen curtains sway gently, the entrance veil drapes realistically from its poles, and doors throughout Solomon's Temple swing on hinged joints.

### Key Decisions (from Research)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Physics engine | **Jolt Physics** | C++17 native, MIT license, actively maintained, proven AAA (Horizon Forbidden West), superior multi-threading, clean CMake integration |
| Character controller | **Jolt CharacterVirtual** (kinematic) | Precise movement, no physics body overhead, built-in stair climbing and slope handling, same approach as Unreal/Unity/Godot |
| Cloth approach | **Standalone CPU XPBD** | Avoids Jolt soft body dependency coupling, well-documented algorithm, sufficient for 5-10 curtains at 500-1000 verts, GPU migration path available later |
| Cloth algorithm | **XPBD** (not mass-spring, not PBD) | Stiffness independent of iteration count, physically meaningful parameters, unconditionally stable |

---

## 2. Research Summary

Three research documents were produced before this design:

1. **`docs/PHYSICS_ENGINE_RESEARCH.md`** — Bullet vs Jolt comparison across 8 criteria. Jolt wins in every category that matters: C++17 API, performance (doubled sim frequency at Guerrilla Games), active maintenance, XPBD soft bodies, CharacterVirtual controller, MIT license.

2. **`docs/CLOTH_SIMULATION_RESEARCH.md`** — Covers mass-spring vs PBD vs XPBD, GPU vs CPU tradeoffs, wind models, collision approaches, pin constraints, self-collision costs. Recommends CPU XPBD with per-triangle wind drag.

3. **`docs/CHARACTER_CONTROLLER_RESEARCH.md`** — Analyzes current AABB controller limitations, compares kinematic vs dynamic approaches, details Jolt's CharacterVirtual API, documents slope/stair/ground handling from PhysX/Unreal/Unity/Godot.

---

## 3. Sub-Phase Breakdown

| Sub-Phase | Scope | New Files | Dependencies |
|-----------|-------|-----------|--------------|
| **8A** | Jolt integration, PhysicsWorld subsystem, rigid body component, collision shapes, static/dynamic/kinematic bodies | ~10 | Jolt (FetchContent) |
| **8B** | Physics-based character controller replacing AABB, capsule shape, stair climbing, slope handling, fly mode | ~4 | 8A |
| **8C** | Hinge, spring, fixed, distance constraints, rope/chain, interactive doors | ~4 | 8A |
| **8D** | XPBD cloth solver, ClothMesh, distance/bend/pin constraints, wind, sphere/plane collision | ~8 | None (standalone) |
| **8E** | Cloth presets (linen curtain, tent fabric, banner), editor UI, scene integration | ~4 | 8D |

**8A and 8D are independent** and could be developed in parallel.
8B and 8C depend on 8A. 8E depends on 8D.

---

## 4. Phase 8A: Jolt Integration and Rigid Body Foundation

### 4.1 Jolt FetchContent Setup

Add to `external/CMakeLists.txt`:

```cmake
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JoltPhysics)
```

Link in `engine/CMakeLists.txt`:
```cmake
target_link_libraries(vestige_engine PUBLIC ... Jolt)
```

### 4.2 PhysicsWorld Subsystem

New class: `engine/physics/physics_world.h/.cpp`

**Responsibilities:**
- Own `JPH::PhysicsSystem`, `JPH::TempAllocatorImpl`, `JPH::JobSystemThreadPool`
- Define broadphase layers (STATIC, DYNAMIC, CHARACTER) and collision filtering
- Provide `update(float deltaTime)` with fixed timestep accumulation
- Provide body creation/destruction interface
- Provide collision query API (raycasts, shape casts)

**Broadphase Layer Design:**

```cpp
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer STATIC(0);     // Walls, floors, pillars
    static constexpr JPH::BroadPhaseLayer DYNAMIC(1);    // Moveable objects
    static constexpr JPH::BroadPhaseLayer CHARACTER(2);   // Player proxy body
    static constexpr uint32_t NUM_LAYERS = 3;
}
```

**Object Layer Design:**

```cpp
namespace ObjectLayers
{
    static constexpr JPH::ObjectLayer STATIC = 0;        // Static geometry
    static constexpr JPH::ObjectLayer DYNAMIC = 1;       // Dynamic rigid bodies
    static constexpr JPH::ObjectLayer CHARACTER = 2;      // Player proxy
    static constexpr JPH::ObjectLayer TRIGGER = 3;        // Trigger volumes (no response)
    static constexpr uint32_t NUM_LAYERS = 4;
}
```

**Collision Matrix:**

| | STATIC | DYNAMIC | CHARACTER | TRIGGER |
|---|---|---|---|---|
| STATIC | - | Yes | Yes | - |
| DYNAMIC | Yes | Yes | Yes | Yes |
| CHARACTER | Yes | Yes | - | Yes |
| TRIGGER | - | Yes | Yes | - |

**Fixed Timestep:**

```cpp
void PhysicsWorld::update(float deltaTime)
{
    m_accumulator += deltaTime;
    while (m_accumulator >= PHYSICS_TIMESTEP)
    {
        m_physicsSystem->Update(PHYSICS_TIMESTEP, m_collisionSteps,
                                m_tempAllocator.get(), m_jobSystem.get());
        m_accumulator -= PHYSICS_TIMESTEP;
    }
}
```

Default: `PHYSICS_TIMESTEP = 1.0f / 60.0f`, `m_collisionSteps = 1`.

### 4.3 RigidBody Component

New class: `engine/physics/rigid_body.h/.cpp`

A `Component` that represents a physics body in the Jolt system.

```cpp
class RigidBody : public Component
{
public:
    enum class BodyType : uint8_t { STATIC, DYNAMIC, KINEMATIC };
    enum class ShapeType : uint8_t { BOX, SPHERE, CAPSULE, CONVEX_HULL, MESH };

    // Configuration (set before adding to PhysicsWorld)
    BodyType bodyType = BodyType::STATIC;
    ShapeType shapeType = ShapeType::BOX;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    glm::vec3 shapeSize = glm::vec3(1.0f);  // Half-extents for box, radius for sphere, etc.

    // Runtime
    void syncToTransform();   // Copy physics body position → entity Transform
    void syncFromTransform(); // Copy entity Transform → physics body (kinematic)

    // Component interface
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

private:
    JPH::BodyID m_bodyId;
    PhysicsWorld* m_physicsWorld = nullptr;
};
```

**Sync strategy:**
- Dynamic bodies: physics → transform (after `PhysicsWorld::update()`)
- Kinematic bodies: transform → physics (before `PhysicsWorld::update()`)
- Static bodies: synced once at creation

### 4.4 Collision Shape Factory

Helper to create Jolt shapes from component configuration:

```cpp
JPH::ShapeRefC createShape(ShapeType type, const glm::vec3& size);
```

Supported shapes:
- **Box**: `JPH::BoxShape(halfExtents)`
- **Sphere**: `JPH::SphereShape(radius)`
- **Capsule**: `JPH::CapsuleShape(halfHeight, radius)`
- **Convex Hull**: `JPH::ConvexHullShape(vertices)` — built from mesh data
- **Triangle Mesh**: `JPH::MeshShape(triangles)` — for complex static geometry

### 4.5 Engine Integration

In `Engine::run()`, add physics update between scene update and controller update:

```
Scene update (entities, components, animation)
  ↓
PhysicsWorld::update(deltaTime)     ← NEW
  ↓
RigidBody::syncToTransform()        ← NEW (dynamic bodies)
  ↓
Controller update (with colliders from physics)
  ↓
Render
```

### 4.6 Debug Visualization

Wire-frame overlay for collision shapes (toggle via editor):
- Green wireframe: static bodies
- Blue wireframe: dynamic bodies
- Yellow wireframe: kinematic bodies
- Red wireframe: triggers

Uses the existing debug line renderer (if available) or a simple GL_LINES pass.

---

## 5. Phase 8B: Physics-Based Character Controller

### 5.1 PhysicsCharacterController

New class: `engine/physics/physics_character_controller.h/.cpp`

Wraps `JPH::CharacterVirtual` and replaces the collision handling in `FirstPersonController`.

**Architecture Decision:** Keep `FirstPersonController` as the input/camera layer. It processes keyboard, mouse, and gamepad input and produces a desired velocity vector. The new `PhysicsCharacterController` handles collision resolution via Jolt.

```
Input (KB/Mouse/Gamepad)
  ↓
FirstPersonController  →  desiredVelocity
  ↓
PhysicsCharacterController  →  resolvedPosition
  ↓
Camera.setPosition(resolvedPosition + eyeOffset)
```

### 5.2 Capsule Shape

Current AABB: 0.6m wide x 1.7m tall.
New capsule: radius 0.3m, half-height 0.55m → total height 0.3 + 0.55 + 0.55 + 0.3 = 1.7m.

```cpp
JPH::CapsuleShape(0.55f, 0.3f)  // halfHeight, radius
```

### 5.3 CharacterVirtual Configuration

```cpp
JPH::CharacterVirtualSettings settings;
settings.mShape = capsuleShape;
settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);  // Match current
settings.mCharacterPadding = 0.02f;
settings.mPenetrationRecoverySpeed = 1.0f;
settings.mPredictiveContactDistance = 0.1f;
settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -0.3f);
settings.mMass = 70.0f;
settings.mMaxStrength = 100.0f;
```

### 5.4 Per-Frame Update Pattern

```cpp
void PhysicsCharacterController::update(float deltaTime,
                                         const glm::vec3& desiredVelocity,
                                         bool jump)
{
    JPH::Vec3 velocity = toJolt(desiredVelocity);

    // Ground state check
    if (m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround)
    {
        velocity.SetY(jump ? JUMP_VELOCITY : 0.0f);
    }
    else
    {
        // Preserve vertical velocity, apply gravity
        velocity.SetY(m_character->GetLinearVelocity().GetY());
    }

    // Apply gravity
    velocity += toJolt(m_gravity) * deltaTime;

    // Smooth input (reduce jitter)
    velocity = 0.25f * velocity + 0.75f * m_character->GetLinearVelocity();

    m_character->SetLinearVelocity(velocity);

    // Combined update: move + stair climb + floor stick
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0, -0.5f, 0);
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0, 0.35f, 0);

    m_character->ExtendedUpdate(deltaTime, toJolt(m_gravity),
                                 updateSettings, m_broadPhaseFilter,
                                 m_objectFilter, m_bodyFilter,
                                 m_shapeFilter, *m_tempAllocator);
}
```

### 5.5 Fly Mode

When fly mode is active, skip gravity and allow vertical velocity from input:

```cpp
if (m_flyMode)
{
    // Direct velocity control, no gravity, no ground checks
    m_character->SetLinearVelocity(toJolt(desiredVelocity));
    m_character->Update(deltaTime, JPH::Vec3::sZero(), ...);
}
```

### 5.6 Migration Strategy

1. Build `PhysicsCharacterController` alongside existing AABB system
2. Add a toggle key (F9) to switch between old and new controllers at runtime
3. Verify feature parity:
   - Walk mode terrain following
   - Slope limiting at 50 degrees
   - Sprint speed multiplier
   - Fly mode
   - Smooth camera height transitions
4. Verify new capabilities:
   - Step climbing on stairs
   - Smooth wall sliding
   - Capsule slides over corners (no AABB snagging)
5. Remove old AABB collision code after validation

### 5.7 Static World Bodies

Convert existing scene geometry to Jolt static bodies:
- Mesh renderers with AABBs → `JPH::BoxShape` static bodies
- Terrain → `JPH::HeightFieldShape` (if available) or `JPH::MeshShape`
- Complex models → `JPH::MeshShape` from triangle data

---

## 6. Phase 8C: Constraints and Joints

### 6.1 Constraint Component

New class: `engine/physics/constraint_component.h/.cpp`

```cpp
class ConstraintComponent : public Component
{
public:
    enum class ConstraintType : uint8_t
    {
        HINGE,     // Doors, gates, lids
        SPRING,    // Swinging objects, suspension
        FIXED,     // Weld objects together
        DISTANCE   // Rope/chain links
    };

    ConstraintType type = ConstraintType::HINGE;

    // Hinge params
    glm::vec3 hingeAxis = glm::vec3(0, 1, 0);
    float minAngle = -180.0f;  // degrees
    float maxAngle = 180.0f;
    float motorSpeed = 0.0f;

    // Spring params
    float springFrequency = 2.0f;  // Hz
    float springDamping = 0.5f;

    // Distance params
    float minDistance = 0.0f;
    float maxDistance = 1.0f;

    Entity* targetEntity = nullptr;  // The other body in the constraint
};
```

### 6.2 Hinge Joint (Doors)

For the Tabernacle/Temple scenes, hinge joints are the primary use case:

```cpp
JPH::HingeConstraintSettings hingeSettings;
hingeSettings.mPoint1 = doorHingeWorldPos;
hingeSettings.mHingeAxis1 = JPH::Vec3(0, 1, 0);  // Y-axis rotation
hingeSettings.mNormalAxis1 = JPH::Vec3(1, 0, 0);
hingeSettings.mLimitsMin = JPH::DegreesToRadians(-120.0f);
hingeSettings.mLimitsMax = JPH::DegreesToRadians(0.0f);
```

### 6.3 Spring Constraint

For hanging lamps, swinging objects:

```cpp
JPH::DistanceConstraintSettings springSettings;
springSettings.mMinDistance = 0.0f;
springSettings.mMaxDistance = ropeLength;
springSettings.mLimitsSpringSettings.mFrequency = 2.0f;
springSettings.mLimitsSpringSettings.mDamping = 0.5f;
```

### 6.4 Rope/Chain

Chains modeled as a series of capsule rigid bodies linked by distance constraints. Each link:
- Small capsule body (dynamic)
- Distance constraint to previous link
- First link attached to ceiling via fixed constraint

### 6.5 Interaction System

Player interacts with constrained objects by:
1. Raycast from camera center
2. If hit a dynamic body with a constraint → apply impulse in look direction
3. For doors: apply torque around hinge axis

Interaction trigger: E key / gamepad A button (reuse existing input mapping).

---

## 7. Phase 8D: Cloth Simulation (CPU XPBD)

### 7.1 ClothMesh

New class: `engine/physics/cloth_mesh.h/.cpp`

```cpp
struct ClothParticle
{
    glm::vec3 position;
    glm::vec3 prevPosition;
    glm::vec3 velocity;
    float invMass;           // 0 = pinned
    glm::vec3 pinTarget;    // World-space pin position (if invMass == 0)
};

struct DistanceConstraint
{
    uint32_t p0, p1;        // Particle indices
    float restLength;
    float compliance;        // XPBD compliance (0 = rigid)
    float lambda;            // Lagrange multiplier (reset each frame)
};

struct ClothMesh
{
    std::vector<ClothParticle> particles;
    std::vector<DistanceConstraint> stretchConstraints;  // Edge constraints
    std::vector<DistanceConstraint> bendConstraints;     // Skip-one constraints
    std::vector<uint32_t> indices;                       // Triangle indices (for rendering + wind)

    int width, height;       // Grid dimensions (for rectangular cloth)
    float restSpacing;       // Distance between adjacent particles at rest
};
```

### 7.2 ClothSolver

New class: `engine/physics/cloth_solver.h/.cpp`

The XPBD solver core:

```cpp
class ClothSolver
{
public:
    struct Config
    {
        float stretchCompliance = 0.0f;    // 0 = inextensible
        float bendCompliance = 0.01f;       // Slight bending allowed
        float damping = 0.99f;              // Velocity damping per step
        int solverIterations = 5;           // XPBD iterations per substep
        int substeps = 3;                   // Simulation substeps per frame
        glm::vec3 gravity = {0, -9.81f, 0};
    };

    void solve(ClothMesh& cloth, float deltaTime, const Config& config,
               const std::vector<ClothCollider>& colliders,
               const glm::vec3& wind);

private:
    void applyExternalForces(ClothMesh& cloth, float dt,
                              const Config& config, const glm::vec3& wind);
    void predictPositions(ClothMesh& cloth, float dt);
    void solveConstraints(ClothMesh& cloth, const Config& config);
    void solveDistanceConstraint(ClothParticle& p0, ClothParticle& p1,
                                  DistanceConstraint& c, float dt);
    void resolveCollisions(ClothMesh& cloth,
                            const std::vector<ClothCollider>& colliders);
    void updateVelocities(ClothMesh& cloth, float dt, float damping);
};
```

**XPBD Distance Constraint Solver:**

```cpp
void ClothSolver::solveDistanceConstraint(ClothParticle& p0, ClothParticle& p1,
                                           DistanceConstraint& c, float dt)
{
    glm::vec3 diff = p0.position - p1.position;
    float dist = glm::length(diff);
    if (dist < 1e-7f) return;

    float C = dist - c.restLength;                    // Constraint violation
    float alphaTilde = c.compliance / (dt * dt);       // Scaled compliance
    float wSum = p0.invMass + p1.invMass;
    if (wSum < 1e-7f) return;

    float deltaLambda = (-C - alphaTilde * c.lambda) / (wSum + alphaTilde);
    c.lambda += deltaLambda;

    glm::vec3 correction = (deltaLambda / dist) * diff;
    p0.position += p0.invMass * correction;
    p1.position -= p1.invMass * correction;
}
```

### 7.3 Wind Model

Per-triangle aerodynamic drag:

```cpp
void ClothSolver::applyWind(ClothMesh& cloth, const glm::vec3& wind, float dragCoeff)
{
    for (size_t i = 0; i + 2 < cloth.indices.size(); i += 3)
    {
        auto& p0 = cloth.particles[cloth.indices[i]];
        auto& p1 = cloth.particles[cloth.indices[i + 1]];
        auto& p2 = cloth.particles[cloth.indices[i + 2]];

        glm::vec3 avgVel = (p0.velocity + p1.velocity + p2.velocity) / 3.0f;
        glm::vec3 relVel = wind - avgVel;

        glm::vec3 edge1 = p1.position - p0.position;
        glm::vec3 edge2 = p2.position - p0.position;
        glm::vec3 normal = glm::cross(edge1, edge2);
        float area = glm::length(normal) * 0.5f;
        if (area < 1e-7f) continue;
        normal = glm::normalize(normal);

        float dot = glm::dot(relVel, normal);
        glm::vec3 force = dragCoeff * area * dot * normal;
        glm::vec3 perVertex = force / 3.0f;

        p0.velocity += perVertex * p0.invMass;
        p1.velocity += perVertex * p1.invMass;
        p2.velocity += perVertex * p2.invMass;
    }
}
```

Wind variation with Perlin noise and gusts:
```cpp
glm::vec3 windAtPoint = baseWind * (1.0f + turbulence * noise3D(pos * 0.5f, time))
                       + gustDir * gustStrength * sin(time * gustFreq);
```

### 7.4 Collision Shapes

New class: `engine/physics/cloth_collider.h/.cpp`

```cpp
struct ClothCollider
{
    enum class Type : uint8_t { SPHERE, CAPSULE, PLANE };
    Type type;
    glm::vec3 position;
    glm::vec3 direction;  // Capsule axis or plane normal
    float radius;         // Sphere/capsule radius
    float height;         // Capsule height
};
```

Collision resolution pushes particles to the surface:

```cpp
void resolveSpherCollision(ClothParticle& p, const ClothCollider& sphere)
{
    glm::vec3 diff = p.position - sphere.position;
    float dist = glm::length(diff);
    if (dist < sphere.radius && dist > 1e-7f)
    {
        p.position = sphere.position + (diff / dist) * sphere.radius;
    }
}
```

### 7.5 ClothComponent

New class: `engine/physics/cloth_component.h/.cpp`

```cpp
class ClothComponent : public Component
{
public:
    // Configuration
    int gridWidth = 20;
    int gridHeight = 20;
    float clothWidth = 2.0f;    // World-space width
    float clothHeight = 2.0f;   // World-space height

    // Pin mode
    enum class PinMode : uint8_t
    {
        TOP_ROW,          // All top particles (flat curtain)
        TOP_CORNERS,      // Only corners (draped cloth)
        TOP_EVERY_N,      // Every Nth top particle (pleated curtain)
        CUSTOM            // User-specified pin indices
    };
    PinMode pinMode = PinMode::TOP_ROW;
    int pinSpacing = 1;

    // Solver config
    ClothSolver::Config solverConfig;

    // Wind
    glm::vec3 windDirection = glm::vec3(0, 0, 1);
    float windStrength = 5.0f;
    float windTurbulence = 0.3f;
    float dragCoefficient = 0.05f;

    // Colliders local to this cloth
    std::vector<ClothCollider> colliders;

    // Component interface
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    // Accessors for renderer
    const ClothMesh& getMesh() const;
    bool isDirty() const;  // True if positions changed since last render

private:
    ClothMesh m_mesh;
    ClothSolver m_solver;
    bool m_dirty = true;
    bool m_initialized = false;

    void initialize();  // Build grid mesh from config
};
```

### 7.6 Cloth Rendering

Cloth renders as a standard mesh with dynamic VBO updates:

1. `ClothComponent::update()` runs the XPBD solver, marks dirty
2. Renderer detects dirty cloth → updates VBO from particle positions
3. Recompute normals per-triangle (flat shading) or per-vertex (smooth shading)
4. Render with existing scene shader + cloth material (linen, goat hair, etc.)

No new shaders needed for Phase 8D — cloth uses the existing PBR pipeline.

### 7.7 Rectangular Grid Generation

```cpp
void ClothComponent::initialize()
{
    float spacingX = clothWidth / (gridWidth - 1);
    float spacingY = clothHeight / (gridHeight - 1);

    // Create particles in a grid
    for (int y = 0; y < gridHeight; ++y)
    {
        for (int x = 0; x < gridWidth; ++x)
        {
            ClothParticle p;
            p.position = worldOrigin + glm::vec3(x * spacingX, -y * spacingY, 0);
            p.prevPosition = p.position;
            p.velocity = glm::vec3(0);
            p.invMass = 1.0f;
            m_mesh.particles.push_back(p);
        }
    }

    // Pin top row (or per pinMode)
    applyPinMode();

    // Create stretch constraints (horizontal + vertical edges)
    // Create shear constraints (diagonal edges)
    // Create bend constraints (skip-one connections)
    // Create triangle indices for rendering + wind
}
```

---

## 8. Phase 8E: Cloth Polish and Scene Integration

### 8.1 Presets

```cpp
struct ClothPreset
{
    std::string name;
    float stretchCompliance;
    float bendCompliance;
    float damping;
    float dragCoefficient;
    int solverIterations;
    int substeps;
};
```

| Preset | Stretch Compliance | Bend Compliance | Damping | Drag | Iterations | Substeps |
|--------|-------------------|-----------------|---------|------|------------|----------|
| Linen curtain | 0.0 | 0.005 | 0.995 | 0.04 | 5 | 3 |
| Tent fabric | 0.0 | 0.001 | 0.99 | 0.06 | 5 | 3 |
| Banner/flag | 0.0 | 0.01 | 0.98 | 0.08 | 5 | 2 |
| Priestly garment | 0.0001 | 0.005 | 0.995 | 0.03 | 8 | 3 |
| Heavy drape | 0.0 | 0.0005 | 0.99 | 0.05 | 8 | 3 |

### 8.2 Editor Integration

Add cloth properties to the inspector panel:
- Grid dimensions (width x height)
- Physical size (meters)
- Pin mode dropdown + pin spacing
- Solver config sliders (compliance, damping, iterations, substeps)
- Wind vector (direction + strength + turbulence)
- Preset selector dropdown
- Collider list (add sphere/capsule/plane)
- Reset button (restore to initial pose)

### 8.3 Scene Integration

For the Tabernacle scene:
- **Entrance veil**: Cloth pinned to top bar, 3m x 2.5m, linen preset, gentle wind
- **Holy Place / Holy of Holies partition**: Cloth pinned at four points, 5 cubit x 5 cubit
- **Tent cover layers**: Large cloth sheets with many pins (top surface)
- **Courtyard gate curtain**: Pinned to gate posts, wider span

Each cloth entity:
1. Entity with Transform (position at attachment point)
2. ClothComponent (grid size, preset, pin mode)
3. MeshRenderer (dynamic VBO, cloth material)

---

## 9. Architecture

### 9.1 System Update Order

```
1. Timer.update()
2. Window.pollEvents()
3. InputManager.update()
4. Scene.update(deltaTime)           // Entity components (animation, tweens, etc.)
5. PhysicsWorld.update(deltaTime)    // Jolt step (rigid bodies, constraints)
6. RigidBody.syncToTransform()       // Physics → entity transforms
7. ClothComponent.update(deltaTime)  // XPBD cloth solver (after rigid bodies settle)
8. Controller.update(deltaTime, desiredVelocity)  // CharacterVirtual movement
9. Renderer.render()
10. Editor.render()
11. Window.swapBuffers()
```

### 9.2 Coordinate System Conversion

Vestige uses GLM (right-handed, Y-up). Jolt uses its own math types (also right-handed, Y-up). Simple conversion helpers:

```cpp
inline JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline glm::vec3 toGlm(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline glm::quat toGlm(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
```

### 9.3 Memory Allocation

Jolt requires custom allocators to be registered before any Jolt code runs:

```cpp
JPH::RegisterDefaultAllocator();  // Or custom allocator
JPH::Factory::sInstance = new JPH::Factory();
JPH::RegisterTypes();
```

This happens in `PhysicsWorld` constructor, before any bodies are created.

---

## 10. File Structure

```
engine/
  physics/
    physics_world.h              // Jolt PhysicsSystem wrapper
    physics_world.cpp
    physics_layers.h             // Broadphase + object layer definitions
    rigid_body.h                 // RigidBody component
    rigid_body.cpp
    constraint_component.h       // Constraint component (hinge, spring, fixed, distance)
    constraint_component.cpp
    physics_character_controller.h  // CharacterVirtual wrapper
    physics_character_controller.cpp
    physics_debug.h              // Debug wireframe visualization
    physics_debug.cpp
    cloth_mesh.h                 // ClothMesh data structures
    cloth_mesh.cpp
    cloth_solver.h               // XPBD solver
    cloth_solver.cpp
    cloth_collider.h             // Sphere/capsule/plane cloth collision shapes
    cloth_collider.cpp
    cloth_component.h            // ClothComponent (Component interface)
    cloth_component.cpp
    cloth_presets.h              // Cloth material presets
    jolt_helpers.h               // toJolt/toGlm conversion helpers (header-only)
tests/
    test_physics_world.cpp       // PhysicsWorld creation, body add/remove
    test_rigid_body.cpp          // Body types, sync, collision shapes
    test_cloth_mesh.cpp          // Grid generation, constraint creation
    test_cloth_solver.cpp        // XPBD solver correctness (hang test, stretch test)
    test_cloth_collider.cpp      // Collision resolution tests
```

---

## 11. Performance Budget

**Target:** All physics work must complete within **4ms per frame** (leaving 12.7ms for rendering and other systems at 60 FPS).

| System | Budget | Expected |
|--------|--------|----------|
| Jolt rigid body step (50 bodies) | 1.0ms | 0.3-0.5ms |
| CharacterVirtual update | 0.5ms | 0.1-0.2ms |
| Cloth XPBD (5 cloths, 500 verts each, 5 iters, 3 substeps) | 2.0ms | 1.0-1.5ms |
| VBO updates for cloth | 0.5ms | 0.1-0.3ms |
| **Total** | **4.0ms** | **~2.0-2.5ms** |

**Scaling limits before GPU migration is needed:**
- ~20 cloth objects at 500 vertices each
- ~100 rigid bodies (well within Jolt's capabilities)

**Profiling plan:** Use the existing engine profiler (GPU/CPU timing) to track physics time each frame. Add `PROFILE_SCOPE("Physics")` markers.

---

## 12. Testing Strategy

### Unit Tests

| Test | What it verifies |
|------|-----------------|
| `test_physics_world` | Jolt initialization, body creation/destruction, timestep accumulation, raycast queries |
| `test_rigid_body` | Body type behavior (static doesn't move, dynamic falls, kinematic follows transform), collision shapes, sync correctness |
| `test_cloth_mesh` | Grid generation (correct particle count, constraint count), pin modes, rest length calculation |
| `test_cloth_solver` | Gravity hang (cloth falls and stabilizes), inextensibility (edge lengths preserved within tolerance), pin constraints (pinned particles don't move), wind (particles displaced in wind direction) |
| `test_cloth_collider` | Sphere pushout (particle never inside sphere), plane collision (particle stays above plane), capsule collision |

### Visual Tests

After each sub-phase:
1. **8A**: Drop a dynamic box onto a static floor — it should bounce and settle
2. **8B**: Walk up stairs, slide along walls, sprint, fly mode — compare with old controller
3. **8C**: Open a door by looking at it and pressing E — hinge constraint limits and returns
4. **8D**: Hang a cloth from pins — it should drape under gravity, respond to wind
5. **8E**: Place Tabernacle curtains — they should sway gently, collide with pillars

---

## 13. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Jolt build integration issues on Linux/AMD | Medium | Jolt officially supports Linux + GCC; test FetchContent early in 8A |
| Character controller feels different from old AABB | High | Side-by-side toggle (F9), tune input smoothing to match existing feel |
| Cloth performance exceeds budget | Medium | CPU XPBD is well within budget for 5-10 cloths; GPU path available as fallback |
| Cloth tunneling through thin geometry | Low | Use substeps (3 default) and collision margin; architectural geometry is thick |
| Jolt version incompatibility | Low | Pin to specific tag (v5.2.0) via FetchContent |
| XPBD solver instability with extreme parameters | Low | Clamp compliance to safe ranges in editor; unit tests verify stability |

---

## Appendix: Research Documents

- `docs/PHYSICS_ENGINE_RESEARCH.md` — Bullet vs Jolt comparison (30+ sources)
- `docs/CLOTH_SIMULATION_RESEARCH.md` — Cloth algorithms, GPU/CPU, wind, collision (40+ sources)
- `docs/CHARACTER_CONTROLLER_RESEARCH.md` — Character controller approaches (30+ sources)
