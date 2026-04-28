# Water Rendering & Performance Research

**Date:** 2026-03-25
**Context:** Vestige runs at 54 FPS with water enabled (3 scene passes per frame). Water surface also shows visible tiling patterns.

---

## Part 1: Water Performance — How Other Engines Do It

### The Core Problem

Vestige renders the scene **3 times per frame** for water (main + reflection + refraction), costing ~10ms. Industry budget for water: **1-3ms maximum** (6-18% of frame).

### What Major Engines Do

| Engine | Reflection | Refraction | Water Cost |
|--------|-----------|-----------|-----------|
| **UE5** | SSR (tile-classified) + Lumen fallback | Screen-space grab of opaque buffer (no 2nd pass) | ~2-3ms |
| **DOOM Eternal** | SSR in forward shader + cubemap fallback | Screen-space | ~1-2ms |
| **Unity HDRP** | SSR + reflection probes | Screen-space | ~2ms |
| **Godot 4** | Screen-space or viewport hack | SCREEN_TEXTURE grab | ~1ms |

**Key insight:** No major engine renders the full scene twice for refraction. They all grab the opaque color buffer in screen-space.

### Recommended Fix Strategy (ranked by impact)

#### 1. Eliminate the Refraction Pass (saves ~3-4ms)

Replace the dedicated refraction render pass with a screen-space grab. After rendering all opaques, bind the main FBO's color attachment as `u_refractionTex` in the water shader. The existing Beer's law absorption code works identically — only the texture source changes.

**Implementation:** Render opaques → bind FBO color as texture → render water (reads opaque buffer with distorted UVs). Zero-cost refraction.

#### 2. Replace Planar Reflections with SSPR (saves ~4-5ms)

**Screen-Space Planar Reflections** (Ghost Recon Wildlands, Remi Genin, Ubisoft):
- Compute shader reflects each depth-buffer pixel across the water plane
- Uses `imageAtomicMax` on an R32UI hash buffer (Y in high bits = natural Z-sort)
- Cost: **0.3-0.5ms** at quarter resolution — 25x cheaper than planar reflection
- Fully compatible with OpenGL 4.5 compute shaders
- Fallback to environment cubemap for off-screen objects

**Source:** https://remi-genin.github.io/posts/screen-space-planar-reflections-in-ghost-recon-wildlands/

#### 3. Hybrid Fallback: Low-Res Planar + SSR

If SSPR is too complex initially:
- Reduce planar reflection to 1/4 resolution (currently 40%, go to 25%)
- Skip terrain and use simplified shaders in reflection pass
- Layer SSR on top for near-field detail
- Blend based on SSR confidence

### Other Water Optimizations (Current Planar Approach)

- **Aggressive far-plane clipping** in reflection camera (50m instead of full scene)
- **LOD bias +2** in reflection pass (force lowest mesh detail)
- **Temporal reflection updates:** Render reflection every 2nd frame, reproject between
- **Checkerboard reflection:** Render half pixels per frame, temporal reconstruct

---

## Part 2: Fixing Water Surface Tiling Patterns

### Why It Tiles

1. Only 2 normal map layers with similar scroll directions
2. Scale ratios 3.7/5.3 ≈ 0.7 — patterns re-align every ~20 UV units
3. Specular highlights amplify normal map features, making repetition obvious
4. Linear normal blending (`n1 + n2`) flattens detail
5. Same UV coords for normal and DuDv — patterns correlate

### Fix Strategy (ranked by ease)

#### Tier 1 — Shader-Only Changes (no new assets)

**A. Add 3rd normal map layer** at micro-detail scale (~11.7x), divergent scroll direction:
```glsl
vec2 scrolledCoords3 = v_worldPos.xz * 11.7 + vec2(flowOffset * 0.6, -flowOffset * 0.5);
vec3 n3 = texture(u_normalMap, scrolledCoords3).rgb * 2.0 - 1.0;
```

**B. Use Whiteout blending** instead of linear:
```glsl
// Current (weak): normalize(n1 + n2)
// Better: normalize(vec3(n1.xy + n2.xy, n1.z * n2.z))
```

**C. Make scroll directions more divergent** — use 0°, 120°, 240° angles

**D. Decorrelate DuDv from normal map** — sample DuDv at one set of coords, use result to offset normal map UVs

#### Tier 2 — Add Small Noise Texture

**E. Noise UV distortion:** Small 64x64 tileable noise texture sampled at very low frequency to subtly offset normal map UVs. Cost: 1 extra texture sample.

#### Tier 3 — Flowmap (best for small water bodies)

**F. Flowmap animation** (Valve SIGGRAPH 2010): Per-pixel flow direction texture with two-phase cycling to hide resets. Makes water look alive with currents/eddies. Ideal for contained basins.

**Source:** https://catlikecoding.com/unity/tutorials/flow/texture-distortion/

#### Tier 4 — Procedural Detail

**G. Simplex FBM detail layer:** Replace highest-frequency normal layer with 2-3 octaves of 3D simplex noise. Truly non-repeating. Cost: equivalent to 2-4 texture samples in ALU.

