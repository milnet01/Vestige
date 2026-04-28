# Phase 6: Particle and Effects System — Research

Research compiled 2026-03-24 for the Vestige 3D Engine.

---

## 1. GPU Compute Particle Architecture

### 1.1 Buffer Layout

The core of a GPU particle system is a set of Shader Storage Buffer Objects (SSBOs) that keep all particle data entirely in VRAM, minimizing CPU-GPU transfer. SSBOs use `std430` layout (OpenGL 4.3+), which provides tight packing without the padding overhead of `std140`.

**Particle data struct** (per-particle): A typical layout stores position (vec4, w = lifetime), velocity (vec4, w = age), color (vec4), and additional attributes (rotation, sprite frame index, start size). Using vec4 alignment avoids padding issues. Total: 48–64 bytes per particle is typical.

**Structure of Arrays (SoA) vs Array of Structures (AoS):** On GPUs, SoA is generally preferred for memory coalescing — adjacent work-items access adjacent addresses. Research benchmarks show SoA can be 30–45% faster than AoS for GPU compute workloads. However, for particle systems where the simulation shader reads all attributes of each particle in a single invocation, AoS with vec4 alignment is acceptable and simpler. Wicked Engine and most practical implementations use AoS.

### 1.2 Dead/Alive List Management

The industry-standard architecture (documented by Wicked Engine) uses:

1. **Particle Buffer**: Fixed-size array of `MAX_PARTICLES` particle structs.
2. **Dead List**: A stack of free indices (initially all indices). Size: `uint × MAX_PARTICLES`.
3. **Alive List A and B** (ping-pong): Each holds indices of living particles.
4. **Counter Buffer**: 5–6 uint32 values — dead count, alive count A/B, emission count, simulation dispatch arg.
5. **Indirect Argument Buffer**: Parameters for `glDispatchComputeIndirect` and `glDrawArraysIndirect`.

Per-frame flow:
- **Emit**: Pop indices off dead list (atomic decrement), push onto alive list A (atomic increment).
- **Simulate**: Read alive list A. Update physics. If alive → append to alive list B. If dead → push back to dead list. Write draw indirect arguments simultaneously.
- **Swap**: Alive list B becomes current. Lists swap roles each frame (ping-pong).

This avoids compaction and keeps all live particle indices contiguous.

**Sources:**
- Wicked Engine: GPU-based particle simulation (wickedengine.net, 2017)
- Compute Shaders and Particles (Alexandru Ene, alexene.dev, 2014)
- NVIDIA Compute Particles Sample (docs.nvidia.com)
- ARM OpenGL ES: Particle Flow Simulation (arm-software.github.io)

### 1.3 Ping-Pong vs Single Buffer

Double-buffering alternates between two alive lists each frame — one read, one written. This eliminates read-after-write hazards without in-place memory barriers. Single-buffer approaches require `coherent` qualifiers and can crash on certain drivers. Double-buffering is more robust and is the standard approach.

### 1.4 Emission on GPU

Using SSBOs with `atomicAdd` on the counter buffer:

1. CPU writes emission count to a uniform or constant buffer.
2. Dispatch one thread per particle to emit.
3. Each thread calls `atomicAdd(deadCounter, -1)` to pop a free index from the dead list.
4. If dead list empty (counter < 0), thread exits (pool exhausted).
5. Thread initializes particle at that index.
6. Thread calls `atomicAdd(aliveCounter, 1)` to append to alive list.

### 1.5 Indirect Dispatch

After simulation, the alive count is known only on the GPU. A small "kickoff" compute shader reads the alive count and writes dispatch arguments into `GL_DISPATCH_INDIRECT_BUFFER`. The simulation uses `glDispatchComputeIndirect`. Similarly, draw arguments are written to `GL_DRAW_INDIRECT_BUFFER` for `glDrawArraysIndirect`. The CPU never reads back particle counts.

**Sources:**
- Wicked Engine GPU particles (wickedengine.net, 2017)
- OpenGL Atomic Counter Wiki (khronos.org)
- OpenGL Compute Shader Wiki — glDispatchComputeIndirect (khronos.org)

---

## 2. Particle Sorting for Transparency

### 2.1 When Sorting is Needed

