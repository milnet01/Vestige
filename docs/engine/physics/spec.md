# Subsystem Specification — `engine/physics`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/physics` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (Jolt foundation since Phase 8A; XPBD cloth since Phase 8D; GPU cloth backend since Phase 9B) |

---

## 1. Purpose

`engine/physics` owns every body that *moves under simulated forces* in the engine: rigid bodies (Jolt), the player / NPC capsule character controller (Jolt `CharacterVirtual`), constraints / joints (hinge / fixed / distance / point / slider, breakable), and cloth (Extended Position-Based Dynamics — XPBD — on the CPU, with a Phase 9B compute-shader backend on the GPU). It exists as its own subsystem because Jolt's worldview (`PhysicsSystem`, `BodyInterface`, `JobSystem`, broadphase + object-layer filters, fixed-step `Update`) does not compose cleanly with the rest of the engine's per-frame variable-rate loop, and because the cloth solver is a fundamentally different integration scheme (XPBD position-projection) that nevertheless needs to interoperate with rigid colliders. The engine's primary use case — first-person walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — drives every choice here: walls and pillars are static rigid bodies that never wake, the player is a `CharacterVirtual` that climbs the bronze altar steps, the temple veil and tent coverings are cloth simulated to drape under their own weight, and a future replay pass (Phase 11A) will re-run the same fixed-timestep stream and reproduce the same drape — bit-for-bit on a single platform.

**Units & coordinate convention** (per CODING_STANDARDS §27): metres, Y-up, right-handed; time in seconds; angles in radians at the API boundary; mass in kilograms; force in newtons. Jolt operates in these same conventions natively when configured with the engine's default settings — no axis-flip or unit-scale fixup is required, and `jolt_helpers.h` is straight component-wise vec3/quat/mat4 conversion.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `PhysicsWorld` — Jolt `PhysicsSystem` wrapper, factory + filters, fixed-step accumulator update, body create / destroy, raycasts, applyForce / applyImpulse / applyImpulseAtPoint | Per-frame engine main loop / scene graph / event bus — `engine/core/` |
| `RigidBody` component — entity-attached static / dynamic / kinematic bodies, sync-to-transform, force / impulse helpers, mesh + convex-hull collider build | Mesh / vertex data sources for the colliders — `engine/renderer/mesh.h` (we copy positions out) |
| `PhysicsCharacterController` — Jolt `CharacterVirtual` capsule, slope / step / floor-stick logic, fly-mode toggle | Camera shake, FOV, mouse-look — `engine/core/first_person_controller.h` (FPC drives input + camera; this controller drives translation) |
| Constraint API — hinge / fixed / distance / point / slider; per-handle breakable threshold; deterministic `std::map` storage with per-slot generation counter | High-level interactables (doors, levers, ragdolls) — those compose constraints from gameplay code or `engine/experimental/physics/` |
| Object + broadphase layer system — `STATIC` / `DYNAMIC` / `CHARACTER` / `TRIGGER` object layers, three-broadphase grouping, `BroadPhaseLayerMapping`, `ObjectLayerPairFilter` | Physics-2D — `engine/physics2d/` (separate subsystem; box2d-style top-down) |
| `ClothSimulator` (CPU XPBD) + `GpuClothSimulator` (GPU compute) sharing `IClothSolverBackend`; `ClothBackendFactory` auto-selection at the 1024-particle threshold | Render-side mesh / shader / material — `engine/renderer/dynamic_mesh.h`, `material.h` (cloth only writes positions + normals into the buffer) |
| `ClothComponent` — entity wrapper that owns a backend + dynamic mesh + cloth preset | Cloth UI panel — `engine/editor/panels/inspector_panel.cpp` |
| `ClothMeshCollider` + `BVH` — Surface-Area-Heuristic (SAH) Bounding Volume Hierarchy for triangle-mesh proximity queries against cloth | General-purpose mesh BVH for rendering / GI culling — owned by the renderer |
| `SpatialHash` — Teschner-2003 / Müller counting-sort uniform grid, used for cloth self-collision broad-phase | Broadphase for rigid bodies — Jolt owns that internally |
| `FabricMaterial` + `FabricDatabase` + `ClothPresets` — Kawabata-derived (KES) physical fabric parameters mapped to XPBD compliances | Visual material (albedo / normal / roughness) — `engine/renderer/material.h` |
| `DeformableMesh` — pre-fracture vertex denting on impact | Full fracture / Voronoi shatter — `engine/experimental/physics/fracture.cpp` (zombie cluster, see ROADMAP §10.9 W13) |
| `PhysicsDebugDraw` — wireframe overlay of every body + constraint, motion-type-coloured | Profiler / GPU markers — `engine/profiler/` |
| `jolt_helpers.h` — `glm::vec3 ↔ JPH::Vec3`, `glm::quat ↔ JPH::Quat`, `glm::mat4 ↔ JPH::Mat44` conversions | Anything Jolt does not expose through these helpers — feature code goes through `PhysicsWorld` |
| `ColliderGenerator` — utility that builds a `ClothMeshCollider` from raw vertex / index data | Triangle-soup generation from rendered geometry — caller's responsibility |
| (out) Ragdoll / fracture / dismemberment / grab / stasis | `engine/experimental/physics/` — code shipped + tested but no production caller (ROADMAP §10.9 W13) |

If a reader can't tell which side of the line a feature falls on after reading this table, the table needs more rows.

## 3. Architecture

