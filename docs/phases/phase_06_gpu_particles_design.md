# Phase 6 Batch 4: GPU Compute Particle System Design

## Overview
A Niagara-inspired GPU compute particle system that moves simulation from CPU to GPU via compute shaders. Features a composable behavior system where particle effects are built by stacking reusable behaviors (forces, modifiers, collision). Supports millions of particles with zero CPU readback via indirect draw commands.

## Research Summary

### Unreal Engine Niagara Architecture (Public Documentation)
Niagara uses a hierarchical module stack: System → Emitter → Particle → Render. Modules are composable behaviors executed sequentially. GPU simulation via compute shaders supports ~2M particles (20x CPU mode). Key design patterns:

- **Composition over inheritance**: Effects built from reusable modules
- **Simulation separate from rendering**: One simulation can drive multiple renderers
- **GPU-first with CPU fallback**: GPU for scale, CPU for events/collision
- **Data-driven**: Behaviors defined as data transformations, not hardcoded logic

No specific Niagara patents were found in USPTO searches. The techniques used (compute shaders, bitonic sort, indirect draw, stream compaction) are all general computer science with extensive prior art.

### GPU Compute Particle Techniques (OpenGL 4.5)
- **SSBOs (std430)**: Particle data stored in GPU buffers, read/written by compute shaders
- **Atomic counters**: Thread-safe particle allocation/deallocation
- **Indirect draw**: `glDrawArraysIndirect` renders variable particle counts without CPU readback
- **Ping-pong buffers**: Double-buffered SSBOs avoid read-write hazards
- **Stream compaction**: Swap-on-death removes dead particles, keeps live particles contiguous
- **Bitonic sort**: O(log²n) parallel sort for back-to-front transparency rendering
- **Depth buffer collision**: Particles sample scene depth texture for world collision

Sources: NVIDIA GPU Gems, Wicked Engine GPU particles, ARM OpenGL ES SDK, OpenGL SuperBible.

## Architecture

### Vestige GPU Particle Pipeline

Our system uses Niagara's best architectural ideas with our own naming and design:

```
Vestige Naming           Niagara Equivalent       Purpose
─────────────────────────────────────────────────────────────
ParticleEffect           NiagaraSystem            Top-level effect (multiple layers)
ParticleLayer            NiagaraEmitter           Single simulation + renderers
ParticleBehavior         NiagaraModule            Composable behavior unit
ParticleRenderer         NiagaraRenderer          Rendering output
GPUParticleSystem        (internal)               GPU compute pipeline manager
```

### System Hierarchy

```
ParticleEffect (top-level, contains multiple layers)
├── ParticleLayer "flames"
│   ├── Spawn: RateBehavior(80/sec), ConeShape(25°)
│   ├── Update: GravityBehavior(0,-2,0), DragBehavior(0.5)
│   ├── Lifetime: ColorOverLife(yellow→red→black), SizeOverLife(0.5→1→0)
│   └── Render: BillboardRenderer(fire_texture.png, ADDITIVE)
├── ParticleLayer "sparks"
│   ├── Spawn: BurstBehavior(5, interval=0.2), PointShape
│   ├── Update: GravityBehavior(0,-9.81,0), NoiseBehavior(0.3)
│   ├── Lifetime: ColorOverLife(white→orange), SizeOverLife(1→0)
│   └── Render: BillboardRenderer(spark.png, ADDITIVE)
└── ParticleLayer "smoke"
    ├── Spawn: RateBehavior(20/sec), ConeShape(10°)
    ├── Update: GravityBehavior(0,0.5,0), DragBehavior(2.0)
    ├── Lifetime: ColorOverLife(grey50%→transparent), SizeOverLife(1→4)
    └── Render: BillboardRenderer(smoke.png, ALPHA_BLEND)
```

### GPU Compute Pipeline (Per Frame)

