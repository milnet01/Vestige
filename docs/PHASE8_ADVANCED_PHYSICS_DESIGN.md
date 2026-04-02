# Phase 8: Advanced Physics Design Document

## Batch 12: Ragdoll, Object Interaction, Destruction, Dismemberment

### Research Sources
- Jolt Physics Ragdoll API (`Jolt/Physics/Ragdoll/Ragdoll.h`, `SwingTwistConstraint.h`)
- GDC 2016 Rainbow Six Siege procedural destruction
- GDC 2019 Control (Remedy) runtime granular destruction
- Dead Space Remake peeling/dismemberment system (2023)
- voro++ Voronoi library (Chris Rycroft, BSD)
- Teschner 2003 spatial hashing
- Muller et al. real-time mesh splitting

---

## 12a: Ragdoll Physics

### Architecture

Uses Jolt's native `RagdollSettings`/`Ragdoll` classes directly rather than wrapping manually. This gives us `Stabilize()` for mass redistribution, `DisableParentChildCollisions()`, `DriveToPoseUsingMotors()` for powered ragdoll, and `DriveToPoseUsingKinematics()` for animation-driven mode.

**Key conversion**: Our `Vestige::Skeleton` must be converted to `JPH::Skeleton` and `JPH::SkeletonPose` to work with the Jolt ragdoll system.

### New Files

| File | Purpose |
|------|---------|
| `physics/ragdoll_preset.h/.cpp` | Defines ragdoll body shapes, joint limits, mass per bone |
| `physics/ragdoll.h/.cpp` | Manages Jolt Ragdoll lifecycle, Vestige<->Jolt skeleton conversion |

### RagdollPreset

Defines the physical properties for each bone in a ragdoll:

```cpp
struct RagdollJointDef
{
    std::string boneName;
    CollisionShapeType shapeType;  // CAPSULE, SPHERE, BOX
    glm::vec3 shapeSize;           // half-extents or radius+halfHeight
    glm::vec3 shapeOffset;         // local offset from bone origin
    float mass;
    // SwingTwist limits (radians)
    float normalHalfCone;          // swing limit around normal axis
    float planeHalfCone;           // swing limit in plane
    float twistMin, twistMax;      // twist angle range
    float maxFrictionTorque;
};

struct RagdollPreset
{
    std::string name;
    std::vector<RagdollJointDef> joints;
    static RagdollPreset createHumanoid();  // 15-joint standard humanoid
};
```

### Ragdoll Class

```cpp
enum class RagdollState { INACTIVE, ACTIVE, POWERED, KINEMATIC };

class Ragdoll
{
public:
    bool create(PhysicsWorld& world, const Skeleton& skeleton,
                const RagdollPreset& preset, const std::vector<glm::mat4>& boneWorldMatrices);
    void destroy();

    void activate();                            // Go limp (full physics)
    void deactivate();                          // Return to animation
    void driveToPose(const std::vector<glm::mat4>& targetMatrices, float dt);  // Powered/kinematic

    void setMotorStrength(float strength);       // 0=limp, 1=full tracking
    void setPartMotionType(int jointIndex, bool dynamic);  // Partial ragdoll

    void syncToEntiy(Entity& entity);           // Update entity transform from ragdoll root
    void getBoneMatrices(std::vector<glm::mat4>& outMatrices) const;  // For skinned rendering

    RagdollState getState() const;
    bool isActive() const;
    int getBodyCount() const;
};
```

### Skeleton Conversion (Vestige -> Jolt)

```cpp
// Build JPH::Skeleton from Vestige::Skeleton
JPH::Ref<JPH::Skeleton> jphSkel = new JPH::Skeleton();
for (int i = 0; i < skeleton.getJointCount(); ++i)
{
    const auto& joint = skeleton.m_joints[i];
    jphSkel->AddJoint(joint.name, joint.parentIndex);
}

// Build JPH::SkeletonPose from bone world matrices
JPH::SkeletonPose pose;
pose.SetSkeleton(jphSkel);
// Convert glm::mat4 -> JPH::Mat44 for each joint
```

### Humanoid Preset (15 joints)

| Bone | Shape | Size | Mass | Swing | Twist |
|------|-------|------|------|-------|-------|
| Hips | Box | 0.15x0.10x0.10 | 15kg | 15deg | +/-10deg |
| Spine | Box | 0.12x0.12x0.08 | 10kg | 15deg | +/-10deg |
| Chest | Box | 0.15x0.14x0.10 | 12kg | 15deg | +/-15deg |
| Head | Sphere | r=0.10 | 5kg | 40deg | +/-45deg |
| UpperArm L/R | Capsule | r=0.04, h=0.12 | 3kg | 80deg/45deg | +/-45deg |
| LowerArm L/R | Capsule | r=0.035, h=0.12 | 2kg | 0deg/120deg | +/-45deg |
| Hand L/R | Box | 0.04x0.02x0.06 | 0.5kg | 30deg | +/-20deg |
| UpperLeg L/R | Capsule | r=0.06, h=0.18 | 8kg | 45deg | +/-30deg |
| LowerLeg L/R | Capsule | r=0.05, h=0.18 | 5kg | 0deg/130deg | +/-15deg |
| Foot L/R | Box | 0.05x0.03x0.10 | 1kg | 25deg | +/-10deg |