```
                               ┌─────────────────────────────┐
                               │       PhysicsWorld          │
                               │ engine/physics/             │
                               │  physics_world.h:41         │
                               └──────────────┬──────────────┘
                                              │ owns (RAII)
       ┌──────────────────────┬───────────────┼──────────────────┬────────────────────┐
       ▼                      ▼               ▼                  ▼                    ▼
┌────────────┐      ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  ┌──────────────┐
│PhysicsSyste│      │TempAllocator │  │JobSystem     │  │BroadPhaseLayer   │  │ConstraintMap │
│m (Jolt)    │      │ (10 MB pool) │  │ThreadPool    │  │Mapping +Filters  │  │ std::map<u32 │
│• broadphase│      │              │  │ (HW-1 thr.)  │  │                  │  │ ,Constraint> │
│• narrow    │      └──────────────┘  └──────────────┘  └──────────────────┘  └──────────────┘
│• solver    │
└─────┬──────┘
      │ stepped at fixedTimestep (1/60 s) inside accumulator
      │
      ▼  one frame's bodies (after solve)

┌────────────┐  attached via component on each Entity
│ RigidBody  │ ──→ syncs Transform ↔ Body (DYNAMIC: physics→transform; KINEMATIC: transform→physics)
└────────────┘
┌─────────────────────────────┐
│ PhysicsCharacterController  │ ──→ wraps Jolt CharacterVirtual; FPC drives desiredVelocity in
└─────────────────────────────┘

────────────────────────────────────────────────────────────────────────────────

                      ┌────────────────────────────────────┐
                      │        ClothComponent              │
                      │ engine/physics/cloth_component.h:28 │
                      └─────────────┬──────────────────────┘
                                    │ owns IClothSolverBackend
                  ┌─────────────────┴────────────────────┐
                  ▼                                      ▼
        ┌──────────────────┐                ┌────────────────────────┐
        │ ClothSimulator   │ (CPU XPBD)     │ GpuClothSimulator      │ (GPU compute)
        │  • particle SoA  │                │  • SSBO particle state │
        │  • dist + dihed  │                │  • greedy graph colour │
        │  • LRA tethers   │                │  • cloth_*.comp.glsl   │
        │  • SpatialHash   │                │  • lazy GPU→CPU mirror │
        │  • XPBD solver   │                │  • SAME XPBD math      │
        └────────┬─────────┘                └────────────┬───────────┘
                 │ both share                            │
                 └──────────────┬────────────────────────┘
                                ▼
                  ┌────────────────────────────┐
                  │ IClothSolverBackend (vt)   │
                  │ cloth_solver_backend.h:56  │
                  └────────────────────────────┘

ClothMeshCollider ──→ BVH (binned-SAH 8-bin, O(N) refit) ──→ queryClosest
SpatialHash ──→ Teschner-2003 / Müller counting-sort (cloth self-collision broad-phase)
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `PhysicsWorld` | class (non-copyable, non-movable) | The Jolt facade. Owns `PhysicsSystem`, `TempAllocator`, `JobSystem`, layer filters, constraint map. `engine/physics/physics_world.h:41` |
| `PhysicsWorldConfig` | struct | Cold-init knobs: `fixedTimestep` (1/60 s), `collisionSteps`, `maxBodies` (4096), `numBodyMutexes`, `maxBodyPairs`, `maxContactConstraints`, `threadCount`. `physics_world.h:29` |
| `RigidBody` | `Component` | Entity-attached body. Holds `BodyMotionType` (`STATIC` / `DYNAMIC` / `KINEMATIC`) + `CollisionShapeType` (`BOX` / `SPHERE` / `CAPSULE` / `CONVEX_HULL` / `MESH`). `rigid_body.h:44` |
| `PhysicsCharacterController` | class | `CharacterVirtual` capsule wrapper. Walk, slope-limit, stair-step, floor-stick, fly-mode. `physics_character_controller.h:42` |
| `PhysicsControllerConfig` | struct | Capsule radius / half-height, eye height, max-slope, mass, max-strength, padding, stair-step, stick-distance, gravity, input-smoothing. `physics_character_controller.h:20` |
| `ConstraintHandle` | struct (POD) | `(index, generation)` pair into `PhysicsWorld::m_constraints`. `physics_constraint.h:25` |
| `ConstraintType` | enum | `HINGE` / `FIXED` / `DISTANCE` / `POINT` / `SLIDER`. `physics_constraint.h:44` |
| `PhysicsConstraint` | class | Wraps a `JPH::TwoBodyConstraint` with break-force threshold + last-step force + body-pair tracking. `physics_constraint.h:58` |
| `ObjectLayers` / `BroadPhaseLayers` | constants | 4 object layers (`STATIC` / `DYNAMIC` / `CHARACTER` / `TRIGGER`) ↦ 3 broadphase layers (`STATIC` / `DYNAMIC` / `CHARACTER`). `physics_layers.h:18,30` |
| `BroadPhaseLayerMapping` / `ObjectLayerPairFilter` / `ObjectVsBroadPhaseFilter` | classes | The three filter interfaces Jolt requires. `physics_layers.h:39, 78, 114` |
| `IClothSolverBackend` | interface | Polymorphic surface shared by CPU XPBD and GPU compute backends. `cloth_solver_backend.h:56` |
| `ClothConfig` | struct | Grid (`width × height`), `spacing`, `particleMass`, `substeps` (≤ `MAX_SUBSTEPS = 64`), stretch / shear / bend compliance, damping, sleep threshold, gravity. `cloth_simulator.h:23` |
| `ClothSimulator` | class | Pure-CPU XPBD reference. Stretch / shear / bend distance constraints + dihedral bending + Long-Range Attachment (LRA) tethers + sphere / plane / cylinder / box / mesh colliders + spatial-hash self-collision + wind. `cloth_simulator.h:80` |
| `GpuClothSimulator` | class | GPU compute-shader backend. Greedy graph-coloured constraint SSBO; per-substep dispatch chain (wind → integrate → constraints → dihedral → LRA → collision → normals); lazy GPU→CPU mirror. `gpu_cloth_simulator.h:37` |
| `ClothBackendPolicy` / `ClothBackendChoice` | enums | `AUTO` / `FORCE_CPU` / `FORCE_GPU`; `chooseClothBackend()` picks at the `GPU_AUTO_SELECT_THRESHOLD = 1024`-particle line. `cloth_backend_factory.h:37, 45, 34` |
| `ClothComponent` | `Component` | Owns one `IClothSolverBackend` + a `DynamicMesh` for rendering. Drives the simulation at a fixed 60 Hz with up to 4 sub-steps per frame. `cloth_component.h:28` |
| `ClothMeshCollider` | class | Triangle-mesh collider for cloth. Owns vertex / index copy + a `BVH`; supports per-frame refit for animated meshes. `cloth_mesh_collider.h:22` |
| `BVH` | class | Binned-SAH (8 bins) bounding-volume hierarchy with O(N) bottom-up refit. `bvh.h:33` |
| `SpatialHash` | class | Teschner-2003 / Müller counting-sort uniform grid. Cloth self-collision broad-phase. `spatial_hash.h:24` |
| `FabricType` / `FabricMaterial` / `FabricDatabase` | enum + struct + class | Named fabrics (chiffon, silk, cotton … fine linen, goat hair, ram skin, tachash) with KES-derived compliances. `fabric_material.h:22, 50, 67` |
| `ClothPresetType` / `ClothPresets` | enum + class | UI-friendly presets (`LINEN_CURTAIN`, `TENT_FABRIC`, `BANNER`, `HEAVY_DRAPE`, `STIFF_FENCE`). `cloth_presets.h:24, 36` |
| `DeformableMesh` | class (static utility) | Vertex denting + normal recompute for pre-fracture impact damage. `deformable_mesh.h:21` |
| `PhysicsDebugDraw` | class | Wireframe overlay (green = static, blue = dynamic, yellow = kinematic) + constraint visualisation. `physics_debug.h:21` |
| `toJolt(...)` / `toGlm(...)` | free functions (header-only) | `glm::vec3 ↔ JPH::Vec3`, `glm::quat ↔ JPH::Quat`, `glm::mat4 ↔ JPH::Mat44` (column-major preserving). `jolt_helpers.h:19` |

## 4. Public API

The subsystem has eleven primary public headers — facade-style API. Headers below are the legitimate `#include` targets for downstream code (per CODING_STANDARDS §18). Group by header:

**`physics_world.h`** — the central facade.

```cpp
struct PhysicsWorldConfig { /* fixedTimestep, maxBodies, threadCount, ... */ };

class PhysicsWorld {
    bool        initialize(const PhysicsWorldConfig& = {});
    void        shutdown();                                     // idempotent
    void        update(float dt);                               // accumulator → Update at fixedTimestep

    // Body lifecycle (CODING_STANDARDS §30: never call Jolt's BodyInterface directly).
    JPH::BodyID createStaticBody   (const JPH::Shape*, const vec3&, const quat& = {});
    JPH::BodyID createDynamicBody  (const JPH::Shape*, const vec3&, const quat& = {},
                                     float mass=1, float friction=0.5, float restitution=0.3);
    JPH::BodyID createKinematicBody(const JPH::Shape*, const vec3&, const quat& = {});
    void        destroyBody(JPH::BodyID);

    // Body queries / mutators.
    vec3        getBodyPosition(JPH::BodyID) const;
    quat        getBodyRotation(JPH::BodyID) const;
    void        setBodyTransform(JPH::BodyID, const vec3&, const quat&);
    void        applyForce       (JPH::BodyID, const vec3&);
    void        applyImpulse     (JPH::BodyID, const vec3&);
    void        applyImpulseAtPoint(JPH::BodyID, const vec3& impulse, const vec3& worldPoint);
    JPH::EMotionType getBodyMotionType(JPH::BodyID) const;
    unsigned    getActiveBodyCount() const;

    // Raycasts. Prefer the maxDistance overload — see "Non-obvious contract details".
    bool        rayCast(const vec3& origin, const vec3& direction,        // legacy
                         JPH::BodyID& outBody, float& outFraction) const;
    bool        rayCast(const vec3& origin, const vec3& direction,        // Phase 10.9 Ph2
                         float maxDistance,
                         JPH::BodyID& outBody, float& outHitDistance,
                         JPH::BodyID ignoreBodyId = {}) const;

    // Constraints.
    ConstraintHandle addHingeConstraint   (BodyID a, BodyID b, vec3 pivot, vec3 hingeAxis,
                                            vec3 normalAxis, float minDeg=-180, float maxDeg=180,
                                            float maxFrictionTorque=0);
    ConstraintHandle addFixedConstraint   (BodyID a, BodyID b);
    ConstraintHandle addDistanceConstraint(BodyID a, BodyID b, vec3 ptA, vec3 ptB,
                                            float minDist=-1, float maxDist=-1,
                                            float springFreq=0, float springDamp=0);
    ConstraintHandle addPointConstraint   (BodyID a, BodyID b, vec3 pivot);
    ConstraintHandle addSliderConstraint  (BodyID a, BodyID b, vec3 slideAxis,
                                            float minLim=-1, float maxLim=1,
                                            float maxFrictionForce=0);
    PhysicsConstraint*       getConstraint(ConstraintHandle);
    void                     removeConstraint(ConstraintHandle);
    void                     removeConstraintsForBody(JPH::BodyID);
    void                     checkBreakableConstraints(float dt);   // call once per frame after update()
    std::vector<ConstraintHandle> getConstraintHandles() const;     // sorted index order

    // Determinism helper (Phase 10.9 Ph7): Hughes-Möller orthonormal basis from a slide axis.
    static vec3 computeSliderNormalAxis(const vec3& slideAxis);

    // Escape hatches (advanced; avoid in feature code).
    JPH::PhysicsSystem*  getSystem();
    JPH::BodyInterface&  getBodyInterface();      // locking variant
    JPH::TempAllocator*  getTempAllocator();
    const BroadPhaseLayerMapping&  getBroadPhaseMapping() const;
    const ObjectLayerPairFilter&   getObjectPairFilter() const;
    const ObjectVsBroadPhaseFilter& getObjectVsBroadPhaseFilter() const;
};
```

