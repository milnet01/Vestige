# Phase 5E: Particle Emitter Editor & Water Surface Editor — Design Document

## Goal

Add two new visual systems to the engine — a particle emitter and a water surface — each with full real-time editor support. Both systems render in the main viewport, are configurable via inspector panels, integrate with undo/redo and scene serialization, and maintain the 60 FPS minimum.

**Milestone:** The user can place a particle emitter and a water surface into a scene, tweak all parameters in real-time, save/load the scene with those elements preserved, and undo/redo all edits.

---

## Current State (End of Phase 5D)

The editor now has:
- **Complete scene persistence:** Save/load `.scene` JSON files, auto-save, crash recovery, recent files
- **Full undo/redo:** Command pattern with 200-command history, merge support for slider drags
- **Content pipeline:** Asset browser, material editor, texture thumbnails, drag-drop
- **Scene construction:** Primitives, model import, prefabs, duplicate/delete, grouping
- **Light editing:** Point/spot/directional with viewport gizmos
- **Rendering pipeline:** Forward rendering with PBR, shadow mapping (cascaded + point), SSAO, bloom, tonemapping, TAA, frustum culling, instancing

**What's missing:**
- No particle effects — scenes are static
- No water surfaces — architectural walkthroughs need pools, basins, fountains
- No curve editor or gradient editor widgets in ImGui

---

## Research Summary

Two research documents were produced (see `docs/` folder):
- `PARTICLE_SYSTEM_RESEARCH.md` — Engine comparisons (Unity/Unreal/Godot/O3DE), GPU vs CPU particles, billboard rendering, SoA data layout, undo integration, serialization
- Water surface research (inline in this document) — Planar reflections, Fresnel, wave models, water shaders, engine editor UIs, FBO setup