### What to Avoid for a 5.5m Basin
- FFT ocean simulation (wrong scale, massive overkill)
- Full stochastic tiling (per-cell blending visible at this scale)
- Tessellation LOD (unnecessary for small flat surface)

---

## Part 3: General Performance Improvements

### Frame Budget (16.67ms target)

| Stage | Target | Notes |
|-------|--------|-------|
| Shadow maps (4 cascades) | 2-3ms | Variable resolution per cascade |
| Water (reflection + refraction) | 1-2ms | With SSPR + screen-space refraction |
| Main geometry | 3-5ms | Forward rendering |
| Terrain | 1-2ms | CDLOD |
| Foliage | 1-1.5ms | Instanced |
| SSAO | 0.5-1ms | Half resolution + temporal |
| Bloom + SMAA + tone map | 1-2ms | |
| UI/editor | 0.3-0.5ms | ImGui |
| **Headroom** | **2-3ms** | Absorbs frame-to-frame variance |

### Shadow Optimizations

- **Variable resolution:** Cascade 0: 2048, Cascade 1-2: 1024, Cascade 3: 512 (saves ~2ms)
- **Shadow caching:** Render static geometry once, only update dynamic objects per frame
- **CSM scrolling:** When camera moves, shift shadow map contents and only re-render the new border (80-90% savings for slow camera movement)
- **Far cascade amortization:** Update cascades 2-3 every 2-4 frames

### Draw Call Reduction

- **Enable MDI:** The infrastructure exists. Populate the mesh pool at scene load. Group by material. One `glMultiDrawElementsIndirect` per material.
- **Compute shader culling:** Dispatch compute to frustum/occlusion cull → write indirect commands → draw. Eliminates CPU-GPU draw submission bottleneck.
- **Bindless textures:** `GL_ARB_bindless_texture` — all textures resident, indexed per-draw via SSBO. Merges draw calls across different textures.

### Temporal Techniques

- **Temporal SSAO:** Half-resolution + 3-frame rotating sample pattern + reprojection. Saves ~50% SSAO cost (Frictional Games: 1.1ms at 1080p)
- **Half-rate shadow cascades:** Far cascades update every 2-4 frames
- **Checkerboard rendering:** Render half pixels, reconstruct from previous frame. 1.4x speedup.
- **Temporal upscaling:** Render at 75% resolution, reconstruct with motion vectors (engine already generates them for TAA)

### VSync & Frame Pacing

- **Triple buffering:** Prevents the 60→30 FPS halving when a frame barely misses VSync
- **FreeSync:** RX 6600 supports it. If the monitor does too, eliminates both tearing and VSync stutter
- Current VSync causes any frame >16.67ms to wait until 33.3ms — this is why 54 FPS, not 59

### OpenGL 4.5 Tips

- **Persistent mapped buffers:** Map once, write per-frame with triple-buffering + fences. Fastest buffer update path.
- **Compute for full-screen passes:** AMD recommends compute over fragment for screen-space effects (eliminates helper lane overhead)
- **Sort draws by pipeline state:** Reduce state transitions between draw calls
- **Avoid RDNA arc functions:** `atan`, `acos` generate 100+ cycles. Use approximations.

---

## Sources

### Water Rendering
- UE5 Single Layer Water: https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine
- SSPR (Ghost Recon): https://remi-genin.github.io/posts/screen-space-planar-reflections-in-ghost-recon-wildlands/
- GPU Gems Ch.1 Gerstner Waves: https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models
- Alex Tardif Water Walkthrough: https://alextardif.com/Water.html
- Catlike Coding Flow Maps: https://catlikecoding.com/unity/tutorials/flow/texture-distortion/
- Far Cry 5 Water (GDC 2018): https://www.gdcvault.com/play/1025033/Advanced-Graphics-Techniques-Tutorial-Water
- Sea of Thieves (SIGGRAPH 2018): https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf
- Ubisoft Aperiodic Ocean (HPG 2024): https://www.ubisoft.com/en-us/studio/laforge/news/5WHMK3tLGMGsqhxmWls1Jw/making-waves-in-ocean-surface-rendering-using-tiling-and-blending

### Anti-Tiling
- Inigo Quilez Texture Repetition: https://iquilezles.org/articles/texturerepetition/
- Blending In Detail (normal blending): https://blog.selfshadow.com/publications/blending-in-detail/
- Valve Flowmaps (SIGGRAPH 2010): https://advances.realtimerendering.com/s2010/Vlachos-Waterflow(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf
- Simplex Noise in GLSL: https://ar5iv.labs.arxiv.org/html/1204.1461

### Performance
- AMD RDNA Performance Guide: https://gpuopen.com/learn/rdna-performance-guide/
- MDI in OpenGL: https://ktstephano.github.io/rendering/opengl/mdi
- GPU-Driven Rendering: https://vkguide.dev/docs/gpudriven/gpu_driven_engines/
- Persistent Mapped Buffers: https://www.cppstories.com/2015/01/persistent-mapped-buffers-in-opengl/
- Temporal SSAO (Frictional Games): https://frictionalgames.com/2014-01-tech-feature-ssao-and-temporal-blur/
- DOOM Eternal Graphics Study: https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy
- Sparse Virtual Shadow Maps: https://ktstephano.github.io/rendering/stratusgfx/svsm