```
┌─────────────────────────────────────────────────────────┐
│ 1. EMIT   (particle_emit.comp.glsl)                     │
│    Spawn new particles into free slots using atomics     │
│    CPU provides: spawnCount, shape params, randomSeed    │
├─────────────────────────────────────────────────────────┤
│ 2. SIMULATE  (particle_simulate.comp.glsl)              │
│    For each alive particle:                              │
│    - Apply behaviors (gravity, drag, noise, wind, etc.) │
│    - Integrate velocity → position (semi-implicit Euler)│
│    - Age particle, mark dead if expired                  │
│    - Evaluate over-lifetime curves (color, size, speed) │
├─────────────────────────────────────────────────────────┤
│ 3. COMPACT  (particle_compact.comp.glsl)                │
│    Remove dead particles, update alive count             │
│    Uses atomic counter + scatter to maintain contiguous  │
├─────────────────────────────────────────────────────────┤
│ 4. SORT  (particle_sort.comp.glsl) — optional           │
│    Bitonic merge sort by camera distance                 │
│    Only for ALPHA_BLEND mode (ADDITIVE skips sorting)   │
├─────────────────────────────────────────────────────────┤
│ 5. INDIRECT UPDATE  (particle_indirect.comp.glsl)       │
│    Write alive count into DrawArraysIndirect command     │
│    No CPU readback needed                                │
├─────────────────────────────────────────────────────────┤
│ 6. RENDER                                                │
│    glDrawArraysIndirect with billboard/mesh/ribbon       │
│    Reads particle SSBO directly in vertex shader         │
└─────────────────────────────────────────────────────────┘
```

### Memory Layout

**GPU Particle SSBO (std430, single interleaved struct):**
```glsl
struct GPUParticle
{
    vec4 position;       // xyz = world pos, w = size
    vec4 velocity;       // xyz = velocity, w = rotation
    vec4 color;          // rgba
    float age;           // seconds since spawn
    float lifetime;      // total lifetime
    float startSize;     // initial size (for over-lifetime)
    uint flags;          // bit 0 = alive, bits 1-7 = behavior flags
};
// Total: 64 bytes per particle (cache-line aligned)
```

**Counters SSBO:**
```glsl
layout(std430, binding = 1) buffer Counters
{
    uint aliveCount;     // Current alive particle count
    uint deadCount;      // Number of dead slots available
    uint emitCount;      // Particles to emit this frame
    uint padding;
};
```

**Free List SSBO:**
```glsl
layout(std430, binding = 2) buffer FreeList
{
    uint freeIndices[];  // Stack of available particle indices
};
```

**Indirect Draw Command SSBO:**
```glsl
layout(std430, binding = 3) buffer IndirectDraw
{
    uint vertexCount;    // 6 for billboard quad
    uint instanceCount;  // = aliveCount (updated by GPU)
    uint firstVertex;    // 0
    uint baseInstance;   // 0
};
```

### Ping-Pong Buffers

Double-buffered particle SSBOs avoid read-write hazards:
- Frame N: Read from buffer A, write to buffer B
- Frame N+1: Read from buffer B, write to buffer A
- Memory barrier between compute and render ensures coherency

### CPU ↔ GPU Path Selection

```
if (maxParticles <= 500)
    → CPU path (existing ParticleEmitterComponent)
    → Low overhead, good for small effects
    → Supports events and coupled lights

if (maxParticles > 500)
    → GPU path (GPUParticleEmitter)
    → Compute shader simulation
    → Indirect draw, no CPU readback
    → Can handle 100k+ particles
```

Both paths share the same configuration format (`ParticleEmitterConfig`) so effects are portable between CPU and GPU.

## Composable Behavior System

### Behavior Types

Behaviors are data descriptors uploaded to the GPU as a uniform buffer. The simulate compute shader reads the behavior list and applies each one.

**Spawn Behaviors:**
| Behavior | Parameters | Description |
|----------|-----------|-------------|
| Rate | particlesPerSecond | Continuous emission |
| Burst | count, interval, repeatCount | Periodic bursts |
| OnEvent | eventType | Spawn in response to events (CPU-only) |

**Emission Shapes:**
| Shape | Parameters | Description |
|-------|-----------|-------------|
| Point | — | Single point, random direction |
| Sphere | radius, surfaceOnly | Within/on sphere surface |
| Cone | angle, radius, length | Cone-shaped emission |
| Box | size (vec3) | Within axis-aligned box |
| Mesh | meshId, surfaceOnly | From mesh vertices/surface (future) |
| Ring | radius, width | Torus-shaped emission |