### Key Design Decisions from Research

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Particle simulation | CPU-based with SoA data layout | Simpler to implement/debug; integrates naturally with undo/redo; 500K particles at 70+ FPS on modest hardware; design for future GPU backend |
| Particle rendering | Instanced billboards (`glDrawArraysInstanced`) | Single draw call per system; static quad VBO + per-instance data; proven efficient |
| Particle blending | Additive default, alpha blend optional | Additive is order-independent (no sorting needed); alpha blend adds camera-distance sort |
| Particle architecture | Generator/Updater pattern (C++ Stories) | Composable, modular; each emission shape = generator, each over-lifetime modifier = updater |
| Editor UI model | Unity-style fixed modules with collapsible sections | Proven UX; simpler than Unreal's Niagara stack; appropriate for small engine |
| Parameter modes | Constant, Random Between Two, Curve | Covers vast majority of use cases; matches Unity approach |
| Water wave model | Sum of sines, upgrade to Gerstner later | Sine is simplest; Gerstner adds quality with minimal cost; FFT is overkill for pools |
| Water mesh | 32x32 to 64x64 flat grid | Architectural water is contained/small; tessellation not needed initially |
| Water reflection | Planar reflection via FBO at half resolution | Best quality for flat water; engine uses forward rendering (no G-buffer for SSR) |
| Water refraction | FBO with depth texture | Enables depth-based color blending and edge foam |
| Fresnel | Schlick approximation (F0 = 0.02) | Already familiar from PBR system; physically correct for water |
| Surface detail | Dual normal map sampling + DuDv distortion | Standard, proven technique; good visual quality |
| Curve widget | ImGui Bezier widget (ocornut/imgui#786) | Public domain, single-function; start with linear multi-keyframe |
| Gradient widget | ImGradient (galloscript gist) | Add/remove/drag color stops with color picker |
| Serialization | JSON via nlohmann/json | Consistent with existing EntitySerializer; emitter config only, not runtime state |

---

## Architecture Overview

Phase 5E adds six new subsystems:

```
+------------------------------------------------------------------+
|                            Editor                                 |
|                                                                   |
|  Particle System                    Water System                  |
|  +--------------------------+       +---------------------------+ |
|  | ParticleEmitterComponent |       | WaterSurfaceComponent     | |
|  | (entity component,       |       | (entity component,        | |
|  |  emitter config,         |       |  wave/color/foam params,  | |
|  |  runtime state)          |       |  grid mesh, FBOs)         | |
|  +--------------------------+       +---------------------------+ |
|  | ParticleData (SoA)       |       | WaterRenderer             | |
|  | (positions, velocities,  |       | (reflection/refraction    | |
|  |  colors, sizes, ages)    |       |  FBO passes, water        | |
|  +--------------------------+       |  shader, composition)     | |
|  | ParticleRenderer         |       +---------------------------+ |
|  | (billboard instancing,   |                                     |
|  |  blending, soft particles|       Shared Widgets               |
|  +--------------------------+       +---------------------------+ |
|                                     | CurveEditor (ImGui)       | |
|                                     | GradientEditor (ImGui)    | |
|                                     +---------------------------+ |
+------------------------------------------------------------------+
```

### Integration Points

- **Scene/Entity System:** Both particle emitters and water surfaces are entity components, following the existing `MeshRenderer`/`LightComponent` pattern
- **Inspector Panel:** New collapsible sections in the inspector when a particle emitter or water surface entity is selected
- **Undo/Redo:** Property-level commands extending `EditorCommand`, with merge for slider drags
- **Scene Serialization:** Extend `EntitySerializer` with `serializeParticleEmitter()`/`serializeWaterSurface()`
- **Create Menu:** Add "Particle Emitter" and "Water Surface" to the entity creation menu
- **Rendering Pipeline:** Particles render after opaques (transparent pass); water renders with reflection/refraction FBO passes before the main transparent pass

---

## Sub-Phase Breakdown

### 5E-1: Particle System Core (~600 lines)

**Goal:** Basic particle emitter that spawns, updates, and renders billboard particles in the viewport.

**New Files:**
| File | Purpose |
|------|---------|
| `engine/scene/particle_emitter.h` | `ParticleEmitterConfig` struct (all parameters), `ParticleData` SoA container |
| `engine/scene/particle_emitter.cpp` | CPU simulation: spawn, update (gravity, age, kill), SoA compaction |
| `engine/renderer/particle_renderer.h` | Billboard instanced rendering, buffer management |
| `engine/renderer/particle_renderer.cpp` | VBO/VAO setup, per-frame buffer upload, draw call |
| `assets/shaders/particle.vert` | Billboard vertex shader (camera-facing quads) |
| `assets/shaders/particle.frag` | Fragment shader (texture sample, color modulation, alpha) |

**ParticleData (SoA Container):**
```cpp
struct ParticleData
{
    int count = 0;
    int maxCount = 0;

    // SoA arrays — pre-allocated to maxCount
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> velocities;
    std::vector<glm::vec4> colors;         // current color (RGBA)
    std::vector<float>     sizes;          // current size
    std::vector<float>     ages;           // seconds alive
    std::vector<float>     lifetimes;      // max lifetime per particle

    void resize(int max);
    void kill(int index);    // swap with last, decrement count
    void spawn(int index);   // initialize at count, increment
};
```

**ParticleEmitterConfig:**
```cpp
struct ParticleEmitterConfig
{
    // Emission
    float emissionRate = 10.0f;          // particles per second
    int maxParticles = 1000;
    bool looping = true;
    float duration = 5.0f;               // system duration (if not looping)

    // Start properties
    float startLifetimeMin = 1.0f;
    float startLifetimeMax = 3.0f;
    float startSpeedMin = 1.0f;
    float startSpeedMax = 3.0f;
    float startSizeMin = 0.1f;
    float startSizeMax = 0.5f;
    glm::vec4 startColor = {1, 1, 1, 1};

    // Forces
    glm::vec3 gravity = {0, -9.81f, 0};

    // Emission shape
    enum class Shape { POINT, SPHERE, CONE, BOX };
    Shape shape = Shape::POINT;
    float shapeRadius = 1.0f;
    float shapeConeAngle = 25.0f;        // degrees
    glm::vec3 shapeBoxSize = {1, 1, 1};

    // Rendering
    enum class BlendMode { ADDITIVE, ALPHA_BLEND };
    BlendMode blendMode = BlendMode::ADDITIVE;
    std::string texturePath;              // optional particle texture
};
```

**Billboard Vertex Shader (camera-facing quads):**
```glsl
// Per-vertex (static quad: 4 vertices, 2 triangles)
layout(location = 0) in vec2 a_quadPos;    // (-0.5,-0.5) to (0.5,0.5)

// Per-instance
layout(location = 1) in vec3 a_worldPos;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_size;

uniform mat4 u_viewProjection;
uniform vec3 u_cameraRight;
uniform vec3 u_cameraUp;

out vec2 v_texCoord;
out vec4 v_color;

void main()
{
    vec3 worldPos = a_worldPos
        + u_cameraRight * a_quadPos.x * a_size
        + u_cameraUp * a_quadPos.y * a_size;
    gl_Position = u_viewProjection * vec4(worldPos, 1.0);
    v_texCoord = a_quadPos + 0.5;
    v_color = a_color;
}
```

**Rendering Integration:**
- Particles render in the transparent pass (after opaques, before post-processing)
- Depth writes OFF (`glDepthMask(GL_FALSE)`), depth test ON
- Additive: `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`
- Alpha blend: `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` with back-to-front sort

**Tests:**
- `test_particle_data.cpp`: SoA spawn/kill/compaction, resize, age update, lifetime expiry

---

### 5E-2: Particle Editor Integration (~500 lines)

**Goal:** Particle emitters are entity components, editable in the inspector, with undo/redo and serialization.

**New/Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/scene/components.h` | Modify | Add `ParticleEmitterComponent` to entity |
| `engine/editor/panels/inspector_panel.cpp` | Modify | Add particle emitter inspector section |
| `engine/editor/commands/particle_property_command.h` | Create | Undo/redo for particle config changes |
| `engine/scene/entity_serializer.cpp` | Modify | Serialize/deserialize particle emitters |
| `engine/editor/entity_factory.cpp` | Modify | Add "Particle Emitter" to create menu |
| `engine/core/engine.cpp` | Modify | Update particle systems each frame; render in transparent pass |

**Inspector Panel Layout:**
```
[Particle Emitter]                    [x] (remove component)
  [Play] [Pause] [Restart]

  ▼ Emission
    Rate:           [====10.0====] particles/sec
    Max Particles:  [===1000=====]
    Looping:        [✓]
    Duration:       [====5.0=====] sec

  ▼ Start Properties
    Lifetime:       [==1.0==] — [==3.0==]
    Speed:          [==1.0==] — [==3.0==]
    Size:           [==0.1==] — [==0.5==]
    Color:          [■ White ■]

  ▼ Shape
    Type:           [Point ▼]
    Radius:         [====1.0====]

  ▼ Forces
    Gravity:        [0] [-9.81] [0]

  ▼ Renderer
    Blend Mode:     [Additive ▼]
    Texture:        [None] [Browse...]
```

**Undo/Redo:** Each slider/field change creates a `ParticlePropertyCommand` that stores old/new `ParticleEmitterConfig`. Uses `canMergeWith()` for consecutive slider drags on the same property.

**Serialization (JSON):**
```json
{
  "particleEmitter": {
    "emissionRate": 10.0,
    "maxParticles": 1000,
    "looping": true,
    "startLifetimeMin": 1.0,
    "startLifetimeMax": 3.0,
    "startSpeedMin": 1.0,
    "startSpeedMax": 3.0,
    "startSizeMin": 0.1,
    "startSizeMax": 0.5,
    "startColor": [1, 1, 1, 1],
    "gravity": [0, -9.81, 0],
    "shape": "point",
    "shapeRadius": 1.0,
    "blendMode": "additive",
    "texturePath": ""
  }
}
```

**Tests:**
- `test_particle_emitter.cpp`: Component add/remove, serialization round-trip, property command undo/redo

---

### 5E-3: Over-Lifetime Modifiers & Curve/Gradient Widgets (~500 lines)

**Goal:** Particles can change color, size, and speed over their lifetime using curves and gradients.

**New/Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/editor/widgets/curve_editor.h` | Create | ImGui curve editor widget (multi-keyframe linear interpolation) |
| `engine/editor/widgets/curve_editor.cpp` | Create | Curve editing: add/remove/drag keyframes, evaluate |
| `engine/editor/widgets/gradient_editor.h` | Create | ImGui color gradient widget (color stops with picker) |
| `engine/editor/widgets/gradient_editor.cpp` | Create | Gradient editing: add/remove/drag stops, evaluate |
| `engine/scene/particle_emitter.h` | Modify | Add over-lifetime fields to config |
| `engine/scene/particle_emitter.cpp` | Modify | Apply over-lifetime modifiers during update |
| `engine/editor/panels/inspector_panel.cpp` | Modify | Add curve/gradient editors to particle inspector |

**Curve Data Structure:**
```cpp
struct AnimationCurve
{
    struct Keyframe
    {
        float time;    // 0.0 to 1.0 (normalized lifetime)
        float value;
    };
    std::vector<Keyframe> keyframes = {{0.0f, 1.0f}, {1.0f, 0.0f}};

    float evaluate(float t) const;  // linear interpolation between keyframes
};
```

**Gradient Data Structure:**
```cpp
struct ColorGradient
{
    struct ColorStop
    {
        float position;    // 0.0 to 1.0
        glm::vec4 color;
    };
    std::vector<ColorStop> stops = {{0.0f, {1,1,1,1}}, {1.0f, {1,1,1,0}}};

    glm::vec4 evaluate(float t) const;  // linear interpolation between stops
};
```

**Over-Lifetime Parameters Added to Config:**
```cpp
// Added to ParticleEmitterConfig:
bool useColorOverLifetime = false;
ColorGradient colorOverLifetime;

bool useSizeOverLifetime = false;
AnimationCurve sizeOverLifetime;    // multiplier applied to startSize

bool useSpeedOverLifetime = false;
AnimationCurve speedOverLifetime;   // multiplier applied to velocity magnitude
```

**Update Loop Integration:**
```cpp
float normalizedAge = particle.age / particle.lifetime;  // 0.0 to 1.0

if (config.useColorOverLifetime)
    particle.color = config.colorOverLifetime.evaluate(normalizedAge);

if (config.useSizeOverLifetime)
    particle.size = startSize * config.sizeOverLifetime.evaluate(normalizedAge);

if (config.useSpeedOverLifetime)
{
    float speedMult = config.speedOverLifetime.evaluate(normalizedAge);
    particle.velocity = glm::normalize(particle.velocity) * startSpeed * speedMult;
}
```

**Tests:**
- `test_animation_curve.cpp`: Evaluate at keyframes, between keyframes, before first, after last
- `test_color_gradient.cpp`: Evaluate at stops, between stops, single-stop gradient

---

### 5E-4: Water Surface Core (~600 lines)

**Goal:** A water surface entity that renders with reflections, refraction, and wave animation.

**New Files:**
| File | Purpose |
|------|---------|
| `engine/scene/water_surface.h` | `WaterSurfaceConfig` struct, `WaterSurface` class (mesh, params) |
| `engine/scene/water_surface.cpp` | Grid mesh generation, parameter management |
| `engine/renderer/water_renderer.h` | Reflection/refraction FBO passes, water shader |
| `engine/renderer/water_renderer.cpp` | Rendering pipeline integration, FBO management |
| `assets/shaders/water.vert` | Vertex shader (sine wave displacement) |
| `assets/shaders/water.frag` | Fragment shader (Fresnel, reflection/refraction blend, normal mapping) |
| `assets/textures/water_normal.png` | Default water normal map (tileable) |
| `assets/textures/water_dudv.png` | Default DuDv distortion map |

**WaterSurfaceConfig:**
```cpp
struct WaterSurfaceConfig
{
    // Geometry
    float width = 10.0f;
    float depth = 10.0f;
    int gridResolution = 32;         // NxN vertices

    // Waves (up to 4 summed sine waves)
    static constexpr int MAX_WAVES = 4;
    struct Wave
    {
        float amplitude = 0.02f;
        float wavelength = 2.0f;
        float speed = 0.5f;
        float direction = 0.0f;      // degrees
    };
    int numWaves = 2;
    Wave waves[MAX_WAVES] = {
        {0.02f, 2.0f, 0.5f, 0.0f},
        {0.01f, 1.5f, 0.3f, 45.0f},
        {0.015f, 3.0f, 0.4f, 90.0f},
        {0.005f, 1.0f, 0.6f, 135.0f}
    };

    // Colors
    glm::vec4 shallowColor = {0.1f, 0.4f, 0.5f, 0.8f};
    glm::vec4 deepColor = {0.0f, 0.1f, 0.2f, 1.0f};
    float depthDistance = 5.0f;

    // Surface
    float refractionStrength = 0.02f;
    float normalStrength = 1.0f;
    float dudvStrength = 0.02f;
    float flowSpeed = 0.3f;
    float specularPower = 128.0f;

    // Foam
    glm::vec3 foamColor = {1.0f, 1.0f, 1.0f};
    float foamThreshold = 0.8f;
    float foamFadeDistance = 1.0f;

    // Reflection
    float reflectionResolutionScale = 0.5f;  // 0.25 to 1.0
};
```

**Water Vertex Shader (sine wave displacement):**
```glsl
uniform float u_time;
uniform int u_numWaves;
uniform vec4 u_waveParams[4];  // (amplitude, wavelength, speed, direction_radians)

vec3 sumOfSines(vec3 pos)
{
    float height = 0.0;
    for (int i = 0; i < u_numWaves; i++)
    {
        float A = u_waveParams[i].x;
        float w = 2.0 * PI / u_waveParams[i].y;  // frequency
        float phi = u_waveParams[i].z * w;         // phase
        float dir = u_waveParams[i].w;
        vec2 D = vec2(cos(dir), sin(dir));
        height += A * sin(dot(D, pos.xz) * w + u_time * phi);
    }
    return vec3(pos.x, pos.y + height, pos.z);
}
```

**Water Fragment Shader (core logic):**
```glsl
uniform sampler2D u_reflectionTex;
uniform sampler2D u_refractionTex;
uniform sampler2D u_refractionDepth;
uniform sampler2D u_normalMap;
uniform sampler2D u_dudvMap;

// Fresnel blend
float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 5.0);
fresnel = F0 + (1.0 - F0) * fresnel;  // Schlick, F0 = 0.02

// DuDv distortion
vec2 distortion = texture(u_dudvMap, texCoords + u_time * flowSpeed).rg * 2.0 - 1.0;
distortion *= u_dudvStrength;

// Sample reflection and refraction with distortion
vec4 reflection = texture(u_reflectionTex, reflectCoords + distortion);
vec4 refraction = texture(u_refractionTex, refractCoords + distortion);

// Depth-based color
float depth = linearizeDepth(texture(u_refractionDepth, refractCoords).r) - fragDepth;
vec4 waterColor = mix(u_shallowColor, u_deepColor, clamp(depth / u_depthDistance, 0.0, 1.0));
refraction = mix(refraction, waterColor, waterColor.a);

// Final blend
fragColor = mix(refraction, reflection, fresnel);
```

**Rendering Pipeline Integration:**
1. Before the main geometry pass, render reflection FBO (mirrored camera, clip above water) and refraction FBO (clip below water)
2. Use `glEnable(GL_CLIP_DISTANCE0)` with `gl_ClipDistance[0]` in the scene vertex shader
3. Render water surface in the transparent pass, after opaques
4. **Important:** Restore `glClipControl` state after reflection pass (Mesa AMD driver requirement per existing memory note)

**Tests:**
- `test_water_surface.cpp`: Grid mesh generation, wave evaluation, config serialization

---

### 5E-5: Water Surface Editor & Polish (~400 lines)

**Goal:** Water surface is an entity component, editable in the inspector, with undo/redo, serialization, and presets.

**New/Modified Files:**
| File | Action | Purpose |
|------|--------|---------|
| `engine/scene/components.h` | Modify | Add `WaterSurfaceComponent` to entity |
| `engine/editor/panels/inspector_panel.cpp` | Modify | Add water surface inspector section |
| `engine/editor/commands/water_property_command.h` | Create | Undo/redo for water config changes |
| `engine/scene/entity_serializer.cpp` | Modify | Serialize/deserialize water surfaces |
| `engine/editor/entity_factory.cpp` | Modify | Add "Water Surface" to create menu |

**Inspector Panel Layout:**
```
[Water Surface]                       [x] (remove component)

  ▼ Geometry
    Width:          [====10.0===]
    Depth:          [====10.0===]
    Grid Resolution:[=====32====]

  ▼ Waves
    Active Waves:   [====2======]
    Wave 1:
      Amplitude:    [===0.02====]
      Wavelength:   [===2.0=====]
      Speed:        [===0.5=====]
      Direction:    [===0°======]
    Wave 2: ...

  ▼ Colors
    Shallow Color:  [■ Teal ■]
    Deep Color:     [■ Dark Blue ■]
    Depth Distance: [====5.0====]

  ▼ Surface Detail
    Normal Strength:[====1.0====]
    DuDv Strength:  [===0.02====]
    Flow Speed:     [===0.3=====]
    Specular Power: [===128=====]

  ▼ Foam
    Foam Color:     [■ White ■]
    Foam Threshold: [===0.8=====]
    Fade Distance:  [===1.0=====]

  ▼ Presets
    [Pool] [Fountain] [Basin] [Custom]
```

**Water Presets:**
```cpp
static const WaterSurfaceConfig PRESET_POOL = {
    .waves = {{0.005f, 3.0f, 0.2f, 0.0f}, {0.003f, 2.0f, 0.15f, 90.0f}},
    .shallowColor = {0.15f, 0.5f, 0.6f, 0.7f},
    .deepColor = {0.02f, 0.15f, 0.3f, 1.0f},
    // ... calm, transparent
};

static const WaterSurfaceConfig PRESET_FOUNTAIN = {
    .waves = {{0.03f, 1.0f, 1.0f, 0.0f}, {0.02f, 0.8f, 0.8f, 60.0f},
              {0.015f, 1.5f, 0.6f, 120.0f}},
    .shallowColor = {0.2f, 0.5f, 0.55f, 0.6f},
    // ... more active, more transparent
};

static const WaterSurfaceConfig PRESET_BASIN = {
    .waves = {{0.001f, 4.0f, 0.1f, 0.0f}},
    .shallowColor = {0.1f, 0.3f, 0.35f, 0.9f},
    .deepColor = {0.02f, 0.08f, 0.15f, 1.0f},
    // ... very calm, opaque
};
```

**Serialization (JSON):**
```json
{
  "waterSurface": {
    "width": 10.0,
    "depth": 10.0,
    "gridResolution": 32,
    "numWaves": 2,
    "waves": [
      {"amplitude": 0.02, "wavelength": 2.0, "speed": 0.5, "direction": 0.0},
      {"amplitude": 0.01, "wavelength": 1.5, "speed": 0.3, "direction": 45.0}
    ],
    "shallowColor": [0.1, 0.4, 0.5, 0.8],
    "deepColor": [0.0, 0.1, 0.2, 1.0],
    "depthDistance": 5.0,
    "refractionStrength": 0.02,
    "normalStrength": 1.0,
    "dudvStrength": 0.02,
    "flowSpeed": 0.3,
    "foamColor": [1.0, 1.0, 1.0],
    "foamThreshold": 0.8
  }
}
```

**Tests:**
- `test_water_editor.cpp`: Preset application, property command undo/redo, serialization round-trip

---

## Performance Budget

| Component | Estimated Cost | Notes |
|-----------|---------------|-------|
| Particle update (1000 particles) | < 0.1ms | SoA cache-friendly iteration |
| Particle render (1000 particles) | < 0.2ms | Single instanced draw call |
| Water reflection FBO (half-res) | 2-3ms | Scene re-render at 1/4 pixel count |
| Water refraction FBO | 1-2ms | Scene with clip plane |
| Water surface draw | < 0.5ms | Single draw call, simple shader |
| **Total 5E overhead** | **~4-6ms** | Fits within 16.67ms budget |

**Optimization levers if needed:**
- Reflection FBO at 1/4 resolution instead of 1/2
- Skip shadows in reflection pass (ambient only)
- Update reflection every other frame
- Reduce particle max count

---

## Accessibility Considerations

- All sliders have numeric input fallback (double-click to type)
- Color pickers include hex input for precise values
- Curve and gradient editors support keyboard navigation (Tab between keyframes, arrow keys to adjust)
- Wave direction uses a numeric field in addition to any visual direction picker
- Preset buttons have tooltip descriptions

---

## Implementation Order

1. **5E-1:** Particle core (SoA data, CPU sim, billboard renderer)
2. **5E-2:** Particle editor (component, inspector, undo/redo, serialization)
3. **5E-3:** Over-lifetime modifiers + curve/gradient widgets
4. **5E-4:** Water surface core (grid mesh, FBOs, water shader)
5. **5E-5:** Water editor (component, inspector, undo/redo, presets, serialization)

Each sub-phase is self-contained and can be tested independently. The curve/gradient widgets (5E-3) are shared infrastructure that water could also use in the future (e.g., depth color gradient).

---

## Sources

### Particle Systems
- [Unity Particle System Modules](https://docs.unity3d.com/Manual/ParticleSystemModules.html)
- [Niagara Emitter Reference (UE)](https://docs.unrealengine.com/4.27/en-US/RenderingAndGraphics/Niagara/EmitterReference)
- [Godot GPUParticles3D](https://docs.godotengine.org/en/stable/classes/class_gpuparticles3d.html)
- [Flexible Particle System (C++ Stories)](https://www.cppstories.com/2014/04/flexible-particle-system-start/)
- [OpenGL Billboard Tutorial](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/billboards/)
- [OpenGL Particles/Instancing Tutorial](http://www.opengl-tutorial.org/intermediate-tutorials/billboards-particles/particles-instancing/)
- [Object Pool Pattern (Game Programming Patterns)](https://gameprogrammingpatterns.com/object-pool.html)
- [ImGui Bezier Widget (Issue #786)](https://github.com/ocornut/imgui/issues/786)
- [ImGradient Color Editor (Gist)](https://gist.github.com/galloscript/8a5d179e432e062550972afcd1ecf112)

### Water Rendering
- [GPU Gems Ch. 1: Effective Water Simulation](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models)
- [ThinMatrix OpenGL Water Tutorial Series](https://thinmatrix.tumblr.com/post/116745646572/opengl-water-tutorial-1-introduction)
- [Simplest Way to Render Pretty Water (Medium)](https://medium.com/@vincehnguyen/simplest-way-to-render-pretty-water-in-opengl-7bce40cbefbe)
- [Catlike Coding: Waves](https://catlikecoding.com/unity/tutorials/flow/waves/)
- [Flow Maps for Animating Water](http://graphicsrunner.blogspot.com/2010/08/water-using-flow-maps.html)
- [3D Game Shaders: Flow Mapping](https://lettier.github.io/3d-game-shaders-for-beginners/flow-mapping.html)
- [GPU Gems Ch. 2: Rendering Water Caustics](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics)
- [Enscape Water Best Practices](https://blog.chaos.com/best-practice-for-water-in-architectural-design)
- [Unity HDRP Water System](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/WaterSystem-use.html)
- [Unreal Engine Water System](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-system-in-unreal-engine)
- [CryEngine Water Shader](https://docs.cryengine.com/display/CEMANUAL/Water+Shader)

See also: `docs/PARTICLE_SYSTEM_RESEARCH.md` for the full 800+ line research document with all sources.