**Additive blending is order-independent.** Since addition is commutative (A+B = B+A), fire, sparks, glowing effects, and dust motes with `GL_ONE, GL_ONE` blending produce identical results regardless of draw order. No sorting needed.

**Alpha blending is order-dependent.** Smoke and any effect using `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` must be drawn back-to-front for correct compositing.

### 2.2 GPU Sorting Algorithms

**Bitonic Sort:** O(n log²n) comparisons, all parallelizable. Conceptually simple, maps cleanly to compute with shared memory. For 1M elements: ~210 dispatch passes. ~4ms on mid-range GPU. Sufficient for particle counts under 100K.

**Radix Sort:** O(n×k) where k = number of bit groups. Constant pass count regardless of size. AMD FidelityFX Parallel Sort provides an optimized open-source implementation (MIT license). Faster than bitonic for 500K+ elements.

### 2.3 Weighted Blended OIT (Alternative to Sorting)

Weighted Blended Order-Independent Transparency avoids sorting entirely:
- **Accumulation** (RGBA16F): `vec4(color.rgb * alpha * weight, alpha * weight)` with additive blending
- **Revealage** (R8): `alpha` with multiplicative blending
- **Composite pass**: Full-screen quad normalizes and blends

Works well for smoke/fog. Main limitation: weight heuristic must be tuned for depth range.

**Sources:**
- GPU Gems 2, Ch. 46: GPU Sorting (NVIDIA)
- AMD FidelityFX Parallel Sort (gpuopen.com)
- McGuire & Bavoil: Weighted Blended OIT (JCGT, 2013)

---

## 3. Forces and Physics in Compute

### 3.1 Integration

**Semi-implicit Euler** is standard for visual particle effects:
```
velocity += acceleration * dt;
position += velocity * dt;
```
First-order but symplectic (conserves energy). Simple, cheap, perfectly adequate.

### 3.2 Common Forces

- **Gravity**: `acceleration += vec3(0, -9.81, 0)` — trivial constant.
- **Wind**: Constant directional force, optionally time-varying with noise for gusting.
- **Drag**: `velocity *= (1.0 - drag * dt)` or `velocity *= pow(damping, dt)` for frame-rate independence. Values: 0.1–0.5 for smoke, near-zero for sparks.

### 3.3 Curl Noise for Turbulence

Curl noise produces a **divergence-free** velocity field (no sinks or sources), making particles flow like real fluid without clumping. Given a 3D noise field N(x,y,z), the curl is:
```
curl.x = dN/dy - dN/dz
curl.y = dN/dz - dN/dx
curl.z = dN/dx - dN/dy
```

**Caching optimization (recommended):** Pre-compute curl noise into a tiling 3D texture (64³ or 128³). At runtime, a single texture lookup replaces expensive analytical noise. Animate by scrolling the sample offset.

**Sources:**
- Curl Noise (Emil Dziewanowski, emildziewanowski.com)
- Noise-Based Particles (Philip Rideout, prideout.net)
- Fast Divergence-Free Noise / Bitangent Noise (atyuwen.github.io)

### 3.4 Vortex Forces

Attract particles into spinning motion:
```
toCenter = vortexPos - particlePos;
tangent = cross(vortexAxis, normalize(toCenter));
force = tangent * strength / (length(toCenter) + epsilon);
```

---

## 4. Collision Detection on GPU

### 4.1 Analytical Plane/Sphere

Simple and cheap. Pass collision primitives as uniforms:

**Plane:** Compare `dot(position - planePoint, planeNormal)`. If negative, push out and reflect velocity with damping factor (0.3–0.7).

**Sphere:** Compare `length(position - center) - radius`. If negative, push to surface and reflect.

### 4.2 Depth Buffer Collision

More general — works with any visible geometry without explicit collision setup:

1. Project particle to screen space via view-projection matrix.
2. Sample scene depth texture at projected UV.
3. Compare linearized depths.
4. If particle behind geometry → collision. Reconstruct surface normal from depth partial derivatives.
5. Reflect velocity off reconstructed normal.

**Limitations:** Only works for camera-visible geometry. Objects behind other objects have no depth representation. Widely used despite limitations (Wicked Engine, Godot GPUParticles3D proposals).