**Force Behaviors (applied during simulation):**
| Behavior | Parameters | Description |
|----------|-----------|-------------|
| Gravity | acceleration (vec3) | Constant acceleration |
| Wind | direction, strength | Directional force (future: EnvironmentForces query) |
| Drag | coefficient | Velocity damping (v *= 1 - drag*dt) |
| Noise | frequency, amplitude, octaves | Curl noise turbulence |
| Orbit | center, speed, radius | Orbital motion around a point |
| Attract | target, strength, falloff | Attract/repel toward a point |
| Vortex | axis, strength, pullStrength | Spiral motion |
| TurbulenceField | scale, intensity | 3D noise-based force field |

**Lifetime Modifiers:**
| Modifier | Parameters | Description |
|----------|-----------|-------------|
| ColorOverLife | gradient (up to 8 stops) | Interpolate color by normalized age |
| SizeOverLife | curve (up to 8 keyframes) | Multiply size by curve value |
| SpeedOverLife | curve (up to 8 keyframes) | Scale velocity magnitude |
| RotationOverLife | curve | Spin rate over lifetime |
| AlphaOverLife | curve | Separate alpha control |

**Collision Behaviors:**
| Behavior | Parameters | Description |
|----------|-----------|-------------|
| DepthBuffer | restitution, friction | Collide with scene geometry via depth buffer |
| GroundPlane | height, restitution | Simple Y-plane collision |
| SphereCollider | center, radius, restitution | Bounce off sphere |
| KillOnCollision | — | Destroy particle on impact |

### Behavior Encoding (GPU-Friendly)

Behaviors are packed into a uniform buffer as fixed-size records:

```glsl
// Up to 16 behaviors per layer
struct BehaviorData
{
    uint type;           // Enum: GRAVITY=0, DRAG=1, NOISE=2, etc.
    uint flags;          // Per-behavior flags
    float params[6];     // Type-specific parameters (6 floats max)
};

layout(std140, binding = 4) uniform BehaviorBlock
{
    BehaviorData behaviors[16];
    int behaviorCount;
    // Over-lifetime data
    vec4 colorStops[8];      // RGBA gradient stops
    float colorStopTimes[8]; // Normalized times [0,1]
    int colorStopCount;
    float sizeKeys[8];       // Size multiplier keyframes
    float sizeKeyTimes[8];   // Normalized times [0,1]
    int sizeKeyCount;
};
```

The simulate shader processes behaviors with a simple loop:
```glsl
for (int i = 0; i < behaviorCount; i++)
{
    switch (behaviors[i].type)
    {
        case 0: applyGravity(p, behaviors[i]); break;
        case 1: applyDrag(p, behaviors[i]); break;
        case 2: applyNoise(p, behaviors[i]); break;
        // ...
    }
}
```

## Rendering Modes

### Billboard (Default)
Camera-facing quads. Vertex shader reads from particle SSBO, expands quad using camera right/up vectors. Same technique as existing CPU particle renderer but data comes from SSBO instead of VBOs.

### Mesh Particles (Future Extension)
Per-particle mesh instances. Each particle drives an instanced mesh draw with position/rotation/scale from the SSBO.

### Ribbon/Trail (Future Extension)
Connected particle strips for smoke trails, magic effects. Requires sequential particle ordering.

## Bitonic Sort for Transparency

For ALPHA_BLEND particles, back-to-front sorting is required. We use bitonic merge sort:

1. **Key Generation**: Compute shader calculates camera-space depth for each particle → writes (depth, index) pairs to sort buffer
2. **Bitonic Sort**: log²(N) passes of compare-and-swap in shared memory
3. **Reorder**: Scatter particles to sorted positions (or use index buffer for indirect rendering)

For ADDITIVE particles, sorting is skipped (order-independent).

Sort is dispatched only when needed and only for layers with ALPHA_BLEND mode.

## Depth Buffer Collision

Particles can collide with scene geometry by sampling the depth buffer:

1. Project particle position to screen space
2. Sample scene depth at that screen position
3. If particle depth > scene depth (behind geometry), push particle to surface
4. Apply restitution and friction to velocity

This gives "free" collision with all rendered geometry without additional collider setup.

## Modified Files

