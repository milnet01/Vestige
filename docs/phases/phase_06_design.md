# Phase 6: Particle and Effects System — Design Document

Design document for the Vestige 3D Engine, Phase 6.
Created 2026-03-24. Based on research in `docs/phases/phase_06_research.md`.

---

## Overview

Phase 6 upgrades the existing CPU-based particle system and basic water renderer from Phase 5E into a full effects pipeline. The work is organized into 5 sub-phases:

| Sub-Phase | Title | Effort | Dependencies |
|-----------|-------|--------|--------------|
| **6-1** | Pipeline Integration | Small | None |
| **6-2** | Particle Textures, Soft Particles & Presets | Medium | 6-1 |
| **6-3** | Fire, Smoke & Atmosphere Effects | Medium | 6-2 |
| **6-4** | Water Reflection & Refraction | Medium-Large | 6-1 |
| **6-5** | Water Caustics & Depth Effects | Small-Medium | 6-4 |

**Design decision — CPU vs GPU particles:** The existing CPU particle system supports editor preview, undo/redo, and serialization well. A full GPU compute migration (dead/alive lists, ping-pong SSBOs, indirect dispatch) is significant infrastructure with diminishing returns for the engine's typical particle counts (10K–50K). The CPU system already handles 500K+ particles at 70 FPS per Phase 5E benchmarks. **We keep CPU simulation and focus on rendering quality instead.** GPU compute particles are deferred to Phase 11 (GPU-Driven Rendering) when the infrastructure for compute dispatch, indirect draw, and SSBO management is more mature.

---

## Phase 6-1: Pipeline Integration

**Goal:** Wire the existing `ParticleRenderer` and `WaterRenderer` into `Renderer::renderScene()` so particles and water actually appear on screen.

**Current state:** `SceneRenderData` collects `particleEmitters` and `waterSurfaces` from the scene, but `renderScene()` never calls the particle or water renderers.

### Render Order

Insert into `Renderer::renderScene()` after transparent geometry, before post-processing:

```
1. Shadow passes (directional + point)
2. Opaque geometry (MDI or instanced)
3. Skybox
4. Terrain
5. Transparent geometry (back-to-front sorted)
6. ** Water surfaces ** (alpha-blended, depth-write off)
7. ** Particles ** (after water so fire/smoke above water renders correctly)
8. Post-processing (bloom, TAA/SMAA, tone mapping)
```

Water renders before particles so that:
- Particles on top of water blend correctly
- Water reflections don't include particles (intentional — particles are transient and excluding them avoids visual noise in reflections)

### Implementation

**Modified files:**
- `engine/renderer/renderer.h` — Add `ParticleRenderer m_particleRenderer` and `WaterRenderer m_waterRenderer` members (move from standalone to renderer-owned).
- `engine/renderer/renderer.cpp` — Init particle/water renderers in `init()`. Call `m_waterRenderer.render(...)` and `m_particleRenderer.render(...)` in `renderScene()` at the correct point. Pass camera, time, lights, and environment cubemap.

**Water rendering call:**
```cpp
// After transparent geometry, before particles
if (!sceneData.waterSurfaces.empty())
{
    m_waterRenderer.render(
        sceneData.waterSurfaces,
        camera, m_frameTime,
        sceneData.directionalLight,
        m_skybox.getCubemapId()
    );
}
```

**Particle rendering call:**
```cpp
// After water
if (!sceneData.particleEmitters.empty())
{
    m_particleRenderer.render(
        sceneData.particleEmitters,
        camera, m_viewProjectionMatrix
    );
}
```

### Testing

- Place a particle emitter and water surface in the demo scene.
- Verify both render correctly in the viewport.
- Verify particles appear above water when positioned above it.
- Verify bloom affects emissive/additive particles.
- Check for depth-sorting issues at particle–geometry boundaries.

---

## Phase 6-2: Particle Textures, Soft Particles & Presets

**Goal:** Add texture support, depth-based soft particles, and preset configurations for common effects.

### 6-2a: Particle Texture Support

