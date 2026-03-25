# Phase 6 Post-Phase Audit Report

**Date:** 2026-03-25
**Phase:** 6 — Particle and Effects System (6-1 through 6-5)
**Scope:** Full codebase (74K lines, ~240 C++ files, ~62 GLSL shaders)
**Process:** 5-tier audit per AUDIT_STANDARDS.md

---

## Summary

| Category | Critical | High | Medium | Low | Total |
|----------|----------|------|--------|-----|-------|
| Bugs & Logic | 0 | 0 | 2 | 3 | 5 |
| Memory & Resources | 1 | 2 | 2 | 0 | 5 |
| Performance | 0 | 4 | 3 | 2 | 9 |
| Security | 2 | 3 | 2 | 0 | 7 |
| Code Quality | 0 | 0 | 1 | 3 | 4 |
| Shaders | 0 | 0 | 1 | 0 | 1 |
| Build | 0 | 0 | 1 | 0 | 1 |
| **Total** | **3** | **9** | **12** | **8** | **32** |

---

## Tier 1: Automated Tools

### Compiler Warnings
- **terrain.h:171,174** — `-Wconversion`: `(m_config.width - 1) * m_config.spacingX` implicit int-to-float. Repeated across all TUs including terrain.h. **Medium — trivial fix.**

### Tests
- 497/497 passed, 1 skipped (MeshBoundsTest needs GL context).

### cppcheck
- 2 errors (both false positives — see Tier 4A verification below)
- 5 portability warnings (glTF `reinterpret_cast` — valid concern, see Security)
- ~130 style suggestions (const-correctness, STL algorithms, `functionStatic`)

---

## Critical Findings

### C1. Raw new/delete for PMR arena allocator
**File:** `engine/renderer/renderer.cpp:117-118, 126-128, 133`
**Tier:** 2 (grep) + 4B (memory)
**Issue:** `new std::pmr::monotonic_buffer_resource(...)` and `delete m_frameResource` used for per-frame arena reset. Violates SECURITY.md §1 "No raw new/delete."
**Fix:** Wrap in `std::unique_ptr`. Complexity: small.

### C2. glTF loader: unchecked buffer/accessor index access
**File:** `engine/utils/gltf_loader.cpp:310-314, 347-351, 370-374, 393-397, 421-424, 458-462`
**Tier:** 4E (security)
**Issue:** Array accesses like `gltfModel.accessors[it->second]`, `gltfModel.bufferViews[accessor.bufferView]`, `gltfModel.buffers[bufferView.buffer]` have no bounds checks. Malformed glTF could trigger out-of-bounds read.
**Fix:** Add bounds validation before each array access. Complexity: medium (~30 lines).

### C3. glTF loader: unchecked pointer arithmetic on buffer data
**File:** `engine/utils/gltf_loader.cpp:313-314, 323-326`
**Tier:** 4E (security)
**Issue:** `buffer.data.data() + bufferView.byteOffset + accessor.byteOffset` with no validation that offsets are within buffer bounds. `base + stride * i` unchecked for overflow.
**Fix:** Validate `byteOffset + byteLength <= buffer.data.size()` before access. Complexity: medium.

---

## High Findings

### H1. glTF texture URI path traversal
**File:** `engine/utils/gltf_loader.cpp:83-84`
**Tier:** 4E (security)
**Issue:** `resolveUri(gltfDir, image.uri)` concatenates paths without sanitization. Malicious glTF can reference `../../etc/passwd` or other paths outside the assets directory.
**Fix:** Canonicalize resolved path and verify it's within the assets directory. Complexity: small.

### H2. glTF scene index not validated
**File:** `engine/utils/gltf_loader.cpp:642`
**Tier:** 4E (security)
**Issue:** `gltfModel.scenes[sceneIdx]` — only checks `!gltfModel.scenes.empty()`, doesn't validate `sceneIdx < scenes.size()`. `defaultScene` value from file is trusted.
**Fix:** Clamp sceneIdx to valid range. Complexity: trivial.

### H3. OBJ loader no file size limit
**File:** `engine/utils/obj_loader.cpp:97`
**Tier:** 4E (security)
**Issue:** Opens OBJ file and reads line-by-line in a while loop with no file size check. A multi-gigabyte OBJ could exhaust memory.
**Fix:** Check file size before parsing, reject above a reasonable limit. Complexity: small.