**`rigid_body.h`** — entity component.

```cpp
enum class BodyMotionType : uint8_t   { STATIC, DYNAMIC, KINEMATIC };
enum class CollisionShapeType : uint8_t { BOX, SPHERE, CAPSULE, CONVEX_HULL, MESH };

class RigidBody : public Component {
    BodyMotionType      motionType   = STATIC;
    CollisionShapeType  shapeType    = BOX;
    vec3                shapeSize    = {0.5, 0.5, 0.5};   // half-extents / radius
    float               mass         = 1, friction = 0.5, restitution = 0.3;
    std::vector<vec3>     collisionVertices;              // CONVEX_HULL or MESH
    std::vector<uint32_t> collisionIndices;               // MESH only

    void setCollisionMesh(const vec3* p, size_t n, const uint32_t* idx=nullptr, size_t nIdx=0);
    void createBody(PhysicsWorld&);                       // call after Transform is valid
    void destroyBody();                                   // safe at any time
    void syncTransform();                                 // dynamic: phys→ent; kinematic: ent→phys
    void addForce  (const vec3&);                         // Newtons
    void addImpulse(const vec3&);                         // N·s
    bool       hasBody() const;
    JPH::BodyID getBodyId() const;
};
```

**`physics_character_controller.h`** — capsule-based first-person body.

```cpp
struct PhysicsControllerConfig { /* capsuleRadius, capsuleHalfHeight, eyeHeight, maxSlopeAngle,
                                    mass, maxStrength, characterPadding, penetrationRecoverySpeed,
                                    predictiveContactDistance, stairStepUp, stickToFloorDistance,
                                    gravity, inputSmoothing */ };

class PhysicsCharacterController {
    bool initialize(PhysicsWorld&, const vec3& feetPos, const PhysicsControllerConfig& = {});
    void shutdown();
    void update(float dt, const vec3& desiredVelocity);   // FPC supplies desiredVelocity each frame

    vec3 getPosition()    const;                          // feet
    void setPosition(const vec3& feetPos);                // teleport
    vec3 getEyePosition() const;                          // feet + eyeHeight
    vec3 getLinearVelocity() const;
    bool isOnGround()     const;
    bool isOnSteepGround() const;
    bool isInAir()        const;
    void setFlyMode(bool); bool isFlyMode() const;
};
```

**`physics_constraint.h`** — handle + wrapper.

```cpp
struct ConstraintHandle { uint32_t index; uint32_t generation; bool isValid() const; };

class PhysicsConstraint {
    ConstraintType  getType() const;
    ConstraintHandle getHandle() const;
    JPH::BodyID     getBodyA() const, getBodyB() const;        // bodyA may be sFixedToWorld
    void            setEnabled(bool);  bool isEnabled() const;
    void            setBreakForce(float N); float getBreakForce() const; // 0 = unbreakable
    float           getCurrentForce() const;                   // last solver iteration
    JPH::HingeConstraint*    asHinge();      // type-safe accessors; null on wrong type
    JPH::FixedConstraint*    asFixed();
    JPH::DistanceConstraint* asDistance();
    JPH::PointConstraint*    asPoint();
    JPH::SliderConstraint*   asSlider();
    JPH::TwoBodyConstraint*  getJoltConstraint();              // raw escape
};
```

**`physics_layers.h`** — collision filtering. Layer constants only; classes are construction details.

```cpp
namespace ObjectLayers      { /* STATIC=0, DYNAMIC=1, CHARACTER=2, TRIGGER=3 */ }
namespace BroadPhaseLayers  { /* STATIC, DYNAMIC, CHARACTER (CHARACTER + TRIGGER share DYNAMIC bp) */ }
class BroadPhaseLayerMapping  : public JPH::BroadPhaseLayerInterface { /* … */ };
class ObjectLayerPairFilter   : public JPH::ObjectLayerPairFilter   { /* … */ };
class ObjectVsBroadPhaseFilter: public JPH::ObjectVsBroadPhaseLayerFilter { /* … */ };
```

**`cloth_solver_backend.h`** — polymorphic cloth contract shared by both backends.

```cpp
inline constexpr int MAX_SUBSTEPS = 64;       // enforced on both backends since Phase 10.9 Cl7
enum class ClothWindQuality { FULL, APPROXIMATE, SIMPLE };

class IClothSolverBackend {
    virtual void initialize(const ClothConfig&, uint32_t seed = 0) = 0;
    virtual void simulate(float dt) = 0;
    virtual void syncBuffersOnly() = 0;        // refresh normals / mirrors w/o integrating (Phase 10.9 Cl2)
    virtual void reset() = 0;
    // Read-back, topology, live-tuning, wind, pins, LRA, colliders — see header for the full surface
    // (~40 methods total). Cylinder / box / mesh colliders are CPU-only; GPU drops them with a warning.
};
```

**`cloth_simulator.h`** + **`gpu_cloth_simulator.h`** — concrete backends; all runtime selection goes through `IClothSolverBackend`.

**`cloth_backend_factory.h`** — auto-selection.

```cpp
constexpr uint32_t GPU_AUTO_SELECT_THRESHOLD = 1024;             // ≈ 32 × 32 grid
enum class ClothBackendPolicy { AUTO, FORCE_CPU, FORCE_GPU };
enum class ClothBackendChoice { CPU, GPU };

ClothBackendChoice chooseClothBackend(const ClothConfig&, ClothBackendPolicy, bool gpuSupported);
std::unique_ptr<IClothSolverBackend> createClothSolverBackend(
    const ClothConfig&, ClothBackendPolicy, const std::string& shaderPath);
```

**`cloth_component.h`** — entity wrapper.

```cpp
class ClothComponent : public Component {
    void initialize(const ClothConfig&, std::shared_ptr<Material>, uint32_t seed = 0);
    void update(float dt) override;                              // 60 Hz fixed inner step, ≤ 4 substeps/frame
    IClothSolverBackend& getSimulator();                         // polymorphic — backend-agnostic
    void setBackendPolicy(ClothBackendPolicy);                   // takes effect on next initialize()
    void setShaderPath(const std::string&);                      // GPU backend asset path
    DynamicMesh& getMesh();
    void syncMesh();                                             // calls backend.syncBuffersOnly()
    void reset();
    void applyPreset(ClothPresetType);
};
```

**`cloth_presets.h`** + **`fabric_material.h`** — preset / fabric tables.

**`physics_debug.h`** — debug overlay.

```cpp
class PhysicsDebugDraw {
    void draw(const PhysicsWorld&, DebugDraw&, const Camera&, float aspectRatio);
    void setEnabled(bool); bool isEnabled() const;
};
```

**`bvh.h`**, **`spatial_hash.h`**, **`cloth_mesh_collider.h`**, **`cloth_constraint_graph.h`**, **`collider_generator.h`**, **`deformable_mesh.h`**, **`jolt_helpers.h`** — supporting primitives, see header for full surface.

**Non-obvious contract details:**