**Sources:**
- Compute Shader Particle System with Depth Collision (Emrik Wenthzel)
- Wicked Engine GPU particles (wickedengine.net)
- Godot Proposal #3898: Depth buffer collision for GPUParticles3D

---

## 5. Performance Benchmarks

| GPU | Particle Count | FPS | Source |
|-----|---------------|-----|--------|
| GTX 1080 | 1,000,000 | 60 | GPUParticles (DX11) |
| GTX 980 (VR) | 2,000,000 | 90 | Mike Turitzin |
| OpenGL 4.5 | 2,000,000 | 60+ | Crisspl/GPU-particle-system |
| WebGPU | 1,000,000+ | interactive | WebGPU particle simulation |

**Extrapolation for RX 6600 (RDNA2):** 28 CUs, 1792 shaders, ~7.0 TFLOPS FP32. Conservative: **500K–1M particles at 60 FPS** with full simulation. Without sorting: 1–2M. For a temple walkthrough needing 10K–50K active particles — trivially within budget.

### 5.1 Workgroup Sizes (RDNA2)

- **64 threads**: One Wave64. Best for divergent workloads.
- **256 threads** (4 waves): AMD's recommended default.
- **512–1024**: Beneficial with heavy shared memory use, but register pressure limits occupancy.

**Recommendation:** `local_size_x = 256` for particle simulation.

**Sources:**
- Optimizing GPU Occupancy (AMD GPUOpen)
- 2M Particle System (Crisspl, GitHub)
- Rendering Particles with Compute (Mike Turitzin)

---

## 6. Fire Rendering Techniques

### 6.1 Billboard Particles with Flipbook Animation

Industry standard for real-time fire. A sprite sheet (8×8 = 64 frames) contains pre-rendered fire frames. The particle system plays through frames over lifetime:

```glsl
float frameIndex = floor(normalizedAge * totalFrames);
float nextFrame = frameIndex + 1;
float blendFactor = fract(normalizedAge * totalFrames);
// Sample both frames, interpolate for smooth animation
```

Frame interpolation (blending adjacent frames) is critical for smooth animation.

### 6.2 Color-Over-Lifetime Ramps

Fire color follows blackbody radiation. A 1D color ramp indexed by particle age: white/yellow at birth → orange mid-life → dark red/black at death. Hand-tuned gradients work better than physically-accurate blackbody for artistic control.

### 6.3 Heat Haze / Distortion

Screen-space post-process:
1. Fire particles write UV offset vectors to a distortion buffer.
2. Composite pass reads distortion buffer and offsets scene color UVs.
3. Animate with scrolling noise texture.

### 6.4 Light Coupling

Approaches:
- **Single dynamic point light** per emitter (cheapest — one extra light). Intensity fluctuates with noise for flickering.
- **Per-particle lights** (expensive in forward rendering, practical only in deferred).
- **Emissive bloom** — render fire with HDR color (> 1.0), let bloom create glow. Doesn't illuminate geometry but looks convincing for small fires.

**Recommended:** Single flickering point light per fire emitter + emissive bloom.

**Sources:**
- Fire Rendering Methods (Zhang Jiajian, zhangjiajian.com)
- VFX Apprentice: Flipbooks in Games
- GPU Gems Ch. 6: Fire in the Vulcan Demo (NVIDIA)
- Blackbody Radiation for Shaders (GitHub)
- Heat Distortion Shader Tutorial (Linden Reid)

---

## 7. Smoke Rendering

### 7.1 Soft Particles (Depth Fade)

Eliminates hard intersection where billboards clip through geometry:
```glsl
float depthDiff = linearSceneDepth - linearParticleDepth;
float softFactor = smoothstep(0.0, transitionSize, depthDiff);
fragColor.a *= softFactor;
```
`transitionSize` of 0.5–2.0 meters is typical.

### 7.2 Noise-Based Alpha Erosion

Smoke dissipates at edges via noise erosion:
```glsl
float noise = texture(noiseTex, uv * tiling + time * scroll).r;
float dissolveThreshold = normalizedAge; // 0 at birth, 1 at death
float alpha = smoothstep(dissolveThreshold, dissolveThreshold + softness, noise);
fragColor.a *= alpha;
```
Creates organic, wispy edges far better than simple alpha fade.