The `ParticleEmitterConfig` already has a `texturePath` field, and the shader has a `u_hasTexture` uniform, but texture loading/binding is not wired up.

**Modified files:**
- `engine/renderer/particle_renderer.h/cpp` — Add texture cache (`std::unordered_map<std::string, GLuint>`). Load textures on first use via `ResourceManager`. Bind to texture unit 0 before drawing each emitter.
- `assets/shaders/particle.frag.glsl` — Already handles `u_hasTexture`; verify it samples correctly.

**Texture format:** RGBA PNG. Common particle textures: soft circle, spark, flame sprite, smoke puff. Ship 4–6 default textures in `assets/textures/particles/`.

### 6-2b: Flipbook (Sprite Sheet) Animation

For animated fire/smoke, support sprite sheet textures:

**New config fields in `ParticleEmitterConfig`:**
```cpp
int flipbookRows = 1;       // Sprite sheet grid rows
int flipbookColumns = 1;    // Sprite sheet grid columns
bool flipbookInterpolate = true;  // Blend between adjacent frames
```

**Shader changes (`particle.vert.glsl` or `particle.frag.glsl`):**
- Compute UV sub-region based on `normalizedAge * totalFrames`.
- Pass frame index and blend factor to fragment shader.
- Sample two adjacent frames, interpolate by blend factor.

**New per-instance attribute:**
- `float a_normalizedAge` — uploaded alongside position/color/size. Used for flipbook frame selection and soft-particle depth fade.

### 6-2c: Soft Particles (Depth Fade)

Eliminate hard intersection where billboards clip through geometry.

**Approach:** Pass the scene depth texture to the particle fragment shader. Compare particle depth with scene depth; fade alpha when close.

```glsl
uniform sampler2D u_depthTexture;
uniform vec2 u_screenSize;

// In fragment shader:
vec2 screenUV = gl_FragCoord.xy / u_screenSize;
float sceneDepth = texture(u_depthTexture, screenUV).r;
float particleDepth = gl_FragCoord.z;
// For reverse-Z: particle is in front when particleDepth > sceneDepth
float depthDiff = particleDepth - sceneDepth;
// Convert to linear distance (reverse-Z: near/depth)
float linearDiff = /* linearize depthDiff based on near plane */;
float softFactor = smoothstep(0.0, u_softDistance, linearDiff);
fragColor.a *= softFactor;
```

**Modified files:**
- `engine/renderer/particle_renderer.h/cpp` — Accept depth texture handle. Bind to texture unit 1.
- `assets/shaders/particle.frag.glsl` — Soft particle logic.
- `engine/renderer/renderer.cpp` — Pass resolved depth texture to particle renderer.

**Note:** Vestige uses reverse-Z infinite far plane. Depth linearization is `linearZ = near / depth` (same formula as SDSM). Particles are in front of geometry when `particleDepth > sceneDepth` in reverse-Z.

### 6-2d: Particle Presets

Create a `ParticlePresets` utility class with static factory methods returning pre-configured `ParticleEmitterConfig`:

| Preset | Key Properties |
|--------|---------------|
| `torchFire()` | Cone shape (narrow, upward), additive, orange→red gradient, 100–200 particles, size 0.05–0.2m, lifetime 0.5–1.5s, flipbook fire texture |
| `candleFlame()` | Point shape, tiny size (0.01–0.05m), yellow→orange, 30–50 particles, lifetime 0.3–0.8s |
| `campfire()` | Cone (wider), 200–500 particles, mixed fire+ember sub-emitters (or single with wider spread) |
| `smoke()` | Cone (wide, upward), alpha blend, grey, expanding size (0.1→1.0m), 50–100 particles, lifetime 2–5s |
| `dustMotes()` | Box shape (large volume), additive, white/tan, tiny size (0.002–0.005m), 500–2000 particles, very slow speed, long lifetime 5–15s |
| `incense()` | Point shape, alpha blend, light grey, tiny initial size expanding slowly, 30–60 particles, lifetime 3–6s, strong upward velocity |

**New file:** `engine/scene/particle_presets.h/cpp`

**Editor integration:** Add "Create from Preset" dropdown in the particle emitter inspector panel and in the entity creation menu.