- **Single-instance, single-thread.** `PhysicsWorld` owns Jolt globals (`Factory::sInstance`, registered types, default allocator). Constructing two `PhysicsWorld` instances simultaneously is **undefined behaviour** — Jolt's globals don't support it. The engine creates exactly one inside `Engine`. (Tests work around this by sharing a fixture or accepting that two worlds in sequence are fine.)
- **Fixed-step accumulator.** `PhysicsWorld::update(dt)` accumulates real time and steps Jolt at exactly `fixedTimestep` (default 1/60 s). The accumulator is clamped to `4 × fixedTimestep` to defeat the "spiral of death" — pause / breakpoint / hitch survives, longer freeze drops time. **Never** call `JPH::PhysicsSystem::Update` from feature code (CODING_STANDARDS §30).
- **Body IDs use `IsInvalid()`, not `== 0`.** `BodyID(0)` is a valid live body; the sentinel is the default-constructed `JPH::BodyID()`. `RigidBody::hasBody()` and `ConstraintHandle::isValid()` honour this.
- **Constraint storage is `std::map<uint32_t, PhysicsConstraint>`** (not `unordered_map`). Phase 10.9 Ph6 made this deterministic on purpose: hash-dependent iteration order would break replay (Phase 11A) and break-order tests. Per-slot `generation` lives on the handle, not a global counter; indices are not reused.
- **Slider `normalAxis` uses Hughes-Möller orthonormalisation** (Phase 10.9 Ph7). `computeSliderNormalAxis(slideAxis)` returns a unit vector perpendicular to `slideAxis` whose value depends only on `slideAxis` — no world-axis bias. The pre-Ph7 code compared against world Y, so two scenes with identical geometry rotated 90° solved differently.
- **Raycast — prefer the `maxDistance` overload** (Phase 10.9 Ph2). The legacy `(origin, direction, body, fraction)` overload encourages the `dir = unit * range; hitPoint = origin + unit * fraction * range` double-scaling pattern. The new overload separates direction (unit) from `maxDistance` (world units), writes `outHitDistance` in world units, and accepts `ignoreBodyId` for self-exclusion (combat / grab raycasts).
- **`BodyInterface` is the locking variant.** All `PhysicsWorld` body operations route through it, so concurrent calls from multiple threads are technically safe — but the engine treats `PhysicsWorld` as main-thread-only anyway (see §7).
- **Cloth backend selection happens at `ClothComponent::initialize()`.** Switching policy after init has no effect until re-init; `setBackendPolicy()` is "sticky for next init" by design.
- **`syncBuffersOnly()` does not integrate.** Phase 10.9 Cl2 fix — `ClothComponent::syncMesh()` previously called `simulate(0.0001f)` to refresh normals after a pin drag, silently injecting a 100 µs gravity tick. Always call `syncBuffersOnly()` for read-back-only refreshes.
- **GPU cloth context-affinity.** `GpuClothSimulator::initialize()` and the destructor require a current OpenGL 4.5 context. `GpuClothSimulator::isSupported()` is the no-context-safe probe — the factory uses it before instantiating.
- **Float determinism:** physics + cloth translation units must not compile with `-ffast-math` / `/fp:fast` (CODING_STANDARDS §30). Phase 11A replay parity depends on bit-identical IEEE-754 behaviour.

**Stability:** the facade above is semver-frozen for `v0.x`. Two known evolution points: (a) Phase 10.9 Ph3 will add `PhysicsWorld::sphereCast` for Phase 10.8 third-person camera wall-probing — additive; (b) `RigidBody::syncTransform` may move from the matrix-override path back to a quaternion-native `Transform` once the engine-wide Euler→quaternion migration lands (Phase 10.9 Ph9 shipped the matrix-override stepping stone — see Open Q4).

## 5. Data Flow

**Steady-state per-frame (engine main loop — `engine/core/engine.cpp:1211`):**

1. `Engine::run()` → `Timer::update()` → `dt`.
2. `Window::pollEvents()` and `InputManager::update()` (engine/core).
3. `FirstPersonController::update(dt, …)` reads input → computes desired XZ velocity. If physics character controller is enabled (`m_usePhysicsController`), the FPC runs `processLookOnly(dt)` (camera/yaw/pitch only) and the desired velocity is fed to `PhysicsCharacterController::update(dt, desiredVel)` instead.
4. `PhysicsWorld::update(dt)` →
   1. accumulator += dt; clamp to `4 × fixedTimestep`.
   2. while accumulator ≥ fixedTimestep: `m_physicsSystem->Update(fixedTimestep, collisionSteps, tempAlloc, jobSystem)`; accumulator -= fixedTimestep.
5. `PhysicsWorld::checkBreakableConstraints(dt)` — walk `m_constraints` in sorted index order, sample per-constraint solver lambda, disable any whose force exceeds threshold. (Phase 10.9 Ph1 will move this *inside* the fixed-step loop and divide by `fixedTimestep`, not frame `dt`. Currently runs at variable rate — Open Q1.)
6. For every entity with a `RigidBody` component: `syncTransform()` — `DYNAMIC` writes `Transform` from Jolt; `KINEMATIC` writes Jolt from `Transform`; `STATIC` is no-op after creation.
7. `SystemRegistry::updateAll(dt)` walks domain systems in `UpdatePhase` order. Cloth components run inside their owning system's update.
8. For every `ClothComponent::update(dt)`:
   1. `m_timeAccumulator += dt`; clamp to `MAX_STEPS_PER_FRAME = 4` × `FIXED_DT`.
   2. while accumulator ≥ `FIXED_DT`: `m_simulator->simulate(FIXED_DT)`; accumulator -= `FIXED_DT`.
   3. Copy `m_simulator->getPositions()` + `getNormals()` into the vertex buffer; upload to `DynamicMesh`.
9. Renderer renders.

**Cloth substep (CPU XPBD — `ClothSimulator::simulate`):**

1. `precomputeWind()` — cache per-particle noise + per-triangle turbulence (once per `simulate()`, reused across all substeps).
2. For each substep `s` of `clamp(config.substeps, 1, MAX_SUBSTEPS)`:
   1. `applyWind(dtSub)` — read cached values; aerodynamic drag per triangle.
   2. Predict positions (Verlet): `pos += vel * dtSub + gravity * dtSub²; prevPos ← pos`.
   3. Solve distance constraints in BFS-depth order (stretch → shear → bend). `solveDistanceConstraint(c, αTilde, dtSub)` computes XPBD position correction.
   4. Solve dihedral bending constraints.
   5. Solve LRA (Long-Range Attachment) tethers — unilateral; only activates if particle drifts past `maxDistance`.
   6. Solve pins (clamp pinned particles to fixed positions; inverse mass = 0).
   7. Apply collisions: ground plane, planes, spheres, cylinders, boxes, mesh-BVH (Phase 8F), self-collision via spatial hash if enabled.
   8. Update velocities: `vel = (pos - prevPos) / dtSub * (1 - damping)`.
3. `recomputeNormals()` for the renderer.
4. Sleep check: if avg kinetic energy < `sleepThreshold` for `SLEEP_FRAME_COUNT = 3` consecutive frames, freeze.

**Cloth substep (GPU compute — `GpuClothSimulator::simulate`):**

Identical XPBD math, dispatched as a chain of compute shaders per substep:
1. `cloth_wind.comp` (force accumulation).
2. `cloth_integrate.comp` (Verlet predict + damping; respects `invMass = 0` for pins).
3. `cloth_constraints.comp` — one dispatch per colour group from greedy graph colouring (no two constraints in a colour share a particle → no atomics).
4. `cloth_dihedral.comp` — one dispatch per dihedral colour group.
5. `cloth_lra.comp` — single dispatch (each LRA writes only its own particle).
6. `cloth_collision.comp` (spheres + planes + ground; cylinder / box / mesh CPU-only).
7. `cloth_normals.comp` (recompute per-vertex normals).

`getPositions()` / `getNormals()` lazily refresh CPU mirror via `glGetNamedBufferSubData` when called between simulate steps; the renderer reads the SSBOs directly once Phase 9B Step 8 lands (see Open Q3).

**Cold start (`Engine::initialize` — physics slice):**

1. `PhysicsWorld::initialize(config)` →
   1. `JPH::RegisterDefaultAllocator()`; install `Trace` + `AssertFailed` handlers (route to `Logger`).
   2. `JPH::Factory::sInstance = new JPH::Factory();` `JPH::RegisterTypes();`.
   3. `TempAllocatorImpl(10 MB)`, `JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, threadCount)`.
   4. `PhysicsSystem::Init(maxBodies=4096, numBodyMutexes=auto, maxBodyPairs=4096, maxContactConstraints=2048, layerMapping, objVsBpFilter, objPairFilter)`.
   5. Set fixed-step parameters; mark initialised.