### 7.3 Expanding Billboards

Grow size over lifetime: `size = mix(startSize, endSize, normalizedAge)`. Start at 0.5m, expand to 2–4m. Combined with alpha fade-out for natural "puff and dissipate."

### 7.4 Off-Screen Rendering Optimization

Render expensive particle effects to a reduced-resolution FBO (1/4 res), composite back. GPU Gems 3 Ch. 23 reports going from 25 FPS to 61 FPS. Edge artifacts mitigable via stencil-based edge detection.

**Sources:**
- NVIDIA: Soft Particles Whitepaper (2007)
- GPU Gems 3, Ch. 23: High-Speed Off-Screen Particles
- CyberAgent NovaShader (particle dissolve)
- GPU Fog Particles (textureless noise-based, GitHub)

---

## 8. Dust Motes / Ambient Particles

### 8.1 Visual Characteristics

Tiny particles that catch light beams — enormous realism boost in architectural interiors. Critical for a biblical temple where light streams through doorways into dim interiors.

### 8.2 Rendering

- **Count**: 500–5000 visible at a time (fill a room volume).
- **Size**: Very small — 0.5–3mm world space, 1–4 pixels on screen.
- **Motion**: Slow drifting, sin-based oscillation. No need for curl noise.
- **Rendering**: `GL_POINTS` with `gl_PointSize`, or tiny billboards. At 1–4 pixels, point sprites suffice.
- **Blending**: Additive (`GL_ONE, GL_ONE`). No sorting needed.
- **Brightness**: Multiply by lighting at particle position. Sample shadow map to make particles in shadow invisible, particles in light beams glow.
- **Culling**: Only render within ~20m of camera. Respawn particles that drift beyond.

Performance: at 5000 particles with point sprites — essentially free (single draw call, minimal geometry).

---

## 9. Incense Smoke

### 9.1 Physical Reference

Real incense smoke: straight vertical column (laminar flow) that breaks up at ~15–20cm, transitioning to turbulent flow. Wisps and ribbons form before dispersing. Driven by Reynolds number increase as warm air accelerates upward.

### 9.2 Particle Implementation

**Emission**: Point source at incense tip, 30–60 particles/sec. Initial velocity straight upward (0.3–0.5 m/s). Minimal horizontal spread.

**Laminar-to-turbulent transition** (key visual feature):
- **Age 0.0–0.3** (laminar): Rise straight up, very low noise.
- **Age 0.3–0.6** (transition): Ramp curl noise amplitude from near-zero to full. Characteristic "breaking up" occurs.
- **Age 0.6–1.0** (turbulent): Full turbulence, horizontal spread, velocity slows, alpha fades.

```glsl
float turbulenceBlend = smoothstep(0.2, 0.5, normalizedAge);
vec3 turbulence = sampleCurlNoise(position * scale + time * 0.1) * strength;
velocity += turbulence * turbulenceBlend * dt;
```

**Rendering**: Small expanding billboards (1cm → 5–10cm). Alpha erosion for wispy edges. Standard alpha blending (not additive — incense smoke is opaque enough to occlude).

**Sources:**
- COMSOL: Incense Stick Laminar-Turbulent Transition
- Unreal Forum: Wispy Incense Smoke Particle
- Houdini: Detailed Smoke Techniques (tokeru.com)

---

## 10. Water Caustics Techniques

### 10.1 Texture-Based Scrolling Caustics (Simplest)

A pre-baked tileable caustics texture scrolls over time. Sample using world-space XZ coordinates of underwater surfaces (not object UVs). The key technique from Alan Zucconi: sample the caustics texture **twice** at different scales and time offsets, then combine with the **`min` operator** (not averaging). Adding **chromatic aberration** (sampling R, G, B with slight positional offsets) simulates wavelength-dependent diffraction.

**Projection onto arbitrary geometry:** Use world-space fragment position as UV: `vec2 causticUV = worldPos.xz * tileScale`. For vertical surfaces, **triplanar projection** blends three axis-aligned projections weighted by surface normal.

### 10.2 Projected Caustic Textures (Light Cookie)