### H4. Per-frame vector allocations in shadow pass
**File:** `engine/renderer/renderer.cpp:1628-1635, 2063`
**Tier:** 4C (performance)
**Issue:** `shadowCasterItems` vector created and populated per-frame per shadow pass. Multiple `std::vector` allocations per cascade (4 cascades × N casters).
**Fix:** Use pre-allocated member vectors with `.clear()` instead of local construction. Complexity: small.

### H5. Per-frame hash map in buildInstanceBatches
**File:** `engine/renderer/renderer.cpp:1517-1554`
**Tier:** 4C (performance)
**Issue:** `std::unordered_map` created and destroyed every frame for instance batching. Hash map allocation overhead in hot path.
**Fix:** Use persistent member map with `.clear()`, or pre-allocated flat structure. Complexity: medium.

### H6. selectShadowCastingPointLights returns vector by value
**File:** `engine/renderer/renderer.cpp:2146`
**Tier:** 4C (performance)
**Issue:** Returns `std::vector` by value per frame, causing heap allocation.
**Fix:** Accept output vector by reference, or use pre-allocated member. Complexity: small.

### H7. Per-frame culling vector allocations
**File:** `engine/renderer/renderer.cpp:1693, 1993`
**Tier:** 4C (performance)
**Issue:** `m_culledItems` and `m_sortedTransparentItems` cleared and rebuilt each frame with push_back. While .clear() preserves capacity, the transparent items vector may reallocate if count varies.
**Fix:** Use `.reserve()` with previous frame's count. Complexity: trivial.

### H8. Framebuffer move constructor missing noexcept
**File:** `engine/renderer/framebuffer.h:36`
**Tier:** 4B (memory)
**Issue:** Move constructor not marked `noexcept`. Prevents efficient use in `std::vector` and other containers.
**Fix:** Add `noexcept`. Complexity: trivial.

### H9. Shader move constructor missing noexcept
**File:** `engine/renderer/shader.h:26`
**Tier:** 4B (memory)
**Issue:** Same as H8 — move constructor should be `noexcept`.
**Fix:** Add `noexcept`. Complexity: trivial.

---

## Medium Findings

### M1. terrain.h int-to-float conversion warning
**File:** `engine/environment/terrain.h:171,174`
**Tier:** 1 (compiler)
**Issue:** `(m_config.width - 1) * m_config.spacingX` — int subtraction result implicitly converted to float.
**Fix:** `static_cast<float>(m_config.width - 1) * m_config.spacingX`. Complexity: trivial.

### M2. Water surface division by zero when gridResolution == 1
**File:** `engine/scene/water_surface.cpp:84-85`
**Tier:** 4A (bugs)
**Issue:** `static_cast<float>(res - 1)` — if `gridResolution` is set to 1 via editor, divides by zero.
**Fix:** Clamp minimum gridResolution to 2 in config setter. Complexity: trivial.

### M3. FBO completeness check continues on failure
**File:** `engine/renderer/water_fbo.cpp:23-32`, `cascaded_shadow_map.cpp:45`, `point_shadow_map.cpp:35`
**Tier:** 4B (memory)
**Issue:** FBO completeness failure is logged but initialization continues. Subsequent bind/draw to incomplete FBO causes undefined behavior.
**Fix:** Return false from init() on FBO failure. Complexity: small.

### M4. SSAO kernel uniform names built with std::to_string in loop
**File:** `engine/renderer/renderer.cpp:522`
**Tier:** 4C (performance)
**Issue:** Per-frame string allocation for uniform name construction (`"u_samples[" + std::to_string(i) + "]"`).
**Fix:** Pre-build uniform name strings once at init. Complexity: small.

### M5. Scene collectRenderData pushes to vectors per frame
**File:** `engine/scene/scene.cpp:217-247`
**Tier:** 4C (performance)
**Issue:** Multiple push_back calls for render items, lights, particles, water each frame.
**Fix:** Pre-allocate or use frame arena. Complexity: medium.

### M6. Shader info log buffer fixed at 512 bytes
**File:** `engine/renderer/shader.cpp:229, 252`
**Tier:** 4E (security)
**Issue:** `char infoLog[512]` — if error message exceeds 511 chars, truncated (but glGetShaderInfoLog handles this safely with size parameter). Not a buffer overflow, but could lose diagnostic information.
**Fix:** Query actual log length first with `glGetShaderiv(GL_INFO_LOG_LENGTH)`. Complexity: small.

### M7. gltf_loader reinterpret_cast portability
**File:** `engine/utils/gltf_loader.cpp:325, 358, 381, 405, 435`
**Tier:** 1 (cppcheck portability)
**Issue:** `reinterpret_cast<const float*>(unsigned char*)` is technically undefined behavior per strict aliasing. Works on all target platforms but not portable.
**Fix:** Use `std::memcpy` into a local float. Complexity: small.