2. Engine creates static collider for the ground + every static block in the demo scene.
3. `PhysicsCharacterController::initialize(world, feetPos, config)` allocates the `CharacterVirtual` Ref.
4. Per-scene cloth components call `ClothComponent::initialize(config, material, seed)` →
   1. Factory probes `GpuClothSimulator::isSupported()` and the particle count → returns CPU XPBD or GPU compute backend.
   2. Backend `initialize(config, seed)` allocates particle / constraint / collider state.
   3. Mesh built; vertex buffer pre-sized.

**Shutdown (`PhysicsWorld::shutdown`):**

1. Walk `m_constraints` in sorted-index order; `RemoveConstraint` each, then clear the map.
2. `BodyInterface::GetBodies` → `RemoveBody` + `DestroyBody` for every survivor.
3. Reset `physicsSystem`, `jobSystem`, `tempAllocator` unique_ptrs (RAII destruction).
4. `JPH::UnregisterTypes()`; `delete JPH::Factory::sInstance; sInstance = nullptr`.
5. Mark uninitialised.

`shutdown()` is idempotent and called by `~PhysicsWorld()`.

**Exception path:** `PhysicsWorld::initialize` returns `true` even on Jolt-side recoverable issues; only `std::bad_alloc` from the allocator can propagate. `PhysicsCharacterController::initialize` returns `false` if `world.isInitialized() == false`. Cloth `initialize` rejects non-finite `particleMass` / `spacing` / `damping` / `gravity` (Phase 10.9 Cl8) and logs a warning; the component stays in an `isReady() == false` state.

## 6. CPU / GPU placement

Per CLAUDE.md Rule 7. Physics core is **CPU**; cloth runs on **CPU XPBD by default** with a **GPU compute backend** at scale (Phase 9B). Both backends share the `IClothSolverBackend` contract.

| Workload | Placement | Reason |
|----------|-----------|--------|
| Rigid-body broadphase + narrowphase + solver (Jolt) | CPU (multi-threaded) | Branching, sparse, decision-heavy; Jolt's job system parallelises across CPU cores. Per CODING_STANDARDS §17 heuristic. |
| Character controller (`CharacterVirtual` capsule cast / collide / depenetrate) | CPU (main thread) | Sparse, contact-driven, decision-heavy; Jolt processes per-character on the calling thread. |
| Constraint solving (rigid joints) | CPU (Jolt's island solver) | Iterative; Jolt schedules across worker threads via the `JobSystemThreadPool`. |
| Raycast / shape cast (rigid) | CPU (main thread) | Read-only against the Jolt broadphase — needs the locking `BodyInterface`. |
| Cloth XPBD particle integration / constraint solve / collision (small grids ≤ 1024 particles) | CPU (`ClothSimulator`) | Per-particle is data-parallel in principle, but at small N the dispatch overhead beats the parallelism win — see Phase 9B design § 4. |
| Cloth XPBD particle integration / constraint solve / collision (large grids > 1024 particles) | GPU compute (`GpuClothSimulator`, GLSL `cloth_*.comp.glsl`) | Per-particle / per-constraint / per-triangle are exactly the per-element data-parallel shapes the §17 heuristic puts on the GPU. Greedy graph colouring lets distance + dihedral constraints solve atomic-free. |
| Cloth normal recomputation | Same backend as the simulation | Per-vertex; runs in `cloth_normals.comp` on GPU, plain loop on CPU. |
| Cloth-mesh BVH build + refit | CPU | Build is one-shot, branching; refit is O(N) bottom-up. |
| Cloth self-collision broad-phase (spatial hash) | CPU | Counting-sort on the host side; cheap rebuild per substep. (GPU spatial hash deferred.) |
| Backend auto-selection (`chooseClothBackend`) | CPU | One-shot decision at `ClothComponent::initialize()`. |
| Debug wireframe (`PhysicsDebugDraw`) | CPU vertex generation, GPU rasterisation | Branching collider iteration on CPU; rendering goes through `engine/renderer/debug_draw.h`. |

**Dual implementation:** `ClothSimulator` (CPU, the spec) and `GpuClothSimulator` (GPU, the runtime above 1024 particles) implement the same `IClothSolverBackend` contract. Both run XPBD with identical mathematical structure (same compliance-formulation, same Jacobi-style position correction per colour group). **The Phase 10.9 Cl1 parity test does not yet exist on disk** — it will land at `tests/test_cloth_simulator_parity.cpp` and drive identical `ClothConfig` on both backends for 2 s of simulated time and assert per-particle position delta < ε. Tracked as §15 Open Q6 (Cl1). Until that test ships, parity is held by visual inspection plus the per-component tests (`test_cloth_simulator.cpp`, `test_gpu_cloth_simulator.cpp`) that exercise each backend's behaviour against a deterministic CPU reference computation.

## 7. Threading model

Per CODING_STANDARDS §13. Two layers of threading apply to this subsystem: the engine's external rule (who can call into `engine/physics`) and Jolt's internal job-system rule (how Jolt parallelises work across worker threads on its own).

**External rule — `engine/physics` is main-thread-only.**

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the only thread that runs `Engine::run`) | All of `PhysicsWorld`, `RigidBody::*`, `PhysicsCharacterController`, every constraint API, `ClothComponent::*`, `ClothSimulator::*`, `GpuClothSimulator::*`, `PhysicsDebugDraw::draw`, `BVH::*`, `SpatialHash::*`. | None at the engine level; Jolt's `BodyInterface` is the locking variant internally. |
| **Worker threads** (engine job system, audio thread, async loaders) | None — must not call into `engine/physics` directly. | n/a |
| **Jolt's own worker threads** (inside `JobSystemThreadPool`, spawned by `PhysicsWorld`) | Jolt-internal only — never re-enter engine code from a Jolt job. | Jolt-internal latches / barriers. |

**Internal rule — Jolt's job system parallelises rigid-body work.**

`PhysicsWorld::initialize` constructs a `JobSystemThreadPool` with `max(1, hardware_concurrency() - 1)` workers (configurable via `PhysicsWorldConfig::threadCount`). On every `m_physicsSystem->Update(...)` call, Jolt fans out broadphase / narrowphase / island-solver work across those workers, joins back, and returns synchronously to the main thread. The engine never sees the workers; they're owned by `PhysicsWorld` and torn down on `shutdown()`.

**Why main-thread-only at the engine layer.** GLFW + the OpenGL 4.5 context are main-thread-bound (per the GLFW manual), and `GpuClothSimulator` requires the GL context to be current — pushing that work to a worker would break it. For CPU rigid-body work, exposing `PhysicsWorld` mutators to other threads adds lock contention with no measurable win at the body counts we ship (≤ 4096). The body interface Jolt exposes (`getBodyInterface()`) is the locking variant, so a future cross-thread caller is technically possible — but until a real consumer demands it, "main thread only" is the documented contract.

**Lock-free / atomic:** none required at the engine layer. Jolt-internal mutexes scale with `numBodyMutexes` (default 0 = auto = one per ~5 bodies); the engine does not configure a custom value.

**Cloth threading.** `ClothSimulator::simulate` is single-threaded inside the substep loop. `GpuClothSimulator::simulate` is single-threaded on the host (just dispatches compute shaders); the actual parallelism is in the GPU dispatch.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. Physics is allocated **~3 ms** on dev hardware (RX 6600 + Ryzen 5 5600), leaving ~13 ms for renderer + everything else. Cloth comes out of that 3 ms when active.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `PhysicsWorld::update` (≤ 32 dynamic bodies, demo scene) | < 0.8 ms | TBD — Phase 11 audit |
| `PhysicsWorld::update` (100 dynamic bodies + 10 constraints) | < 2.0 ms | TBD — Phase 11 audit |
| `PhysicsWorld::checkBreakableConstraints` (≤ 16 breakables) | < 0.05 ms | TBD — Phase 11 audit |
| `PhysicsCharacterController::update` (per character, walk + slope + step) | < 0.2 ms | TBD — Phase 11 audit |
| `PhysicsWorld::rayCast` (single, demo scene broadphase) | < 0.05 ms | TBD — Phase 11 audit |
| `RigidBody::syncTransform` (per-component, dynamic) | < 0.005 ms | TBD — Phase 11 audit |
| `ClothSimulator::simulate` (32 × 32 grid, 10 substeps) | < 1.5 ms | TBD — Phase 11 audit |
| `ClothSimulator::simulate` (64 × 64 grid, 10 substeps) | < 4.0 ms (would push frame budget — auto-selects GPU above 1024 particles) | TBD — Phase 11 audit |
| `GpuClothSimulator::simulate` (64 × 64 grid, 10 substeps, no readback) | < 0.6 ms | TBD — Phase 11 audit |
| `GpuClothSimulator::getPositions` (with mirror readback) | < 0.5 ms (one-shot when called) | TBD — Phase 11 audit |
| `BVH::build` (Tabernacle wall mesh, ~5k tris) | < 20 ms (one-shot, scene-load) | TBD — Phase 11 audit |
| `BVH::refit` (animated mesh, ~5k tris) | < 0.3 ms | TBD — Phase 11 audit |
| `BVH::queryClosest` (single point) | < 0.02 ms | TBD — Phase 11 audit |
| `SpatialHash::build` (1024-particle cloth) | < 0.05 ms | TBD — Phase 11 audit |
| `PhysicsWorld::initialize` (one-shot) | < 50 ms | TBD — Phase 11 audit |
| `ClothBackendFactory::createClothSolverBackend` (no GL touch path) | < 1 ms | TBD — Phase 11 audit |