| File | Changes |
|------|---------|
| New: `renderer/gpu_particle_system.h` | Core GPU pipeline: SSBO management, compute dispatch, indirect draw |
| New: `renderer/gpu_particle_system.cpp` | Implementation of GPU compute pipeline |
| New: `scene/gpu_particle_emitter.h` | Emitter component with composable behavior system |
| New: `scene/gpu_particle_emitter.cpp` | Behavior configuration, CPU↔GPU data upload |
| New: `shaders/particle_emit.comp.glsl` | Emission compute shader |
| New: `shaders/particle_simulate.comp.glsl` | Simulation compute shader (behaviors, physics, aging) |
| New: `shaders/particle_compact.comp.glsl` | Stream compaction (remove dead particles) |
| New: `shaders/particle_sort.comp.glsl` | Bitonic merge sort for transparency |
| New: `shaders/particle_indirect.comp.glsl` | Update indirect draw command from alive count |
| New: `shaders/particle_gpu.vert.glsl` | Billboard vertex shader reading from SSBO |
| Mod: `renderer/particle_renderer.h/.cpp` | Add GPU rendering path alongside existing CPU path |
| Mod: `scene/particle_presets.h/.cpp` | Add GPU preset variants |
| New: `tests/test_gpu_particle_system.cpp` | Unit tests |

## API Design

### GPUParticleSystem (Low-Level GPU Pipeline)

```cpp
class GPUParticleSystem
{
public:
    bool init(const std::string& shaderPath, uint32_t maxParticles);
    void shutdown();

    // Per-frame pipeline
    void emit(uint32_t count, const EmissionParams& params);
    void simulate(float deltaTime, const BehaviorBlock& behaviors);
    void compact();
    void sort(const glm::mat4& viewMatrix);  // Optional, for alpha blend
    void updateIndirectCommand();

    // Rendering
    void bindForRendering();                  // Bind SSBO + indirect buffer
    GLuint getIndirectBuffer() const;
    GLuint getParticleSSBO() const;
    uint32_t getMaxParticles() const;

    // Debug
    uint32_t readAliveCount() const;          // CPU readback (debug only)
};
```

### GPUParticleEmitter (High-Level Component)

```cpp
class GPUParticleEmitter : public Component
{
public:
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    // Configuration (same format as CPU emitter)
    void setConfig(const ParticleEmitterConfig& config);
    const ParticleEmitterConfig& getConfig() const;

    // Behavior composition
    void addBehavior(ParticleBehaviorType type, const BehaviorParams& params);
    void removeBehavior(ParticleBehaviorType type);
    void clearBehaviors();

    // Rendering mode
    void setBlendMode(ParticleEmitterConfig::BlendMode mode);
    void setTexturePath(const std::string& path);

    // State
    bool isGPUPath() const;                   // true = GPU, false = CPU fallback
    uint32_t getAliveCount() const;           // Approximate (1-frame delay)

    // Access to underlying GPU system (for renderer)
    GPUParticleSystem* getGPUSystem();
    const GPUParticleSystem* getGPUSystem() const;
};
```

### EmissionParams (CPU → GPU upload)

```cpp
struct EmissionParams
{
    glm::vec3 worldPosition;          // Emitter world position
    glm::mat3 worldRotation;          // Emitter orientation
    ParticleEmitterConfig::Shape shape;
    float shapeRadius;
    float shapeConeAngle;
    glm::vec3 shapeBoxSize;
    float startLifetimeMin, startLifetimeMax;
    float startSpeedMin, startSpeedMax;
    float startSizeMin, startSizeMax;
    glm::vec4 startColor;
    uint32_t randomSeed;              // Per-frame random seed
};
```

### BehaviorBlock (CPU → GPU uniform buffer)

```cpp
struct BehaviorEntry
{
    ParticleBehaviorType type;
    uint32_t flags;
    float params[6];
};

struct BehaviorBlock
{
    BehaviorEntry behaviors[16];
    int behaviorCount;

    // Over-lifetime curves (baked to GPU-friendly format)
    glm::vec4 colorStops[8];
    float colorStopTimes[8];
    int colorStopCount;

    float sizeKeys[8];
    float sizeKeyTimes[8];
    int sizeKeyCount;

    float speedKeys[8];
    float speedKeyTimes[8];
    int speedKeyCount;
};
```

