# Comprehensive Technology Research for Vestige 3D Engine

**Date:** 2026-03-25
**Target:** C++17, OpenGL 4.5, AMD RDNA2 (RX 6600), Linux/Mesa

---

## Table of Contents

1. [OpenGL 4.5+ Best Practices (2024-2026)](#1-opengl-45-best-practices-2024-2026)
2. [Common OpenGL Memory Leak Patterns](#2-common-opengl-memory-leak-patterns)
3. [OpenGL Security Considerations](#3-opengl-security-considerations)
4. [Cascaded Shadow Map Best Practices](#4-cascaded-shadow-map-best-practices)
5. [AMD Mesa Driver Known Issues](#5-amd-mesa-driver-known-issues)
6. [OpenGL Performance on Linux](#6-opengl-performance-on-linux)
7. [Experimental OpenGL Features](#7-experimental-opengl-features)
8. [C++17 Game Engine Patterns](#8-c17-game-engine-patterns)
9. [SMAA Implementation Best Practices](#9-smaa-implementation-best-practices)
10. [TAA Ghosting and Artifacts](#10-taa-ghosting-and-artifacts)
11. [Water Rendering State of the Art](#11-water-rendering-state-of-the-art)
12. [Particle System Performance](#12-particle-system-performance)
13. [Terrain Rendering Optimization](#13-terrain-rendering-optimization)
14. [PBR Rendering Correctness](#14-pbr-rendering-correctness)

---

## 1. OpenGL 4.5+ Best Practices (2024-2026)

### Key Findings

#### Direct State Access (DSA) -- Mandatory for Modern OpenGL

DSA eliminates the bind-to-edit pattern, allowing direct manipulation of objects by handle. This reduces global state changes and makes code clearer and less error-prone.

**Critical DSA function replacements:**

| Legacy Pattern | DSA Replacement |
|---|---|
| `glGenTextures` + `glBindTexture` | `glCreateTextures` |
| `glActiveTexture` + `glBindTexture` | `glBindTextureUnit(unit, texture)` |
| `glGenBuffers` + `glBindBuffer` | `glCreateBuffers` |
| `glBindBuffer` + `glBufferData` | `glNamedBufferStorage` / `glNamedBufferData` |
| `glBindBuffer` + `glMapBuffer` | `glMapNamedBuffer` / `glMapNamedBufferRange` |
| `glGenFramebuffers` + `glBindFramebuffer` | `glCreateFramebuffers` |
| `glBindFramebuffer` + `glFramebufferTexture` | `glNamedFramebufferTexture` |
| `glBindVertexArray` + `glVertexAttribPointer` | `glVertexArrayVertexBuffer` + `glVertexArrayAttribFormat` + `glVertexArrayAttribBinding` |
| `glTexParameteri` | `glTextureParameteri` |
| `glTexImage2D` | `glTextureStorage2D` + `glTextureSubImage2D` |
| `glGenerateMipmap` | `glGenerateTextureMipmap` |
| `glClear` on bound FBO | `glClearNamedFramebufferiv` / `fv` |

**Remaining bind calls needed at runtime:**
- `glBindFramebuffer` -- still required to set the render target
- `glBindVertexArray` -- still required to set the vertex layout for drawing

#### Immutable Storage (Mandatory)

Always use immutable storage for both textures and buffers:

- **Textures:** `glTextureStorage2D` allocates all mipmap levels at once. The driver can optimize knowing the full allocation up front. Prevents accidental re-specification errors. Required for texture views.
- **Buffers:** `glNamedBufferStorage` (with appropriate flags) creates immutable buffers. Use `GL_DYNAMIC_STORAGE_BIT` only if CPU updates are needed. Use `GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT` for persistent mapping.

#### Sampler Objects

Decouple sampling state from texture objects:
- Create one sampler per visual effect/pass
- Bind both texture and sampler atomically
- Use explicit layout bindings in GLSL: `layout(binding = 0) uniform sampler2D myTex;`
- Cache samplers via hash map (cheap to create/destroy)

#### Vertex Input Best Practice

Separate vertex format from buffer binding (OpenGL 4.3+):
- Create one VAO per vertex layout, NOT per mesh
- Use `glVertexArrayVertexBuffer` and `glVertexArrayElementBuffer` at runtime to swap data
- This eliminates redundant VAO state changes

#### Compute Shaders for Post-Processing

- Use compute dispatches instead of full-screen quad rasterization
- Use `textureLod()` exclusively -- implicit derivatives are undefined outside fragment stages
- Always call `glMemoryBarrier()` after dispatch to synchronize writes
- Calculate work groups: `(dimension + local_size - 1) / local_size`

#### Debugging

- Prioritize frame debuggers (RenderDoc, NVIDIA Nsight) over `glGetError()` macros
- Use `glDebugMessageCallback` with `GL_DEBUG_OUTPUT_SYNCHRONOUS` during development for stack-traceable errors
- Disable synchronous mode in release builds for performance

### Common Pitfalls

1. Mixing sampler objects with texture-embedded sampling state causes state leaks
2. Forgetting `glMemoryBarrier` after compute dispatches causes race conditions
3. Using `texture()` instead of `textureLod()` in compute shaders is undefined behavior
4. Texture views require immutable storage -- they fail with mutable textures
5. `glBindTextureUnit(unit, 0)` unbinds the texture; some drivers are strict about this

### Sources

- [Best Practices for Modern OpenGL](https://juandiegomontoya.github.io/modern_opengl.html)
- [Guide to Modern OpenGL Functions](https://github.com/fendevel/Guide-to-Modern-OpenGL-Functions)
- [Direct State Access - OpenGL Wiki](https://www.khronos.org/opengl/wiki/Direct_State_Access)
- [DSA Tutorial - ktstephano](https://ktstephano.github.io/rendering/opengl/dsa)
- [Performance Advantages of DSA - NVIDIA Forums](https://forums.developer.nvidia.com/t/performance-advantages-of-direct-state-access/156822)

### Vestige Recommendations

1. **Audit all GL calls** and replace every `glGen*` with `glCreate*`, every bind-to-edit with DSA equivalents
2. **Use immutable storage everywhere** -- `glTextureStorage2D` for textures, `glNamedBufferStorage` for buffers
3. **Create a sampler cache** -- hash map of sampler configurations, shared across materials
4. **One VAO per vertex layout** -- swap buffer bindings instead of VAOs
5. **Use explicit layout bindings** in all GLSL shaders to avoid `glUniform1i` for sampler locations
6. **Enable GL debug output** in debug builds with synchronous mode for immediate error detection

---

## 2. Common OpenGL Memory Leak Patterns

### Key Findings

#### Leak Pattern 1: Missing Delete Calls

The most common leak -- creating OpenGL objects (textures, buffers, shaders, programs, framebuffers, renderbuffers) without corresponding `glDelete*` calls. Especially dangerous when:
- Creating textures inside a render loop without deleting old ones
- Recreating framebuffers on window resize without deleting the old attachments
- Compiling shaders every frame instead of caching programs

#### Leak Pattern 2: Forgotten Shader Objects

After linking a shader program, the individual shader objects (`GL_VERTEX_SHADER`, `GL_FRAGMENT_SHADER`) should be detached and deleted. They remain in GPU memory until explicitly freed.

#### Leak Pattern 3: Orphaned Framebuffer Attachments

When replacing a framebuffer attachment (e.g., on resize), the old texture/renderbuffer must be deleted separately. Attaching a new texture does not delete the old one.

#### Leak Pattern 4: Multithreaded Shader Compilation Leak

Using `glMaxShaderCompilerThreadsARB` for multi-threaded shader compilation has been reported to cause memory usage that rises and is never released after loading completes.

#### Leak Pattern 5: Context Destruction Without Cleanup

All OpenGL objects must be deleted before destroying the GL context. Objects not explicitly deleted become leaks -- the driver may or may not reclaim them.

#### Leak Pattern 6: Persistent Mapped Buffer Misuse

If using persistent mapped buffers and the application creates new buffer storage without unmapping/deleting the old one, the old allocation persists.

### RAII Best Practices for OpenGL

**Move-only wrapper types:**
- Forbid copying (deleted copy constructor/assignment)
- Support move semantics (steal resource handle, zero out source)
- Destructor calls `glDelete*` -- safe to call with handle 0
- Default-construct to handle 0 (null object)

```cpp
// Pattern for OpenGL RAII wrapper
class GLBuffer
{
public:
    GLBuffer() : m_handle(0) {}
    ~GLBuffer() { if (m_handle) glDeleteBuffers(1, &m_handle); }

    // No copying
    GLBuffer(const GLBuffer&) = delete;
    GLBuffer& operator=(const GLBuffer&) = delete;

    // Move support
    GLBuffer(GLBuffer&& other) noexcept : m_handle(other.m_handle) { other.m_handle = 0; }
    GLBuffer& operator=(GLBuffer&& other) noexcept
    {
        if (this != &other)
        {
            if (m_handle) glDeleteBuffers(1, &m_handle);
            m_handle = other.m_handle;
            other.m_handle = 0;
        }
        return *this;
    }

private:
    GLuint m_handle;
};
```

### Sources

- [Common Mistakes - OpenGL Wiki](https://www.khronos.org/opengl/wiki/Common_Mistakes)
- [OpenGL RAII and Multi-threading - GameDev.net](https://www.gamedev.net/forums/topic/660295-opengl-raii-and-multi-threading/)
- [OpenGL RAII in C++11 - riptutorial](https://riptutorial.com/opengl/example/24911/in-cplusplus11-and-later)
- [Zero-Overhead RAII Wrapper](http://noisecode.net/blog/2019/01/21/a-zero-overhead-raii-wrapper-part-i/)
- [Shader Compiler Thread Leak - NVIDIA Forums](https://forums.developer.nvidia.com/t/we-are-experiencing-like-a-memory-leak-using-the-glmaxshadercompilerthreadsarb/258442)

### Vestige Recommendations

1. **Every GL resource must have a RAII wrapper** -- move-only, destructor calls appropriate `glDelete*`
2. **Audit resize paths** -- window/framebuffer resize must delete old textures/renderbuffers before creating new ones
3. **Detach and delete shader objects** after program linking
4. **Never create GL objects in hot loops** -- use pooling or lazy initialization
5. **Add a resource leak detector** in debug builds that tracks all `glCreate*` / `glDelete*` pairs and reports imbalances at shutdown
6. **Avoid multi-threaded shader compilation** on Mesa/AMD due to potential leak issues -- compile on the main context thread

---

## 3. OpenGL Security Considerations

### Key Findings

#### Buffer Overflow Prevention

**GL_KHR_robustness / GL_ARB_robustness:**
- Request a robust context at creation time (via GLFW: `glfwWindowHint(GLFW_CONTEXT_ROBUSTNESS, GLFW_LOSE_CONTEXT_ON_RESET)`)
- With robustness enabled, out-of-bounds buffer access returns zero instead of crashing or reading garbage
- `GL_ARB_robust_buffer_access_behavior`: indices outside vertex buffer return zero; writes are discarded
- Trade-off: slight performance cost for defined behavior on out-of-bounds access

**Buffer size validation:**
- Always validate buffer sizes before `glNamedBufferSubData` or `glMapNamedBufferRange`
- Use `glGetNamedBufferParameteriv` with `GL_BUFFER_SIZE` to verify allocation
- Compute shader SSBOs should use runtime length queries (`length()` on unsized arrays)

#### Shader Security

**GLSL injection is NOT a typical attack vector** in native applications since shaders are compiled from source strings the application controls. However:
- Never construct shader source from untrusted input (e.g., user-provided filenames, modding scripts)
- If supporting user mods, use SPIR-V with offline validation via [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools)
- Validate SPIR-V modules offline before loading -- OpenGL drivers do NOT fully validate SPIR-V at runtime

**Shader robustness:**
- Avoid unbounded loops in shaders -- always have clear iteration limits
- Implement error handling for shader compilation failures with fallback shaders
- Use `glGetShaderiv(GL_COMPILE_STATUS)` and `glGetProgramiv(GL_LINK_STATUS)` checks

#### Debug Callback for Security Monitoring

Use `glDebugMessageCallback` to detect:
- Buffer overruns reported by the driver
- Invalid operations that could indicate corruption
- Deprecated function usage that may have security implications

**Synchronous vs. Asynchronous:**
- Synchronous (`GL_DEBUG_OUTPUT_SYNCHRONOUS`): same thread, stacktrace-able -- use in debug builds
- Asynchronous: better performance, log-based monitoring -- use in release builds
- Filter noise with `glDebugMessageControl` (e.g., ignore NVIDIA info message 131185)

### Sources

- [KHR_robustness - Khronos Registry](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_robustness.txt)
- [ARB_robust_buffer_access_behavior](https://developer.download.nvidia.com/opengl/specs/GL_ARB_robust_buffer_access_behavior.txt)
- [WebGL Security Best Practices](https://blog.pixelfreestudio.com/webgl-security-best-practices-ensuring-safe-3d-web-experiences/)
- [CISA Buffer Overflow Alert](https://www.cisa.gov/resources-tools/resources/secure-design-alert-eliminating-buffer-overflow-vulnerabilities)
- [SPIRV-Tools - Khronos](https://github.com/KhronosGroup/SPIRV-Tools)
- [OpenGL Debug Output - Khronos Wiki](https://www.khronos.org/opengl/wiki/Debug_Output)
- [LearnOpenGL - Debugging](https://learnopengl.com/In-Practice/Debugging)

### Vestige Recommendations

1. **Request a robust context** via GLFW at startup for defined out-of-bounds behavior
2. **Validate all buffer sizes** before writes -- add assertion macros for debug builds
3. **Never construct GLSL from user input** -- load shaders only from trusted, bundled files
4. **Implement fallback shaders** for compilation failures (solid-color emergency shader)
5. **Always check compile/link status** with info log retrieval
6. **Enable debug callback** in debug builds with synchronous output
7. **Add bounds checking** to all SSBO writes in compute shaders

---

## 4. Cascaded Shadow Map Best Practices

### Key Findings

#### Stable CSM: Preventing Shimmering

Two causes of shadow shimmering must be addressed:

**1. Rotation Instability (solved by sphere fitting):**
- Wrap each frustum slice in a bounding sphere (rotationally invariant volume)
- Project the sphere into light space and derive ortho bounds from its center and radius
- This prevents bounds from changing shape as the camera rotates

**2. Translation Instability (solved by texel snapping):**
- Calculate texel size: `f = (sphereRadius * 2.0) / shadowMapResolution`
- Snap the ortho projection min/max to texel increments: `floor(value / f) * f`
- This ensures world-space positions always map to the same relative position within shadow texels

**Complete texel snapping code:**
```cpp
float shadowMapRes = getShadowMapResolution();
float texelSize = (sphereRadius * 2.0f) / shadowMapRes;

// Transform sphere center to light view space
vec3 centerLS = lightViewMatrix * sphereCenter;

float minX = floor((centerLS.x - sphereRadius) / texelSize) * texelSize;
float minY = floor((centerLS.y - sphereRadius) / texelSize) * texelSize;
float extent = floor((sphereRadius * 2.0f) / texelSize) * texelSize;
float maxX = minX + extent;
float maxY = minY + extent;
// Use these for ortho projection
```

#### Cascade Split Schemes

- **Logarithmic splitting**: Best for large depth ranges, concentrates resolution near camera
- **Practical split (PSSM)**: `C_split = lerp(near * (far/near)^(i/n), near + (far-near)*(i/n), lambda)` where lambda (0-1) blends between log and linear
- **Manual splits**: Engine-specific tuning, common choice for controlled environments
- Vestige example: Cascade 1: 0.1-20, Cascade 2: 20-80, Cascade 3: 80-400

#### SDSM (Sample Distribution Shadow Maps)

- Analyze the depth buffer on the GPU (compute shader) to find tight min/max depth per cascade
- Tighter bounds = higher effective shadow resolution
- Requires depth pre-pass or previous frame's depth buffer
- Implementation: Parallel reduction on depth buffer to find min/max, then compute optimal split distances

#### Soft Shadows

- Separable Gaussian blur on downsampled shadow map is cost-effective
- PCF (Percentage Closer Filtering) with Poisson disk sampling for per-pixel softness
- PCSS (Percentage Closer Soft Shadows) for contact-hardening shadows with variable penumbra

#### Cascade Blending

- Blend between cascades in a transition region to hide seams
- Use a lerp factor based on depth within the cascade's range
- Alternatively, sample both cascades in the overlap zone and blend

#### Bias Strategies

- Slope-scale bias: `bias = constantBias + slopeBias * max(abs(dDepth/dx), abs(dDepth/dy))`
- Normal offset bias: offset the sampling position along the surface normal before projecting into shadow space
- These two combined effectively eliminate both acne and Peter Panning

#### Performance: Texture Array Storage

- Store all cascades in a `GL_TEXTURE_2D_ARRAY` -- sample with `vec3(u, v, cascadeIndex)`
- Single texture bind for all cascades
- Enables hardware-accelerated array lookup

#### Far Cascade Update Optimization

- Far cascades change slowly -- consider updating every other frame
- Add a small buffer zone to account for the skipped frame's movement

### Sources

- [LearnOpenGL - CSM](https://learnopengl.com/Guest-Articles/2021/CSM)
- [NVIDIA - Cascaded Shadow Maps](https://developer.download.nvidia.com/SDK/10.5/opengl/src/cascaded_shadow_maps/doc/cascaded_shadow_maps.pdf)
- [Microsoft - Cascaded Shadow Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)
- [Stable CSM - Theo Mader](https://dev.theomader.com/stable-csm/)
- [Stable CSM Blog Post](http://longforgottenblog.blogspot.com/2014/12/rendering-post-stable-cascaded-shadow.html)
- [Alex Tardif - Shadow Mapping](https://alextardif.com/shadowmapping.html)
- [A Sampling of Shadow Techniques - MJP](https://therealmjp.github.io/posts/shadow-maps/)
- [BlackMesa CSM 2.0](https://chetanjags.wordpress.com/2023/07/17/blackmesa-xenengine-part3-csm-2-0/)

### Vestige Recommendations

1. **Implement sphere fitting + texel snapping** for all cascades -- eliminates shimmering
2. **Use GL_TEXTURE_2D_ARRAY** for cascade storage -- single bind, hardware-accelerated
3. **Implement SDSM** via compute shader depth reduction for tighter cascade bounds
4. **Use normal offset bias + slope-scale bias** combined to eliminate acne and Peter Panning
5. **Blend between cascades** in a 5-10% overlap zone to hide transitions
6. **Consider updating far cascades every other frame** for performance
7. **Remember:** `glClipControl` is global state -- shadow passes MUST restore `GL_NEGATIVE_ONE_TO_ONE` (per existing project memory)

### Bugs to Watch For

- Shadow shimmering if texel snapping is not applied to both X and Y
- Peter Panning if bias is too large
- Shadow acne if bias is too small
- Light bleeding at cascade boundaries if blending zone is too narrow
- Depth precision issues if near plane is too close (keep near >= 0.1)

---

## 5. AMD Mesa Driver Known Issues

### Key Findings

#### RDNA2-Specific Issues

**Sampler Binding Requirement (CRITICAL for Vestige):**
Mesa's AMD driver requires ALL declared GLSL samplers to have valid textures bound at draw time, even if the sampler is not actually used in the current execution path. This causes crashes or undefined behavior if any declared sampler uniform has no texture bound.

**Workaround:** Create a 1x1 white dummy texture and bind it to all texture units that have declared but conditionally-unused samplers.

**radeonsi_clamp_div_by_zero:**
Mesa includes a workaround option that clamps division-by-zero results to `FLT_MAX` instead of `NaN`. This fixes rendering artifacts in some applications. Enabled by default in some Mesa versions.

**radeonsi_zerovram:**
Initializes all VRAM allocations to zero. Can fix mysterious rendering corruption but has a performance cost. Useful for debugging.

#### Mesa 24.x - 26.x Changes

**ACO Shader Compiler (Mesa 24.0+):**
- RadeonSI switched to the ACO shader compiler by default (previously LLVM-only)
- ACO provides better ISA code generation, faster GPU performance, and faster compile times
- Can be explicitly enabled with `AMD_DEBUG=useaco` or falls back automatically without LLVM
- Significant improvement for shader-heavy workloads

**Buffer Object Fence Tracking Rewrite (Mesa 24.0):**
- Complete rewrite of BO fence tracking decreased command submission thread overhead by 46%
- CPU-bound benchmarks saw 12% performance improvement
- Additional slab allocator optimization gave 10-18% more in some benchmarks

**NGG Culling:**
- Enabled by default for RDNA2, provides hardware-accelerated primitive culling
- The RX 6600 specifically benefited from this being enabled

#### Mesa 25.x - 26.x

- Mesa 25.0 continued RadeonSI optimizations, particularly shader compilation efficiency
- Mesa 26.0 added ray tracing performance improvements (RADV/Vulkan, not directly relevant to GL)
- Mesa 26.0 OpenGL driver received "performance optimizations despite its legacy status"

#### Known Regression Patterns

- Mesa 24.3.1 caused system freezing on some AMD APU systems (fixed in later point releases)
- KDE Wayland users more affected by display-related regressions
- When upgrading Mesa, test with known-good scenes before deploying

### Sources

- [Mesa Environment Variables](https://docs.mesa3d.org/envvars.html)
- [Mesa 25.0.0 Release Notes](https://docs.mesa3d.org/relnotes/25.0.0.html)
- [Mesa 25.3.2 Release Notes](https://docs.mesa3d.org/relnotes/25.3.2.html)
- [Mesa 26.0 Release - GamingOnLinux](https://www.gamingonlinux.com/2026/02/mesa-26-0-is-out-bringing-ray-tracing-performance-improvements-for-amd-radv/)
- [RadeonSI First Win 2024 - Phoronix](https://www.phoronix.com/news/RadeonSI-First-Win-2024)
- [ACO for RadeonSI - GamingOnLinux](https://www.gamingonlinux.com/forum/topic/5874/)
- [RX 6600 Performance - Phoronix](https://www.phoronix.com/review/radeon-rx-6600/5)
- [Mesa 26.0.2 Bug Fixes - GamingOnLinux](https://www.gamingonlinux.com/2026/03/mesa-26-0-2-arrives-with-more-bug-fixes-for-linux-graphics-drivers/)

### Vestige Recommendations

1. **Maintain the dummy texture workaround** for unused samplers -- this is critical on Mesa/AMD
2. **Target Mesa 25.0+** minimum to benefit from ACO shader compiler improvements
3. **Test with `AMD_DEBUG=info`** periodically to check for driver warnings
4. **Use `AMD_DEBUG=w32ps`** to experiment with Wave32 pixel shaders for potential performance gains on RDNA2
5. **Monitor Mesa release notes** for RadeonSI regressions when updating the system
6. **Consider `AMD_DEBUG=useaco`** if building Mesa without LLVM support
7. **Test `radeonsi_zerovram=1`** if encountering mysterious rendering corruption during development
8. **Avoid Delta Color Compression issues** -- if encountering texture corruption, test with `AMD_DEBUG=nodcc`

### Bugs to Watch For

- System freezes after suspend/resume with OpenGL applications on X11 (Mesa driver issue)
- Shader compilation may take longer on first run with ACO; subsequent runs benefit from shader caching
- `glClipControl` state not being restored properly between passes (previously discovered in Vestige)

---

## 6. OpenGL Performance on Linux

### Key Findings

#### Mesa-Specific Optimizations

**RadeonSI Buffer Object Optimizations (2024):**
- Command submission thread overhead reduced by 46% through fence tracking rewrite
- Slab allocator optimization provides additional 10-18% in CPU-bound scenarios
- These improvements are in Mesa 24.0+ and benefit all RadeonSI users

**ACO Compiler Benefits:**
- Faster shader compilation than LLVM backend
- Better GPU instruction scheduling for RDNA2
- Lower compile-time overhead means faster startup and reduced stutter

**Key Mesa Environment Variables for Performance:**

| Variable | Purpose |
|---|---|
| `AMD_DEBUG=useaco` | Force ACO shader compiler (default in Mesa 24+) |
| `AMD_DEBUG=w32ps` | Force Wave32 for pixel shaders (may improve RDNA2 perf) |
| `AMD_DEBUG=w32cs` | Force Wave32 for compute shaders |
| `AMD_DEBUG=nodcc` | Disable Delta Color Compression (debug corruption) |
| `AMD_DEBUG=nohyperz` | Disable Hyper-Z (debug depth issues) |
| `AMD_DEBUG=info` | Enable detailed driver info logging |
| `MESA_GL_VERSION_OVERRIDE=4.6` | Override reported GL version |
| `mesa_glthread=true` | Enable GL threading (often default) |

#### Driver Overhead Reduction Strategies

**1. Persistent Mapped Buffers (PMB):**
- Map once with `GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`
- Keep the pointer forever -- no map/unmap overhead per frame
- Triple-buffer the data: divide buffer into 3 sections, write to one while GPU reads from another
- No synchronization needed with triple buffering (each section has its own fence)
- **Fastest approach** for streaming uniform/vertex data

**PMB Setup:**
```cpp
glNamedBufferStorage(buffer, size * 3, nullptr,
    GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
void* ptr = glMapNamedBufferRange(buffer, 0, size * 3,
    GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
```

**2. Multi-Draw Indirect (MDI):**
- Pack all draw commands into a GPU buffer (`GL_DRAW_INDIRECT_BUFFER`)
- Submit with single `glMultiDrawElementsIndirect` call
- Combine with compute shader culling: bind draw buffer as SSBO, set `instanceCount=0` for culled objects
- `gl_DrawID` provides per-command identification in shaders
- Eliminates hundreds of individual draw call submissions

**3. GPU-Driven Frustum Culling:**
- Implement frustum culling in a compute shader instead of CPU
- Read bounding volumes from SSBO, write surviving draw commands to indirect buffer
- Hierarchical-Z occlusion culling: generate depth pyramid, check bounding boxes against appropriate mip level
- Can reduce GPU frametime by up to 75% in complex scenes

**4. Batching and State Sorting:**
- Sort draw calls by shader program first, then by material/texture
- Minimize `glUseProgram` and `glBindTextureUnit` changes
- Merge meshes sharing the same material into single vertex/index buffers

#### Wayland vs. X11

- Mesa documentation recommends Wayland over X11 for "additional modifiers for better performance"
- X11 has known issues with suspend/resume causing memory leaks in GL applications

### Sources

- [Mesa Performance Tips](https://docs.mesa3d.org/perf.html)
- [Mesa Environment Variables](https://docs.mesa3d.org/envvars.html)
- [Persistent Mapped Buffers - C++ Stories](https://www.cppstories.com/2015/01/persistent-mapped-buffers-in-opengl/)
- [PMB Benchmark Results](https://www.cppstories.com/2015/01/persistent-mapped-buffers-benchmark/)
- [Buffer Object Streaming - Khronos Wiki](https://www.khronos.org/opengl/wiki/Buffer_Object_Streaming)
- [Multi-Draw Indirect - ktstephano](https://ktstephano.github.io/rendering/opengl/mdi)
- [Indirect Rendering - cpp-rendering.io](https://cpp-rendering.io/indirect-rendering/)
- [Frustum Culling - Bruno Opsenica](https://bruop.github.io/frustum_culling/)
- [GPU Occlusion Culling - NVIDIA](https://github.com/nvpro-samples/gl_occlusion_culling)

### Vestige Recommendations

1. **Implement persistent mapped buffers** with triple-buffering for all streaming data (uniforms, dynamic vertices, particle data)
2. **Move to Multi-Draw Indirect** for scene rendering -- pack all meshes into shared buffers
3. **Implement compute shader frustum culling** writing to an indirect draw buffer
4. **Sort draw calls** by program, then texture/material to minimize state changes
5. **Profile with `AMD_DEBUG=info`** to identify driver bottlenecks
6. **Ensure mesa_glthread is enabled** (usually default, but verify)
7. **Consider Wayland** if not already using it for compositor performance benefits
8. **Use ReBAR** if available (Resizable BAR in BIOS) -- improves GPU memory access patterns

---

## 7. Experimental OpenGL Features

### Key Findings

#### Worth Using: GL_ARB_buffer_storage (Core in 4.4)

Already covered above in persistent mapped buffers -- essential for performance.

#### Worth Using: GL_ARB_multi_draw_indirect (Core in 4.3)

Already covered above -- critical for GPU-driven rendering.

#### Worth Using: GL_ARB_compute_shader (Core in 4.3)

Compute shaders provide stateless parallel processing with access to:
- Textures, image variables, atomic counters, SSBOs
- Shared memory between work group invocations
- Atomic operations on shared variables

**Key patterns:**
- Frustum/occlusion culling
- Particle simulation
- Depth buffer reduction (for SDSM)
- Post-processing effects
- Histogram computation (for auto-exposure)

#### Worth Using: GL_ARB_shader_image_load_store (Core in 4.2)

Enables read-write access to textures from any shader stage:
- Useful for compute-based post-processing
- Required for screen-space techniques like SSAO, SSR
- Use `imageLoad` / `imageStore` with appropriate `glMemoryBarrier`

#### Worth Using: GL_ARB_shader_storage_buffer_object (Core in 4.3)

SSBOs provide flexible, large read-write storage:
- Unsized arrays in shaders (`buffer { vec4 data[]; }`)
- Runtime length queries
- Essential for GPU-driven rendering pipelines
- Better than UBOs for large/variable-size data

#### Worth Exploring: GL_ARB_bindless_texture

- Removes texture binding overhead entirely
- Textures referenced by 64-bit handles stored in buffers
- **Not yet core** -- requires extension support
- Mesa/RadeonSI support is available but check version
- Significant performance win for scenes with many unique textures

#### Worth Exploring: GL_ARB_indirect_parameters (Core in 4.6)

- `glMultiDrawElementsIndirectCount` -- draw count comes from a GPU buffer
- Enables fully GPU-driven rendering where even the number of draw calls is determined by compute shaders
- Eliminates the last CPU-GPU synchronization point for draw submission

#### Compute Shader Patterns

**1. Parallel Reduction:**
- Used for depth buffer min/max (SDSM), histogram, etc.
- Two-pass: reduce within work groups using shared memory, then reduce work group results

**2. Prefix Sum (Scan):**
- Used for stream compaction (packing sparse data)
- Essential for GPU particle systems and indirect rendering

**3. Spatial Hashing:**
- O(n) neighborhood search using count sort + parallel prefix scan
- Dramatically faster than brute-force for particle collisions

### Sources

- [GL_ARB_compute_shader - Khronos](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_compute_shader.txt)
- [GL_ARB_indirect_parameters - Khronos](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_indirect_parameters.txt)
- [OpenGL Extensions - Khronos Wiki](https://www.khronos.org/opengl/wiki/OpenGL_Extension)
- [Mesa Extension Support Matrix](https://mesamatrix.net/)

### Vestige Recommendations

1. **Prioritize compute shaders** for culling, particle simulation, and post-processing
2. **Implement SSBOs** for material data, instance data, and draw commands
3. **Explore GL_ARB_indirect_parameters** for fully GPU-driven rendering
4. **Test GL_ARB_bindless_texture** on the target Mesa version for texture-heavy scenes
5. **Implement parallel reduction** as a reusable utility for SDSM, auto-exposure, etc.
6. **Always check extension support** at runtime with `glGetStringi(GL_EXTENSIONS, i)`

---

## 8. C++17 Game Engine Patterns

### Key Findings

#### Entity Component System (ECS)

**Data-Oriented Design (DOD) Principles:**
- Organize data by type (Structure of Arrays), not by entity (Array of Structures)
- Sequential memory access maximizes CPU cache hits
- Process components in tight loops over contiguous arrays

**Popular C++17 ECS Libraries:**
- **EnTT** -- fastest, most popular, header-only, uses sparse set storage
- **Gaia-ECS** -- type-safe, archetype-based, C++17
- **FLECS** -- C/C++, archetype-based, excellent tooling

**Vestige's current Subsystem + Event Bus pattern** is a valid alternative to full ECS for an architectural walkthrough engine. ECS is most beneficial when processing thousands of similar entities (e.g., particles, NPCs). For Vestige's use case (moderate entity counts, complex rendering), the current architecture may be more appropriate.

#### Memory Management Strategies

**1. Arena/Linear Allocator:**
- Allocate large buffer up front, bump a pointer for each allocation
- Free everything at once (e.g., at frame end)
- Perfect for per-frame temporary data
- Zero overhead for individual allocations

**2. Pool Allocator:**
- Pre-allocate fixed-size slots in a large block
- Track free slots with a free list or bitset
- Ideal for same-sized objects (components, particles, nodes)
- No fragmentation within the pool

**3. Frame Allocator (Transient Arena):**
- Arena that resets every frame
- Used for temporary geometry, command lists, string formatting
- Avoids all per-frame heap allocations

**4. Stack Allocator:**
- LIFO allocation pattern
- Good for recursive algorithms and scoped temporary data
- Deallocations must be in reverse order

#### Modern C++17 Features for Game Engines

**`std::variant` for type-safe unions:**
- Event types in event buses
- Component storage without virtual dispatch

**`std::optional` for nullable values:**
- Resource loading results
- Optional component queries

**`if constexpr` for compile-time branching:**
- Template specialization in serializers
- Conditional code paths based on component types

**Structured bindings:**
- Cleaner iteration over maps and pairs
- Unpacking query results

**`std::string_view` for zero-copy string handling:**
- Shader source handling
- Configuration parsing
- Resource name lookups

**RAII everywhere:**
- Smart pointers for ownership (`unique_ptr` for single owner, `shared_ptr` only when truly shared)
- Custom deleters for OpenGL resources
- Scope guards for exception-safe state management

### Sources

- [EnTT - Gaming Meets Modern C++](https://github.com/skypjack/entt)
- [12 Best ECS for C++](https://dev.to/marcosplusplus/the-12-best-entity-component-systems-for-c-98h)
- [Advanced C++ Game Programming 2025](https://generalistprogrammer.com/tutorials/advanced-cpp-game-programming-expert-techniques-2025)
- [Building a Modern C++ Game Engine](https://www.rhelmer.org/blog/building-whiskers-engine-cpp-game-engine/)
- [Arena Allocator - Ryan Fleury](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)
- [Pool Allocator Design - Gamedeveloper](https://www.gamedeveloper.com/programming/designing-and-implementing-a-pool-allocator-data-structure-for-memory-management-in-games)
- [Memory Management for Game Engines](https://palospublishing.com/memory-management-techniques-for-real-time-game-engines-in-c/)

### Vestige Recommendations

1. **Implement a frame allocator** for per-frame temporary data (render commands, string formatting)
2. **Add a pool allocator** for fixed-size objects (scene nodes, components, particles)
3. **Keep the Subsystem + Event Bus pattern** -- it fits Vestige's architectural walkthrough focus better than full ECS
4. **Use `std::string_view`** for all read-only string parameters (shader names, resource paths)
5. **Use `std::variant`** for event payloads in the event bus instead of inheritance hierarchies
6. **Avoid `std::shared_ptr`** except where true shared ownership exists -- prefer `std::unique_ptr`
7. **Use RAII wrappers** for all OpenGL resources with move semantics
8. **Profile memory allocations** -- `new`/`delete` calls per frame should be near zero in hot paths

---

## 9. SMAA Implementation Best Practices

### Key Findings

#### Common SMAA Bugs in OpenGL

**1. Coordinate System Mismatch (CRITICAL):**
- SMAA was written for DirectX where texture origin is top-left
- OpenGL texture origin is bottom-left
- Both the area texture and search texture may need Y-axis flipping
- The blend weight pass will produce incorrect checkerboard patterns if coordinates are wrong

**2. Search/Area Texture Loading Errors:**
- Using `GL_R` instead of `GL_RED` in `glTexImage2D` causes incorrect loading
- The search texture format is particularly finicky -- verify it loads correctly by inspecting in RenderDoc
- Area texture: typically `GL_RG8` format (two channels)
- Search texture: typically `GL_R8` format (one channel)

**3. sRGB Handling:**
- `GL_FRAMEBUFFER_SRGB` must be enabled in the final blending pass
- Without sRGB, colors will appear washed out or overly dark
- Ensure intermediate render targets use linear color space

**4. Edge Detection Threshold:**
- Too low: false edges everywhere, blurring that should be sharp
- Too high: missed edges, visible aliasing
- Recommended starting value: 0.1 for color edge detection

#### SMAA T2x (Temporal Enhancement)

**Jitter Pattern:**
- Frame 0: camera jitter `(0.25, -0.25)`, subsampleIndices `float4(1, 1, 1, 0)`
- Frame 1: camera jitter `(-0.25, 0.25)`, subsampleIndices `float4(2, 2, 2, 0)`
- Apply jitter to the projection matrix before rendering

**Velocity Buffer:**
- Required for temporal reprojection to reject invalid history
- Calculate per-pixel motion vectors from current and previous frame matrices
- Enable `SMAA_REPROJECTION` macro in the SMAA shader

**Ghosting Tuning:**
- `SMAA_REPROJECTION_WEIGHT_SCALE` controls history weight
- Increasing current-frame weight reduces ghosting but also reduces AA quality
- Balance is scene-dependent -- test with both fast and slow camera movement

#### Quality Levels

| Mode | Passes | Quality | Cost |
|---|---|---|---|
| SMAA 1x | 3 passes (edge, weight, blend) | Good | Low |
| SMAA T2x | 3 passes + temporal resolve | Very Good | Medium |
| SMAA S2x | 3 passes + 2x MSAA | Excellent | High |
| SMAA 4x | 3 passes + 2x MSAA + temporal | Best | Highest |

### Sources

- [SMAA Official Page](https://www.iryoku.com/smaa/)
- [SMAA Reference Implementation](https://github.com/iryoku/smaa)
- [GLSL SMAA Implementation](https://github.com/dmnsgn/glsl-smaa)
- [SMAA OpenGL Integration Issues](https://github.com/iryoku/smaa/issues/8)
- [Porting SMAA to OpenGL - GameDev.net](https://www.gamedev.net/forums/topic/620516-help-porting-smaa-from-direct3d-to-opengl/)
- [SMAA T2x in UE5](https://acinonyx.nz/adding-smaa-to-unreal-engine-5/)
- [Filmic SMAA](http://www.klayge.org/material/4_11/Filmic%20SMAA%20v7.pdf)

### Vestige Recommendations

1. **Verify coordinate system** -- if SMAA produces checkerboard artifacts, flip Y on area/search textures
2. **Use correct texture formats**: area = `GL_RG8`, search = `GL_R8`
3. **Enable `GL_FRAMEBUFFER_SRGB`** only in the final blending pass
4. **Start with SMAA 1x** and add T2x later for temporal stability
5. **For T2x:** implement motion vectors first, then add subpixel jitter to the projection matrix
6. **Test edge detection threshold** (0.05-0.15 range) -- too low causes blur, too high misses edges
7. **Debug with RenderDoc** -- inspect edge and blend weight textures to verify correctness

### Bugs to Watch For

- Inverted edge detection on AMD/Mesa due to coordinate system differences
- Area texture corruption if loaded with wrong byte alignment (set `GL_UNPACK_ALIGNMENT` to 1)
- Blend weight texture appearing all black -- usually a framebuffer attachment or format error
- Temporal ghosting in SMAA T2x -- tune reprojection weight carefully

---

## 10. TAA Ghosting and Artifacts

### Key Findings

#### Root Cause of TAA Ghosting

Ghosting occurs when the history buffer contains outdated color information that hasn't been properly invalidated:
- Moving objects leave trails of their previous positions
- Disoccluded regions show stale background colors
- Camera rotation can cause trailing on all edges

#### State of the Art: k-DOP Clipping (SIGGRAPH Asia 2024)

**The most significant recent advancement** in TAA ghosting mitigation:

- Previous methods (AABB clipping, variance clipping) use simple bounding boxes in color space to validate history
- These methods are "only situationally effective" -- they fail in many common cases
- **k-DOP (k-Discrete Oriented Polytopes)** use multiple oriented halfplanes instead of axis-aligned boxes
- **16 axes** provides generally satisfying results
- Performance cost: approximately **0.2 ms**
- Much more robust ghosting mitigation than previous methods

**k-DOP vs. Previous Methods:**

| Method | Ghosting Quality | Performance | Robustness |
|---|---|---|---|
| AABB Clipping | Poor | Very Fast | Inconsistent |
| Variance Clipping | Good | Fast | Moderate |
| k-DOP Clipping (16 axes) | Excellent | +0.2ms | Consistent |

**Implementation tools available:** [k-DOP optimizer on GitHub](https://github.com/vga-group/taa-kdop-optimizer)

#### Variance Clipping (Current Standard)

Still widely used and effective in many cases:
1. Sample 3x3 neighborhood of current frame
2. Calculate mean and standard deviation in YCoCg color space
3. Build an AABB: `center = mean`, `extents = gamma * stddev`
4. Clamp or clip history color against this AABB
5. **Gamma control:** `MIN_VARIANCE_GAMMA` during movement, `MAX_VARIANCE_GAMMA` when still

**YCoCg color space** improves precision of the AABB intersection compared to RGB.

#### Disocclusion Detection

- Compare current and previous frame depth buffers
- If depth difference exceeds a threshold, mark pixel as "no-history"
- No-history pixels use only current frame data (no blending with history)
- Reduces ghosting in newly revealed regions

#### Motion Vector Quality

- Per-pixel motion vectors are critical -- per-object is not sufficient
- Skinned meshes need per-vertex motion vectors
- Depth-based reprojection works for static geometry but fails for dynamic objects

#### Sharpening

TAA inherently softens the image due to temporal accumulation:
- Apply a sharpening pass after TAA
- CAS (Contrast Adaptive Sharpening) is popular and efficient
- Alternatively, reduce the TAA blend factor (more current frame = sharper but more aliased)

### Sources

- [k-DOP Clipping - SIGGRAPH Asia 2024](https://dl.acm.org/doi/10.1145/3681758.3697996)
- [k-DOP Optimizer Tool](https://github.com/vga-group/taa-kdop-optimizer)
- [Intel TAA Reference](https://github.com/GameTechDev/TAA)
- [TAA - Wikipedia](https://en.wikipedia.org/wiki/Temporal_anti-aliasing)
- [TAA Rendering Pipeline - Babylon.js](https://doc.babylonjs.com/features/featuresDeepDive/postProcesses/TAARenderingPipeline)

### Vestige Recommendations

1. **Implement variance clipping in YCoCg** as the baseline ghosting mitigation
2. **Consider k-DOP clipping** (16 axes) as an upgrade path -- 0.2ms cost for significantly better quality
3. **Implement disocclusion detection** using depth buffer comparison between frames
4. **Generate per-pixel motion vectors** -- critical for correct history validation
5. **Apply CAS sharpening** after TAA to restore perceived detail
6. **Use two gamma values** for variance clipping: tighter during motion, looser when stationary
7. **Test with fast camera movement** to verify ghosting is acceptably low
8. **Blend factor:** start with 0.9 (90% history, 10% current) and tune based on visual quality

### Bugs to Watch For

- Ghosting on specular highlights that change rapidly (reflections, water)
- Disocclusion artifacts at object edges when motion vectors are inaccurate
- Excessive blur if blend factor favors history too much
- Jitter pattern visible as sub-pixel noise if not enough temporal samples accumulate

---

## 11. Water Rendering State of the Art

### Key Findings

#### Wave Simulation

**FFT-Based Ocean Simulation (Gold Standard):**
- Based on Jerry Tessendorf's "Simulating Ocean Water" paper
- Create waves in frequency domain with specific amplitudes/phases
- Apply inverse FFT to generate height map
- Modify phases over time for animation
- Can use JONSWAP spectrum for realistic ocean wave distribution
- Less repetitive than simple sine wave sums

**For Vestige's scope (architectural walkthrough):** Simple Gerstner wave summation may be more appropriate than full FFT ocean simulation, unless large bodies of water are featured.

#### Reflection Techniques

**Screen Space Reflections (SSR):**
- Calculate in screen space using fragment shaders
- Constant-time regardless of scene complexity
- Limited to what's visible on screen (no off-screen reflections)
- Good for rivers, pools, and contained water bodies

**Planar Reflections:**
- Reflect camera below water surface, render scene from below
- Flip and apply to water surface
- Excellent quality for flat water surfaces
- Performance cost: renders the scene twice
- Limited to flat surfaces

**For Vestige:** Use SSR as primary with planar reflection as optional high-quality mode for important water features.

#### Caustics

**World-Space Projection (Recommended):**
- Reconstruct world position from depth buffer
- Project caustics texture along light direction
- Use volume bounds to mask caustic areas

**Quality Enhancements:**
- Dual-texture layering: two caustics textures at different speeds/scales, combined with `min` blending
- Chromatic aberration: sample caustics 3x with UV offsets for R/G/B channels
- Luminance masking: reduce caustics in shadowed areas
- Edge softening: fade based on distance from volume center

**Performance:** Single texture sample with math transforms -- minimal fill-rate impact.

#### Foam

Three independent foam layers for realism:
1. **Wave-breaking whitecaps** -- appear on wave crests
2. **Ambient surface foam** -- general surface detail
3. **Shoreline foam** -- where water meets geometry
Each layer has its own textures, colors, and coverage controls.

#### Underwater Effects

- Underwater fog with depth-based color absorption
- Animated screen-space distortion (post-process)
- Smooth waterline transitions using per-pixel depth detection
- Ocean floor caustics using animated caustic patterns projected onto terrain

### Sources

- [Simulating Ocean Water - Tessendorf](https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf)
- [Water Caustics - NVIDIA GPU Gems](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics)
- [Real-Time Caustics - Martin Renou](https://medium.com/@martinRenou/real-time-rendering-of-water-caustics-59cda1d74aa)
- [Rendering Realtime Caustics - Alexander Ameye](https://ameye.dev/notes/realtime-caustics/)
- [Simplest Pretty Water in OpenGL](https://medium.com/@vincehnguyen/simplest-way-to-render-pretty-water-in-opengl-7bce40cbefbe)
- [FFT Ocean Simulation - OpenGL](https://github.com/nasa-gibbs/Ocean-Simulation)
- [Real-Time Underwater Spectral Rendering - 2024](https://onlinelibrary.wiley.com/doi/10.1111/cgf.15009)
- [Water Rendering and Simulation - vterrain.org](http://vterrain.org/Water/)

### Vestige Recommendations

1. **Use Gerstner waves** for small-medium water bodies (pools, rivers in temples)
2. **Implement SSR** as the primary reflection method -- constant cost and works well for contained water
3. **Add world-space caustic projection** with dual-texture layering and chromatic aberration
4. **Implement underwater fog** and screen-space distortion as post-process effects
5. **Use depth-based foam** near shoreline geometry
6. **Reserve FFT ocean simulation** for large water scenes if/when needed
7. **Use `GL_TEXTURE_2D` with wrapping** for caustic textures to cover large areas seamlessly
8. **Render water as a separate pass** with its own shader and blending configuration

### Bugs to Watch For

- Z-fighting between water surface and submerged geometry
- SSR artifacts at screen edges (reflections suddenly cut off)
- Caustics appearing on surfaces above water (need depth-based masking)
- Foam texture tiling visible at shallow viewing angles

---

## 12. Particle System Performance

### Key Findings

#### GPU Compute Shader Approach (Recommended)

**Performance Results (2025):**
- Up to 168,600 particles/ms throughput
- Consistent 5.7-6.0 ms frame times from 10,000 to 1,000,000 particles
- Interactive frame rates (>30 FPS) with up to 500,000 particles
- 1 million particles still responsive

**Structure of Arrays (SoA) layout:**
- 30-45% improved computation throughput over Array of Structures
- GPU compute shaders benefit enormously from coalesced memory access
- Store position, velocity, color, life in separate buffers

**Spatial Hashing:**
- Count Sort + Parallel Prefix Scan for O(n) neighborhood search
- Critical for particle-particle and particle-geometry collision
- Dramatically faster than brute-force O(n^2)

#### Single-Pixel Particle Rendering (Compute Shader)

A specialized technique for rendering many small particles:
- Render single-pixel particles via compute shader instead of rasterization
- **31-350% faster** than rasterization depending on configuration
- Particles smaller than a pixel are proportionally darkened
- Particles larger than a pixel are proportionally brightened
- Uses atomic operations for additive blending (no hardware blend support in compute)
- Hi-Z depth buffer optimization: query 1/16th resolution mip for fast depth rejection

**Atomic blending workaround:**
- HDR half-float textures lack atomic operations
- Convert colors to integer, pack channels into uint32s
- Split RGB across two uint32s for sufficient precision
- Use `imageAtomicAdd` on the packed integers

#### GPU Particle Pipeline

1. **Emit:** Compute shader spawns new particles based on emitter state
2. **Simulate:** Compute shader updates position/velocity/life for all particles
3. **Sort:** Optional -- back-to-front sorting for alpha-blended particles via bitonic sort
4. **Compact:** Stream compaction to remove dead particles (prefix sum)
5. **Render:** Draw surviving particles using indirect draw count

#### Adaptive Particle System (2024)

Recent research (Onufriienko, 2024) demonstrates adaptive particle systems where:
- Particle counts dynamically adjust based on screen coverage and importance
- LOD for particles: reduce simulation detail for distant particles
- Compute shader handles all adaptation logic on GPU

### Sources

- [Compute Shader Particle Rendering - Mike Turitzin](https://miketuritzin.com/post/rendering-particles-with-compute-shaders/)
- [GPU Particle System - OpenGL 4.5](https://github.com/Crisspl/GPU-particle-system)
- [GPU Particle System with Indirect Rendering](https://github.com/diharaw/gpu-particle-system)
- [GPU-Based Particle Simulation - Wicked Engine](https://wickedengine.net/2017/11/gpu-based-particle-simulation/)
- [Adaptive Particle System - Onufriienko 2024](https://science.lpnu.ua/mmc/all-volumes-and-issues/volume-11-number-1-2024/using-compute-shader-adaptive-particle-system)
- [Large-Scale Particle Fluid Simulation - 2025](https://www.mdpi.com/2076-3417/15/17/9706)
- [Compute Shaders - Nathan Delaire](https://nathandelaire.gitlab.io/portfolio/post/2024-07-27-devlog8-compute-shaders/)

### Vestige Recommendations

1. **Use compute shaders for the entire particle pipeline** -- emit, simulate, compact, draw
2. **Use SoA memory layout** for particle data (separate position, velocity, life buffers)
3. **Implement indirect rendering** for particles -- compute shader writes draw count
4. **Use prefix sum** for dead particle compaction
5. **For alpha-blended particles:** consider bitonic sort in compute or use OIT (Order-Independent Transparency)
6. **Implement Hi-Z depth test** in particle rendering compute for fast occlusion
7. **Start simple:** point sprites with texture atlas before moving to mesh particles
8. **Profile:** ensure particle system fits within the 60 FPS budget -- target < 2ms for particles

### Bugs to Watch For

- Atomic operation precision loss when packing colors into uint32
- Race conditions if `glMemoryBarrier` is missing between simulation and rendering dispatches
- Particle popping when stream compaction reorders live particles
- Depth test inverted when using compute-based particle rendering

---

## 13. Terrain Rendering Optimization

### Key Findings

#### Geometry Clipmaps

**Concept:**
- Cache terrain in a set of nested regular grids centered on the viewer
- Each level has the same grid resolution but covers a larger area
- Incrementally update grids as the viewer moves

**GPU Implementation:**
- Use vertex textures to read height data in the vertex shader
- Nearly all computation on GPU, minimal CPU involvement
- Update only the changed strips when the viewer moves

**Seamless Transitions:**
- Introduce transition regions near the outer perimeter of each level
- Morph geometry and textures to interpolate to the next-coarser level
- Implemented in vertex/pixel shaders for smooth LOD blending

#### GPU Tessellation Approach

- Use tessellation shaders to generate triangle meshes from control points
- Different tessellation levels for different LOD
- Eliminate cracks by matching outer tessellation factors on shared edges
- Works well with modern OpenGL 4.0+ tessellation pipeline

#### Concurrent Binary Tree (CBT)

Modern GPU-friendly data structure for bisection-based terrain tessellation:
- Supports adaptive triangulations over square domains
- Can be extended to arbitrary polygon meshes
- Acts as a memory pool manager for efficient GPU allocation
- Capable of rendering planetary-scale geometry

#### Dynamic Tile-Map (2025)

Novel method for crack-free terrain rendering:
- Uses tile subindex information to construct adjacency maps
- Significantly reduces search space for neighboring tiles
- Eliminates cracks between different LOD levels automatically

#### Virtual Texturing

For terrain textures specifically:
- Sparse texture representation -- not all data needs to be resident
- Reduces driver state changes by consolidating into a virtual texture atlas
- Address translation + caching + streaming for on-demand loading
- Particularly important for large terrains with unique texturing

#### Heightmap Updates

- Use FBOs with shaders for procedural heightmap updates
- Upload only changed strips from pre-computed heightmaps as the camera moves
- Gives the illusion of seamless, never-ending terrain

### Sources

- [NVIDIA - GPU Geometry Clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
- [ARM - Terrain with Geometry Clipmaps](https://arm-software.github.io/opengl-es-sdk-for-android/terrain.html)
- [Geometry Clipmaps - Mike Savage](https://mikejsavage.co.uk/geometry-clipmaps/)
- [GPU Tessellation Terrain - ResearchGate](https://www.researchgate.net/publication/313230763_Geometry_Clipmaps_Terrain_Rendering_Using_Hardware_Tessellation)
- [Dynamic Tile-Map Generation 2025](https://onlinelibrary.wiley.com/doi/10.4218/etrij.2024-0496)
- [SIGGRAPH 2024 - Real-Time Rendering Advances](https://advances.realtimerendering.com/s2024/index.html)

### Vestige Recommendations

1. **For architectural walkthroughs:** terrain may be modest in scope -- start with a simple heightmap mesh with 2-3 LOD levels
2. **Use geometry clipmaps** if large outdoor areas are needed (e.g., temple grounds)
3. **Implement tessellation-based LOD** for smooth transitions without cracks
4. **Match outer tessellation factors** on shared edges to prevent cracks between LOD levels
5. **Use texture splatting** (blend 4 terrain textures by weight) for efficient terrain materials
6. **Consider virtual texturing** only if terrain area exceeds what fits in VRAM
7. **Use compute shaders** for heightmap-based normal generation rather than CPU
8. **Profile vertex shader cost** -- terrain vertices can be expensive if displacement maps are used

### Bugs to Watch For

- T-junction cracks at LOD boundaries (must match tessellation levels)
- Texture stretching on steep terrain slopes (use triplanar mapping)
- Floating-point precision issues for large-scale terrain (use camera-relative rendering)
- Heightmap seam artifacts at clipmap update boundaries

---

## 14. PBR Rendering Correctness

### Key Findings

#### Energy Conservation Fundamentals

**The core rule:** A surface cannot reflect more light than it receives.

In microfacet BRDFs, rough surfaces should have:
- **Larger** reflection highlights (more scattered)
- **Dimmer** peak intensity (same total energy spread over larger area)

Many older rendering systems got this wrong -- producing overly bright rough surfaces or overly dark smooth ones.

#### Common PBR Mistakes

**1. Missing Multiscatter Compensation (CRITICAL):**
- Standard GGX microfacet BRDF only models single-bounce scattering
- At roughness = 1.0, up to **60% of energy is lost**
- This makes rough metals appear too dark
- **Solution:** Kulla-Conty energy compensation

**2. Incorrect Fresnel:**
- Using Schlick approximation without accounting for roughness
- F0 values should come from IOR tables, not arbitrary artist input
- Common F0: dielectrics ~0.04, metals 0.5-1.0 (varies by metal)
- Grazing angle reflections should approach 1.0 for all materials

**3. Diffuse-Specular Balance:**
- Energy not reflected specularly should contribute to diffuse
- `kD = (1.0 - F) * (1.0 - metallic)` -- metals have no diffuse
- Incorrect balance causes energy creation or destruction

**4. Linear vs. sRGB Confusion:**
- All PBR calculations must happen in linear space
- sRGB textures must be loaded with `GL_SRGB8_ALPHA8` or manually converted
- Output must be converted to sRGB in the final pass (or use `GL_FRAMEBUFFER_SRGB`)
- HDR intermediate buffers should use `GL_RGBA16F` (always linear)

**5. Incorrect Normal Distribution Function (NDF):**
- GGX/Trowbridge-Reitz is the standard
- Must use the correct roughness remapping: `alpha = roughness * roughness`
- Some implementations forget this squaring, causing incorrect roughness response

**6. Incorrect IBL (Image-Based Lighting):**
- Split-sum approximation must match the direct lighting BRDF
- Pre-filtered environment map mip levels must correspond to roughness
- BRDF LUT must be generated with the same BRDF model used for direct lighting
- Mismatched approximations cause visible seams between direct and indirect lighting

#### Kulla-Conty Energy Compensation

**The Problem:** Single-scatter GGX loses up to 60% of energy at high roughness.

**The Solution:** Add a secondary BRDF lobe to compensate for missing multi-bounce energy.

**Implementation:**

Two precomputed LUTs required:
1. **E(mu, roughness):** Directional energy integral, 128x128 or 32x32 texture
2. **E_avg(roughness):** Average energy, 1D texture or 32-entry array

**The MS BRDF formula:**
```
f_ms(wo, wi) = (1 - E(wo)) * (1 - E(wi)) / (PI - E_avg)
```

**For metals (Fresnel-dependent):**
```
F_ms(F0) = 0.04 * F0 + 0.66 * F0^2 + 0.3 * F0^3
```

**GLSL implementation sketch:**
```glsl
float MSBRDF(float roughness, float NdotV, float NdotL,
             sampler2D E_lut, sampler2D E_avg_lut)
{
    float E_o = 1.0 - texture(E_lut, vec2(NdotV, roughness)).r;
    float E_i = 1.0 - texture(E_lut, vec2(NdotL, roughness)).r;
    float E_avg = texture(E_avg_lut, vec2(roughness, 0.5)).r;
    return E_o * E_i / max(0.001, PI - E_avg);
}
```

**Key notes:**
- MS term only becomes significant at roughness > 0.8
- Overhead is minimal (two texture lookups)
- Both GGX specular and Oren-Nayar diffuse need compensation
- Must be verified with a white furnace test: sphere in uniform white environment should reflect exactly as much light as it receives

### Sources

- [PBR Theory - LearnOpenGL](https://learnopengl.com/PBR/Theory)
- [Marmoset - Basic PBR Theory](https://marmoset.co/posts/basic-theory-of-physically-based-rendering/)
- [Physically Based Rendering Book (4th Ed)](https://pbr-book.org/)
- [Energy Compensation Pt. 1 - PataBlog](https://patapom.com/blog/BRDF/MSBRDFEnergyCompensation/)
- [Energy Compensation Pt. 2 - PataBlog](https://patapom.com/blog/BRDF/MSBRDFEnergyCompensation2/)
- [Multiple-Scattering BRDF - PataBlog](https://patapom.com/blog/BRDF/MSBRDF/)
- [Turquin - Practical Multiscatter Compensation (PDF)](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf)
- [JCGT - Revisiting Kulla-Conty](https://jcgt.org/published/0008/01/03/paper.pdf)
- [Google Filament Renderer Documentation](https://google.github.io/filament/Filament.html)
- [Kulla-Conty IBL Implementation](https://github.com/wyzwzz/KullaConty-IBL)
- [Enterprise PBR Shading Model - Dassault](https://github.com/DassaultSystemes-Technology/EnterprisePBRShadingModel/blob/master/spec-2022x.md.html)

### Vestige Recommendations

1. **Implement Kulla-Conty energy compensation** -- precompute the two LUTs at engine startup, add the MS lobe to the PBR shader
2. **Implement a white furnace test** as an automated visual test -- sphere should be invisible against a white background
3. **Ensure alpha = roughness^2** in the GGX NDF
4. **Load albedo/color textures as `GL_SRGB8_ALPHA8`** -- other maps (normal, roughness, metallic) must be `GL_RGBA8` (linear)
5. **Verify F0 values** for all materials against published IOR tables
6. **Ensure diffuse-specular balance:** `kD = (1.0 - F) * (1.0 - metallic)`
7. **Match IBL split-sum** with direct lighting BRDF model
8. **Test in multiple lighting conditions** -- PBR should look correct everywhere, not just in one specific setup

### Bugs to Watch For

- Rough metals appearing too dark (missing multiscatter compensation)
- Black specular at grazing angles (incorrect Fresnel)
- Energy creation (surfaces appearing brighter than the light illuminating them)
- sRGB textures loaded as linear (everything too bright and washed out)
- Roughness response feeling wrong (forgetting alpha = roughness^2)
- IBL and direct lighting producing visibly different material appearance

---

## Summary: Top Priority Actions for Vestige

### Immediate (High Impact, Low Risk)
1. Audit and convert all GL calls to DSA equivalents
2. Enable debug callback with synchronous output in debug builds
3. Add RAII wrappers for all GL resources with leak detection
4. Implement Kulla-Conty energy compensation for PBR correctness
5. Add texel snapping to CSM for shadow stability

### Near-Term (High Impact, Moderate Effort)
6. Implement persistent mapped buffers with triple buffering
7. Move to Multi-Draw Indirect for scene rendering
8. Implement compute shader frustum culling
9. Add variance clipping (YCoCg) to TAA
10. Fix any SMAA coordinate system issues

### Future (Strategic Improvements)
11. Implement SDSM via compute shader depth reduction
12. Add k-DOP clipping for TAA (upgrade from variance clipping)
13. Implement frame allocator and pool allocator
14. Explore GL_ARB_bindless_texture and GL_ARB_indirect_parameters
15. Implement GPU particle pipeline with compute shaders
