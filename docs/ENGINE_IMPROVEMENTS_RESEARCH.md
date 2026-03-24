# Vestige Engine Improvements Research

Research into best practices, optimizations, and experimental features for the Vestige 3D engine (C++17, OpenGL 4.5, AMD RDNA2 / RX 6600, Linux/Mesa).

---

## Table of Contents

1. [OpenGL 4.5 Best Practices for AMD RDNA2 / Mesa](#1-opengl-45-best-practices-for-amd-rdna2--mesa)
2. [Terrain Rendering Improvements](#2-terrain-rendering-improvements)
3. [Modern C++17/20 Patterns for Game Engines](#3-modern-c1720-patterns-for-game-engines)
4. [Experimental OpenGL Extensions on RDNA2](#4-experimental-opengl-extensions-on-rdna2)
5. [Performance Profiling Tools](#5-performance-profiling-tools)
6. [Shadow Map Improvements](#6-shadow-map-improvements)
7. [Modern Anti-Aliasing Beyond TAA](#7-modern-anti-aliasing-beyond-taa)
8. [GPU-Driven Rendering Techniques](#8-gpu-driven-rendering-techniques)

---

## 1. OpenGL 4.5 Best Practices for AMD RDNA2 / Mesa

### RadeonSI Driver Architecture

The RadeonSI driver is Mesa's Gallium3D-based OpenGL implementation for AMD GPUs from GCN through RDNA3 (GFX6-GFX12). It translates OpenGL calls through the Gallium abstraction layer and provides full OpenGL 4.6 support on RDNA2.

**Key architectural details:**

- **Shader compilation strategy:** RadeonSI splits shaders into three parts -- main body, prolog (state-dependent prefix: vertex buffer loads, instance divisors), and epilog (state-dependent suffix: color format conversion, alpha test). Prologs and epilogs are cached globally, so state changes between draws do not require full recompilation.
- **NIR pipeline:** All shader sources (GLSL, SPIR-V, TGSI) convert to NIR before backend compilation. Optimization stages include algebraic simplification, constant folding, dead code elimination, and driver-specific lowering for NGG (Next Generation Geometry).
- **Wave size:** GFX10+ supports both Wave32 (32 threads) and Wave64 (64 threads). Fragment shaders on GFX11+ prefer Wave64 for better VALU utilization. Compute workgroups not divisible by 64 benefit from Wave32.
- **ACO backend compiler:** Mesa uses ACO (AMD Compiler) by default instead of LLVM for faster compilation and competitive runtime performance.

### GLThread (Threaded OpenGL Dispatch)

Since Mesa 22.3, RadeonSI enables `glthread` by default. This offloads OpenGL command marshalling to a separate thread, yielding ~30% gains in CPU-bound workloads (e.g., Minecraft). The engine benefits automatically -- no opt-in needed.

**What it means for Vestige:** CPU-side rendering command submission is already threaded by the driver. Focus optimization effort on reducing draw call count and state changes rather than micro-optimizing command submission.

### Driver-Specific Pitfalls and Tips

| Pitfall | Details | Mitigation |
|---------|---------|------------|
| Geometry shader emulation | GS is emulated on RDNA2 and can be 2-5x slower than instanced draws | Use instanced draws or compute + indirect instead of GS |
| All declared samplers must be bound | Mesa AMD requires ALL declared GLSL samplers to have valid textures bound at draw time, even if code path is unused | Always bind dummy textures to unused sampler slots |
| Shader variant explosion | Excessive dynamic state changes cause RadeonSI to generate many prolog/epilog variants | Group draws by matching state; minimize blend mode / format changes between draws |
| VGPR pressure | Excessive register usage reduces wave occupancy | Keep fragment shaders lean; inspect with `AMD_DEBUG=asm` |
| LDS contention | Tessellation and compute shaders compete for on-chip Local Data Store | Budget LDS carefully when mixing tessellation + compute in same frame |
| Monolithic shader fallback | Very complex shaders may force monolithic compilation (slower) | Keep shader complexity manageable; use specialization |

### Debug Environment Variables

```bash
AMD_DEBUG=asm myapp      # View generated GPU assembly
AMD_DEBUG=nir myapp      # Inspect NIR intermediate representation
AMD_DEBUG=mono myapp     # Force monolithic shaders (debugging)
AMD_DEBUG=w32ge myapp    # Force Wave32 for geometry stages
AMD_DEBUG=w64ge myapp    # Force Wave64 for geometry stages
```

These are invaluable for diagnosing shader compilation issues and register pressure.

### Practical Optimization Checklist

1. **Batch draws by state.** Group objects sharing the same shader, blend mode, and textures. This minimizes RadeonSI's dirty-flag processing.
2. **Use Direct State Access (DSA).** OpenGL 4.5 DSA functions (`glNamedBufferStorage`, `glTextureStorage2D`, etc.) reduce driver overhead vs. bind-to-edit.
3. **Prefer immutable storage.** `glBufferStorage` / `glTextureStorage2D` enable driver optimizations that `glBufferData` / `glTexImage2D` cannot.
4. **Use persistent mapped buffers.** `GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT` for frequently updated data (uniforms, instance data) avoids implicit synchronization.
5. **Avoid CPU-GPU sync points.** Never call `glGetTexImage`, `glReadPixels` synchronously. Use PBOs or fences.
6. **Minimize shader program switches.** On RadeonSI, switching programs triggers state re-validation. Sort draw calls by program.
7. **Use UBOs for small frequently-changing data.** RadeonSI optimizes small UBO updates; use SSBOs only for large/variable-sized data.

**Applicability:** Directly applicable to Vestige today.
**Difficulty:** Low -- these are best-practice changes, not architectural.

### Sources
- [RadeonSI Driver Architecture (DeepWiki)](https://deepwiki.com/bminor/mesa-mesa/3.1-radeonsi-amd-opengl-driver)
- [Mesa 22.3 RadeonSI GLThread (Phoronix)](https://www.phoronix.com/news/Mesa-22.3-RadeonSI-glthread-On)
- [Mesa 3D Graphics Library](https://mesa3d.org/)
- [Mesamatrix Extension Support](https://mesamatrix.net/)

---

## 2. Terrain Rendering Improvements

Vestige currently uses CDLOD terrain with quadtree LOD, heightmap sculpting, and splatmap painting. Here are the next-level improvements.

### 2A. Virtual Texturing (Sparse Virtual Textures / SVT)

**What it is:** Also called "megatexture" (coined by John Carmack), SVT tiles a massive virtual texture and streams only visible tiles into GPU memory. Instead of loading entire terrain texture sets, only tiles visible at the current LOD are resident.

**How it works:**
1. Scene renders a "feedback pass" with a special shader that outputs which virtual texture tiles are needed at which mip level.
2. CPU reads the feedback buffer and schedules tile loading from disk.
3. Tiles are uploaded to a physical texture atlas (cache) via `glTexSubImage2D`.
4. An indirection texture (page table) maps virtual tile coordinates to physical atlas locations.
5. Main render samples through the indirection texture to find the physical tile.

**Why it matters for Vestige:**
- Terrain splatmaps can grow very large with multiple material layers. SVT lets you composite layers offline into a single virtual texture, reducing per-pixel sampling from N material layers to 1 cached lookup.
- Enables "infinite" terrain detail without proportional VRAM usage.

**OpenGL support:** `GL_ARB_sparse_texture` is supported on RadeonSI for GFX9 (Vega) and newer, confirmed for RDNA2. The extension separates GPU address space reservation from physical backing, allowing on-demand tile commitment.

**Applicability:** High -- directly benefits the terrain system. Can be layered on top of existing CDLOD.
**Difficulty:** High -- requires feedback pass, tile cache manager, indirection texture, and async streaming pipeline. 3-4 weeks of focused work.

### 2B. GPU-Driven Terrain LOD with Compute Shaders

**What it is:** Move the CDLOD quadtree traversal from CPU to GPU using compute shaders. The compute shader evaluates LOD criteria (distance, screen-space error) and directly writes draw commands to an indirect buffer.

**How it works:**
1. Compute shader reads heightmap and camera position.
2. For each quadtree node, compute LOD level based on distance and error metric.
3. Write visible nodes as `DrawElementsIndirectCommand` structs into an SSBO.
4. Render with `glMultiDrawElementsIndirect` -- zero CPU involvement in LOD selection.

**Why it matters:** Eliminates CPU-side quadtree traversal entirely. The GPU can process thousands of nodes in parallel. Particularly beneficial for large terrains where CPU traversal becomes the bottleneck.

**Applicability:** Medium-high -- existing CDLOD could be migrated incrementally.
**Difficulty:** Medium -- requires compute shader development and restructuring the terrain renderer's data flow.

### 2C. Tessellation-Based Terrain

**What it is:** Instead of pre-built mesh LODs, render terrain as coarse patches and use hardware tessellation to subdivide based on distance. The tessellation evaluation shader samples the heightmap for displacement.

**How it works:**
- Tessellation Control Shader (TCS) calculates per-edge tessellation levels based on screen-space edge length.
- Tessellation Evaluation Shader (TES) samples heightmap and displaces vertices.
- "Auto LOD" mode maintains constant screen-space triangle size.

**Pros:**
- Continuous LOD with no popping or stitching artifacts.
- Very low CPU overhead -- just submit patches.
- Hardware tessellation units handle subdivision efficiently.

**Cons:**
- OpenGL tessellation is limited to 64 subdivision levels per patch.
- On AMD RDNA2, tessellation performance is good but not as optimized as on NVIDIA.
- More complex to integrate with the existing CDLOD quadtree.

**Applicability:** Medium -- could replace or supplement CDLOD for close-range detail.
**Difficulty:** Medium -- TCS/TES shader development, integrating with existing heightmap system.

### 2D. GPU Quadtree with Linear Trees

Research from Eurographics shows quadtrees can be implemented entirely on the GPU using "linear trees" -- a pointer-free representation where nodes are encoded as bit codes. This enables GPU-side node splitting/merging, frustum culling, and LOD selection without CPU involvement.

**Applicability:** Medium -- advanced technique that pairs well with compute-driven terrain.
**Difficulty:** High -- novel data structure requiring careful implementation.

### Sources
- [CDLOD Reference Implementation (GitHub)](https://github.com/fstrugar/CDLOD)
- [Sparse Virtual Texturing Notes (Holger Dammertz)](https://holger.dammertz.org/stuff/notes_VirtualTexturing.html)
- [Sparse Virtual Textures (Toni Sagrista)](https://tonisagrista.com/blog/2023/sparse-virtual-textures/)
- [Sparse Textures OpenGL (AMD GPUOpen)](https://gpuopen.com/learn/sparse-textures-opengl/)
- [GPU Terrain Rendering with Clipmaps (ARM SDK)](https://arm-software.github.io/opengl-es-sdk-for-android/terrain.html)
- [NVIDIA Terrain Tessellation Sample](https://docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/terraintessellationsample.htm)
- [Tessellated Terrain with Dynamic LOD (Victor Bush)](https://victorbush.com/2015/01/tessellated-terrain/)
- [Quadtrees on the GPU (ResearchGate)](https://www.researchgate.net/publication/331761994_Quadtrees_on_the_GPU)

---

## 3. Modern C++17/20 Patterns for Game Engines

### 3A. Polymorphic Memory Resources (PMR)

**What it is:** C++17's `<memory_resource>` header provides pluggable allocators without infecting every template parameter.

**Key types:**
- `std::pmr::monotonic_buffer_resource` -- bump allocator that never deallocates until destruction. Fastest possible allocation. Ideal for per-frame scratch data.
- `std::pmr::pool_resource` -- manages fixed-size chunk pools. Good for particle systems, component storage.
- `std::pmr::unsynchronized_pool_resource` -- single-threaded variant, no locking overhead.

**How to use in Vestige:**
```cpp
// Per-frame scratch allocator (reset each frame)
alignas(64) char frameBuffer[1024 * 1024]; // 1MB stack buffer
std::pmr::monotonic_buffer_resource frameResource(frameBuffer, sizeof(frameBuffer));
std::pmr::vector<DrawCommand> drawCommands(&frameResource);
// Allocation is pointer-bump -- extremely fast
// Entire buffer freed at frame end when frameResource goes out of scope
```

**Why it matters:** Eliminates heap fragmentation for per-frame allocations. The monotonic buffer is faster than `malloc` by an order of magnitude because it just bumps a pointer.

**Applicability:** High -- per-frame command lists, temporary containers, particle spawn lists.
**Difficulty:** Low -- PMR is standard C++17, drop-in replacement for `std::vector<T>` with `std::pmr::vector<T>`.

### 3B. Job System with Work Stealing

**What it is:** A thread pool where each worker thread has a private deque. Workers push/pop jobs from their own deque (LIFO for cache locality) and steal from other workers' deques (FIFO for load balancing) when idle.

**Architecture:**
1. Create N-1 worker threads for N CPU cores (main thread is also a worker).
2. Each worker has a lock-free work-stealing deque.
3. Jobs are small structs with a function pointer and embedded data (no heap allocation per job).
4. Parent-child dependencies tracked via atomic `unfinishedJobs` counter.
5. `Wait(job)` spins and steals work while waiting -- never blocks.

**Performance reference:** On a 6-core/12-thread CPU (matching Vestige's Ryzen 5 5600), 65,000 jobs complete in ~5ms with work stealing vs. ~18ms single-threaded.

**How to use in Vestige:**
- Terrain quadtree traversal as parallel jobs
- Frustum culling across multiple object groups
- Physics/collision on worker threads
- Async resource loading

**Applicability:** High -- the Ryzen 5 5600 has 12 threads that are largely unused today.
**Difficulty:** Medium -- lock-free deque requires careful implementation. Consider using an existing library (FiberTaskingLib, enkiTS).

### 3C. Entity Component System (ECS)

**What it is:** An architectural pattern separating entities (IDs), components (data), and systems (behavior). Two main storage strategies:

| Strategy | Iteration | Modification | Used By |
|----------|-----------|--------------|---------|
| Sparse Set (EnTT) | O(n) per component type; cache-miss if iterating multiple types | Very cheap (array swap) | EnTT, Minecraft (Bedrock) |
| Archetype (flecs, gaia-ecs) | Excellent cache coherence for multi-component queries | Expensive (entity moves between tables) | Unity DOTS, flecs, gaia-ecs |

**EnTT** is the most popular C++17 ECS, header-only, used by Minecraft Bedrock Edition. Sparse-set storage is fast for < 100K entities and when entity composition changes frequently.

**Archetype ECS (gaia-ecs, flecs)** stores entities with identical component sets contiguously. Excels at iterating millions of entities with consistent archetypes. Chunks are sized (8-16 KiB) to fit L1 cache.

**Recommendation for Vestige:** EnTT is the pragmatic choice. The engine's entity count is moderate (architectural walkthrough, not an MMO), and the sparse-set model's cheap modification aligns well with editor workflows where components are constantly added/removed during scene editing.

**Applicability:** Medium -- significant architectural change to adopt fully, but can be introduced incrementally for new subsystems.
**Difficulty:** Medium for incremental adoption (new systems only), High for full migration.

### 3D. C++17 Features Worth Adopting

| Feature | Use Case in Vestige |
|---------|-------------------|
| `std::variant` | Type-safe shader uniform values, event payloads |
| `std::optional` | Nullable resource handles, query results |
| `if constexpr` | Compile-time branching in template code (renderer backends) |
| Structured bindings | Cleaner iteration over maps, multi-return values |
| `std::string_view` | Zero-copy string parameters for asset paths, shader source |
| Fold expressions | Variadic event dispatch, component registration |
| `std::filesystem` | Cross-platform asset path handling (replace platform ifdefs) |

**Applicability:** High -- these are incremental improvements with zero risk.
**Difficulty:** Low.

### Sources
- [Polymorphic Allocators in C++17 (ModernEscpp)](https://www.modernescpp.com/index.php/polymorphic-allocators-in-c17/)
- [Optimization with Allocators in C++17 (ModernEscpp)](https://www.modernescpp.com/index.php/optimization-with-allocators-in-c17/)
- [std::pmr::monotonic_buffer_resource (cppreference)](https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource.html)
- [Job System 2.0: Lock-Free Work Stealing (Molecular Matters)](https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/)
- [FiberTaskingLib (GitHub)](https://github.com/RichieSams/FiberTaskingLib)
- [EnTT: Gaming meets modern C++ (GitHub)](https://github.com/skypjack/entt)
- [gaia-ecs (GitHub)](https://github.com/richardbiely/gaia-ecs)
- [ECS Back and Forth Part 9: Sparse Sets and EnTT](https://skypjack.github.io/2020-08-02-ecs-baf-part-9/)
- [Sparse-set vs Archetype ECS Performance (Eurographics)](https://diglib.eg.org/bitstreams/766b72a4-70ae-4e8e-935b-949d589ed962/download)

---

## 4. Experimental OpenGL Extensions on RDNA2

### 4A. Bindless Textures (GL_ARB_bindless_texture)

**Status on Mesa RadeonSI:** Supported since Mesa ~18.x. Confirmed working on RDNA2.

**What it is:** Instead of binding textures to numbered slots (max ~16-32 per stage), get a 64-bit integer handle for each texture and pass handles via SSBOs/UBOs. Shaders index into an array of `sampler2D` handles.

**API workflow:**
```cpp
GLuint64 handle = glGetTextureHandleARB(texture);
glMakeTextureHandleResidentARB(handle);
// Store handle in SSBO, pass to shader
// Shader: layout(std430) readonly buffer { sampler2D textures[]; };
```

**Critical constraints:**
- Once a handle is created, NO texture state can be changed (format, filtering, etc.). Complete all configuration first.
- Handles must be made resident before use and non-resident when no longer needed.
- On AMD, use `#extension GL_EXT_nonuniform_qualifier : require` if indexing with non-uniform expressions.
- Never made it to core OpenGL -- remains an extension. Requires `#extension GL_ARB_bindless_texture : require` in shaders.
- **RenderDoc does not support bindless textures.** Must use NVIDIA NSight or AMD's tools for debugging.

**Performance benefit:** Eliminates texture binding overhead between draw calls. Combined with MDI, enables rendering entire scenes with zero texture rebinds.

**Use in Vestige:**
- Material system: store all material textures (albedo, normal, roughness, etc.) as bindless handles in a single SSBO.
- Terrain splatmap: pass all terrain layer textures as a bindless array instead of binding per-layer.
- Foliage: all grass/tree texture variants accessible without rebinding.

**Applicability:** High -- directly reduces draw call overhead.
**Difficulty:** Medium -- requires restructuring texture management and material system. The RenderDoc limitation complicates debugging.

### 4B. Sparse Textures (GL_ARB_sparse_texture)

**Status on Mesa RadeonSI:** Supported for GFX9 (Vega) and newer. RDNA2 (GFX10.3) is supported.

**What it is:** Partially resident textures where only needed tiles are physically backed in VRAM. The GPU address space can be much larger than physical VRAM.

**Key concepts:**
- `glTexPageCommitmentARB()` commits/decommits individual tiles (pages).
- Reading an uncommitted tile returns undefined data (or zero with `GL_ARB_sparse_texture2`).
- Tile sizes are hardware-dependent (query with `glGetInternalformativ(GL_TEXTURE_2D, format, GL_VIRTUAL_PAGE_SIZE_X_ARB, ...)`).
- Works with `GL_ARB_sparse_texture_clamp` to control mip level selection.

**Use in Vestige:**
- Foundation for virtual texturing (Section 2A above).
- Huge terrain heightmaps that only load tiles near the camera.
- Lightmap atlases where only visible sections are resident.

**Applicability:** Medium-high -- most valuable when combined with a full SVT pipeline.
**Difficulty:** High -- sparse texture alone is simple API-wise, but building a useful streaming system around it is complex.

### 4C. Compute Shaders for Terrain

**Status:** Core in OpenGL 4.3, fully supported on RDNA2.

**Opportunities in Vestige:**
1. **GPU frustum culling** -- test terrain CDLOD nodes against frustum in compute, output to indirect draw buffer.
2. **Heightmap erosion** -- GPU-accelerated terrain sculpting (hydraulic/thermal erosion).
3. **Grass/foliage placement** -- compute shader scatters instances based on splatmap, performs per-instance culling.
4. **Terrain normal generation** -- compute normals from heightmap in parallel.
5. **LOD selection** -- compute shader evaluates screen-space error for each terrain patch and writes draw commands.

**Applicability:** Very high -- compute shaders are the enabler for most GPU-driven techniques.
**Difficulty:** Low-medium per feature (compute shader basics are straightforward).

### 4D. Persistent Mapped Buffers (GL_ARB_buffer_storage)

**Status:** Core in OpenGL 4.4, fully supported.

**What it is:** Map a buffer once with `GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT` and keep the pointer valid for the buffer's lifetime. Write from CPU; GPU reads without explicit unmap/remap.

**Benefits:**
- Eliminates `glMapBuffer`/`glUnmapBuffer` per frame.
- No implicit synchronization (must use fences for triple-buffering).
- Ideal for streaming vertex data (particles, foliage instances, UI).

**Applicability:** High -- particle system and foliage instancing would benefit immediately.
**Difficulty:** Low -- straightforward API change, just need proper fence synchronization.

### Sources
- [Bindless Textures Tutorial (J Stephano)](https://ktstephano.github.io/rendering/opengl/bindless)
- [Bindless Texture Wiki (Khronos)](https://www.khronos.org/opengl/wiki/Bindless_Texture)
- [Bindless Without Blowing Your Legs Off (GitHub Gist)](https://gist.github.com/JuanDiegoMontoya/55482fc04d70e83729bb9528ecdc1c61)
- [ARB_bindless_texture for RadeonSI (Mesa-dev)](https://lists.freedesktop.org/archives/mesa-dev/2017-May/156260.html)
- [RadeonSI Sparse Texture Support (Phoronix)](https://www.phoronix.com/news/RadeonSI-Sparse-Textures)
- [Sparse Textures OpenGL (AMD GPUOpen)](https://gpuopen.com/learn/sparse-textures-opengl/)
- [ARB_sparse_texture2 (Khronos Registry)](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sparse_texture2.txt)
- [Sparse Virtual Texturing Implementation (GitHub)](https://github.com/DudleyHK/SparseVirtualTexturing)

---

## 5. Performance Profiling Tools

### 5A. RenderDoc (Primary Tool)

**What it is:** Free, MIT-licensed graphics debugger. Captures single frames and allows full introspection of the GPU pipeline.

**OpenGL support:** Core profile 3.2-4.6 on Linux. Shader debugging supported for vertex, pixel, and compute shaders (requires ARB_compute_shader and ARB_shader_storage_buffer_object -- both present on RDNA2).

**Shader debugging caveat:** "Not every shader can be debugged." Shaders that compile cleanly to SPIR-V debug reliably. Complex shaders with vendor-specific features may not.

**Key tips for Vestige:**
- **Ctrl+G** in texture viewer: jump to specific pixel coordinates.
- Right-click channel buttons (R/G/B/A) to isolate channels -- useful for inspecting normal maps, splatmaps.
- Enable "API validation" during capture for debug/error messages.
- Use the Mesh Viewer to visualize terrain geometry and verify LOD transitions.
- Bookmark interesting draw calls for comparison across captures.
- **Limitation:** RenderDoc does NOT support bindless textures. If adopting bindless, must use other tools for texture debugging.

### 5B. AMD Radeon GPU Profiler (RGP)

**What it is:** AMD's dedicated GPU profiling tool. Profiles at the hardware level -- wavefront occupancy, cache hit rates, memory bandwidth.

**RenderDoc integration:** RGP profiles can be generated directly from RenderDoc captures. Events correlate across both tools.

**Key capabilities:**
- Per-draw-call GPU timing at hardware level.
- Wavefront occupancy visualization (are your shaders under-utilizing the GPU?).
- Cache hierarchy analysis (L0/L1/L2 hit rates).
- Instruction timing breakdown.

**Applicability:** Essential for diagnosing GPU-bound scenarios. Complements RenderDoc's API-level view.
**Difficulty:** Low -- install and use alongside RenderDoc.

### 5C. Radeon GPU Analyzer (RGA)

**What it is:** Offline shader compiler and performance analyzer. Compiles shaders to GPU ISA without running the application.

**Use case:** Before committing a shader change, compile it with RGA to check VGPR count, occupancy, and instruction count. Catches performance regressions at authoring time.

**Applicability:** Medium -- useful for shader-heavy development phases.
**Difficulty:** Low.

### 5D. AMD_DEBUG Environment Variables

As documented in Section 1, these are the fastest way to inspect shader compilation:

```bash
AMD_DEBUG=asm    # Final GPU assembly
AMD_DEBUG=nir    # NIR IR (intermediate representation)
AMD_DEBUG=preoptir  # IR before optimization
```

**Applicability:** High -- zero setup, immediately actionable.
**Difficulty:** Trivial.

### 5E. Built-in GPU Timer Queries

Vestige already has CPU + GPU timer queries in its profiler. Additional tips:
- Use `GL_TIME_ELAPSED` queries around individual passes (shadow, terrain, water, particles) for per-pass GPU timing.
- Use `GL_TIMESTAMP` queries at frame boundaries for frame-level timing.
- Triple-buffer query objects to avoid pipeline stalls from reading results too early.

### Sources
- [RenderDoc](https://renderdoc.org/)
- [RenderDoc OpenGL Support](https://renderdoc.org/docs/behind_scenes/opengl_support.html)
- [RenderDoc Tips & Tricks](https://renderdoc.org/docs/getting_started/tips_tricks.html)
- [AMD Radeon GPU Profiler (GPUOpen)](https://gpuopen.com/rgp/)
- [RenderDoc + RGP Interop (GPUOpen)](https://gpuopen.com/manuals/rgp_manual/renderdoc_and_rgp_interop/)
- [Debugging Tools (OpenGL Wiki)](https://www.khronos.org/opengl/wiki/Debugging_Tools)

---

## 6. Shadow Map Improvements

Vestige currently uses Cascaded Shadow Maps (CSM). Here are improvements ranked by impact/difficulty.

### 6A. PCSS (Percentage-Closer Soft Shadows)

**What it is:** Extends PCF (Percentage-Closer Filtering) with variable-kernel filtering based on blocker distance. Produces contact-hardening shadows -- sharp near contact points, soft further away.

**Three-step algorithm:**
1. **Blocker search:** Sample the shadow map in a fixed radius around the fragment. Average depths of samples closer to the light (blockers) using Poisson disk or Vogel disk sampling.
2. **Penumbra estimation:** `penumbraWidth = (receiverDepth - avgBlockerDepth) / avgBlockerDepth * lightSize`
3. **PCF filtering:** Standard PCF with kernel size proportional to penumbra width.

**Optimized variant (Wojciech Sterna, 2018):**
- Use **Vogel disk** sampling (Fibonacci spiral) instead of Poisson -- cheaper to compute at runtime, excellent distribution when combined with per-pixel rotation.
- **Penumbra mask** technique: separate penumbra estimation (low resolution) from shadow mask rendering. This decouples the two expensive passes and allows running the blocker search at reduced resolution.
- Performance approaches regular soft shadows with 16-32 samples.

**Integration with CSM:** Apply PCSS per cascade. Larger cascades use larger light sizes for physically plausible penumbra scaling.

**Applicability:** Very high -- most impactful visual upgrade for shadows.
**Difficulty:** Medium -- shader-only change on top of existing CSM. Blocker search adds ~0.5-1ms.

### 6B. Variance Shadow Maps (VSM)

**What it is:** Store mean and squared-mean of depth in the shadow map. Use Chebyshev's inequality to estimate shadow probability. Enables hardware texture filtering (bilinear, mipmaps, anisotropic) on shadow maps.

**Pros:**
- Filterable with standard GPU texture hardware -- mipmaps, anisotropic filtering, MSAA.
- Blur passes work correctly (unlike PCF which requires per-sample comparison).
- Fewer biasing complications than PCF.

**Cons:**
- **Light bleeding:** When large depth ranges exist between occluders and receivers, VSM produces light leaks. This is the primary practical problem.
- Requires 2 FP16/FP32 values per texel (2x memory of standard shadow maps).

**Applicability:** Medium -- good for specific scenarios but light bleeding limits general use.
**Difficulty:** Low-medium -- straightforward shader change + gaussian blur pass.

### 6C. Exponential Variance Shadow Maps (EVSM)

**What it is:** Applies an exponential warp to depth values before storing, which significantly reduces light bleeding compared to VSM.

**Two variants:**
- **EVSM2:** Stores positive exponential terms only (2 channels).
- **EVSM4:** Stores positive + negative terms (4 channels). Best quality.

**Performance:** EVSM4 with 2048x2048 cascades, 4xMSAA, mipmaps, 8xAF adds ~11.5ms vs ~3ms for 7x7 fixed PCF. Expensive but high quality.

**Biasing:** 32-bit EVSM needs bias ~0.01. 16-bit EVSM needs bias ~0.5 (precision issues).

**Applicability:** Medium -- high quality but expensive. Best used selectively (hero lights, sun).
**Difficulty:** Medium -- builds on VSM with additional complexity.

### 6D. Sample Distribution Shadow Maps (SDSM)

**What it is:** Analyze the depth buffer to tightly fit cascade splits to the actual depth range of visible pixels, rather than using fixed/logarithmic splits.

**How it works:**
1. Read min/max depth from the depth buffer (via compute shader reduction).
2. Clamp cascade near/far planes to actual visible depth range.
3. Optionally use logarithmic partitioning within the clamped range.

**Benefit:** "The min Z can give you huge improvements" -- eliminates wasted shadow map resolution on empty depth ranges.

**Caveat:** Requires depth buffer readback. Can be deferred 1 frame to avoid pipeline stall (minimal visual impact).

**Applicability:** High -- direct improvement to existing CSM with minimal changes.
**Difficulty:** Low -- compute shader for depth reduction + cascade fitting adjustment.

### 6E. Cascade Stabilization

**What it is:** Snap cascade projection matrices to texel-sized increments to prevent "shadow crawling" (temporal instability as camera moves).

**How:** Fit a sphere to the frustum split, project to that sphere, then quantize to texel grid.

**Applicability:** High if not already implemented.
**Difficulty:** Low -- math adjustment to cascade matrix calculation.

### Recommendation Priority

1. **SDSM** (Low effort, high impact on cascade utilization)
2. **Cascade stabilization** (Low effort, eliminates temporal artifacts)
3. **PCSS with Vogel disk** (Medium effort, best visual improvement)
4. **EVSM** or **VSM** only if PCSS filtering is insufficient for specific scenarios

### Sources
- [A Sampling of Shadow Techniques (MJP)](https://therealmjp.github.io/posts/shadow-maps/)
- [PCSS Original Paper (NVIDIA)](https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf)
- [PCSS OpenGL Implementation (GitHub)](https://github.com/pboechat/PCSS)
- [Contact-hardening Soft Shadows Made Fast (Wojciech Sterna)](https://www.gamedev.net/tutorials/programming/graphics/contact-hardening-soft-shadows-made-fast-r4906/)
- [Summed-Area Variance Shadow Maps (GPU Gems 3)](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps)
- [Advanced Soft Shadow Mapping (NVIDIA GDC08)](https://developer.download.nvidia.com/presentations/2008/GDC/GDC08_SoftShadowMapping.pdf)
- [LearnOpenGL Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)

---

## 7. Modern Anti-Aliasing Beyond TAA

Vestige currently uses TAA. Here are improvements and alternatives.

### 7A. Improving Existing TAA

The most impactful work is fixing TAA's known issues rather than replacing it.

**Variance Clipping (Marco Salvi):**
Instead of simple min/max neighborhood clamping, compute mean and standard deviation across a 3x3 neighborhood and build an AABB of expected color values:
```
mu = m1 / 9.0
sigma = sqrt(abs((m2 / 9.0) - (mu * mu)))
minc = mu - gamma * sigma
maxc = mu + gamma * sigma
history = clip_aabb(history, minc, maxc)
```
This is more stable than hard clamping and reduces both ghosting AND flickering.

**YCoCg color space:** Performing variance clipping in YCoCg space (luminance + chrominance) produces tighter bounds and fewer color artifacts. Some implementations report mixed results -- test on Vestige's specific content.

**Catmull-Rom history sampling:** Instead of bilinear sampling of the history buffer, use an optimized Catmull-Rom filter. This prevents blurring from reprojection (history samples rarely land on pixel centers).

**Luminance weighting:** Apply adaptive blend weights based on luminance:
```
sourceWeight = 1.0 / (1.0 + luminance(source))
historyWeight = 1.0 / (1.0 + luminance(history))
```
Reduces high-frequency flickering on bright/specular surfaces.

**Velocity buffer optimization:** Skip velocity writes for static objects. Instead, derive camera-only motion from depth buffer reprojection in a separate compute pass. Saves bandwidth.

**Compute shader conversion:** Move TAA resolve to a compute shader. Use groupshared memory for the 3x3 neighborhood sampling -- reduces texture fetches from 9 to 1 per pixel (neighbors share fetched data).

**Applicability:** Very high -- these are direct improvements to the existing TAA implementation.
**Difficulty:** Low-medium per improvement. Can be done incrementally.

### 7B. SMAA (Subpixel Morphological Anti-Aliasing)

**What it is:** A post-processing AA technique with three passes: edge detection, blending weight calculation, neighborhood blending. Handles geometric edges, diagonal lines, and subpixel features.

**Three-pass pipeline:**
1. **Edge detection:** Luma-based (catches visible edges) or depth-based (fastest, may miss some edges).
2. **Blending weight calculation:** Uses precomputed lookup textures (AreaTex, SearchTex) to determine blend factors.
3. **Neighborhood blending:** Applies the calculated weights to blend neighboring pixels.

**Variants:**
- **SMAA 1x:** Pure spatial, no temporal component. ~1ms at 1080p.
- **SMAA T2x:** Combines SMAA with temporal reprojection. Better quality, similar to TAA.
- **SMAA S2x:** Combines with MSAA (2x). Requires MSAA resolve.

**Comparison with TAA:**
| Aspect | TAA | SMAA 1x | SMAA T2x |
|--------|-----|---------|----------|
| Ghosting | Yes (fixable) | None | Mild |
| Blurring | Yes (fixable) | None | Minimal |
| Subpixel detail | Excellent | Good | Very good |
| Temporal stability | Excellent | Poor (shimmer) | Good |
| Performance | ~0.5ms | ~1ms | ~1.5ms |
| Thin geometry | Can lose detail | Preserves | Preserves |

**Recommendation:** TAA with the improvements from 7A is generally superior for Vestige's use case (architectural walkthrough with static geometry and smooth camera motion). SMAA 1x is worth having as a user option for players who prefer no ghosting/blurring.

**Applicability:** Medium -- good option for players who dislike TAA.
**Difficulty:** Low -- well-documented, reference shader available (SMAA.hlsl from iryoku/smaa).

### 7C. FXAA (Fast Approximate Anti-Aliasing)

**What it is:** Single-pass luminance-edge-detection filter. The simplest and cheapest AA option.

**Pros:** ~0.3ms, trivial to implement (single shader).
**Cons:** Blurs the image noticeably. Cannot distinguish geometric edges from texture detail.

**Recommendation:** Offer as a "performance" AA option for users who want maximum FPS.

**Applicability:** Low-medium -- only as a fallback option.
**Difficulty:** Very low -- single shader, well-documented.

### 7D. Specular Anti-Aliasing (Complement to TAA)

**What it is:** Filters normal maps at the material level to prevent specular shimmer/flickering that TAA alone cannot fully resolve.

Techniques:
- **Lean mapping / LEAN mapping:** Precompute normal distribution from normal maps.
- **Geometric specular AA (Tokuyoshi):** Modify roughness based on geometric normal curvature.
- **Voxel Cone AA:** Filter normals using mipmapped normal variance.

**Applicability:** Medium -- helps TAA stability significantly for specular content.
**Difficulty:** Medium.

### Sources
- [TAA Step by Step (Ziyad Barakat)](https://ziyadbarakat.wordpress.com/2020/07/28/temporal-anti-aliasing-step-by-step/)
- [TAA and the Quest for the Holy Trail (Eduardo Lopez)](https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/)
- [Practical TAA Implementation (Alex Tardif)](https://alextardif.com/TAA.html)
- [TAA Tutorial (Sugu Lee)](https://sugulee.wordpress.com/2021/06/21/temporal-anti-aliasingtaa-tutorial/)
- [Intel TAA Implementation (GitHub)](https://github.com/GameTechDev/TAA)
- [SMAA: Enhanced Subpixel Morphological Antialiasing (iryoku)](https://www.iryoku.com/smaa/)
- [SMAA Reference Implementation (GitHub)](https://github.com/iryoku/smaa)
- [SMAA OpenGL/Vulkan Demo (GitHub)](https://github.com/turol/smaaDemo)
- [Anti-Ghosting TAA (Steve Karolewics)](http://stevekarolewics.com/articles/anti-ghosting-taa.html)

---

## 8. GPU-Driven Rendering Techniques

GPU-driven rendering moves scene traversal, culling, and draw command generation from the CPU to the GPU. OpenGL 4.5 supports this fully through compute shaders, SSBOs, and indirect draw.

### 8A. Multi-Draw Indirect (MDI)

**What it is:** Submit many draw calls in a single API call by storing draw commands in a GPU buffer.

**Draw command struct (20 bytes for indexed draws):**
```cpp
struct DrawElementsIndirectCommand
{
    uint count;         // Index count
    uint instanceCount; // 0 = culled, 1+ = visible
    uint firstIndex;    // Offset into index buffer
    int  baseVertex;    // Added to each index
    uint baseInstance;  // Per-draw data index (material ID, transform index)
};
```

**API call:**
```cpp
glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, drawCount, 0);
```

**Key built-in shader variables:**
- `gl_DrawID` -- which command in the buffer (use for material/transform lookup).
- `gl_BaseInstance` -- can encode per-draw metadata.

**Performance benefit:** Replaces hundreds of individual `glDrawElements` calls with one API call. The driver batches everything.

**Applicability:** Very high -- immediate draw call reduction.
**Difficulty:** Low-medium -- restructure vertex/index data into shared buffers.

### 8B. GPU Frustum Culling via Compute Shader

**Pipeline:**
1. Store all object AABBs in an SSBO.
2. Store draw commands for all objects in another SSBO (same buffer bound as `GL_DRAW_INDIRECT_BUFFER`).
3. Dispatch compute shader: for each object, test AABB against frustum planes. Set `instanceCount = 0` if culled, `1` if visible.
4. `glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT)`.
5. `glMultiDrawElementsIndirect(...)` -- culled objects are skipped by GPU (instanceCount=0).

**Compute shader pattern:**
```glsl
layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer DrawCommands {
    DrawElementsIndirectCommand commands[];
};

layout(std430, binding = 1) readonly buffer ObjectData {
    AABB bounds[];
};

uniform mat4 u_viewProj;
uniform vec4 u_frustumPlanes[6];

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= objectCount) return;

    bool visible = testAABBFrustum(bounds[idx], u_frustumPlanes);
    commands[idx].instanceCount = visible ? 1 : 0;
}
```

**Performance:** GPU processes thousands of objects in parallel. The CPU does zero per-object work.

**Applicability:** Very high for scenes with many objects (furniture, architectural details).
**Difficulty:** Medium.

### 8C. GPU Occlusion Culling with Hierarchical-Z

**What it is:** Build a mipchain of the depth buffer (Hi-Z). In the compute shader, project each object's bounding box to screen space, fetch the maximum depth from the appropriate Hi-Z mip level, and cull objects behind existing geometry.

**Two-phase approach (from NVIDIA sample):**
1. **Phase 1:** Render previously-visible objects. Build Hi-Z from resulting depth buffer.
2. **Phase 2:** Test previously-invisible objects against Hi-Z. Render newly visible objects.
3. Maintain visibility flags across frames.

**Applicability:** Medium -- most beneficial for dense scenes with high overdraw. Architectural walkthroughs with many rooms/corridors benefit.
**Difficulty:** Medium-high -- Hi-Z mip generation + two-phase rendering.

### 8D. AZDO (Approaching Zero Driver Overhead)

**What it is:** A suite of OpenGL features/extensions for minimal CPU overhead:

| Feature | Purpose | OpenGL Version |
|---------|---------|---------------|
| Multi-Draw Indirect | Batch draw calls | 4.3 (core) |
| Bindless Textures | Eliminate texture binding | Extension |
| Persistent Mapped Buffers | Eliminate map/unmap | 4.4 (core) |
| Direct State Access | Eliminate bind-to-edit | 4.5 (core) |
| Shader Storage Buffers | Large GPU-writable data | 4.3 (core) |

**All of these are available on RDNA2 with Mesa RadeonSI.**

**The AZDO approach combined:**
1. Pack all meshes into a single mega-VBO/IBO.
2. Store all transforms in an SSBO, indexed by `gl_DrawID` or `gl_BaseInstance`.
3. Store all material data (including bindless texture handles) in another SSBO.
4. Generate draw commands on GPU via compute shader (culling + LOD).
5. Render everything with a single `glMultiDrawElementsIndirect` call.

**Result:** Entire scene renders with 1 shader program, 0 texture binds, 0 buffer binds, 1 draw call.

**Applicability:** This is the end-goal rendering architecture. Can be adopted incrementally.
**Difficulty:** High for full implementation, but each component (MDI, bindless, persistent buffers) provides independent benefits.

### 8E. GPU-Driven LOD Selection

**What it is:** Compute shader evaluates screen-space size of each object's bounding sphere and selects appropriate LOD level. Writes the correct mesh offset/count into the draw command.

**Combined with MDI:** Each LOD level's index range is pre-computed. The compute shader selects which range to use per draw command.

**Applicability:** Medium -- most useful when Vestige has mesh LOD generation.
**Difficulty:** Medium.

### Implementation Roadmap for Vestige

**Phase 1 (Low effort, high return):**
- Adopt DSA and immutable buffer storage throughout
- Implement persistent mapped buffers for particle/foliage streaming
- Consolidate meshes into shared VBO/IBO

**Phase 2 (Medium effort):**
- Implement MDI for static scene objects
- Add GPU frustum culling compute shader
- Use `gl_DrawID` for per-draw material lookup

**Phase 3 (Higher effort):**
- Adopt bindless textures for material system
- GPU-driven terrain LOD with compute + indirect draw
- Hi-Z occlusion culling

### Sources
- [Multi-Draw Indirect Tutorial (J Stephano)](https://ktstephano.github.io/rendering/opengl/mdi)
- [NVIDIA MDI Sample](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/opengl_samples/multidrawindirectsample.htm)
- [AZDO OpenGL Techniques (GitHub)](https://github.com/potato3d/azdo)
- [Modern OpenGL 4.5 Techniques (GitHub)](https://github.com/potato3d/modern-opengl)
- [GPU-Driven Rendering Overview (Vulkan Guide)](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)
- [Generating Draw Commands on GPU (Lingtorp)](https://lingtorp.com/2018/12/05/OpenGL-SSBO-indirect-drawing.html)
- [NVIDIA OpenGL Occlusion Culling (GitHub)](https://github.com/nvpro-samples/gl_occlusion_culling)
- [GPU Controlled Rendering (ImgTec)](https://docs.imgtec.com/sdk-documentation/html/whitepapers/GPUControlledRendering.html)
- [Indirect Rendering: A Way to a Million Draw Calls (cpp-rendering.io)](https://cpp-rendering.io/indirect-rendering/)
- [Modern OpenGL Rendering API (Jesse Fong)](http://www.jessefong.com/blog_modern_opengl)
- [GPU-Driven Rendering in AnKi Engine](https://anki3d.org/gpu-driven-rendering-in-anki-a-high-level-overview/)

---

## Summary: Priority Matrix

| Improvement | Impact | Effort | Prerequisite |
|------------|--------|--------|-------------|
| DSA + immutable buffers | Medium | Low | None |
| Persistent mapped buffers | Medium | Low | None |
| AMD_DEBUG shader inspection | High | Trivial | None |
| PMR frame allocators | Medium | Low | None |
| SDSM cascade optimization | High | Low | Existing CSM |
| Cascade stabilization | Medium | Low | Existing CSM |
| TAA variance clipping | High | Low-Med | Existing TAA |
| TAA Catmull-Rom history | Medium | Low | Existing TAA |
| PCSS soft shadows | Very High | Medium | Existing CSM |
| Multi-Draw Indirect | High | Medium | Shared VBO/IBO |
| GPU frustum culling | High | Medium | MDI |
| SMAA as user option | Medium | Low | None |
| Compute-driven terrain LOD | High | Medium | Compute basics |
| Bindless textures | High | Medium | Material refactor |
| Virtual texturing (SVT) | Very High | High | Sparse textures |
| Job system | High | Medium | None |
| ECS (EnTT) | Medium | High | Architectural change |
| Hi-Z occlusion culling | Medium | Medium-High | GPU culling |
| Full AZDO pipeline | Very High | High | All above |

**Recommended order:** Start with the low-effort/high-impact items in the top rows. The GPU-driven rendering techniques (MDI, frustum culling, bindless) form a natural progression. The shadow and AA improvements are independent and can be done in parallel.
