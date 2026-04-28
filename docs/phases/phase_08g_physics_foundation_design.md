# Phase 8G: Physics Foundation Design

## Overview

Batch 9 adds two foundational physics features:
- **9a: Convex Hull + Triangle Mesh collision shapes** for RigidBody
- **9b: Fabric Material System** with physically-based cloth presets

## 9a: Collision Shape Expansion

### Current State

RigidBody supports three primitive shapes: BOX, SPHERE, CAPSULE. These cover basic props but not complex architectural geometry (pillars, furniture, terrain mesh colliders).

### New Shape Types

| Shape | Jolt Class | Body Types | Use Case |
|-------|-----------|-----------|----------|
| CONVEX_HULL | ConvexHullShapeSettings | Static, Dynamic, Kinematic | Props, furniture, irregular objects |
| MESH | MeshShapeSettings | Static only (Jolt limitation) | Architecture, complex static geometry |

### Jolt API Summary

**ConvexHullShape:**
- Takes `Array<Vec3>` vertex positions → builds convex hull automatically
- Max 256 hull vertices (interior points discarded)
- `cDefaultConvexRadius = 0.05f` rounds edges for performance
- Mass computed automatically from hull volume + density

**MeshShape:**
- Takes `VertexList` (Float3 array) + `IndexedTriangleList` (index triples)
- Builds internal BVH for fast queries
- `MustBeStatic()` returns true — dynamic only with manual mass and no mesh-vs-mesh collision
- Up to 32 materials per mesh

### Data Storage

RigidBody gains optional collision mesh data:
```cpp
std::vector<glm::vec3> collisionVertices;   // Positions only
std::vector<uint32_t> collisionIndices;     // Triangle indices (for MESH)
```

Populated by a utility function `setCollisionMesh(vertices, indices)` at setup time. For CONVEX_HULL, only vertices are needed (Jolt builds the hull). For MESH, both vertices and indices are required.

### Inspector UI

New `drawRigidBody()` method in InspectorPanel:
- Shape type combo (Box, Sphere, Capsule, Convex Hull, Mesh)
- Size editors for primitive shapes
- Vertex/triangle count display for hull/mesh shapes
- Motion type combo (Static, Dynamic, Kinematic)
- Mass, friction, restitution sliders
- Warning if MESH used with non-static motion type

### Error Handling

- ConvexHullShapeSettings::Create() can fail (too few points, degenerate hull)
- Log error and fall back to AABB-derived box shape
- MeshShape with 0 triangles: skip body creation
- MESH + DYNAMIC: log warning, force STATIC

## 9b: Fabric Material System

### Research Summary

**KES (Kawabata Evaluation System)** measures 6 fabric property categories. Key mappings to XPBD:
- KES B (bending rigidity) → bend compliance (inverse relationship)
- KES G (shear stiffness) → shear compliance (inverse)
- KES EMT (extensibility) → stretch compliance (direct)
- KES MIU (friction) → collision friction
- KES W (weight) → particle mass

No major game engine ships fabric presets by name — this is a genuine differentiator.

### Architecture

**FabricMaterial** struct stores physically-inspired properties:
```cpp
struct FabricMaterial
{
    FabricType type;
    const char* name;
    const char* description;
    float densityGSM;         // g/m² (fabric weight)
    float stretchCompliance;  // XPBD stretch compliance
    float shearCompliance;    // XPBD shear compliance
    float bendCompliance;     // XPBD bend compliance
    float damping;            // Velocity damping
    float friction;           // Surface friction coefficient
    float dragCoefficient;    // Aerodynamic drag
};
```

**FabricType** enum — 15 fabric types organized in 3 categories:

Common fabrics (10):
- CHIFFON, SILK, COTTON, LINEN, POLYESTER, WOOL, VELVET, DENIM, CANVAS, LEATHER

Biblical/historical fabrics (5):
- FINE_LINEN, EMBROIDERED_VEIL, GOAT_HAIR, RAM_SKIN, TACHASH

### Fabric Property Data

Values calibrated from KES research, textile industry data, and validated against existing presets:

| Fabric | Density (GSM) | Stretch | Shear | Bend | Damping | Friction | Drag |
|--------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Chiffon | 35 | 0.0003 | 0.001 | 0.08 | 0.008 | 0.15 | 2.5 |
| Silk | 80 | 0.0002 | 0.0008 | 0.05 | 0.010 | 0.18 | 2.2 |
| Cotton | 150 | 0.0001 | 0.0005 | 0.015 | 0.020 | 0.35 | 1.5 |
| Linen | 180 | 0.00008 | 0.0004 | 0.008 | 0.022 | 0.45 | 1.4 |
| Polyester | 150 | 0.00015 | 0.0006 | 0.025 | 0.015 | 0.25 | 1.6 |
| Wool | 280 | 0.00008 | 0.0004 | 0.010 | 0.025 | 0.40 | 1.2 |
| Velvet | 300 | 0.0001 | 0.0005 | 0.012 | 0.028 | 0.50 | 1.3 |
| Denim | 400 | 0.00003 | 0.0002 | 0.003 | 0.030 | 0.55 | 1.0 |
| Canvas | 450 | 0.00002 | 0.00015 | 0.002 | 0.035 | 0.65 | 0.9 |
| Leather | 800 | 0.000005 | 0.00008 | 0.0005 | 0.045 | 0.85 | 0.7 |
| Fine Linen | 120 | 0.00005 | 0.0003 | 0.005 | 0.020 | 0.40 | 1.5 |
| Embroidered Veil | 220 | 0.00004 | 0.00025 | 0.003 | 0.025 | 0.45 | 1.3 |
| Goat Hair | 1588 | 0.00003 | 0.00015 | 0.001 | 0.040 | 0.60 | 0.8 |
| Ram Skin | 700 | 0.000008 | 0.0001 | 0.0003 | 0.045 | 0.80 | 0.6 |
| Tachash | 1200 | 0.000005 | 0.00008 | 0.0002 | 0.050 | 0.85 | 0.5 |

Note: Existing `linenCurtain` preset (mass=0.02, stretch=0.00005, bend=0.005) matches Fine Linen closely, validating these values.

### Conversion to ClothConfig

```cpp
ClothPresetConfig FabricDatabase::toPresetConfig(FabricType type);
```

Maps FabricMaterial → ClothPresetConfig:
- `particleMass` derived from density (GSM / 5000, tuned for typical grid density)
- Compliance values used directly
- Wind strength scaled inversely with density (lighter fabrics catch more wind)
- Substeps scaled with stiffness (stiffer = fewer substeps needed)

### Integration with Existing Presets

The existing 5 ClothPresetType presets remain unchanged. FabricType provides a separate, richer selector. The cloth inspector gets a "Fabric Material" combo that applies properties via `FabricDatabase::toPresetConfig()`.

### Biblical Fabric Context

Source: Exodus 25-40, KES research, 2025 Tandfonline goat hair tent study.

- **Fine Linen (Shesh Moshzar)**: Thread count comparable to 200-300 modern TC. Very low stretch (flax: 2-3% elongation). Used for inner curtains, courtyard hangings.
- **Goat Hair**: 1,588 g/m² — "very heavy textile". Fiber diameter 78-80µm. Swells when wet for waterproofing. Second covering layer.
- **Ram Skin**: Tanned sheepskin, dyed red. Minimal drape. Third covering.
- **Tachash**: Debated identity (dugong/sea cow, beaded leather). Very stiff, waterproof. Outermost covering.

## Sources

- Jolt Physics: ConvexHullShape.h, MeshShape.h, HeightFieldShape.h (local headers)
- Kawabata Evaluation System: Wikipedia, NC State TPACC, ScienceDirect
- KES fabric measurements: PMC6316920 (functional fabrics study)
- XPBD compliance: Macklin, Müller, Chentanez 2016; blog.mmacklin.com
- Fabric densities: Sinosilk, Hero & Villain Style, Marvelous Designer
- Friction: Optitex, Engineering Toolbox
- Goat hair: Tandfonline 2025 (Iranian nomadic tents)
- Biblical textiles: Metropolitan Museum Egyptian linen, TheTorah.com (tachash)
- Game engines: UE5 Chaos Cloth, Unity Cloth, NvCloth, Houdini Vellum, Blender Cloth