Profiler markers / capture points: `PhysicsDebugDraw::draw` emits a `glPushDebugGroup("PhysicsDebugDraw")`. `GpuClothSimulator::simulate` wraps each compute dispatch in `glPushDebugGroup("ClothCompute::<phase>")` so RenderDoc captures show the wind / integrate / constraints / dihedral / LRA / collision / normals chain explicitly. Logger emits a single `Info` line per `PhysicsWorld::initialize` containing thread count and timestep.

Stress harnesses: `tests/test_advanced_physics.cpp` exercises constraint chains; `tests/test_cloth_solver_improvements.cpp` exercises 64 × 64 cloth at full substep count. Neither runs in the 60 FPS budget — they're correctness regressions, not performance gates.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (RAII via `std::unique_ptr`); Jolt's internal `TempAllocatorImpl` is a 10 MB pre-allocated pool reused across every `PhysicsSystem::Update` call (no per-frame heap traffic on the steady-state path). |
| Per-frame transient | None. Both backends keep all working buffers across frames. CPU cloth `m_selfCollisionNeighbors` and `m_cachedParticleWind` retain capacity so per-substep work doesn't heap-alloc. GPU cloth retains all SSBOs / UBOs. |
| Peak working set | Jolt: ~10 MB temp pool + body table (~256 bytes / body × 4096 max ≈ 1 MB) + broadphase index ≈ **12 MB** for `maxBodies = 4096`. Cloth: 32 × 32 grid ≈ 1024 particles × (3 vec3 + 2 floats + indices) ≈ **80 KB** CPU; SSBOs ≈ 200 KB GPU. BVH: 4 tris/leaf, ~50 bytes/node ≈ **0.5 MB** for a 5k-tri mesh. **Subsystem total: < 20 MB** under the demo scene. |
| Ownership | `Engine` owns the single `PhysicsWorld` by value (`engine.h:142`). `PhysicsWorld` owns Jolt resources via `unique_ptr`. `RigidBody` (component) holds a non-owning `PhysicsWorld*` + a Jolt `BodyID`. `ClothComponent` owns its `IClothSolverBackend` via `unique_ptr`. `ClothMeshCollider` is referenced by non-owning pointer from `ClothSimulator::m_meshColliders` — caller manages lifetime (CODING_STANDARDS §22 — explicit, documented). |
| Lifetimes | `PhysicsWorld` is engine-lifetime. Bodies + constraints + character controllers are scene-lifetime (created on scene load, destroyed on scene unload via `RigidBody::destroyBody` and `removeConstraintsForBody`). Cloth components are scene-lifetime. Jolt globals (`Factory::sInstance`, registered types) are engine-lifetime (created in `PhysicsWorld::initialize`, torn down in `shutdown`). |

No `new`/`delete` in feature code (CODING_STANDARDS §12). Jolt's `RegisterDefaultAllocator()` routes Jolt-internal allocations through standard malloc/free. The 10 MB `TempAllocator` size is a Jolt convention (fits the largest expected per-step working set for 4k bodies); revisit if `maxBodies` ever grows beyond ~16k.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions on the steady-state path.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| `PhysicsWorld::initialize` failed (e.g. Jolt allocator OOM) | `std::bad_alloc` propagates | App aborts; matches §11 — OOM is fatal during init. |
| `PhysicsWorld::initialize` called twice | `Logger::warning("PhysicsWorld already initialized")` + return `true` | Idempotent — caller sees success, no state change. |
| `RigidBody::createBody` with `MESH` shape but no indices | `Logger::error` + body not created (`m_bodyId.IsInvalid() == true`) | Caller checks `hasBody()`. |
| `RigidBody::createBody` with `CONVEX_HULL` and < 4 vertices | Jolt rejects via assert (debug) / produces an empty hull (release) | Fix the caller — degenerate input. |
| `RigidBody::syncTransform` called before `createBody` | No-op (defensive `hasBody()` guard) | None — silent fast path. |
| `PhysicsCharacterController::initialize` with uninitialised world | Returns `false` + `Logger::error` | Caller defers character creation until `world.isInitialized()`. |
| `ConstraintHandle` is stale (slot freed + bumped generation) | `getConstraint(handle) == nullptr`; `removeConstraint` is no-op | Caller treats stale handle as invalid; no crash. |
| Constraint creation with two `Body::sFixedToWorld` bodies | Logger warning + `ConstraintHandle{}` (invalid) returned | Caller fixes the call. |
| Breakable constraint exceeded threshold | Constraint disabled in-place; `isEnabled() == false`; reported via `getConstraint(...)->isEnabled()` | Game code subscribes to a per-constraint "broke" signal (currently polled, not event-bus — Open Q2). |
| `rayCast` with zero-length direction or `maxDistance ≤ 0` | Returns `false`; `outBodyId.IsInvalid() == true` | Caller treats as "no hit" — Phase 10.9 Ph2 pinned the contract in `Ph2_ZeroOrNegativeRangeMisses`. |
| `BVH::build` with mismatched vertex / index counts | `Logger::error`; `isBuilt() == false` | Caller checks `isBuilt()`. |
| Cloth `initialize` with non-finite `particleMass` / `spacing` / `damping` / `gravity` (Phase 10.9 Cl8) | `Logger::error` + early return; component stays `isReady() == false` | Caller fixes the config. |
| Cloth `simulate` with `substeps > MAX_SUBSTEPS` (silently) | Internally clamped; `setSubsteps` on either backend also clamps | None — defensive cap (Phase 10.9 Cl7). |
| GPU cloth `initialize` with no GL context | `Logger::error`; falls back via factory to CPU backend | None — factory handles it. |
| GPU cloth shader compile / link failed | `Logger::error`; `hasShaders() == false`; `simulate()` is a no-op (skeleton path) | Asset path mis-set; check `setShaderPath()` matches install layout. |
| Cylinder / box / mesh collider added to GPU backend | One-time `Logger::warning`; collider dropped | Use the CPU backend if these colliders are required (or wait on Phase 9B Step ≥ 11 — Open Q3). |
| Save / replay determinism break (Phase 11A only) | Detected at replay time via per-tick state hash mismatch | Bisect physics translation units for `-ffast-math` regression; re-run with `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED`. |
| Programmer error (null shape ptr, OOB body ID) | `JPH_ASSERT` (debug) / UB (release), routed through `joltAssertFailed` → `Logger::error` | Fix the caller. |
| Out of memory (`std::bad_alloc` from any allocator) | Propagates | App aborts (matches CODING_STANDARDS §11). |

