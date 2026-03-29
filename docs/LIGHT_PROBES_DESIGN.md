# Light Probes Design Document

## Problem

The engine has a single global IBL (Image-Based Lighting) environment map derived from the skybox. Every surface — indoors or outdoors — samples the same sky for ambient and specular reflections. This causes:

1. **Sky reflections indoors**: Tent walls reflect bright sky instead of dim interior
2. **Camera-tracking specular**: IBL specular uses `reflect(-viewDir, normal)`, creating a "phantom light" on walls that shifts with camera movement
3. **No bounce light**: Sunlight entering through the tent doorway doesn't illuminate the interior — the interior IBL shows sky, not bounced warm light

## Solution: Light Probes

Place cubemap capture points at key positions. Each probe renders the local environment from its position, then convolves irradiance/prefilter maps. Surfaces inside a probe's influence volume use the probe's IBL instead of the global sky IBL.

**References:**
- Unity Light Probes: https://docs.unity3d.com/Manual/LightProbes.html
- LearnOpenGL IBL: https://learnopengl.com/PBR/IBL/Diffuse-irradiance
- Valve's approach in Source 2: ambient cubes placed by artists

## Architecture

### New Files
- `engine/renderer/light_probe.h/.cpp` — LightProbe class (GPU textures, capture, influence volume)
- `engine/renderer/light_probe_manager.h/.cpp` — Manages probes, assigns per-entity

### Modified Files
- `assets/shaders/scene.frag.glsl` — Probe uniforms + blended IBL sampling
- `engine/renderer/renderer.h/.cpp` — Probe manager, fallback bindings, per-entity probe binding in draw loop
- `engine/core/engine.cpp` — Probe placement + capture trigger
- `engine/CMakeLists.txt` — New source files

### Texture Units
| Unit | Current | With Probes |
|------|---------|------------|
| 10 | Free | `u_probeIrradianceMap` (samplerCube) |
| 11 | Free | `u_probePrefilterMap` (samplerCube) |
| 14 | `u_irradianceMap` (global) | Unchanged |
| 15 | `u_prefilterMap` (global) | Unchanged |
| 16 | `u_brdfLUT` (shared) | Unchanged |

BRDF LUT is environment-independent — shared across all probes.

### Shader Uniforms
```glsl
uniform bool u_hasProbe;                       // Is a probe active for this entity?
uniform float u_probeWeight;                   // 0=global only, 1=probe only
uniform samplerCube u_probeIrradianceMap;       // Unit 10
uniform samplerCube u_probePrefilterMap;        // Unit 11
```

### IBL Blending
```glsl
vec3 irradiance = mix(globalIrradiance, probeIrradiance, u_probeWeight);
vec3 prefilteredColor = mix(globalPrefilt, probePrefilt, u_probeWeight);
```

## Probe Capture

Each probe renders the scene from its position into a 128x128 HDR cubemap (6 faces), then convolves:
- **Irradiance**: 32x32 cubemap (cosine-weighted hemisphere integral)
- **Prefilter**: 128x128 cubemap with 5 mip levels (GGX importance sampling)

Capture reuses the existing irradiance/prefilter shaders from EnvironmentMap. The scene render uses a simplified pass: geometry + directional light + point lights, no shadows, no post-processing.

**Capture projection**: 90° FOV, near=0.1, far=100.0 (wider range than sky capture's 10.0).

**Timing**: One-time at scene load, after all geometry and lights are placed.

## Influence Volume & Blending

Each probe has an AABB influence volume. Entities inside get the probe's IBL. A configurable `fadeDistance` creates a smooth transition at the boundary:

```
weight = 1.0 - clamp((distToEdge) / fadeDistance, 0, 1)
```

For the Tabernacle: tent interior probe AABB matches the tent walls. Entities at the tent entrance blend between outdoor and indoor IBL.

## Performance

| Metric | Cost |
|--------|------|
| Capture | 6 face renders @ 128x128 + convolution (one-time) |
| Memory per probe | ~2.5 MB (irradiance 36KB + prefilter 2.4MB) |
| Per-frame | 1 AABB test per entity + 2 extra cubemap samples when inside probe |
| Draw call impact | Minimal — indoor/outdoor meshes use different materials, so batches align with probe volumes |

## Implementation Steps

1. Create LightProbe class (GPU textures, bind, influence volume)
2. Modify scene shader (add probe uniforms + blended IBL)
3. Modify renderer (fallback bindings, sampler assignments)
4. Create LightProbeManager (probe collection, assignment)
5. Implement probe capture (scene render to cubemap + convolution)
6. Integrate per-entity probe binding in renderer draw loop
7. Wire into engine.cpp (place tent interior probe, trigger capture)
8. Remove per-material iblMultiplier workarounds

## Mesa AMD Considerations

- Units 10-11 always bound with fallback cubemaps when no probe is active
- Probe samplerCubes match the type already on those units (no sampler type mismatch)
- `u_hasProbe` guard prevents sampling unused textures, but textures are valid regardless

## Future Extensions

- Editor UI: LightProbeComponent + inspector panel + "Recapture" button
- Probe serialization in scene files
- Multiple overlapping probes with priority/blending
- Real-time probe updates (for dynamic scenes)
- Spherical Harmonics encoding (lower memory, faster lookup)