### Testing

- Load each preset in the demo scene, verify visual quality.
- Verify soft particles eliminate hard edges at geometry intersections.
- Verify flipbook animation plays smoothly for fire textures.
- Check that additive particles don't show sorting artifacts.

---

## Phase 6-3: Fire, Smoke & Atmosphere Effects

**Goal:** Complete fire/smoke rendering with light coupling, noise erosion, and atmospheric dust.

### 6-3a: Light Coupling (Fire → Flickering Point Light)

When a particle emitter uses a fire preset, automatically create/update a co-located point light.

**Implementation:**
- Add `bool emitsLight = false`, `glm::vec3 lightColor = {1.0, 0.6, 0.2}`, `float lightRange = 5.0f`, `float lightIntensity = 1.0f`, `float flickerSpeed = 10.0f` to `ParticleEmitterConfig`.
- In `ParticleEmitterComponent::update()`, when `emitsLight` is true, compute a flickering intensity: `intensity = baseIntensity * (0.8 + 0.2 * sin(time * flickerSpeed) * sin(time * flickerSpeed * 1.7 + 0.3))`. Use multi-frequency sin products for organic flicker.
- The coupled light is reported to the scene via a new `getCoupledLight()` method, which returns an optional `PointLight` struct.
- `Scene::collectRenderDataRecursive()` checks each particle emitter for coupled lights and adds them to `data.pointLights`.

**Modified files:**
- `engine/scene/particle_emitter.h/cpp` — Light coupling fields, flickering update, `getCoupledLight()`.
- `engine/scene/scene.cpp` — Collect coupled lights.
- `engine/editor/panels/inspector_panel.cpp` — Expose light coupling UI.

### 6-3b: Noise Erosion for Smoke

Add noise-based alpha erosion to the particle fragment shader for wispy smoke edges.

**Implementation:**
- Ship a default 256×256 tileable noise texture (`assets/textures/particles/noise_erosion.png`).
- New `ParticleEmitterConfig` field: `bool useNoiseErosion = false`.
- In `particle.frag.glsl`:
  ```glsl
  if (u_useNoiseErosion)
  {
      float noise = texture(u_noiseTex, v_texCoord * 2.0 + u_time * 0.1).r;
      float dissolve = v_normalizedAge; // 0 at birth, 1 at death
      float erosion = smoothstep(dissolve - 0.1, dissolve + 0.1, noise);
      fragColor.a *= erosion;
  }
  ```

**Modified files:**
- `assets/shaders/particle.frag.glsl` — Noise erosion logic.
- `engine/renderer/particle_renderer.cpp` — Bind noise texture (unit 2), pass time uniform.
- `engine/scene/particle_emitter.h` — `useNoiseErosion` field.

### 6-3c: Dust Motes

Dust motes are a special case: large number of tiny, slow-moving, long-lived particles rendered as point sprites.

**Implementation option A (simplest):** Just use the existing particle system with the `dustMotes()` preset. Additive blending, tiny size, slow random drift.

**Implementation option B (optimized):** Dedicated `DustMoteSystem` using `GL_POINTS` with `gl_PointSize`:
- Maintains a fixed pool of N particles in a camera-following volume.
- No emission/death — particles that leave the volume are teleported to a random position inside.
- Brightness modulated by shadow map sampling (particles in light glow, particles in shadow invisible).
- Single draw call, no per-frame CPU upload (all on GPU via SSBO or compute).

**Recommendation:** Start with option A (preset). If performance or visual quality needs improvement, upgrade to option B later.

### 6-3d: Incense Smoke

Use the `incense()` preset with noise erosion enabled. The laminar-to-turbulent transition is achieved via:
- Low initial speed spread (nearly vertical emission).
- `useSpeedOverLifetime = true` with a curve that slows particles over time.
- `useSizeOverLifetime = true` with a curve that expands size gradually.
- `useColorOverLifetime = true` with white→grey→transparent gradient.
- `useNoiseErosion = true` for wispy dissolution.