### Integration with SkeletonAnimator

1. **Animation -> Ragdoll**: Snapshot bone matrices from `SkeletonAnimator::getBoneMatrices()`, create ragdoll at that pose, optionally transfer bone velocities.
2. **Ragdoll -> Rendering**: `Ragdoll::getBoneMatrices()` returns matrices in the same format as `SkeletonAnimator::getBoneMatrices()` (globalTransform * inverseBindMatrix).
3. **Powered ragdoll**: Call `driveToPose()` each frame with target matrices from animation, adjustable motor strength for blending.

---

## 12b: Object Interaction (Grab/Carry/Throw)

### New Files

| File | Purpose |
|------|---------|
| `physics/grab_system.h/.cpp` | Manages grab/carry/throw lifecycle |
| `scene/interactable_component.h/.cpp` | Marks entities as interactable |

### InteractableComponent

```cpp
enum class InteractionType : uint8_t { GRAB, PUSH, TOGGLE };

class InteractableComponent : public Component
{
public:
    InteractionType type = InteractionType::GRAB;
    float maxGrabMass = 50.0f;      // Max mass player can pick up
    float throwForce = 10.0f;       // Impulse multiplier on throw
    float grabDistance = 2.0f;       // Max reach distance
    float holdDistance = 1.5f;       // Distance from camera when held
    bool highlighted = false;        // Currently looked at
};
```

### GrabSystem

```cpp
class GrabSystem
{
public:
    void update(PhysicsWorld& world, const Camera& camera, float dt);

    bool tryGrab(PhysicsWorld& world, const Camera& camera);
    void release();
    void throwObject(const glm::vec3& direction, float force);

    bool isHolding() const;
    Entity* getHeldEntity() const;
    Entity* getLookedAtEntity() const;

private:
    // Held object state
    JPH::BodyID m_heldBody;
    Entity* m_heldEntity = nullptr;
    ConstraintHandle m_holdConstraint;  // Point constraint to invisible kinematic body
    JPH::BodyID m_holderBody;           // Invisible kinematic body at hold position

    // Look-at detection
    Entity* m_lookedAtEntity = nullptr;
    float m_lookAtDistance = 0.0f;
};
```

### Grab Mechanic

1. **Raycast** from camera to find interactable entity
2. **Create kinematic "holder" body** at the grab point
3. **Create distance constraint** between held object and holder body (spring-based for smooth following)
4. **Each frame**: Move holder body to camera forward * holdDistance
5. **On throw**: Remove constraint, apply impulse in camera direction

---

## 12c: Dynamic Destruction (Voronoi Fracture)

### New Files

| File | Purpose |
|------|---------|
| `physics/fracture.h/.cpp` | Voronoi fracture algorithm |
| `physics/deformable_mesh.h/.cpp` | Pre-fracture mesh deformation (denting) |
| `physics/breakable_component.h/.cpp` | Component marking entities as destructible |

### Approach: Pre-Fracture with Runtime Activation

1. **At setup**: Generate Voronoi fragments offline, store as hidden sub-meshes
2. **On impact**: Swap intact mesh for fragments, create rigid bodies
3. **Debris management**: Sleep timer, distance culling, fragment pooling

### BreakableComponent

```cpp
class BreakableComponent : public Component
{
public:
    float breakForce = 100.0f;          // Impact force threshold
    float breakImpulse = 50.0f;         // Impulse threshold
    int fragmentCount = 8;               // Number of Voronoi fragments
    float fragmentLifetime = 5.0f;       // Seconds before cleanup
    float fragmentMassDensity = 500.0f;  // kg/m^3 for fragment mass
    std::string interiorMaterial;        // Material for cut faces

    bool isFractured() const;
    void fracture(const glm::vec3& impactPoint, const glm::vec3& impulse);

    // Pre-computed fragment data
    struct Fragment
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        glm::vec3 centroid;
        float volume;
    };
    std::vector<Fragment> m_fragments;
};
```

### Fracture Algorithm

```cpp
class Fracture
{
public:
    struct FractureResult
    {
        struct Fragment
        {
            std::vector<glm::vec3> positions;
            std::vector<glm::vec3> normals;
            std::vector<glm::vec2> uvs;
            std::vector<uint32_t> indices;
            std::vector<bool> isInteriorFace;  // True for cut faces
            glm::vec3 centroid;
            float volume;
        };
        std::vector<Fragment> fragments;
    };

    /// Generate Voronoi fracture of a convex mesh
    static FractureResult fractureConvex(
        const std::vector<glm::vec3>& vertices,
        const std::vector<uint32_t>& indices,
        int fragmentCount,
        const glm::vec3& impactPoint,    // Bias seed points toward impact
        uint32_t seed = 0);

    /// Generate seed points biased toward impact location
    static std::vector<glm::vec3> generateSeeds(
        const AABB& bounds,
        int count,
        const glm::vec3& impactBias,
        uint32_t seed);
};
```

