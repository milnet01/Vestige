# Phase 8D Design: Cloth Simulation (CPU XPBD)

## Overview

CPU-based cloth simulation using Extended Position-Based Dynamics (XPBD). XPBD improves on classical PBD by introducing compliance parameters that give physically meaningful stiffness values independent of iteration count and timestep. The cloth system is standalone (does not use Jolt) and renders through the existing engine pipeline with a new dynamic mesh class.

**Reference:** Macklin, Muller, Chentanez — "XPBD: Position-Based Simulation of Compliant Constrained Dynamics" (2016)

## Architecture

```
ClothSimulator          (XPBD solver — particle positions, velocities, constraints)
  |
ClothComponent          (Entity component — owns simulator + dynamic mesh, integrates with scene)
  |
DynamicMesh             (GPU-streamable mesh with per-frame vertex updates)
```

The cloth system is **independent of Jolt**. Jolt handles rigid bodies; cloth uses its own particle-based solver. This avoids coupling and keeps cloth performance predictable.

## XPBD Algorithm

Per-frame update with `S` substeps (default S=10):

```
dt_sub = dt / S
for each substep:
    1. Apply external forces (gravity, wind) → update velocities
    2. Predict positions: x_pred = x + v * dt_sub
    3. Solve constraints (Gauss-Seidel):
       for each constraint:
           compute C(x)          // constraint function
           compute ∇C            // gradient
           Δx = -C / (w1|∇C1|² + w2|∇C2|² + α/dt²) * ∇C
           where α = compliance = 1/stiffness
           x += w * Δx           // apply correction (w = inverse mass)
    4. Update velocities: v = (x - x_old) / dt_sub
    5. Apply damping: v *= (1 - damping * dt_sub)
```

Key advantage over PBD: the compliance term `α/dt²` makes stiffness independent of substep count.

## Constraint Types

### 1. Distance Constraint (Stretch)
- Between adjacent particles in the grid
- **Structural:** horizontal + vertical neighbors (resist stretching)
- **Shear:** diagonal neighbors (resist shearing)
- Compliance: ~0 for stiff fabric (canvas), ~0.001 for elastic (silk)

```
C = |x1 - x2| - restLength
∇C1 = (x1 - x2) / |x1 - x2|
∇C2 = -∇C1
```

### 2. Bending Constraint (Dihedral Angle)
- Between particles separated by 2 edges (skip-one neighbors)
- Simple approach: distance constraint between non-adjacent particles
- This is computationally cheap and works well for real-time
- Compliance: ~0.01 for stiff (cardboard), ~1.0 for soft (silk)

```
// Simplified bending: distance constraint between particles [i][j] and [i+2][j]
C = |x1 - x2| - restLength
```

### 3. Pin Constraint
- Fixes a particle to a world-space position
- Zero compliance = rigid attachment
- Non-zero compliance = elastic attachment (e.g., flag pole give)

```
C = |x - pinPosition|
∇C = (x - pinPosition) / |x - pinPosition|
```

## Collision

### Sphere Collision
- For each particle, check distance to sphere center
- If inside, project to surface along the normal
- Apply friction: tangential velocity *= (1 - friction)

### Plane Collision (Ground)
- Half-space test: if particle.y < planeY, push up
- Apply friction to XZ velocity components

### Capsule Collision (Optional — Phase 8E)
- Closest point on line segment to particle
- Same radius check as sphere

## Wind Model

Per-triangle aerodynamic force:
```
v_rel = v_triangle_avg - v_wind
normal = triangle face normal
area = triangle area
// Drag: opposes relative motion through air
F_drag = -0.5 * dragCoeff * area * dot(v_rel, normal) * normal
// Lift: perpendicular to relative motion (simplified)
F_lift = 0.5 * liftCoeff * area * cross(cross(v_rel, normal), v_rel_normalized)
```

For simplicity, Phase 8D uses drag-only (lift adds complexity with minimal visual benefit). Wind is applied per-triangle, then distributed equally to the 3 vertices.

## DynamicMesh Class

The existing `Mesh` uses `glNamedBufferStorage` (immutable). Cloth needs per-frame vertex updates.