`Result<T, E>` / `std::expected` not yet used in this subsystem — the codebase predates the policy. Migration is on the engine-wide list (Open Q4).

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Public API contract — body create / destroy / move / impulse | `tests/test_physics_world.cpp` | Public API contract |
| Raycast Ph2 contract — world-unit distance, self-exclude, zero-range | `tests/test_physics_world.cpp::PhysicsWorldRayCast.*` | Regression (Phase 10.9 Ph2) |
| Constraints — every type + breakable + handle generation + deterministic order | `tests/test_physics_constraint.cpp` | Public API + Ph6 / Ph7 determinism |
| Character controller — capsule motion, slope limits, fly mode | `tests/test_physics_character_controller.cpp` | Public API contract |
| Rigid body component — sync direction, motion-type semantics, Ph9 quaternion-exact | `tests/test_rigid_body.cpp` | Component contract |
| Cloth simulator core — XPBD step, pins, LRA, sleep, wind, collisions, NaN guard | `tests/test_cloth_simulator.cpp` | Public API + Cl2 / Cl7 / Cl8 |
| Cloth backend interface — every backend honours the IClothSolverBackend contract | `tests/test_cloth_solver_backend.cpp` | Polymorphic contract parity (proxy until Cl1 parity test) |
| Cloth backend factory — auto / force / fallback policy | `tests/test_cloth_backend_factory.cpp` | Policy table + GL-context fallback |
| Cloth constraint graph + colouring | `tests/test_cloth_constraint_graph.cpp` | Stretch / shear / bend generation; greedy colouring; LRA; dihedrals |
| Cloth presets + fabric DB | `tests/test_cloth_presets.cpp` | Preset → config mapping |
| Cloth collision (mesh / BVH) | `tests/test_cloth_collision.cpp` | Mesh-collider end-to-end |
| Cloth solver improvements — adaptive damping, friction, thick particles | `tests/test_cloth_solver_improvements.cpp` | Phase 8 cloth-improvements design |
| GPU cloth backend — SSBO lifecycle, pin upload, support probe | `tests/test_gpu_cloth_simulator.cpp` | Public API contract (skips when no GL) |
| Advanced physics integrations — character + constraints + cloth together | `tests/test_advanced_physics.cpp` | Smoke / integration |
| 2D physics (separate subsystem reach-through) | `tests/test_physics2d_system.cpp` | Out of scope here; listed for completeness |

**Adding a test for `engine/physics`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `PhysicsWorld` directly without an `Engine` instance — every primitive in this subsystem **except `GpuClothSimulator`** is unit-testable headlessly. GPU cloth tests use `engine/testing/visual_test_runner.h` to obtain a current GL 4.5 context and skip cleanly when none is available. Deterministic seeding uses `engine/utils/deterministic_lcg_rng.h` (cloth wind seed; same seed → bit-identical wind force on both CPU and GPU backends).