Treat caustics as a projected light pattern — project downward from water surface using a projective texture matrix (similar to shadow mapping). Apply as additive blend on top of base lighting.

### 10.3 Procedural Voronoi Caustics

Smooth Voronoi noise (Worley/cellular noise) produces convincing patterns procedurally with no texture assets. Animate by adding `time * speed` to input. Sample two layers at different scales/speeds, combine with `min()`. Eliminates tiling artifacts.

### 10.4 Recommendation

Start with **texture-based scrolling caustics with dual sampling and min blending**. Cheapest, one texture asset, good results. Later upgrade to procedural Voronoi for infinite variety.

**Sources:**
- GPU Gems Ch. 2: Water Caustics (NVIDIA)
- Believable Caustics Reflections (Alan Zucconi, 2019)
- Water Caustics Triplanar Projection (z4gon, GitHub)

---

## 11. Planar Reflections

### 11.1 Reflected Camera Setup

1. Reflect camera position: `reflectedY = 2 * waterPlaneY - cameraY`
2. Negate pitch angle
3. Render scene from reflected viewpoint to FBO
4. Sample using projective texture mapping: `uv = clipPos.xy / clipPos.w * 0.5 + 0.5`

### 11.2 Clip Plane Methods

**A. `gl_ClipDistance` (recommended):**
```glsl
gl_ClipDistance[0] = dot(modelMatrix * vertexPosition, clipPlane);
```
Enable with `glEnable(GL_CLIP_DISTANCE0)`. Clip plane `vec4(0, 1, 0, -waterY)` keeps geometry above water. Cleanest modern approach.

**B. Oblique Near Plane (Eric Lengyel / Terathon):** Modify projection matrix so near plane coincides with water plane. Eliminates separate clipping but reduces depth buffer precision.

**C. Fragment discard:** `if (worldPos.y < waterY) discard;` — simple but wasteful.

**Note:** Vestige uses reversed-Z — `gl_ClipDistance` avoids touching `glClipControl` entirely, which is important per the project's known issue with shadow pass state.

### 11.3 FBO Setup

- Color: `GL_RGBA8` or `GL_RGBA16F`
- Depth: `GL_DEPTH_COMPONENT24` renderbuffer (needed for depth test, not sampled)
- Resolution: **half viewport** (960×540 for 1080p) — saves significant fill rate

### 11.4 Performance and Optimization

Planar reflections ~double scene rendering cost for reflected area.

Optimizations:
- Half or quarter resolution (4× fewer fragments)
- Skip shadows, particles, foliage in reflection pass
- Distance culling (skip when water < 5% screen coverage)
- Reduced LOD meshes in reflection
- Update every 2nd–3rd frame when camera moves slowly

**Sources:**
- Oblique Near-Plane Clipping (Eric Lengyel, terathon.com)
- gl_ClipDistance Reference (docs.gl)
- BonzaiSoftware OpenGL Water Tutorial

---

## 12. Planar Refraction

### 12.1 Rendering

Use main camera with clip plane discarding everything **above** water: `vec4(0, -1, 0, waterY)`. Render to FBO with **depth texture** (needed for water depth effects).

### 12.2 DuDv Distortion

Sample DuDv map with scrolling UVs twice with opposing directions:
```glsl
vec2 d1 = texture(dudvMap, vec2(uv.x + time * flow, uv.y)).rg * 2.0 - 1.0;
vec2 d2 = texture(dudvMap, vec2(-uv.x, uv.y + time * flow)).rg * 2.0 - 1.0;
vec2 total = (d1 + d2) * dudvStrength;
vec2 refractUV = clamp(screenUV + total, 0.001, 0.999);
```
Clamp prevents sampling outside texture (black edge artifacts).

### 12.3 Depth-Based Absorption (Beer's Law)

```glsl
float waterDepth = linearDepthRefraction - linearDepthWater;
vec3 absorption = exp(-absorptionCoeffs * waterDepth);
// absorptionCoeffs: vec3(0.4, 0.2, 0.1) — red absorbed fastest
refractedColor = mix(refractedColor, deepWaterColor, 1.0 - absorption);
```

### 12.4 Combining with Fresnel