```cpp
class DynamicMesh
{
public:
    void create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void updateVertices(const std::vector<Vertex>& vertices);  // glNamedBufferSubData
    void bind() const;
    void unbind() const;
    uint32_t getIndexCount() const;
    GLuint getVao() const;
    const AABB& getLocalBounds() const;
    void updateBounds(const std::vector<Vertex>& vertices);    // recompute AABB
};
```

Uses `GL_DYNAMIC_STORAGE_BIT` flag with `glNamedBufferStorage` so `glNamedBufferSubData` works. The VAO setup is identical to `Mesh`.

## ClothSimulator

The pure simulation class. No OpenGL, no entity references — just particles and constraints.

```cpp
struct ClothConfig
{
    uint32_t width = 20;              // particles along X
    uint32_t height = 20;             // particles along Z
    float spacing = 0.1f;             // meters between particles
    float particleMass = 0.1f;        // kg per particle
    int substeps = 10;
    float stretchCompliance = 0.0f;   // 0 = perfectly rigid
    float shearCompliance = 0.0001f;
    float bendCompliance = 0.01f;
    float damping = 0.01f;
    glm::vec3 gravity = {0, -9.81f, 0};
};

class ClothSimulator
{
public:
    void initialize(const ClothConfig& config);
    void simulate(float deltaTime);

    // Particle access
    uint32_t getParticleCount() const;
    const glm::vec3* getPositions() const;
    const glm::vec3* getNormals() const;       // recomputed after simulation

    // Constraints
    void pinParticle(uint32_t index, const glm::vec3& worldPos);
    void unpinParticle(uint32_t index);
    void setPinPosition(uint32_t index, const glm::vec3& worldPos);

    // Collision
    void addSphereCollider(const glm::vec3& center, float radius);
    void clearSphereColliders();
    void setGroundPlane(float height);

    // Wind
    void setWind(const glm::vec3& direction, float strength);
    void setDragCoefficient(float drag);

    // Config
    void setSubsteps(int substeps);
    const ClothConfig& getConfig() const;

private:
    ClothConfig m_config;

    // SoA layout for cache efficiency
    std::vector<glm::vec3> m_positions;
    std::vector<glm::vec3> m_prevPositions;
    std::vector<glm::vec3> m_velocities;
    std::vector<float> m_inverseMasses;        // 0 = pinned
    std::vector<glm::vec3> m_normals;

    // Constraints (stored as index pairs + rest length + compliance)
    struct DistanceConstraint {
        uint32_t i0, i1;
        float restLength;
        float compliance;
    };
    std::vector<DistanceConstraint> m_stretchConstraints;
    std::vector<DistanceConstraint> m_shearConstraints;
    std::vector<DistanceConstraint> m_bendConstraints;

    struct PinConstraint {
        uint32_t index;
        glm::vec3 position;
    };
    std::vector<PinConstraint> m_pinConstraints;

    // Collision primitives
    struct SphereCollider { glm::vec3 center; float radius; };
    std::vector<SphereCollider> m_sphereColliders;
    float m_groundPlaneY = -1000.0f;

    // Wind
    glm::vec3 m_windDirection = {0, 0, 0};
    float m_windStrength = 0.0f;
    float m_dragCoeff = 1.0f;

    // Grid topology for triangle iteration
    uint32_t m_gridW = 0, m_gridH = 0;
    std::vector<uint32_t> m_indices;

    void solveDistanceConstraint(DistanceConstraint& c, float dtSqInv);
    void solvePinConstraints();
    void applyCollisions();
    void recomputeNormals();
    void applyWind(float dt);
};
```

### Data layout rationale
SoA (Structure of Arrays) for positions/velocities/masses because the solver iterates over all particles per constraint type, touching only position data. This gives better cache utilization than AoS.

## ClothComponent

```cpp
class ClothComponent : public Component
{
public:
    void initialize(const ClothConfig& config, std::shared_ptr<Material> material);
    void update(float deltaTime) override;

    ClothSimulator& getSimulator();
    DynamicMesh& getMesh();

private:
    ClothSimulator m_simulator;
    DynamicMesh m_mesh;
    std::shared_ptr<Material> m_material;
    std::vector<Vertex> m_vertexBuffer;  // CPU-side for mesh updates
};
```