### Implementation Strategy (No External Library)

Instead of depending on voro++, implement a simplified Voronoi fracture:

1. Generate N seed points inside mesh AABB (biased toward impact)
2. For each seed, compute the convex cell as intersection of bisector half-planes with all other seeds
3. Clip each convex cell against the mesh AABB (simplified — works for convex meshes)
4. Triangulate each cell face for rendering
5. Mark faces that are internal cuts vs original surface

For non-convex meshes, first decompose into convex parts, fracture each independently.

### Deformable Mesh

```cpp
class DeformableMesh
{
public:
    /// Apply impact deformation (denting) to vertex positions
    static void applyImpact(
        std::vector<Vertex>& vertices,
        const glm::vec3& impactPoint,
        const glm::vec3& impactDirection,
        float radius,
        float depth);
};
```

---

## 12d: Dismemberment System

### New Files

| File | Purpose |
|------|---------|
| `physics/dismemberment.h/.cpp` | Mesh splitting, zone management, severed part creation |
| `physics/dismemberment_zones.h/.cpp` | Zone definitions, damage tracking |

### Approach: Pre-Computed Splits

At asset load time, classify triangles by bone ownership and pre-compute cut data. At runtime, activate splits by swapping index buffers.

### DismembermentZone

```cpp
struct DismembermentZone
{
    int boneIndex;                      // Skeleton bone this zone maps to
    glm::vec3 cutPlaneNormal;           // Cut direction (bone-perpendicular)
    float cutPlaneOffset;               // Along bone axis
    float health;                       // Current HP
    float maxHealth;                    // Threshold for severance
    float damageVisualScale;            // 0=pristine, 1=fully damaged
    int capMeshIndex;                   // Pre-modeled wound cap mesh
    int stumpMeshIndex;                 // Geometry left on body side
    std::vector<int> childZones;        // Severing parent auto-severs children
    bool severed;
};
```

### DismembermentZones (Manager)

```cpp
class DismembermentZones
{
public:
    void addZone(const DismembermentZone& zone);
    void applyDamage(int zoneIndex, float damage);
    bool isZoneSevered(int zoneIndex) const;
    int findZoneForBone(int boneIndex) const;

    const std::vector<DismembermentZone>& getZones() const;

    /// Create default humanoid zones
    static DismembermentZones createHumanoid(const Skeleton& skeleton);

private:
    std::vector<DismembermentZone> m_zones;
};
```

### Dismemberment (Runtime Splitting)

```cpp
class Dismemberment
{
public:
    struct SplitResult
    {
        // Body side
        std::vector<Vertex> bodyVertices;
        std::vector<uint32_t> bodyIndices;
        // Severed part
        std::vector<Vertex> limbVertices;
        std::vector<uint32_t> limbIndices;
        // Cap geometry (cross-section)
        std::vector<Vertex> capVertices;
        std::vector<uint32_t> capIndices;
        // Physics
        glm::vec3 limbCentroid;
        float limbVolume;
        glm::vec3 limbVelocity;
    };

    /// Split a skinned mesh at a bone boundary
    static SplitResult splitAtBone(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const DismembermentZone& zone,
        const std::vector<glm::mat4>& boneMatrices);

    /// Classify vertices by bone weight dominance
    static void classifyVertices(
        const std::vector<Vertex>& vertices,
        int cutBoneIndex,
        float weightThreshold,
        std::vector<int>& outSide);  // 0=body, 1=limb
};
```

### Vertex Classification

For each vertex, check bone weights:
- If dominant weight is for the severed bone or its children -> limb side
- Otherwise -> body side
- Boundary vertices (split weights) -> duplicate, renormalize weights for each side

### Cap Mesh Generation

1. Collect all edges that cross the body/limb boundary
2. Project edge intersection points onto the cut plane
3. Sort points by angle around the cut plane centroid
4. Fan-triangulate the polygon
5. Apply cross-section material (bone/meat/skin rings or simple solid color)

---

## Performance Budget

| System | Budget | Strategy |
|--------|--------|----------|
| Ragdoll | 15-25 bodies per ragdoll, 10 simultaneous | Jolt auto-sleep, Stabilize() |
| Grab | 1 constraint + 1 kinematic body | Negligible |
| Fracture | 5-15 fragments per object, 100 max scene-wide | Pre-fracture, sleep timer, distance cull |
| Dismemberment | 1 mesh split per event | Pre-computed classification, partial VBO update |

## Testing Strategy

Each sub-system gets standalone unit tests:
- Ragdoll: preset creation, skeleton conversion, state transitions
- Interaction: component properties, grab/throw state machine
- Fracture: seed generation, volume computation, vertex splitting
- Dismemberment: zone creation, vertex classification, mesh splitting