**Coverage gap:** the **CPU↔GPU cloth parity harness** (Phase 10.9 Cl1, Open Q6) is not yet shipped — the §6 dual-implementation rule cites it as the parity test, and until it lands we rely on per-backend tests + visual inspection. `PhysicsWorld::initialize` failure modes other than OOM are not exercised (Jolt's failure surface is small and assertion-driven, not return-code-driven). The full `Engine::run` loop is exercised through every other `tests/test_*` that links the engine library plus the visual-test harness.

## 12. Accessibility

`engine/physics` itself produces no user-facing pixels or sound — but it owns the **player character's relationship with geometry**, which surfaces three accessibility concerns downstream:

- **Reduced motion** (`Settings::accessibility::reducedMotion`) — when enabled, gameplay code should clamp camera shake (camera shake originates outside this subsystem; physics produces no extra motion). The character controller's `inputSmoothing` defaults to 0.75 — moderate smoothing — which already keeps motion gentle for partially-sighted players. Aggressive impulse responses (jump-pad style) should be gated by an accessibility check at the call site.
- **Photosensitive safety** (`PhotosensitiveSafetyWire` in `engine/core/settings.h`) — physics doesn't directly emit flashes, but breakable-constraint break events typically trigger particle / VFX bursts in gameplay code. Those bursts must respect the photosensitive caps; physics is responsible only for emitting the break signal cleanly enough that downstream VFX can throttle.
- **Input accessibility** — character controller exposes `inputSmoothing` and the FPC handles binding / dead-zone. The physics character controller takes a `desiredVelocity` already filtered by FPC — no rebindable inputs originate here.

Constraint summary for downstream UIs / gameplay code:
- Walking speed, jump height, fall damage thresholds: caller-controlled (game-side). Defaults pinned to Tabernacle-walkthrough scale (1.7 m eye height, 0.35 m stair-step).
- Slope-limit defaults to 50° — anything steeper than that is "unwalkable" terrain, used by gameplay code to gate accessible paths.
- Cloth wind: `windQuality = SIMPLE` is selectable per-component to remove all wind-driven motion (relevant for reduced-motion mode if the component is in the player's eyeline).

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/scene/component.h` | engine subsystem | `RigidBody` + `ClothComponent` derive from `Component`. |
| `engine/utils/deterministic_lcg_rng.h` | engine subsystem | Cloth wind randomness — replay-stable. |
| `engine/utils/aabb.h` | engine subsystem (transitive via core / scene) | Box collider math. |
| `engine/core/logger.h` | engine subsystem | Error / warn / info routing; Jolt `Trace` + `AssertFailed` shimmed through `Logger`. |
| `engine/renderer/dynamic_mesh.h`, `renderer/material.h`, `renderer/shader.h`, `renderer/debug_draw.h`, `renderer/camera.h`, `renderer/mesh.h` | engine subsystem | Cloth mesh upload, debug overlay, fabric material, GPU cloth shader load. **Dependency direction is one-way: physics depends on renderer for asset / draw plumbing; renderer does not depend on physics.** |
| `Jolt/Jolt.h` + `Jolt/Physics/...` — **pinned at v5.2.0** (`external/CMakeLists.txt:329`) | external (third-party) | Rigid-body simulation, constraints, character controller. Cross-platform character determinism (Windows / Linux / macOS within a single CPU architecture) was introduced in upstream Jolt 5.3.0 — **the engine has not yet bumped to 5.3.0**, so character-replay parity across hosts is not yet a guarantee here. Tracked in §15 (Jolt-version posture). |
| `<glm/glm.hpp>`, `<glm/gtc/quaternion.hpp>` | external | Engine-side math (vec3 / quat / mat4) — bridged to Jolt via `jolt_helpers.h`. |
| `<glad/gl.h>` | external | OpenGL 4.5 entry points (GPU cloth SSBOs / compute dispatch). |
| `<map>`, `<memory>`, `<vector>`, `<cstdint>`, `<thread>`, `<cmath>`, `<cstdarg>`, `<cstdio>`, `<string>` | std | Constraint storage, RAII, particle SoA, type widths, Jolt thread-count probe, Jolt trace formatting. |

**Direction:** `engine/physics` depends on `engine/scene`, `engine/utils`, `engine/core`, `engine/renderer`. Nothing in `engine/physics` includes `engine/scene/scene.h`, `engine/scene/scene_manager.h`, or `engine/editor/`. Downstream subsystems that depend on physics: `engine/core` (engine.h owns one `PhysicsWorld`), `engine/systems/cloth_system.h` (drives `ClothComponent::update`), `engine/editor/panels/*` (read `PhysicsWorld` state for inspector / debug). `engine/experimental/physics/` depends on this subsystem (uses `PhysicsWorld`) and is **out of scope** for this spec.

## 14. References

External cited sources (≤ 1 year old where possible):

- jrouwe / Jolt Physics 5.x **Release Notes** — engine pin is **v5.2.0**. The 5.3.0 / 5.4.0+ items below describe upstream features the engine has *not* yet adopted; they're cited here as the upgrade target, not as shipped behaviour. <https://jrouwe.github.io/JoltPhysicsDocs/5.1.0/md__docs__release_notes.html> · <https://github.com/jrouwe/JoltPhysics/blob/master/Docs/ReleaseNotes.md>
- Jorrit Rouwe. *Jolt Physics 5.3.0 release announcement (2025-03-15)* — cross-platform character determinism validation; **upstream-available, not yet in the engine pin**. <https://x.com/jrouwe/status/1901025550983946259>
- Miles Macklin, Matthias Müller, Nuttapong Chentanez. *XPBD: Position-Based Simulation of Compliant Constrained Dynamics* (MIG 2016) — the iteration-count and timestep-independent compliance formulation underpinning `ClothSimulator`. <https://matthias-research.github.io/pages/publications/XPBD.pdf>
- *MGPBD: A Multigrid Accelerated Global XPBD Solver* (arXiv 2025-05) — current-research benchmark; informs why the engine sticks with greedy-colour Jacobi for now (multigrid not justified at the cloth sizes shipped). <https://arxiv.org/html/2505.13390v1>
- *DiffXPBD: Differentiable Position-Based Simulation of Compliant Constraint Dynamics* (ACM 2023, ongoing line of work) — confirms XPBD remains the dominant cloth approach in 2025-era research. <https://dl.acm.org/doi/10.1145/3606923>
- Glenn Fiedler. *Fix Your Timestep!* — accumulator + spiral-of-death; same pattern in `PhysicsWorld::update`. <https://gafferongames.com/post/fix_your_timestep/>
- André Leite. *Taming Time in Game Engines: Fixed Timestep Game Loop* (2025) — modern restating of the accumulator pattern with cross-language code. <https://andreleite.com/posts/2025/game-loop/fixed-timestep-game-loop/>
- *Game Engines and Determinism* (Duality.ai blog, 2025) — fixed-timestep + audited float usage as the determinism foundation; Trackmania / Dreams cited as production references. <https://www.duality.ai/blog/game-engines-determinism>
- Wang et al. *Efficient BVH-based Collision Detection Scheme with Ordering and Restructuring* (CGF 2018) — binned-SAH BVH approach used in `BVH::build`. <https://min-tang.github.io/home/BVH-OR/>
- Tang et al. *PSCC: Parallel Self-Collision Culling with Spatial Hashing on GPUs* — referenced for the "spatial hash for many independent particles, BVH for deformable bodies" rule of thumb that guided the cloth split (spatial hash for particle self-collision; BVH for triangle-mesh colliders). <https://min-tang.github.io/home/PSCC/files/pscc.pdf>
- Teschner et al. *Optimized Spatial Hashing for Collision Detection of Deformable Objects* (VMV 2003) — the hash function in `SpatialHash`.
- Kim, Chentanez, Müller. *Long Range Attachments — A Method to Simulate Inextensible Clothing in Computer Games* (SCA 2012) — LRA tether constraints in `ClothSimulator`.
- Christer Ericson. *Real-Time Collision Detection*, ch. 5 — closest-point-on-triangle (Voronoi-region method) in `BVH::closestPointOnTriangle`.
- Jakub Tomšů. *Fixed timestep without interpolation* (2025) — confirms "no interpolation needed at 60 Hz physics + 60 FPS render" matches `PhysicsWorld`'s setup. <https://jakubtomsu.github.io/posts/fixed_timestep_without_interpolation/>
- ISO C++ Core Guidelines, *Concurrency* (CP.20–CP.43) — threading conventions referenced from CODING_STANDARDS §13. <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-concurrency>

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §27 (units), §30 (Jolt conventions — this spec must match), §29 (GPU debug markers).
- `ARCHITECTURE.md` §1–6 (subsystem map).
- `CLAUDE.md` rules 1, 5, 6, 7 (research-first, no workarounds, Formula Workbench for numerical design, CPU/GPU placement).
- `docs/phases/phase_08_design.md`, `phase_08c_design.md` (constraints), `phase_08d_design.md` (cloth XPBD), `phase_08e_design.md` (cloth polish + GPU research), `phase_08f_cloth_collision_design.md` (mesh BVH), `phase_08g_physics_foundation_design.md`, `phase_08_cloth_solver_improvements_design.md` — non-obvious rationale for shipped behaviour.
- `docs/phases/phase_09b_gpu_cloth_design.md` — full GPU cloth pipeline design.
- `ROADMAP.md` Phase 10.9 Slice 7 (Ph1–Ph9 physics determinism) and Slice 17 (Cl1–Cl8 cloth regressions) — most of the non-obvious recent contract changes are documented there.

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | **Ph1.** Move `checkBreakableConstraints` (and the character-controller update) inside the fixed-step loop. The breakable lambda should divide by `m_fixedTimestep`, not the variable frame `dt` — currently breaks feel inconsistent at variable frame rates. Tracked in ROADMAP §10.9 Slice 7. | milnet01 | Phase 10.9 Slice 7 close |
| 2 | **Ph3.** Add `PhysicsWorld::sphereCast(origin, direction, radius, maxDistance, ...)`. Originally Phase 10.8 CM3 (third-person camera wall-probe); pulled into Phase 10.9 so 10.8 consumes it instead of authoring it. | milnet01 | Phase 10.9 Slice 7 close |
| 3 | **Ph4.** Breakable-constraint force sums currently include linear lambdas only — extend to rotation lambdas (hinge limit) and slider position-limit lambdas; without them, hinge / slider limit breaks under-trigger. | milnet01 | Phase 10.9 Slice 7 close |
| 4 | **Ph5.** Character-vs-character pair filter: `CHARACTER` layer currently disallows internal collisions (so two characters pass through each other), which means a future ragdoll in the `CHARACTER` layer phases through the player. Split `PLAYER_CHARACTER` / `NPC_CHARACTER` or use Jolt collision groups. | milnet01 | Phase 10.9 Slice 7 close |
| 5 | **Ph8.** Constraint creation must take `BodyLockMultiWrite` on `{bodyA, bodyB}`. Today a raw `JPH::Body*` escapes a single-body `BodyLockWrite` scope (`physics_world.cpp:322-344`) and is used at multiple call sites outside the lock. UB under concurrent broadphase update. | milnet01 | Phase 10.9 Slice 7 close |
| 6 | **Cl1.** CPU↔GPU cloth parity harness: headless test that drives identical `ClothConfig` on both backends for 2 s and asserts per-particle position delta < ε. Depends on shader infrastructure (`Sh1–Sh4`). The §6 dual-implementation rule names this test as the parity gate. | milnet01 | Phase 10.9 Slice 17 close |
| 7 | **Cl4.** GPU `buildAndUploadDihedrals` hard-codes `dihedralCompliance = 0.01f` — expose a setter (per-constraint uniform override or re-upload) OR document the GPU-backend limitation on `IClothSolverBackend`. | milnet01 | Phase 10.9 Slice 17 close |
| 8 | **Cl5.** GPU `reset()` semantics: capture proper rest snapshot (mirror CPU `m_initialPositions`) OR document divergent semantics in the header. Pinned particles currently snap to last-pinned-position, not initial grid. | milnet01 | Phase 10.9 Slice 17 close |
| 9 | **Cl6.** Replace `std::unordered_map<uint64_t>` rehashing in `ClothSimulator::buildDihedralConstraints` with a pre-sorted edge vector — 390k inserts on a 256² cloth currently causes an editor "apply preset" hitch. | milnet01 | Phase 10.9 Slice 17 close |
| 10 | `Result<T, E>` / `std::expected` not yet adopted — bool returns + `Logger::error` predate the codebase-wide policy. Migration on the broader engine-wide list, not physics-specific. | milnet01 | post-MIT release (Phase 12) |
| 11 | `RigidBody::syncTransform`'s Phase 10.9 Ph9 stepping-stone uses a matrix-override path because `Transform.rotation` is Euler-vec3 (and 36 read-sites depend on that). The native fix is to make `Transform` quaternion-first; tracked engine-wide. | milnet01 | post-Phase 10.9 (Phase 11 entry) |
| 12 | Performance budgets in §8 are placeholders — needs a one-shot Tracy / RenderDoc capture against the demo scene + a 64×64 GPU cloth panel to fill in measured numbers. | milnet01 | Phase 11 audit |
| 13 | Replay determinism (Phase 11A): no per-tick state hash exists yet. Once added, mismatch detection during replay catches `-ffast-math` regressions, NaN propagation, and worker-count drift. | milnet01 | Phase 11A entry |
| 14 | Cloth wind seed currently lives on `ClothSimulator` only — `GpuClothSimulator` does not yet thread the seed through to its wind compute shader. Will surface as a parity-test failure once Cl1 lands. | milnet01 | Phase 10.9 Slice 17 close (with Cl1) |
| 15 | **Jolt-version posture** (CLAUDE.md Rule 8). Engine pin = v5.2.0; upstream master / 5.3.0+ has cross-platform character determinism, sensor-static detection, constraint priority, RISC-V SIMD, additional perf optimisations. Bump + rerun parity tests (no-op on engine-side code is the goal; Jolt API churn at minor versions is small). | milnet01 | Phase 11 entry |
| 16 | **CODING_STANDARDS §30 drift.** §30 names `engine/physics/jolt_layers.h` and `JoltHelpers::initialize` — the actual files are `engine/physics/physics_layers.h` and `jolt_helpers.h` (with free-function vec3/quat/mat4 conversions, no `initialize`). §30 needs the references corrected. | milnet01 | next CODING_STANDARDS pass |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/physics` formalised post-Phase 10.9 Wave 4 (Ph2 / Ph6 / Ph7 / Ph9 / Cl2 / Cl3 / Cl7 / Cl8 landed). Cloth dual-implementation, constraint determinism story, and Jolt threading model captured for the first time in one document. |