The `update()` method:
1. Calls `m_simulator.simulate(deltaTime)`
2. Copies particle positions + normals into `m_vertexBuffer`
3. Calls `m_mesh.updateVertices(m_vertexBuffer)`
4. Updates culling bounds

## Rendering Integration

Cloth renders through the existing `Renderer::drawMesh()` path. The `ClothComponent` provides a `DynamicMesh` and `Material`. The renderer doesn't need to know it's cloth — it just draws the mesh with the material.

**Double-sided rendering:** Cloth needs to be visible from both sides. Add a `doubleSided` flag to Material, and when set, disable face culling before drawing.

## Performance Budget

Target: 60 FPS with one 32x32 cloth (1024 particles, ~3000 constraints).

| Operation | Estimated cost (per frame) |
|-----------|--------------------------|
| XPBD solve (10 substeps, 3000 constraints) | ~0.5ms |
| Normal recomputation (1800 triangles) | ~0.05ms |
| Vertex upload (1024 vertices * 96 bytes) | ~0.01ms |
| **Total** | **~0.56ms** |

Well within the 16.6ms budget. A 64x64 cloth (4096 particles) would cost ~2ms — still fine.

## Implementation Steps

### Step 1: DynamicMesh class
- New file: `engine/renderer/dynamic_mesh.h/.cpp`
- Same vertex format as Mesh, but with `GL_DYNAMIC_STORAGE_BIT`
- `updateVertices()` using `glNamedBufferSubData`

### Step 2: ClothSimulator
- New file: `engine/physics/cloth_simulator.h/.cpp`
- Particle system with SoA layout
- Grid generation (positions, UVs, indices)
- Distance constraints (stretch + shear + bend)
- XPBD solver loop
- Pin constraints
- Ground plane + sphere collision
- Wind (drag model)
- Normal recomputation

### Step 3: ClothComponent
- New file: `engine/physics/cloth_component.h/.cpp`
- Owns ClothSimulator + DynamicMesh
- Bridges simulation to rendering
- Component::update() drives simulation + mesh upload

### Step 4: Material double-sided flag
- Add `bool m_doubleSided` to Material
- Disable face culling in Renderer when drawing double-sided meshes

### Step 5: Engine integration
- Add demo cloth to the physics demo scene
- Pin top-row particles
- Add wind toggle (W key or similar)
- Add cloth to debug drawing (wireframe overlay option)

### Step 6: Unit tests
- `test_cloth_simulator.cpp`
- Test: gravity pulls unpinned cloth down
- Test: pinned particles stay in place
- Test: distance constraints maintain rest length (approximately)
- Test: ground plane prevents penetration
- Test: sphere collision pushes particles out
- Test: wind applies force to facing triangles
- Test: zero-mass particle is immovable
- Test: normal recomputation produces unit normals

### Step 7: Update CMakeLists
- Add new source files to engine and test targets

## Testing Strategy

Unit tests focus on the simulator (no GPU). The `ClothSimulator` is pure CPU with no OpenGL dependency, making it fully testable with Google Test.

Visual testing: run the engine, verify cloth drapes correctly, reacts to wind, doesn't penetrate colliders.

## Files Changed/Created

**New files:**
- `engine/renderer/dynamic_mesh.h`
- `engine/renderer/dynamic_mesh.cpp`
- `engine/physics/cloth_simulator.h`
- `engine/physics/cloth_simulator.cpp`
- `engine/physics/cloth_component.h`
- `engine/physics/cloth_component.cpp`
- `tests/test_cloth_simulator.cpp`
- `docs/phases/phase_08d_design.md` (this file)

**Modified files:**
- `engine/renderer/material.h` — add `doubleSided` flag
- `engine/renderer/renderer.cpp` — respect `doubleSided` during draw
- `engine/core/engine.cpp` — demo cloth in physics scene
- `engine/CMakeLists.txt` — new source files
- `tests/CMakeLists.txt` — new test file