Schlick's approximation: `fresnel = R0 + (1 - R0) * pow(1 - cosTheta, 5)` where R0 ≈ 0.02 for water. Looking straight down = mostly refraction; shallow angle = mostly reflection.

**Sources:**
- Simple Water Rendering (OpenSAGE)
- Schlick's Approximation (Wikipedia)
- Beer's Law for Raytracing (demofox.org)

---

## 13. Water Depth Effects

### 13.1 Soft Edges

```glsl
float waterThickness = linearSceneDepth - linearWaterDepth;
float edgeFade = smoothstep(0.0, softEdgeDistance, waterThickness);
```
Fades water alpha near shorelines and object intersections.

### 13.2 Foam at Intersections

1. Compare water depth with scene depth — small difference = intersection.
2. Leading-edge falloff gradient: 1.0 at intersection → 0.0 at foam extent.
3. Sample foam texture (noise/Voronoi) twice at different scales/scroll rates.
4. Mask with intersection gradient.

### 13.3 Depth Fog / Murky Water

```glsl
float fogFactor = 1.0 - exp(-fogDensity * waterThickness);
vec3 underwater = mix(refractedColor, fogColor, fogFactor);
```

**Sources:**
- Water Shader Breakdown (Fire Face)
- Shoreline Shader Breakdown (Cyanilux)
- Creating a Stylized Water Shader (GameIdea, 2026)

---

## 14. Screen-Space Planar Reflections (SSPR)

An alternative to full planar reflections:

1. **Projection pass** (compute): For each pixel below water, reflect across water plane, project back to screen. Write source pixel via `atomicMax` (selects closest to water).
2. **Resolve pass:** Read hash buffer, fetch color.

**Performance: 0.3–0.4ms at quarter resolution** (Ubisoft, Ghost Recon Wildlands). Dramatically cheaper than full scene re-render. Limitation: can only reflect visible geometry.

**Recommendation for Vestige:** Start with planar reflections at half resolution (simplest, complete results). SSPR is the upgrade path if performance is tight.

**Sources:**
- SSPR in Ghost Recon Wildlands (Remi Genin)
- AMD FidelityFX SSSR (GPUOpen)

---

## 15. Engine Integration Patterns

### 15.1 Wicked Engine (Most Relevant Reference)

- C++, MIT license, GPU particle system.
- 6 GPU buffers: particle buffer, dead list, alive list ×2, counter buffer, indirect args.
- Pipeline: Kickoff → Emit → Simulate → Sort → Render.
- Billboard expansion in vertex shader via `gl_VertexID / 6` (particle index) and `gl_VertexID % 6` (quad corner). No geometry shader.
- CPU work per frame: one uniform update + dispatch/draw calls.

### 15.2 Key Lessons

1. **Billboard expansion via vertex ID** — consensus approach, avoids AMD geometry shader perf issues.
2. **Dead/alive list with ping-pong** — standard GPU particle management.
3. **Indirect dispatch/draw** — essential for fully GPU-driven pipeline.
4. **Memory barriers** between each compute/draw stage with appropriate bits.

### 15.3 Memory Barriers

- After **emit** compute: `GL_SHADER_STORAGE_BARRIER_BIT`
- After **simulate** compute: `GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT`
- Before **draw**: `GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT` if reading SSBO as vertex data.

**Sources:**
- Wicked Engine source code and documentation (wickedengine.net)
- glMemoryBarrier Reference (khronos.org)

---

## 16. Performance Budget for RX 6600

### 16.1 Particle Budget

50K total active particles across all effects is more than sufficient for a temple walkthrough and trivial for the RX 6600. Typical scene: ~5K fire, ~2K smoke, ~3K dust motes, ~500 incense = ~10K total.

### 16.2 Water Budget at 1080p

| Component | Estimated Cost |
|-----------|---------------|
| Base scene render | 4–8 ms |
| Half-res reflection (simplified) | 2–4 ms |
| Half-res refraction | 1–3 ms |
| Water surface shader | < 1 ms |
| Caustics (texture-based) | < 0.5 ms |
| **Total water** | **~4–9 ms** |

Well within the 16.6ms budget for 60 FPS, especially since not all scenes will have water.
