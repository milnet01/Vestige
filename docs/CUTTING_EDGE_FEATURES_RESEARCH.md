# Cutting-Edge & Experimental Features Research

**Target:** C++17 / OpenGL 4.5 / AMD RDNA2 (RX 6600) / Mesa RadeonSI on Linux
**Date:** 2026-03-25

This document surveys 15 advanced and experimental graphics/engine features, evaluating each for
feasibility, driver support, implementation complexity, and whether it is worth pursuing for the
Vestige 3D engine.

---

## Table of Contents

1. [Mesh Shaders](#1-mesh-shaders)
2. [Bindless Textures](#2-bindless-textures)
3. [Sparse Textures / Virtual Texturing](#3-sparse-textures--virtual-texturing)
4. [GPU-Driven Rendering](#4-gpu-driven-rendering)
5. [Nanite-Like LOD Systems](#5-nanite-like-lod-systems)
6. [Ray Tracing in OpenGL](#6-ray-tracing-in-opengl)
7. [Neural Rendering / Upscaling (FSR)](#7-neural-rendering--upscaling-fsr)
8. [Volumetric Lighting and Fog](#8-volumetric-lighting-and-fog)
9. [Global Illumination](#9-global-illumination)
10. [Screen-Space Reflections Improvements](#10-screen-space-reflections-improvements)
11. [Atmospheric Scattering](#11-atmospheric-scattering)
12. [Procedural Generation Techniques](#12-procedural-generation-techniques)
13. [Animation Systems](#13-animation-systems)
14. [Physics Integration](#14-physics-integration)
15. [Audio Integration](#15-audio-integration)

---

## 1. Mesh Shaders

### What They Are

Mesh shaders replace the traditional vertex/geometry shader pipeline with two new programmable
stages: **task shaders** (optional, analogous to compute pre-pass) and **mesh shaders** (output
triangles directly). They enable GPU-driven geometry processing, culling at the meshlet level,
and more efficient use of modern GPU architectures.

### Current Status: PRODUCTION-READY (Mesa 26.0+)

- **Extension:** `GL_EXT_mesh_shader` (cross-vendor, replacing NVIDIA's `GL_NV_mesh_shader`)
- **Mesa/AMD Support:** Completed for Mesa 26.0 (released 2026-02-11). AMD engineer Qiang Yu
  led the effort. Supported on **GFX10.3 and newer** -- this includes RDNA2 (RX 6600).
- The `GL_EXT_mesh_shader` extension was merged into the official OpenGL Registry and RadeonSI
  support followed shortly after.
- The Zink (OpenGL-on-Vulkan) driver also supports it.

### Implementation Complexity: HIGH

- Requires rethinking the entire geometry pipeline (meshlets, cluster-based submission).
- Must build a meshlet data structure at load time (splitting meshes into small clusters of
  ~64-128 vertices).
- Task shaders add another layer for LOD selection and culling.
- Debugging is harder than traditional vertex shaders.
- Libraries like `meshoptimizer` help with meshlet generation.

### Key Resources

- [Phoronix: RadeonSI Mesh Shaders Mesa 26.0](https://www.phoronix.com/news/RadeonSI-Mesh-Shaders-Mesa-26.0)
- [EXT_mesh_shader specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_mesh_shader.txt)
- [OpenGL Resurges in 2025 with Mesh Shader Extensions](https://www.webpronews.com/opengl-resurges-in-2025-with-mesh-shader-extensions-for-gaming/)
- [Supergoodcode: Mesh Shaders in the Current Year](https://www.supergoodcode.com/mesh-shaders-in-the-current-year/)
- [Mesamatrix](https://mesamatrix.net/)

### Verdict: PURSUE (medium-term)

Mesh shaders are now production-ready on our exact hardware via Mesa 26.0. They are the future
of geometry processing and enable GPU-driven rendering, per-meshlet culling, and Nanite-like LOD.
However, the traditional vertex pipeline works well today, so this should be a medium-term goal
after core features are stable. Prioritize this when implementing GPU-driven rendering (topic 4)
or Nanite-like LOD (topic 5).

---

## 2. Bindless Textures

### What They Are

`GL_ARB_bindless_texture` allows textures to be referenced by 64-bit GPU handles rather than
being bound to numbered texture units. This eliminates the traditional limit of ~16-32 active
texture units and removes bind/unbind overhead entirely.

### Current Status: PRODUCTION-READY

- **Extension:** `GL_ARB_bindless_texture` (never made it into core OpenGL, still an extension
  even in 4.6)
- **Mesa/AMD Support:** Supported in RadeonSI since Mesa 18.x (2017-2018). 70+ patches
  implemented the full feature. Mature and stable on RDNA2.
- Both NVIDIA and AMD fully support it in hardware.

### How It Works

1. Create a texture and get its handle: `glGetTextureHandleARB(texture)`
2. Make it resident: `glMakeTextureHandleResidentARB(handle)`
3. Pass the 64-bit handle via UBO/SSBO to shaders
4. In GLSL, cast the handle to a sampler: `sampler2D(handle)`

### Performance Benefits

- Eliminates texture bind/unbind calls entirely
- Enables single draw call for objects with different textures (via Multi-Draw Indirect)
- Can reduce CPU overhead by an order of magnitude in texture-heavy scenes
- Essential building block for GPU-driven rendering

### Implementation Complexity: MEDIUM

- Straightforward API usage once understood
- Must manage texture residency carefully (GPU memory)
- Need to refactor material system to use handles instead of bind slots
- Shader changes are moderate (accept handles via SSBO, cast to samplers)

### Key Resources

- [Bindless Textures Tutorial (J Stephano)](https://ktstephano.github.io/rendering/opengl/bindless)
- [Bindless Texture - OpenGL Wiki](https://www.khronos.org/opengl/wiki/Bindless_Texture)
- [Phoronix: Bindless Textures in Mesa](https://www.phoronix.com/forums/forum/linux-graphics-x-org-drivers/opengl-vulkan-mesa-gallium3d/957402-bindless-textures-land-in-mesa-git-for-radeonsi)
- [NVIDIA Bindless Graphics Tutorial](https://www.nvidia.com/en-us/drivers/bindless-graphics/)

### Verdict: STRONGLY PURSUE (short-term)

This is one of the highest-value additions for the engine. It is mature, well-supported on our
hardware, medium complexity, and is a prerequisite for GPU-driven rendering. The combination of
bindless textures + Multi-Draw Indirect is the foundation of modern OpenGL rendering. Should be
implemented before or alongside GPU-driven rendering.

---

## 3. Sparse Textures / Virtual Texturing

### What They Are

Sparse textures (`GL_ARB_sparse_texture`) allow allocating very large textures where only
portions are physically backed by GPU memory. Pages are committed/decommitted on demand,
enabling virtual texturing -- streaming texture data at the granularity of tiles.

### Current Status: PRODUCTION-READY

- **Extension:** `GL_ARB_sparse_texture` (based on original `GL_AMD_sparse_texture`)
- **Mesa/AMD Support:** Supported in RadeonSI since Mesa 22.0 for GFX9/Vega and newer.
  RDNA2 (RX 6600) is fully supported. Additional sparse texture enhancements landed for
  RDNA4 in Mesa 25.3.

### How It Works

1. Create a texture with `GL_TEXTURE_SPARSE_ARB` flag
2. Query the page size with `glGetInternalformativ(GL_VIRTUAL_PAGE_SIZE_*)`
3. Commit/decommit pages with `glTexPageCommitmentARB()`
4. Use a feedback pass to determine which pages are needed at what mip level
5. Stream page data from disk to committed pages

### Implementation Complexity: VERY HIGH

- Requires a complete texture streaming pipeline (page cache, priority queue, async I/O)
- Need a feedback mechanism (typically a low-res render pass that records page requests)
- Must handle page faults gracefully (fallback to lower mip levels)
- Disk format needs to support tiled/paged access
- A research paper ("The Sad State of Hardware Virtual Textures") notes significant practical
  challenges with current hardware implementations

### Key Resources

- [AMD GPUOpen: Sparse Textures OpenGL](https://gpuopen.com/learn/sparse-textures-opengl/)
- [Phoronix: RadeonSI Sparse Textures](https://www.phoronix.com/news/RadeonSI-Sparse-Textures)
- [Holger Dammertz: Sparse Virtual Texturing Notes](https://holger.dammertz.org/stuff/notes_VirtualTexturing.html)
- [GitHub: SparseVirtualTexturing](https://github.com/DudleyHK/SparseVirtualTexturing)
- [Toni Sagrista: Sparse Virtual Textures](https://tonisagrista.com/blog/2023/sparse-virtual-textures/)

### Verdict: DEFER (long-term)

While the hardware support exists, the implementation complexity is extreme. Virtual texturing
is most beneficial for open-world games with massive texture datasets. For architectural
walkthroughs (Tabernacle, Solomon's Temple), conventional texture management with manual LOD
should suffice. Revisit when the engine needs to handle very large environments.

---

## 4. GPU-Driven Rendering

### What It Is

GPU-driven rendering shifts draw call generation, culling, and LOD selection from the CPU to the
GPU using compute shaders and indirect drawing commands. The CPU submits a single (or very few)
multi-draw-indirect calls; the GPU decides what to render.

### Current Status: PRODUCTION-READY

The key OpenGL features are all available:
- `glMultiDrawElementsIndirect()` / `glMultiDrawArraysIndirect()` -- core since OpenGL 4.3
- Shader Storage Buffer Objects (SSBOs) -- core since OpenGL 4.3
- Compute shaders -- core since OpenGL 4.3
- Atomic counters -- core since OpenGL 4.2
- Combined with bindless textures (topic 2), this eliminates nearly all CPU-side per-object work

### Architecture

1. **Object buffer:** All per-object data (transform, material index, mesh offset) in one large
   SSBO
2. **Compute culling pass:** A compute shader reads object data, performs frustum culling (and
   optionally occlusion culling), and writes `DrawElementsIndirectCommand` structs into an
   indirect draw buffer
3. **Single draw call:** `glMultiDrawElementsIndirect()` consumes the buffer, drawing all
   visible objects in one call
4. **Bindless materials:** Each object's material index points into a bindless texture handle
   array in another SSBO

### Performance Benefits

- GPU-side frustum culling of 1M+ objects in < 0.5ms
- CPU submits only 1-2 draw calls per frame regardless of object count
- Eliminates driver overhead from thousands of individual draw calls
- Reduces CPU-GPU synchronization points

### Implementation Complexity: HIGH

- Requires refactoring the renderer to batch all geometry into shared vertex/index buffers
- Need a compute shader culling pipeline
- Must manage persistent mapped buffers and synchronization carefully
- Material system must move to bindless (topic 2)
- Debugging is harder (GPU-side logic)

### Key Resources

- [Multi-Draw Indirect Tutorial (J Stephano)](https://ktstephano.github.io/rendering/opengl/mdi)
- [Indirect Rendering: A Way to a Million Draw Calls](https://cpp-rendering.io/indirect-rendering/)
- [GitHub: AZDO OpenGL Techniques](https://github.com/potato3d/azdo)
- [AnKi Engine: GPU-Driven Rendering Overview](https://anki3d.org/gpu-driven-rendering-in-anki-a-high-level-overview/)
- [OpenGL MDI with Per-Instance Textures](https://litasa.github.io/blog/2017/09/04/OpenGL-MultiDrawIndirect-with-Individual-Textures)

### Verdict: STRONGLY PURSUE (medium-term)

This is the single most impactful architectural upgrade for rendering performance. It requires
bindless textures (topic 2) as a prerequisite. The recommended approach:
1. First implement bindless textures
2. Then implement MDI with a shared geometry buffer
3. Then add GPU frustum culling via compute shader
4. Optionally add occlusion culling later

---

## 5. Nanite-Like LOD Systems

### What They Are

Inspired by Unreal Engine 5's Nanite, these systems use hierarchical mesh cluster LOD with
seamless transitions. Geometry is split into meshlets (clusters of ~64-128 triangles), organized
into a DAG (directed acyclic graph) of LOD levels, and the GPU selects per-cluster LOD each frame.

### Current Status: EXPERIMENTAL

- No standard library or API -- this is an engine architecture pattern
- Several open-source implementations exist but none are production-grade for OpenGL
- Best results require mesh shaders (topic 1) for per-cluster rendering
- `meshoptimizer` library provides meshlet generation and cluster LOD building

### Implementation Approach

1. **Offline:** Use `meshoptimizer` to split meshes into meshlets, then build a cluster LOD
   hierarchy via `meshopt_simplifyCluster` and related APIs
2. **Runtime:** Task shader evaluates screen-space error per cluster, selects appropriate LOD
   level; mesh shader renders selected clusters
3. **Fallback without mesh shaders:** Use compute shader to build an indirect draw buffer with
   selected clusters, then render via MDI

### Key Implementations

- [nanite-at-home (C++ / OpenGL)](https://github.com/P1ben/nanite-at-home) -- cluster-based
  mesh simplification in C++ with OpenGL
- [nanite-webgpu](https://github.com/Scthe/nanite-webgpu) -- full Nanite pipeline in WebGPU
  (software rasterizer, meshlet LOD, culling)
- [meshoptimizer Nanite-like LOD](https://deepwiki.com/zeux/meshoptimizer/3.2-nanite-like-lod-system)
- [Recreating Nanite: Mesh Shader Time](https://blog.jglrxavpok.eu/2024/05/13/recreating-nanite-mesh-shader-time.html)
- [NVIDIA vk_lod_clusters](https://github.com/nvpro-samples/vk_lod_clusters) -- Vulkan but
  instructive architecture

### Implementation Complexity: VERY HIGH

- Requires mesh shaders or a complex compute+MDI fallback
- Offline mesh processing pipeline needed
- DAG-based LOD selection is algorithmically complex
- Software rasterizer may be needed for very small clusters (< pixel size)
- Debugging cluster-level issues is challenging

### Verdict: DEFER (long-term aspirational)

This is the holy grail of geometry rendering but the implementation complexity is extreme. For
architectural walkthroughs with relatively modest geometry counts, traditional LOD or simple
meshlet rendering is sufficient. Revisit after mesh shaders (topic 1) and GPU-driven rendering
(topic 4) are working. Start with `meshoptimizer` for meshlet generation as a first step.

---

## 6. Ray Tracing in OpenGL

### What It Is

Ray tracing in OpenGL means using compute shaders to trace rays against scene geometry, since
OpenGL has no native ray tracing extensions (unlike Vulkan's `VK_KHR_ray_tracing_pipeline`).

### Current Status: FEASIBLE BUT LIMITED

- **No hardware RT in OpenGL:** There is no OpenGL extension for hardware-accelerated ray
  tracing. The RX 6600 has basic HW RT units, but they are only accessible via Vulkan.
- **Compute shader RT:** Fully possible using OpenGL 4.3+ compute shaders. Rays are traced
  against BVH structures stored in SSBOs.
- **Performance:** Software ray tracing in compute shaders is orders of magnitude slower than
  hardware RT. Suitable for limited effects (reflections on a few surfaces, ambient occlusion
  at low resolution) but not full scene ray tracing at 60 FPS.

### Implementation Approaches

1. **Full compute shader path tracer:** Each pixel traces rays through a BVH. Works but slow
   for real-time. Good for offline/progressive rendering.
2. **Hybrid rasterization + compute RT:** Rasterize the scene normally, then use compute
   shaders for specific effects (reflections, shadows, AO) at reduced resolution.
3. **Screen-space ray tracing:** Not true RT but traces rays against the depth buffer. Faster
   but limited to on-screen information (see topic 10).

### Key Resources

- [LWJGL: Ray Tracing with OpenGL Compute Shaders](https://github.com/LWJGL/lwjgl3-wiki/wiki/2.6.1.-Ray-tracing-with-OpenGL-Compute-Shaders-(Part-I))
- [GPU Raytracer with OpenGL Compute (imgeself)](https://imgeself.github.io/posts/2019-06-26-raytracer-5-gpu-opengl/)
- [GitHub: raytracing-gl (P. Shirley port)](https://github.com/D-K-E/raytracing-gl)
- [GitHub: OpenGL-Raytracer (Hirevo)](https://github.com/Hirevo/OpenGL-Raytracer)

### Implementation Complexity: HIGH (for useful results)

- BVH construction and traversal in compute shaders
- Performance tuning is critical (warp/wavefront divergence, memory access patterns)
- Denoising needed for acceptable image quality at low sample counts

### Verdict: SELECTIVE PURSUIT (medium-term)

Do not try to build a general-purpose ray tracer in OpenGL. Instead, use compute shaders for
specific limited effects:
- **Reflections:** On a few key surfaces (polished gold, water) at half resolution
- **Ambient occlusion:** Low-resolution SSAO alternative
- **Light probes:** Bake irradiance probes using compute shader RT (see topic 9)

For full ray tracing, the path forward is the eventual Vulkan backend where hardware RT is
accessible.

---

## 7. Neural Rendering / Upscaling (FSR)

### What It Is

Temporal and spatial upscaling techniques that render at a lower resolution and reconstruct a
higher-resolution image. AMD's FidelityFX Super Resolution (FSR) is the open-source option.

### Current Status

| Technique | API Support | Quality | Complexity | Status |
|-----------|------------|---------|------------|--------|
| FSR 1.0 (spatial) | **Works in OpenGL** (fragment shader) | Good | LOW | Production-ready |
| FSR 2.0 (temporal) | **OpenGL port exists** | Very Good | MEDIUM-HIGH | Experimental port |
| FSR 3.0+ (frame gen) | Vulkan/DX12 only | Excellent | N/A for OpenGL | Not applicable |
| DLSS | NVIDIA only, no OpenGL | Excellent | N/A | Not applicable |

### FSR 1.0 in OpenGL

FSR 1.0 is a **spatial upscaler** -- it takes a single low-resolution frame and produces a
higher-resolution output using edge-adaptive filtering (EASU pass) and sharpening (RCAS pass).

- **It just works in OpenGL.** Despite AMD only officially supporting DX12/Vulkan, the shader
  code is pure math that runs on any GPU with fragment shaders.
- **Implementation:** Two fragment shader passes. Render scene at lower resolution, apply EASU
  (upscale), then RCAS (sharpen). Can also be done as compute shader passes.
- The shader code is open-source (MIT license) on GitHub.

### FSR 2.0 in OpenGL

FSR 2.0 is a **temporal upscaler** -- it uses motion vectors, depth, and previous frames to
reconstruct high-quality output. Much better quality than FSR 1.0.

- **OpenGL port exists:** [FidelityFX-FSR2-OpenGL](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL)
  by JuanDiegoMontoya, with a detailed [porting blog post](https://juandiegomontoya.github.io/porting_fsr2.html).
- FSR 2.0's modular backend design makes API ports feasible.
- Requires providing motion vectors, depth buffer, and exposure data.
- The OpenGL port is community-maintained, not officially supported by AMD.

### Implementation Complexity

- **FSR 1.0:** LOW -- Two shader passes, well-documented, open-source GLSL available
- **FSR 2.0:** MEDIUM-HIGH -- Multiple compute passes, temporal accumulation, motion vector
  generation, jittered projection

### Key Resources

- [FSR 1.0 GitHub](https://github.com/GPUOpen-Effects/FidelityFX-FSR)
- [FSR 1.0 Demystified (jntesteves)](https://jntesteves.github.io/shadesofnoice/graphics/shaders/upscaling/2021/09/11/amd-fsr-demystified.html)
- [FSR 2.0 OpenGL Port](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL)
- [Porting FSR 2 to OpenGL (blog)](https://juandiegomontoya.github.io/porting_fsr2.html)
- [FSR on Shadertoy](https://www.shadertoy.com/view/stXSWB)
- [AMD GPUOpen FSR 1.2 Manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-spatial/)

### Verdict: STRONGLY PURSUE FSR 1.0 (short-term), CONSIDER FSR 2.0 (medium-term)

FSR 1.0 is a low-hanging fruit: trivial to implement, immediate performance benefit (render at
75% resolution, upscale to native), and works perfectly in OpenGL. FSR 2.0 offers much better
quality but requires motion vectors and a more complex integration. FSR 1.0 should be one of
the first advanced features added.

---

## 8. Volumetric Lighting and Fog

### What It Is

Real-time simulation of light scattering through participating media (fog, haze, dust, god
rays). Modern approaches use frustum-aligned 3D voxel grids processed by compute shaders.

### Current Status: PRODUCTION-READY

The technique is well-established and has been used in AAA games since ~2014. OpenGL compute
shaders provide all necessary functionality.

### Implementation Approaches

| Approach | Quality | Performance | Complexity |
|----------|---------|-------------|------------|
| Post-process radial blur (god rays) | Low | Very fast | LOW |
| Ray marching in fragment shader | Good | Medium | MEDIUM |
| Frustum-aligned voxel grid + compute | Excellent | Good | HIGH |

**Recommended: Frustum-aligned voxel grid**

1. **Inject:** Compute shader fills a 3D texture (frustum-aligned) with scattering/extinction
   coefficients and light contributions
2. **Propagate:** Ray-march through the 3D texture front-to-back, accumulating in-scattered
   light using Beer's Law: `I = I0 * exp(-alpha * d)`
3. **Apply:** During the main lighting pass, sample the volume texture to add fog color and
   attenuation

Key optimizations:
- Blue noise dithering to eliminate banding artifacts
- Temporal reprojection for stability
- Exponential depth distribution (more voxels near camera)

### Key Resources

- [Bart Wronski: Volumetric Fog (SIGGRAPH 2014)](https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf)
- [GitHub: volumetric-fog (OpenGL + compute)](https://github.com/diharaw/volumetric-fog)
- [GitHub: opengl-volumetric-lighting](https://github.com/clayne/opengl-volumetric-lighting)
- [NVIDIA GPU Gems 3: Volumetric Light Scattering](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process)
- [Ray-Marching Volumetric Fog (Lon van Ettinger)](https://lonvanettinger.com/portfolio-pages/fog-ray-march-article-1)

### Implementation Complexity: MEDIUM-HIGH

- 3D texture management and frustum alignment
- Compute shader pipeline (inject, propagate, apply)
- Temporal reprojection adds complexity but is important for quality
- Integration with shadow maps for shadowed fog

### Verdict: PURSUE (medium-term)

Volumetric lighting adds tremendous atmosphere to architectural walkthroughs -- imagine light
streaming through the Tabernacle entrance or dust motes in Solomon's Temple. Start with simple
post-process god rays (low complexity), then upgrade to the full voxel grid approach.

---

## 9. Global Illumination

### What It Is

Techniques for simulating indirect light bounces -- light that reflects off surfaces and
illuminates other surfaces. Critical for realistic interior scenes.

### Current Status: MULTIPLE APPROACHES AVAILABLE

| Technique | Quality | Performance | Dynamic? | Complexity | OpenGL? |
|-----------|---------|-------------|----------|------------|---------|
| Light probes (baked) | Good | Very fast | No | LOW | Yes |
| Irradiance volumes | Good | Fast | Partial | MEDIUM | Yes |
| Voxel Cone Tracing (VCT) | Very good | Medium | Yes | HIGH | Yes |
| DDGI | Excellent | Medium | Yes | VERY HIGH | Partial |
| Screen-space GI (SSGI) | Fair | Fast | Yes | MEDIUM | Yes |

### Voxel Cone Tracing (VCT)

The most practical fully-dynamic GI solution for OpenGL:

1. **Voxelize** the scene into a 3D texture (128^3 or 256^3) using geometry shader or
   compute shader
2. **Inject** direct lighting into the voxel grid
3. **Generate mipmaps** of the 3D texture (anisotropic filtering preferred)
4. **Trace cones** from each surface point into the voxel mipmap hierarchy to gather indirect
   diffuse and specular illumination

Available as C++ / GLSL implementations on GitHub. The Wicked Engine uses this approach.

### DDGI (Dynamic Diffuse Global Illumination)

Uses a grid of irradiance probes, each storing low-resolution irradiance and depth in octahedral
maps. Originally requires hardware ray tracing to update probes, but can be adapted to use:
- Compute shader software ray tracing (slow but works in OpenGL)
- SDF (signed distance field) tracing as an alternative to RT
- The technique has O(1) rendering complexity -- probes are sampled in the lighting shader

### Recommended Approach for Vestige

1. **Short-term:** Baked light probes (spherical harmonics) placed in the scene editor.
   Simple, fast, excellent quality for static architecture.
2. **Medium-term:** Voxel Cone Tracing for dynamic GI (moving lights, time of day changes).
3. **Long-term:** DDGI when Vulkan backend enables hardware RT probe updates.

### Key Resources

- [Voxel Cone Tracing (GitHub, C++/GLSL)](https://github.com/Friduric/voxel-cone-tracing)
- [Real-Time GI Using OpenGL and VCT (arXiv)](https://arxiv.org/abs/2104.00618)
- [Voxel-Based GI (Wicked Engine)](https://wickedengine.net/2017/08/voxel-based-global-illumination/)
- [Deferred Voxel Shading for Real-Time GI](https://jose-villegas.github.io/post/deferred_voxel_shading/)
- [DDGI Paper (Morgan McGuire et al.)](https://morgan3d.github.io/articles/2019-04-01-ddgi/)
- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [Dynamic Voxel-Based GI (2025)](https://onlinelibrary.wiley.com/doi/10.1111/cgf.15262)

### Implementation Complexity: MEDIUM (baked probes) to VERY HIGH (VCT/DDGI)

### Verdict: PURSUE INCREMENTALLY

Baked light probes are a must-have for interior architectural scenes and are straightforward to
implement. VCT is the logical next step for dynamic scenarios. DDGI should wait for Vulkan.

---

## 10. Screen-Space Reflections Improvements

### What They Are

Screen-space reflections (SSR) trace rays against the depth buffer to find reflections of
on-screen geometry. Modern improvements use hierarchical ray marching and temporal filtering for
quality and performance.

### Current Status: PRODUCTION-READY

SSR is a well-understood technique used in most modern game engines. OpenGL implementations exist.

### Key Improvements Over Basic SSR

**Hierarchical Ray Marching (Hi-Z tracing):**
- Build a hierarchical Z-buffer (depth buffer mipmap chain)
- Start ray marching at the finest mip level (mip 0)
- If no intersection, drop to a coarser mip level and take larger steps
- On intersection, refine at finer mip levels
- Result: Same quality with far fewer iterations (significant performance improvement)

**Temporal Filtering:**
- Accumulate reflection results across frames using temporal reprojection
- Reject stale samples using depth/normal validation
- Dramatically reduces noise from stochastic sampling
- AMD's FidelityFX SSSR implements this approach

**Stochastic Screen-Space Reflections (SSSR):**
- AMD FidelityFX SSSR: Searches the rendered image hierarchically, supports rough surfaces
  via importance-sampled GGX, includes temporal denoiser
- Open-source, designed for real-time performance

### Implementation Complexity: MEDIUM-HIGH

- Hi-Z buffer generation (compute shader mipmap chain)
- Ray marching kernel with mip level traversal
- Temporal accumulation with reprojection
- Edge cases: off-screen data, back-face reflections, contact hardening

### Key Resources

- [AMD FidelityFX SSSR](https://gpuopen.com/fidelityfx-sssr/)
- [Notes on SSR with FidelityFX SSSR](https://interplayoflight.wordpress.com/2022/09/28/notes-on-screenspace-reflections-with-fidelityfx-sssr/)
- [Efficient GPU Screen-Space Ray Tracing (JCGT)](https://jcgt.org/published/0003/04/04/paper.pdf)
- [Screen Space Reflections (3D Game Shaders for Beginners)](https://lettier.github.io/3d-game-shaders-for-beginners/screen-space-reflection.html)
- [High Performance SSR Algorithm](https://maorachow.github.io/3d/graphics/2024/09/11/a-high-performance-screen-space-reflection-algorithm.html)
- [SSR in The Surge (Deck17)](https://www.slideshare.net/slideshow/screen-space-reflections-in-the-surge/63239467)

### Verdict: PURSUE (medium-term)

SSR with Hi-Z tracing is a significant visual upgrade for reflective surfaces (gold fixtures,
polished stone, water). The engine already has a deferred rendering pipeline which makes SSR
integration natural. Start with basic linear ray marching, then add Hi-Z acceleration and
temporal filtering.

---

## 11. Atmospheric Scattering

### What It Is

Physically-based rendering of sky color, aerial perspective, and sun/moon appearance by
simulating Rayleigh and Mie scattering through the atmosphere.

### Current Status: PRODUCTION-READY

Two well-established models with available implementations:

### Bruneton Model (2008, updated 2017)

- Precomputes a 4D lookup table (packed into 3D textures) for transmittance, irradiance, and
  inscattered light
- Accounts for multiple scattering
- Works from ground level to outer space
- **2017 update** by Eric Bruneton adds better documentation, configurable solar spectrum, and
  cleaner code
- OpenGL/GLSL implementations available

### Hillaire Model (2020)

- Used by Unreal Engine
- Uses smaller, simpler lookup tables (Transmittance LUT, Multi-Scattering LUT, Sky View LUT)
- Faster to compute than Bruneton, fewer artifacts
- Supports dynamic atmosphere composition changes
- More suitable for real-time applications with changing weather
- New approximation for multiple scattering evaluated in real time

### Recommendation

The Hillaire model is preferred for Vestige:
- Simpler LUT structure (3 small 2D textures vs. Bruneton's large 3D textures)
- Better suited to dynamic time-of-day
- Production-proven in Unreal Engine
- The Bruneton model is a valid fallback with more available OpenGL implementations

### Implementation Complexity: MEDIUM

- Precompute LUTs (can be done once at startup or when atmosphere params change)
- Sky rendering shader samples LUTs based on view direction and sun position
- Aerial perspective applied to distant objects in the main lighting pass
- Integration with existing skybox system

### Key Resources

- [Bruneton: Precomputed Atmospheric Scattering (updated implementation)](https://ebruneton.github.io/precomputed_atmospheric_scattering/)
- [GitHub: bruneton-sky-model (OpenGL)](https://github.com/diharaw/bruneton-sky-model)
- [Hillaire: A Scalable Sky and Atmosphere Technique](https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.14050)
- [Hillaire paper PDF](https://sebh.github.io/publications/egsr2020.pdf)
- [Sky Rendering in Production (Shadertoy)](https://www.shadertoy.com/view/slSXRW)
- [Atmosphere Rendering Blog (trist.am)](https://www.trist.am/blog/2024/atmosphere-rendering/)
- [CPP Rendering: Sky and Atmosphere](https://cpp-rendering.io/sky-and-atmosphere-rendering/)
- [GLSL Atmosphere (Rayleigh + Mie)](https://github.com/wwwtyro/glsl-atmosphere)

### Verdict: PURSUE (medium-term)

Atmospheric scattering is essential for outdoor scenes and time-of-day systems. The Hillaire
model provides excellent quality at manageable complexity. This pairs well with volumetric
lighting (topic 8) for a complete atmospheric rendering pipeline.

---

## 12. Procedural Generation Techniques

### What They Are

GPU-accelerated generation of terrain, vegetation placement, noise fields, and other content
using compute shaders and tessellation.

### Current Status: PRODUCTION-READY

All required OpenGL features are core since 4.3 (compute shaders) and 4.0 (tessellation).

### Key Techniques

**Noise Generation (Compute Shader):**
- Perlin noise, Simplex noise, Worley noise computed on GPU
- Fractal Brownian motion (fBm) for terrain height maps
- GPU shared memory enables efficient Sobel operator for normal calculation
- Output to texture or SSBO for downstream consumption

**Terrain Generation:**
- Compute shader generates heightmap from layered noise functions
- Tessellation shaders provide distance-based LOD (more triangles near camera)
- Clipmap or quadtree for large terrain management
- Normal maps computed from heightmap via Sobel filter

**Vegetation Placement:**
- Compute shader scatters instances based on terrain slope, height, moisture maps
- Output instance transforms to SSBO for instanced rendering
- Frustum and distance culling in compute pre-pass
- Pairs with the existing foliage system (Phase 5G)

### Implementation Complexity: MEDIUM

- Noise functions in GLSL are straightforward (many reference implementations)
- Terrain pipeline is well-documented with multiple OpenGL 4.5 examples
- Vegetation placement builds on existing instancing infrastructure

### Key Resources

- [Procedural Terrain Generation (OpenGL 4.5)](https://github.com/math-araujo/procedural-terrain-generation)
- [NVIDIA GPU Gems 3: Procedural Terrains](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu)
- [Procedural Terrain with Tessellation (GitHub)](https://github.com/Vattghern7/ProceduralTerrainGeneration)
- [Perlin Noise Terrain in OpenGL (Medium)](https://medium.com/@muhammedcan.erbudak/terrain-rendering-via-perlin-noise-and-opengl-geometry-shaders-55eb10aa1d3c)
- [OpenGL Procedural Terrain (GameDev.net)](https://gamedev.net/blogs/blog/9537-opengl-procedural-terrain-generation/)

### Verdict: PURSUE SELECTIVELY (medium-term)

Terrain generation is relevant if the engine expands to outdoor/landscape scenes. Compute shader
noise generation is useful regardless (texture generation, detail noise for materials). Vegetation
placement via compute shader is a natural evolution of Phase 5G's foliage system.

---

## 13. Animation Systems

### What They Are

Skeletal animation with GPU skinning transforms mesh vertices based on a bone hierarchy. Modern
approaches move the entire animation pipeline (sampling, blending, hierarchy evaluation, skinning)
to the GPU.

### Current Status: PRODUCTION-READY

GPU skinning has been standard practice for over a decade. Newer GPU-driven approaches push
further.

### Architecture

**Traditional GPU Skinning:**
1. CPU evaluates animation (sample keyframes, blend, traverse bone hierarchy)
2. CPU uploads final bone matrices to UBO/SSBO
3. Vertex shader transforms vertices by bone weights

**Modern GPU-Driven Animation:**
1. Compute shader samples animation keyframes from SSBO
2. Compute shader performs blend tree evaluation (interpolate between animations)
3. Compute shader traverses bone hierarchy (parallel prefix scan for parent chain)
4. Compute shader writes final bone matrices
5. Vertex shader performs skinning (or compute shader writes final vertex positions)
6. Benefit: Thousands of animated characters with minimal CPU involvement

### Key Considerations

- **glTF format** is recommended for skeletal animation data (well-supported, standardized)
- **Assimp** library handles model loading including bone hierarchies
- **Blend trees** require careful GPU implementation of the hierarchy scan stage
- A 2025 paper describes a complete GPU-based solution for large-scale skeletal animation
  with arbitrary skeleton complexity

### Implementation Complexity: MEDIUM (traditional) to HIGH (GPU-driven)

- Traditional approach: Assimp for loading, CPU animation eval, vertex shader skinning
- GPU-driven approach: Multiple compute shader passes, careful synchronization

### Key Resources

- [LearnOpenGL: Skeletal Animation](https://learnopengl.com/Guest-Articles/2020/Skeletal-Animation)
- [OGLdev: Skeletal Animation with Assimp](https://ogldev.org/www/tutorial38/tutorial38.html)
- [GPU-Based Large-Scale Skeletal Animation (2025 paper)](https://arxiv.org/html/2505.06703v1)
- [glTF Skeletal Animation (lisyarus)](https://lisyarus.github.io/blog/posts/gltf-animation.html)
- [GPU Skinning of MD5 Models (3DGameProgramming)](https://www.3dgep.com/gpu-skinning-of-md5-models-in-opengl-and-cg/)

### Verdict: PURSUE (medium-term)

Skeletal animation is essential for characters and animated objects. Start with traditional CPU
animation + vertex shader skinning (simpler, well-documented). Upgrade to GPU-driven animation
later if character counts demand it. Use glTF format and Assimp for import.

---

## 14. Physics Integration

### What They Are

Rigid body dynamics, collision detection, and response for interactive objects, character
controllers, and environmental interactions.

### Current Status: PRODUCTION-READY (multiple mature options)

| Engine | License | Language | Key Users | Multithreaded | Maturity |
|--------|---------|----------|-----------|---------------|----------|
| **Jolt Physics** | MIT | C++17 | Horizon Forbidden West, Death Stranding 2 | Yes (designed for it) | High |
| **Bullet Physics** | zlib | C++ | Many indie/AAA titles | Yes | Very High |
| **PhysX** | BSD 3-Clause | C++ | Unreal Engine, Unity | Yes + GPU | Very High |
| **Havok** | Commercial | C++ | AAA titles | Yes | Very High |

### Recommendation: Jolt Physics

Jolt is the best fit for Vestige:

1. **Modern C++17** -- matches our codebase, uses STL, no RTTI, no exceptions
2. **MIT license** -- fully permissive, no restrictions
3. **CMake integration** -- `FetchContent` or `add_subdirectory`, clean build
4. **Designed for multithreading** -- modern job system architecture
5. **Active development** -- used in major shipping titles
6. **Lightweight** -- no external dependencies beyond STL
7. **Linux support** -- builds with GCC 9+ on Linux

Why not Bullet? Bullet is older, has a less clean API, and the maintainer has moved on. Bullet3
was never fully completed. Jolt is its spiritual successor with a modern design.

Why not PhysX? PhysX is more complex to integrate, historically NVIDIA-focused (though now
open-source), and overkill for architectural walkthroughs.

### Implementation Complexity: MEDIUM

- CMake integration is straightforward (HelloWorld example available)
- API is well-documented
- Rigid body physics for doors, objects, character controllers
- For architectural walkthroughs, only basic collision detection may be needed initially

### Key Resources

- [Jolt Physics GitHub](https://github.com/jrouwe/JoltPhysics)
- [Jolt HelloWorld CMake Example](https://github.com/jrouwe/JoltPhysicsHelloWorld)
- [Jolt Documentation](https://jrouwe.github.io/JoltPhysics/)
- [Getting Started with Jolt (Wedekind)](https://www.wedesoft.de/simulation/2024/09/26/jolt-physics-engine/)
- [Jolt Physics on Conan](https://conan.io/center/recipes/joltphysics)

### Verdict: PURSUE with Jolt (medium-term)

Physics is not immediately critical for first-person walkthroughs (basic collision can use
simple AABB/OBB tests), but Jolt integration is clean and opens the door to interactive objects,
doors, destructibles, and eventually game mechanics. Integrate Jolt when interactive elements
are needed.

---

## 15. Audio Integration

### What They Are

3D spatial audio systems that position sounds in 3D space relative to the listener, with
distance attenuation, Doppler effects, and environmental reverb.

### Current Status: PRODUCTION-READY (multiple options)

| Library | License | 3D Audio | Complexity | Linux | Best For |
|---------|---------|----------|------------|-------|----------|
| **OpenAL Soft** | LGPL | Yes | LOW | Excellent | Indie/custom engines |
| **SoLoud** | zlib/public domain | Yes | VERY LOW | Yes | Simple integration |
| **FMOD** | Commercial (free < $200k) | Yes | MEDIUM | Yes | Pro features |
| **Wwise** | Commercial (free < $150k) | Yes | HIGH | Yes | AAA projects |

### Recommendation: OpenAL Soft (primary) or SoLoud (simpler alternative)

**OpenAL Soft** is the recommended choice:
- LGPL license (dynamic linking keeps your code proprietary if desired)
- Excellent Linux support (often pre-installed on Linux distros)
- Well-established API with extensive documentation
- True 3D positional audio with HRTF support
- Environmental effects (reverb, echo) via EFX extension

**SoLoud** as alternative:
- Public domain / zlib license (most permissive)
- Dead simple API: `soloud.play3d(sound, x, y, z)`
- No external dependencies
- Supports 3D audio with custom attenuation
- Virtual voices (mixes only the most audible sounds)
- Multiple simultaneous sound sources, gapless looping
- Less feature-rich than OpenAL but sufficient for many games

### Implementation Complexity: LOW

- Both libraries are header/source inclusion or simple library linking
- Basic 3D audio: set listener position/orientation, play sounds at 3D positions
- Environmental effects require more setup but are well-documented

### Key Resources

- [OpenAL Soft (official)](https://openal-soft.org/)
- [SoLoud (official)](https://solhsa.com/soloud/)
- [SoLoud GitHub](https://github.com/jarikomppa/soloud)
- [Game Audio Backends Guide](https://www.sonorousarts.com/blog/audio-backends-game-development/)
- [GameDev.net: Audio Library Recommendations](https://www.gamedev.net/forums/topic/707815-what-sound-library-do-you-all-recommend-for-a-c-custom-engine/)

### Verdict: PURSUE (medium-term)

Audio adds immense atmosphere to architectural walkthroughs -- footsteps echoing in the Temple,
ambient wind, crackling torches. Start with SoLoud for simplicity (fire-and-forget API), or
OpenAL Soft if environmental reverb/HRTF is important from the start. Integration is
straightforward either way.

---

## Priority Summary

### Short-Term (next 1-3 phases)

| # | Feature | Complexity | Impact | Why Now |
|---|---------|------------|--------|---------|
| 2 | **Bindless Textures** | Medium | High | Prerequisite for GPU-driven rendering; mature on our HW |
| 7 | **FSR 1.0 Upscaling** | Low | Medium | Trivial to add, immediate perf headroom |

### Medium-Term (3-6 phases out)

| # | Feature | Complexity | Impact | Why Then |
|---|---------|------------|--------|----------|
| 4 | **GPU-Driven Rendering** | High | Very High | Biggest perf architectural win; needs bindless first |
| 1 | **Mesh Shaders** | High | High | Mesa 26.0 support just landed; enables cluster rendering |
| 8 | **Volumetric Lighting** | Medium-High | High | Huge atmospheric impact for interiors |
| 10 | **SSR (Hi-Z + Temporal)** | Medium-High | Medium | Natural fit for deferred pipeline |
| 11 | **Atmospheric Scattering** | Medium | Medium | Needed for outdoor scenes, time of day |
| 13 | **Animation Systems** | Medium | High | Required for characters/animated objects |
| 14 | **Physics (Jolt)** | Medium | Medium | Needed for interactivity |
| 15 | **Audio (OpenAL/SoLoud)** | Low | Medium | Adds atmosphere with minimal effort |
| 9 | **GI (Baked Probes)** | Medium | High | Critical for realistic interiors |

### Long-Term (6+ phases out)

| # | Feature | Complexity | Impact | Why Later |
|---|---------|------------|--------|-----------|
| 9 | **GI (VCT/DDGI)** | Very High | Very High | Dynamic GI after baked probes are working |
| 5 | **Nanite-Like LOD** | Very High | High | Needs mesh shaders + GPU-driven rendering first |
| 7 | **FSR 2.0 Temporal** | Medium-High | High | After motion vectors are in the pipeline |
| 12 | **Procedural Generation** | Medium | Medium | When outdoor scenes are needed |
| 6 | **Ray Tracing (Compute)** | High | Medium | Limited value in OpenGL; better via Vulkan |
| 3 | **Sparse Textures** | Very High | Low-Medium | Only if massive texture datasets needed |

### Recommended Implementation Order

```
Phase N:   Bindless Textures + FSR 1.0
Phase N+1: GPU-Driven Rendering (MDI + Compute Culling)
Phase N+2: Baked Light Probes (GI) + Audio Integration
Phase N+3: Mesh Shaders + Volumetric Lighting
Phase N+4: SSR (Hi-Z) + Atmospheric Scattering
Phase N+5: Animation System + Physics (Jolt)
Phase N+6: FSR 2.0 + Voxel Cone Tracing
Phase N+7: Nanite-Like LOD + Procedural Generation
```

---

## Hardware-Specific Notes (AMD RX 6600 / RDNA2 / Mesa RadeonSI)

### Supported Extensions Summary

| Extension | Mesa Version | Status |
|-----------|-------------|--------|
| `GL_ARB_bindless_texture` | 18.x+ | Stable |
| `GL_ARB_sparse_texture` | 22.0+ | Stable |
| `GL_EXT_mesh_shader` | 26.0+ | New (Feb 2026) |
| `GL_ARB_compute_shader` | Core 4.3 | Stable |
| `GL_ARB_shader_storage_buffer_object` | Core 4.3 | Stable |
| `GL_ARB_multi_draw_indirect` | Core 4.3 | Stable |
| `GL_ARB_gpu_shader_int64` | Supported | Stable |

### RDNA2-Specific Considerations

- **No hardware ray tracing in OpenGL:** The RX 6600's RT accelerators are only accessible via
  Vulkan (`VK_KHR_ray_tracing_pipeline`). Compute shader RT is the only OpenGL option.
- **Geometry shader performance:** Still emulated on AMD hardware; prefer mesh shaders or
  compute+MDI over geometry shader-based approaches.
- **Mesh shaders:** GFX10.3 (RDNA2) is the minimum supported generation for `GL_EXT_mesh_shader`
  in Mesa 26.0. The RX 6600 qualifies.
- **Compute shader performance:** RDNA2 has excellent compute performance with 32 CUs and
  good occupancy. Compute-heavy techniques (volumetric fog, GPU culling, noise generation)
  run well.
- **Shared memory:** 64 KB per workgroup on RDNA2 -- useful for compute shader optimizations.
- **Wave size:** 32 or 64 threads per wave on RDNA2 (wave32 is default and generally preferred
  for compute).

---

*Research compiled from web searches across Phoronix, AMD GPUOpen, Khronos, GitHub, academic
papers, and engine developer blogs. See inline links for all sources.*