### ParticleBehaviorType Enum

```cpp
enum class ParticleBehaviorType : uint32_t
{
    GRAVITY     = 0,
    DRAG        = 1,
    NOISE       = 2,
    ORBIT       = 3,
    ATTRACT     = 4,
    VORTEX      = 5,
    TURBULENCE  = 6,
    WIND        = 7,

    // Collision
    DEPTH_BUFFER_COLLISION = 10,
    GROUND_PLANE           = 11,
    SPHERE_COLLIDER        = 12,

    // Kill conditions
    KILL_ON_COLLISION      = 20,
};
```

## Curl Noise Implementation

For turbulence/noise force behavior, we use 3D curl noise:

```glsl
// Curl of a 3D noise field gives divergence-free velocity
// This creates swirling, realistic turbulence
vec3 curlNoise(vec3 p, float frequency)
{
    float eps = 0.001;
    vec3 dx = vec3(eps, 0, 0);
    vec3 dy = vec3(0, eps, 0);
    vec3 dz = vec3(0, 0, eps);

    float x = snoise(p + dy) - snoise(p - dy)
            - snoise(p + dz) + snoise(p - dz);
    float y = snoise(p + dz) - snoise(p - dz)
            - snoise(p + dx) + snoise(p - dx);
    float z = snoise(p + dx) - snoise(p - dx)
            - snoise(p + dy) + snoise(p - dy);

    return vec3(x, y, z) / (2.0 * eps);
}
```

Uses simplex noise (public domain, Ashima Arts implementation).

## Performance Expectations

| Particle Count | CPU Path | GPU Path | Notes |
|---------------|----------|----------|-------|
| 100 | 0.01ms | 0.05ms | CPU wins (dispatch overhead) |
| 500 | 0.05ms | 0.06ms | Break-even point |
| 5,000 | 0.5ms | 0.08ms | GPU 6x faster |
| 50,000 | 5.0ms | 0.15ms | GPU 33x faster |
| 500,000 | N/A | 0.8ms | GPU-only territory |

## Test Plan

- **Emission**: Verify particle count increases correctly after emit dispatch
- **Simulation**: Gravity moves particles downward, drag reduces velocity
- **Compaction**: Dead particles removed, alive count correct after compact
- **Behavior composition**: Multiple behaviors applied correctly
- **Over-lifetime**: Color/size/speed interpolation matches expected curves
- **Configuration**: All ParticleEmitterConfig fields respected
- **CPU/GPU parity**: Same config produces similar results on both paths
- **Indirect draw**: Instance count matches alive count without CPU readback
- **Sort**: Alpha-blend particles sorted back-to-front (visual test)
- **Depth collision**: Particles bounce off scene geometry (visual test)

## Niagara Feature Comparison

| Feature | Niagara | Vestige GPU Particles | Notes |
|---------|---------|----------------------|-------|
| Composable behaviors | ✅ Module stack | ✅ Behavior list | Same pattern, different naming |
| GPU simulation | ✅ Compute shaders | ✅ Compute shaders | Standard technique |
| CPU fallback | ✅ | ✅ Auto-select | Based on maxParticles threshold |
| Billboard renderer | ✅ | ✅ | Camera-facing quads |
| Mesh renderer | ✅ | 🔮 Future | Instanced mesh particles |
| Ribbon renderer | ✅ | 🔮 Future | Connected strips |
| Events | ✅ CPU-only | ✅ CPU-only | GPU events are hard |
| Indirect draw | ✅ | ✅ | No CPU readback |
| GPU sorting | ✅ | ✅ Bitonic | For alpha blend |
| Depth collision | ✅ | ✅ | Sample scene depth buffer |
| Curl noise | ✅ | ✅ | Divergence-free turbulence |
| Coupled lights | ✅ | ✅ CPU path only | GPU lights complex |
| LOD/distance culling | ✅ | 🔮 Future | Per-layer LOD |
| Budget system | ✅ | 🔮 Future | Global particle budget |
| Data interfaces | ✅ | 🔮 Future | External data binding |
| Simulation stages | ✅ | ❌ Not needed | Iterative GPU compute |