### M8. Enum values use UPPER_SNAKE_CASE instead of PascalCase
**File:** Multiple files (AntiAliasMode, MaterialType, AlphaMode, EditorMode, etc.)
**Tier:** Previous audit (deferred)
**Issue:** Coding standards §2 requires PascalCase for enum class values. Current codebase uses UPPER_SNAKE_CASE.
**Fix:** Codebase-wide rename. Complexity: large (deferred — was already noted in previous audit).

### M9. CpuProfileScope missing explicit
**File:** `engine/profiler/cpu_profiler.h:62`
**Tier:** 1 (cppcheck)
**Issue:** Single-argument constructor not marked `explicit` per coding standards §7.
**Fix:** Add `explicit`. Complexity: trivial.

---

## Low Findings

### L1. Duplicate if-condition in engine.cpp
**File:** `engine/core/engine.cpp:591/584, 923/852`
**Tier:** 1 (cppcheck)
**Issue:** Same `if (editorActive)` condition checked twice in separate blocks. Not a bug — different code paths handle different editor states — but could be simplified.
**Fix:** Consider restructuring to avoid duplicate checks. Complexity: small.

### L2. Multiple "can be const" suggestions
**File:** ~60 occurrences across engine/editor/*.cpp
**Tier:** 1 (cppcheck)
**Issue:** Variables, parameters, and member functions that could be const-qualified.
**Fix:** Batch const-correctness pass. Complexity: medium (many files, mechanical).

### L3. STL algorithm suggestions
**File:** ~30 occurrences across engine/
**Tier:** 1 (cppcheck)
**Issue:** Raw loops that could use `std::transform`, `std::find_if`, `std::copy_if`, etc.
**Fix:** Consider converting where readability improves. Complexity: medium.

### L4. AABB::transformed shadows function name
**File:** `engine/utils/aabb.h:68`
**Tier:** 1 (cppcheck)
**Issue:** Local variable `transformed` shadows the enclosing function `transformed()`.
**Fix:** Rename local to `result` or `transformedCorner`. Complexity: trivial.

### L5. std::endl in logger
**File:** `engine/core/logger.cpp:65`
**Tier:** 2 (grep)
**Issue:** Single `std::endl` usage — unnecessary flush.
**Fix:** Replace with `"\n"`. Complexity: trivial.

### L6. Irradiance convolution shader uses float loop counter
**File:** `assets/shaders/irradiance_convolution.frag.glsl:31-33`
**Tier:** 4G (shaders)
**Issue:** `for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)` — float loop counters can have precision issues.
**Fix:** Use int loop counter with float conversion. Only affects one-time IBL generation. Complexity: trivial.

### L7. entity_factory.cpp redundant initialization
**File:** `engine/editor/entity_factory.cpp:198`
**Tier:** 1 (cppcheck)
**Issue:** Variable `entityName` initialized then immediately overwritten.
**Fix:** Remove initial value. Complexity: trivial.

### L8. inspector_panel.cpp unread variable
**File:** `engine/editor/panels/inspector_panel.cpp:1187`
**Tier:** 1 (cppcheck)
**Issue:** Variable `before` assigned but never read.
**Fix:** Remove or use. Complexity: trivial.

---

## Verified False Positives (excluded)

These findings from automated tools or subagents were verified and found to be non-issues:

1. **entity.cpp:59,77 — "Returning pointer to local variable"** — cppcheck false positive. Code gets raw pointer from `unique_ptr` BEFORE `std::move()`, so pointer remains valid after the move into `m_children`.

2. **test_command_history.cpp:244-246 — "Out of bounds access, log is empty"** — cppcheck false positive. `ASSERT_EQ(log.size(), 3u)` at line 243 aborts the test if log is empty. Lines 244-246 only execute if log has exactly 3 entries.

3. **performance_profiler.cpp:73 — "Division by zero"** — False positive. Guarded by `if (m_currentFrameTimeMs > 0.0f)` at line 71 with early return of 0.0f.

4. **particle_emitter.cpp:243 — "Array index out of bounds"** — False positive. Caller at line 133 checks `m_data.count < m_data.maxCount` before calling `spawnParticle()`.

5. **Texture unit 9 conflict (scene.frag.glsl vs screen_quad.frag.glsl)** — Not a real conflict. These shaders run in different render passes. OpenGL texture bindings are set before each shader's draw calls. Unit numbers in comments are documentation, not static assignments.

---

## Deferred from Previous Audit (still open)

These items were identified in the pre-Phase-6 audit and remain unresolved:

- Per-frame heap allocations in shadow pass (PMR arena planned — partially addressed)
- Per-glyph draw calls in text rendering (batch needed)
- Full heightmap copy on brush stroke begin (snapshot affected region only)
- ShadowMap class unused (replaced by CascadedShadowMap)
- SSR/Contact shadow FBO allocated but feature disabled
- Enum values UPPER_SNAKE_CASE → PascalCase (large rename)
- 3 floating dependency branches (ImGui, ImGuizmo, imgui-filebrowser)
- Test target lacks warning flags
- 30+ subsystems with zero test coverage
- No headless GL test infrastructure

---

## Tier 5: Online Research

### 5.1 PMR Arena Reset — Better Approach Found

**Finding:** `std::pmr::monotonic_buffer_resource` has a `release()` method that resets the bump pointer without destroying the object. The current delete/new pattern is unnecessary.

**Fix (replaces C1):**
```cpp
void Renderer::resetFrameAllocator()
{
    m_frameResource.release();  // Resets bump pointer, no heap allocation
}
```
This eliminates both the raw new/delete AND the per-frame heap round-trip. The `null_memory_resource()` upstream is correct — ensures no silent heap fallback.

**Sources:** cppreference.com, badlydrawnrod.github.io

### 5.2 glTF Loader Security — Known CVEs

| CVE | Library | Description |
|-----|---------|-------------|
| CVE-2022-3008 | TinyGLTF | Insecure file path expansion in `LoadFromString()` — DoS/RCE |
| CVE-2026-32845 | cgltf | Integer overflow in sparse accessor validation — heap over-read |
| TinyGLTF Dec 2025 | TinyGLTF | Buffer overflow via `LoadFromString()` |

**Validation checklist from Khronos glTF-Validator:**
- `ACCESSOR_TOO_LONG` — accessor extends beyond buffer view
- `ACCESSOR_INDEX_OOB` — index value exceeds vertex count
- `BUFFER_VIEW_TOO_LONG` — buffer view extends beyond buffer
- `ACCESSOR_OFFSET_ALIGNMENT` — offset not aligned to component size

All of these are missing from Vestige's loader. Findings C2, C3, H1, H2 are validated by real-world CVEs.

**Sources:** ubuntu.com/security/notices/USN-7129-1, github.com/KhronosGroup/glTF-Validator

### 5.3 Texture Unit Reuse — Confirmed Safe

Two different shader programs can safely use the same texture unit number. Texture units are global context state — whatever texture is bound to unit N at draw time is what the shader samples. The scene.frag.glsl (unit 9 = caustics) and screen_quad.frag.glsl (unit 9 = bloom) finding is confirmed as a false positive.

### 5.4 Per-Frame Uniform String Allocation — Solutions

**Immediate fix (recommended):** Pre-built static string arrays:
```cpp
static const std::array<std::string, 4> CASCADE_SPLIT_NAMES = {
    "u_cascadeSplits[0]", "u_cascadeSplits[1]",
    "u_cascadeSplits[2]", "u_cascadeSplits[3]"
};
```

**Future (Phase 7):** Use Uniform Buffer Objects (UBOs) or SSBOs for bulk array data. Essential for bone matrices — uploading 100+ matrices as individual uniforms would be prohibitively slow.

### 5.5 Phase 7 Animation — Experimental Features

**Compute shader skinning:** Skin mesh once via compute dispatch, reuse pre-skinned VBO across all render passes (shadow × 4 cascades + main + contact shadow + motion vectors = 7+ passes). Avoids re-skinning per pass. OpenGL 4.5 fully supports this.

**Dual quaternion skinning (DQS):** Eliminates "candy wrapper" artifact (volume collapse at twisted wrists/elbows). Nearly identical GPU cost to linear blend skinning. Implement LBS first, add DQS as per-mesh option.

**Animation compression (ACL):** State-of-the-art library, now default in UE 5.3. Key techniques:
- 48-bit quaternion quantization (128-bit → 48-bit, 0.000043 error)
- Catmull-Rom curve fitting (661 frames → 90 frames)
- Cache-friendly contiguous keyframe layout

**SSBOs for bone matrices:** Essential — UBOs are limited to ~16 KB (250 bones max). SSBOs have no practical limit. Upload all skeleton matrices for all entities into a single large SSBO.

**Sources:** wickedengine.net, github.com/nfrechette/acl, technology.riotgames.com, kavan07skinning