The curl-noise turbulence transition (laminar → turbulent based on age) would be ideal but requires per-particle force modulation that the current CPU system doesn't support cleanly. **Deferred** to the GPU compute particle phase. The preset-based approach produces an acceptable approximation.

### Testing

- Place torch fire preset: verify flickering point light illuminates nearby walls.
- Place smoke: verify noise erosion creates wispy edges, not hard alpha cutoff.
- Place dust motes in an indoor scene: verify tiny glowing particles visible in light beams.
- Place incense: verify thin rising column that disperses.
- Check frame timing: all effects combined should add < 2ms at typical particle counts.

---

## Phase 6-4: Water Reflection & Refraction

**Goal:** Add planar reflection and refraction FBOs for realistic water.

### 6-4a: Reflection FBO

**New class: `WaterFbo`** — Manages reflection and refraction render targets.

```cpp
class WaterFbo
{
public:
    bool init(int reflectionWidth, int reflectionHeight,
              int refractionWidth, int refractionHeight);
    void resize(int reflectionWidth, int reflectionHeight,
                int refractionWidth, int refractionHeight);

    void bindReflectionFbo();
    void bindRefractionFbo();
    void unbind(int viewportWidth, int viewportHeight);

    GLuint getReflectionTexture() const;
    GLuint getRefractionTexture() const;
    GLuint getRefractionDepthTexture() const;

private:
    // Reflection: color only (depth as renderbuffer)
    GLuint m_reflectionFbo = 0;
    GLuint m_reflectionColorTex = 0;
    GLuint m_reflectionDepthRbo = 0;
    int m_reflectionWidth = 0, m_reflectionHeight = 0;

    // Refraction: color + depth texture (depth needed for water thickness)
    GLuint m_refractionFbo = 0;
    GLuint m_refractionColorTex = 0;
    GLuint m_refractionDepthTex = 0;
    int m_refractionWidth = 0, m_refractionHeight = 0;
};
```

**New file:** `engine/renderer/water_fbo.h/cpp`

**FBO specifications:**
- Reflection: half viewport resolution. `GL_RGBA8` color, `GL_DEPTH_COMPONENT24` renderbuffer.
- Refraction: half viewport resolution. `GL_RGBA8` color, `GL_DEPTH_COMPONENT32F` texture (needed for water depth effects, must be texture not renderbuffer).
- All created with DSA (`glCreateFramebuffers`, `glCreateTextures`, `glNamedFramebufferTexture`).

### 6-4b: Clip Plane via gl_ClipDistance

Use `gl_ClipDistance[0]` for clipping during reflection/refraction passes. This avoids touching `glClipControl` (important for Vestige's reverse-Z setup).

**Scene vertex shader modification (`scene.vert.glsl`):**
```glsl
uniform vec4 u_clipPlane; // (0, 1, 0, -waterY) for reflection
                          // (0, -1, 0, waterY) for refraction
                          // (0, 0, 0, 0) for normal rendering (no clip)

// In main():
gl_ClipDistance[0] = dot(worldPosition, u_clipPlane);
```

All vertex shaders that render geometry visible in water (scene, terrain, foliage) need this uniform. When `u_clipPlane = vec4(0)`, `gl_ClipDistance[0] = 0` for all vertices — effectively no clipping.

**Enable/disable:** `glEnable(GL_CLIP_DISTANCE0)` before reflection/refraction passes, `glDisable(GL_CLIP_DISTANCE0)` after.

### 6-4c: Reflected Camera

Reflect the camera about the water plane for the reflection pass:

```cpp
glm::vec3 reflectedPos = cameraPos;
reflectedPos.y = 2.0f * waterY - reflectedPos.y;

glm::vec3 reflectedTarget = cameraTarget;
reflectedTarget.y = 2.0f * waterY - reflectedTarget.y;

glm::mat4 reflectionView = glm::lookAt(reflectedPos, reflectedTarget, glm::vec3(0, 1, 0));
```

Pitch is naturally inverted by reflecting both position and target.

### 6-4d: Render Pipeline Integration

In `Renderer::renderScene()`, before the main opaque pass:

```
1. If scene has water surfaces:
   a. Determine highest water Y from all water surfaces
   b. REFLECTION PASS:
      - Bind reflection FBO
      - Set u_clipPlane = (0, 1, 0, -waterY)
      - Enable GL_CLIP_DISTANCE0
      - Render opaque geometry + terrain + skybox with reflected camera
      - (Skip particles, shadows, transparent geometry for performance)
      - Disable GL_CLIP_DISTANCE0
   c. REFRACTION PASS:
      - Bind refraction FBO
      - Set u_clipPlane = (0, -1, 0, waterY + 0.1) // small offset avoids edge artifacts
      - Enable GL_CLIP_DISTANCE0
      - Render opaque geometry + terrain with main camera
      - Disable GL_CLIP_DISTANCE0
   d. Unbind, restore main FBO
2. Normal rendering continues...
3. When rendering water surfaces, bind reflection/refraction textures
```

### 6-4e: Water Shader Upgrade

**Modified `water.frag.glsl`:**

```glsl
uniform sampler2D u_reflectionTex;
uniform sampler2D u_refractionTex;
uniform sampler2D u_refractionDepthTex;
uniform bool u_hasReflectionTex;

// Projective texture coordinates
vec2 screenUV = (v_clipSpace.xy / v_clipSpace.w) * 0.5 + 0.5;
vec2 reflectUV = vec2(screenUV.x, 1.0 - screenUV.y); // flip Y for reflection

// Apply DuDv distortion
reflectUV = clamp(reflectUV + totalDistortion, 0.001, 0.999);
vec2 refractUV = clamp(screenUV + totalDistortion, 0.001, 0.999);

// Sample
vec3 reflectionColor = u_hasReflectionTex
    ? texture(u_reflectionTex, reflectUV).rgb
    : texture(u_environmentMap, reflectDir).rgb; // fallback to cubemap

vec3 refractionColor = texture(u_refractionTex, refractUV).rgb;

// Beer's law absorption
float refractionDepth = texture(u_refractionDepthTex, refractUV).r;
float linearRefractDepth = u_cameraNear / refractionDepth; // reverse-Z
float linearWaterDepth = u_cameraNear / gl_FragCoord.z;
float waterThickness = linearRefractDepth - linearWaterDepth;
vec3 absorption = exp(-u_absorptionCoeffs * max(waterThickness, 0.0));
refractionColor *= absorption;
refractionColor = mix(refractionColor, u_deepColor.rgb, 1.0 - absorption.b);

// Fresnel blend
float fresnel = R0 + (1.0 - R0) * pow(1.0 - cosTheta, 5.0);
vec3 finalColor = mix(refractionColor, reflectionColor, fresnel);
```

**New `WaterSurfaceConfig` fields:**
```cpp
glm::vec3 absorptionCoeffs = {0.4f, 0.2f, 0.1f}; // Beer's law per-channel
float softEdgeDistance = 1.0f; // Fade alpha near shore
```

### Testing

- Place water surface in scene with objects above and below water line.
- Verify reflection shows mirrored scene (no objects below water).
- Verify refraction shows distorted underwater scene.
- Verify Fresnel: looking down = mostly refraction, shallow angle = mostly reflection.
- Verify Beer's law: deep water appears darker/bluer.
- Verify no artifacts at water edges (DuDv clamping).
- Verify shadows render correctly (gl_ClipDistance doesn't interfere with shadow passes).
- Check frame timing: reflection + refraction adds < 6ms at half resolution.

---

## Phase 6-5: Water Caustics & Depth Effects

**Goal:** Add caustics, soft edges, and shore foam to complete the water rendering.

### 6-5a: Caustics

**Approach:** Texture-based scrolling caustics with dual sampling and min blending.

**New texture:** `assets/textures/water/caustics.png` — 256×256 tileable caustic pattern (greyscale).

**Implementation in `scene.frag.glsl` (main scene shader):**

```glsl
uniform sampler2D u_causticsTex;
uniform float u_causticsScale;      // world-space tiling (default 0.1)
uniform float u_causticsIntensity;  // additive strength (default 0.3)
uniform float u_causticsTime;
uniform float u_waterY;             // water surface height
uniform bool u_causticsEnabled;

// In fragment shader, after base lighting:
if (u_causticsEnabled && v_worldPos.y < u_waterY)
{
    vec2 causticUV1 = v_worldPos.xz * u_causticsScale + u_causticsTime * vec2(0.03, 0.02);
    vec2 causticUV2 = v_worldPos.xz * u_causticsScale * 1.4 + u_causticsTime * vec2(-0.02, 0.03);

    // Chromatic aberration
    float r = texture(u_causticsTex, causticUV1 + vec2(0.001, 0.0)).r;
    float g = texture(u_causticsTex, causticUV1).r;
    float b = texture(u_causticsTex, causticUV1 - vec2(0.001, 0.0)).r;
    vec3 caustic1 = vec3(r, g, b);

    r = texture(u_causticsTex, causticUV2 + vec2(0.0, 0.001)).r;
    g = texture(u_causticsTex, causticUV2).r;
    b = texture(u_causticsTex, causticUV2 - vec2(0.0, 0.001)).r;
    vec3 caustic2 = vec3(r, g, b);

    vec3 caustics = min(caustic1, caustic2) * u_causticsIntensity;

    // Fade with depth below water
    float depthBelowWater = u_waterY - v_worldPos.y;
    float depthFade = 1.0 - smoothstep(0.0, 5.0, depthBelowWater);
    caustics *= depthFade;

    finalColor.rgb += caustics * lightIntensity;
}
```

**Modified files:**
- `assets/shaders/scene.frag.glsl` — Caustics logic.
- `engine/renderer/renderer.cpp` — Set caustic uniforms when a water surface exists. Load caustics texture.
- `engine/renderer/water_renderer.h/cpp` — Store/expose water Y and caustics parameters.

### 6-5b: Soft Water Edges

Fade water alpha where water thickness is small (near shore/object intersections):

```glsl
// In water.frag.glsl, before final color output:
float edgeFade = smoothstep(0.0, u_softEdgeDistance, waterThickness);
fragColor.a *= edgeFade;
```

Already has `waterThickness` from the refraction depth comparison in Phase 6-4.

### 6-5c: Shore Foam (Optional Polish)

Add foam at water-geometry intersections:

**New texture:** `assets/textures/water/foam.png` — 256×256 tileable foam pattern.

```glsl
uniform sampler2D u_foamTex;
uniform float u_foamDistance; // how far from shore foam extends (default 0.5m)

float foamFactor = 1.0 - smoothstep(0.0, u_foamDistance, waterThickness);
if (foamFactor > 0.01)
{
    vec2 foamUV1 = v_worldPos.xz * 2.0 + u_time * vec2(0.01, 0.02);
    vec2 foamUV2 = v_worldPos.xz * 1.5 + u_time * vec2(-0.02, 0.01);
    float foam = texture(u_foamTex, foamUV1).r * texture(u_foamTex, foamUV2).r;
    finalColor.rgb = mix(finalColor.rgb, vec3(1.0), foam * foamFactor * 0.5);
}
```

### Testing

- Place objects below water surface. Verify caustics appear on their surfaces.
- Verify caustics fade with depth (deep objects have less caustic effect).
- Verify chromatic aberration produces subtle color fringing.
- Verify soft edges at water/terrain boundary (no hard alpha cutoff).
- Verify shore foam appears at shallow water intersections.
- Check that caustics only render on geometry below the water Y plane.
- Profile: caustics should add < 0.5ms.

---

## New Files Summary

| File | Description |
|------|-------------|
| `engine/renderer/water_fbo.h/cpp` | Reflection/refraction FBO management |
| `engine/scene/particle_presets.h/cpp` | Factory methods for common effect presets |
| `assets/textures/particles/soft_circle.png` | Default soft circular particle |
| `assets/textures/particles/fire_flipbook.png` | 8×8 fire sprite sheet |
| `assets/textures/particles/smoke.png` | Smoke puff texture |
| `assets/textures/particles/spark.png` | Spark/ember texture |
| `assets/textures/particles/noise_erosion.png` | Tileable noise for alpha erosion |
| `assets/textures/water/caustics.png` | Tileable caustic pattern |
| `assets/textures/water/foam.png` | Tileable shore foam pattern |

## Modified Files Summary

| File | Changes |
|------|---------|
| `engine/renderer/renderer.h/cpp` | Own particle/water renderers, reflection/refraction passes, caustics |
| `engine/renderer/particle_renderer.h/cpp` | Texture support, soft particles, flipbook, noise erosion |
| `engine/renderer/water_renderer.h/cpp` | Reflection/refraction textures, absorption, soft edges |
| `engine/scene/particle_emitter.h/cpp` | Light coupling, flipbook config, noise erosion flag |
| `engine/scene/water_surface.h` | Absorption coefficients, soft edge distance |
| `engine/scene/scene.cpp` | Collect coupled lights from particle emitters |
| `assets/shaders/particle.vert.glsl` | Pass normalizedAge to fragment |
| `assets/shaders/particle.frag.glsl` | Texture, soft particles, flipbook, noise erosion |
| `assets/shaders/water.vert.glsl` | Pass clip-space position for projective texturing |
| `assets/shaders/water.frag.glsl` | Reflection/refraction sampling, Beer's law, soft edges, foam |
| `assets/shaders/scene.vert.glsl` | gl_ClipDistance[0] for water clip plane |
| `assets/shaders/scene.frag.glsl` | Caustics on underwater geometry |
| `assets/shaders/terrain.vert.glsl` | gl_ClipDistance[0] for water clip plane |
| `engine/editor/panels/inspector_panel.cpp` | Light coupling UI, preset dropdown |
| `engine/editor/entity_factory.cpp` | Preset-based entity creation |
| `engine/CMakeLists.txt` | Add water_fbo.cpp, particle_presets.cpp |

---

## Performance Budget

| Component | Budget | Notes |
|-----------|--------|-------|
| Pipeline integration | ~0 ms | Just wiring existing code |
| Particle textures | ~0.1 ms | Texture bind per emitter |
| Soft particles | ~0.2 ms | Depth texture sample per fragment |
| Flipbook animation | ~0.1 ms | Two texture samples + interpolation |
| Noise erosion | ~0.1 ms | One noise texture sample |
| Light coupling | ~0.5 ms | Extra point lights (shadow optional) |
| Reflection pass (half-res) | 2–4 ms | Simplified scene re-render |
| Refraction pass (half-res) | 1–3 ms | Below-water geometry only |
| Caustics | ~0.3 ms | Additional texture samples on underwater geom |
| Soft edges + foam | ~0.1 ms | Per-fragment depth compare |
| **Total (worst case)** | **~5–9 ms** | Well within 16.6ms @ 60 FPS |

Scenes without water skip reflection/refraction entirely (~0ms overhead). The particle rendering overhead is negligible at typical counts (< 10K particles).

---

## Execution Order

```
6-1 (pipeline integration) → 6-2 (textures, soft particles, presets) → 6-3 (fire/smoke/dust)
                           → 6-4 (water reflection/refraction) → 6-5 (caustics, foam)
```

6-2/6-3 (particles) and 6-4/6-5 (water) are independent tracks that both depend on 6-1. Within each track, sub-phases are sequential.

---

## Verification

After each sub-phase:
1. `cmake --build build` — zero errors, zero warnings.
2. `./build/bin/vestige_tests` — all tests pass.
3. Visual test: launch engine, verify effects render correctly.
4. Frame timing: check profiler overlay, no regression below 60 FPS.

After Phase 6-4 (water FBOs):
- Verify shadow passes are unaffected by `gl_ClipDistance` changes.
- Verify reverse-Z depth linearization is correct for Beer's law absorption.
- Test with terrain + water: verify clip plane correctly separates above/below water.

After all Phase 6:
- Place a complete scene: terrain, water with caustics, torch fire on walls, smoke from chimney, dust motes in interior, incense in tabernacle.
- Verify all effects coexist without visual artifacts.
- Profile total frame time with all effects active.
