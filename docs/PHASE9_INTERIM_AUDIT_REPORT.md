# Phase 9 Interim Audit Report (Post 9A-9D)

**Date:** 2026-04-10
**Scope:** Full codebase (~83K lines), all tiers
**Test suite:** 1513/1513 pass (1 GPU-dependent test skipped)
**Build:** 0 warnings from project code (external-only: OpenAL Soft, Recast/Detour, PipeWire headers)

---

## Summary

| Severity | Count |
|----------|-------|
| Critical | 10 |
| High     | 16 |
| Medium   | 22 |
| Low      | 12 |
| **Total** | **60** |

---

## Critical Findings

### C1. Cloth wind noise repeated across all substeps
**File:** `engine/physics/cloth_simulator.cpp:1756-1796`
**Issue:** `applyWind()` is called once per substep. For FULL quality, each particle computes 3x `hashNoise()` calls. With 400 particles and 10 substeps = **12,000 hashNoise calls per cloth per frame**. With multiple cloth panels (Tabernacle curtains/veils), this reaches 60,000+ calls. However, the time value `t = m_elapsed` is constant across all substeps within a single `simulate()` call, so the noise values are identical every substep.
**Fix:** Move per-particle perturbation computation **outside** the substep loop. Compute once, store in a per-particle buffer, apply in each substep. ~90% reduction in wind noise CPU cost.
**Complexity:** Medium (~30 lines)

### C2. Triple scene rendering for water reflection/refraction
**File:** `engine/core/engine.cpp:1029-1065`
**Issue:** The entire scene is rendered 3x per frame: main pass + reflection pass + refraction pass (at 25% resolution). This roughly triples draw call count and is likely the primary source of reported slowdown.
**Fix:** (a) Frustum-cull water plane before enabling extra passes, (b) temporal amortization — render reflection every 2nd frame, (c) LOD-cull reflection pass to nearest N objects, (d) consider SSR for distant water.
**Complexity:** Medium (~40 lines)

### C3. Water fragment shader: 9-15 noise evaluations per fragment at FULL quality
**File:** `assets/shaders/water.frag.glsl:141-173`
**Issue:** At quality tier 0 (FULL): 3x `waterFbm3()` calls (3 octaves each) = 9 noise evaluations. Each `noised()` involves floor, fract, quintic interpolation, 4 hash lookups, 4 dot products. For a full-screen water surface at 1080p: ~18M noise evaluations per frame.
**Fix:** Default to APPROXIMATE tier (4 noise evals). Add distance-based quality falloff (FULL near camera, APPROXIMATE far). The `u_waterQualityTier` uniform is already wired.
**Complexity:** Small (~10 lines)

### C4. Per-triangle aerodynamic drag in substep inner loop
**File:** `engine/physics/cloth_simulator.cpp:1799-1843`
**Issue:** For every triangle per substep: compute centroid (3 adds + divide), hashNoise, cross product, dot product, normalize. A 20x20 grid has ~722 triangles. 10 substeps = 7,220 iterations per cloth per frame, each with spatial noise that produces near-identical values across substeps.
**Fix:** Pre-compute per-triangle wind velocity (with spatial noise) once before substep loop. Cache `windVel[tri]`. Apply precomputed wind in each substep.
**Complexity:** Medium (~25 lines)

### C5. PCSS shadow: 32 shadow map texture reads per object
**File:** `assets/shaders/scene.frag.glsl:516-541`
**Issue:** `blockerSearch()` performs 16 texture reads, then PCF does 16 more samples. Rotation matrix computed twice. Total: 32 shadow map samples per cascade per fragment.
**Fix:** Reduce to 8+8 samples, or cache blocker search results. Pre-compute rotation matrix once.
**Complexity:** Medium (~20 lines)

### C6. Normal recomputation every frame without dirty tracking
**File:** `engine/physics/cloth_simulator.cpp:1573-1612`
**Issue:** `recomputeNormals()` iterates ALL vertices and triangles every frame, even when cloth is sleeping. 400 particles + 722 triangles = ~1,500 operations per cloth per frame.
**Fix:** Add dirty flag — only recompute when cloth positions actually changed. Skip if cloth is in sleep state.
**Complexity:** Small (~5 lines)

### C7. Rule-of-Five violations — 7 GPU resource classes missing move semantics
**Files:**
- `engine/renderer/instance_buffer.h` — owns VBO
- `engine/renderer/light_probe.h` — owns VAO, VBO, FBO, RBO, textures
- `engine/renderer/point_shadow_map.h` — owns FBO, cubemap
- `engine/renderer/cascaded_shadow_map.h` — owns FBO, texture array
- `engine/renderer/color_grading_lut.h` — owns GLuint textures
- `engine/renderer/font.h` — owns atlas texture, FreeType resources
- `engine/renderer/water_fbo.h` — owns two FBO setups

**Issue:** All have destructors but no move constructor/assignment. Cannot be safely used in STL containers, prevents resource transfer.
**Fix:** Add move constructor + move assignment to each (nullify source GPU IDs).
**Complexity:** Medium (~15 lines per class, 7 classes)

### C8. Water samplerCube u_environmentMap may lack fallback on Mesa AMD
**File:** `assets/shaders/water.frag.glsl:7`, `engine/renderer/water_renderer.cpp:124`
**Issue:** Shader declares `samplerCube u_environmentMap`. When `u_hasEnvironmentMap = false`, the shader skips it logically, but Mesa AMD driver requires ALL declared samplers to have valid textures bound. No fallback cubemap binding visible in water render path.
**Fix:** Bind `m_fallbackCubemap` to unit 2 before water render pass.
**Complexity:** Trivial (~2 lines)

### C9. Divide-by-zero in water_renderer normal normalization
**File:** `engine/renderer/water_renderer.cpp:308-311`
**Issue:** `float len = std::sqrt(nx*nx + ny*ny + nz*nz);` then divides by `len` without zero check. In flat areas or NaN propagation, produces NaN in texture data.
**Fix:** Guard with `if (len > 0.0f)` before division.
**Complexity:** Trivial (~2 lines)

### C10. Divide-by-zero in cloth wind direction interpolation
**File:** `engine/physics/cloth_simulator.cpp:1729`
**Issue:** `dirDiff * (dirStep / dirLen)` — if target equals current direction, `dirLen = 0`, division produces NaN that persists in `m_windDirOffset` state.
**Fix:** Guard with `if (dirLen > 1e-6f)`.
**Complexity:** Trivial (~2 lines)

---

## High Findings

### H1. glPolygonMode toggled inside render loops
**File:** `engine/renderer/renderer.cpp:1354-1368, 2599-2728`
**Issue:** `glPolygonMode()` set/restored per mesh inside render loops (4 locations). Excessive state changes in hot path.
**Fix:** Batch wireframe objects separately, toggle mode once per batch.
**Complexity:** Medium (~30 lines)

### H2. Foliage render loop push_back per-frame allocation
**File:** `engine/renderer/foliage_renderer.cpp:80-129, 232-277`
**Issue:** `m_visibleByType[typeId].push_back(inst)` accumulates in hot culling loop. Dynamic resizing each frame.
**Fix:** Pre-reserve or reuse vectors with `clear()` (preserves capacity).
**Complexity:** Small (~5 lines)

### H3. Event subscription without unsubscribe in destructor
**File:** `engine/renderer/renderer.cpp:115-118`
**Issue:** Subscribes to `WindowResizeEvent` in constructor. Destructor doesn't unsubscribe. If EventBus outlives Renderer, callback captures dangling `this`.
**Fix:** Store subscription ID, unsubscribe in destructor.
**Complexity:** Small (~5 lines)

### H4. Stochastic tiling: 7 texture lookups per material
**File:** `assets/shaders/scene.frag.glsl:686-700`
**Issue:** `textureStochastic()` samples same texture 3 times per call. Called for every material texture (diffuse, normal, metallic, roughness, AO, emissive). Up to 21+ texture operations for fully-textured surfaces.
**Fix:** Add quality tier to disable stochastic tiling at lower quality, or reduce to 2 samples.
**Complexity:** Small (~10 lines)

### H5. Triplanar mapping: 6 texture operations per fragment
**File:** `assets/shaders/terrain.frag.glsl:211-235`
**Issue:** When enabled on steep slopes: 3 normal map reads + 3 detail computations per fragment.
**Fix:** Reduce to 2-axis triplanar on lower quality tiers.
**Complexity:** Small (~10 lines)

### H6. SSAO TBN construction per fragment (3 normalizations)
**File:** `assets/shaders/ssao.frag.glsl:59-67`
**Issue:** Gram-Schmidt TBN per fragment before 64-sample loop. 3 normalization operations.
**Fix:** Use depth-only derivatives approach (faster, avoids TBN entirely).
**Complexity:** Medium (~20 lines)

### H7. glTF loader missing file size validation
**File:** `engine/utils/gltf_loader.cpp:1397-1401`
**Issue:** No file size limit before loading via tinygltf. OBJ loader has 256MB limit. Large malformed glTF files can cause OOM.
**Fix:** Add `std::filesystem::file_size()` check before `tinygltf::TinyGLTF::LoadASCIIFromFile()`.
**Complexity:** Small (~5 lines)

### H8. glTF loader unchecked array access in generateFlatNormals
**File:** `engine/utils/gltf_loader.cpp:404-420`
**Issue:** `vertices[indices[i]]` without bounds check that `indices[i] < vertices.size()`.
**Fix:** Add bounds validation before indexing.
**Complexity:** Small (~5 lines)

### H9. Unchecked animation sampler indexing
**File:** `engine/utils/gltf_loader.cpp:1267`
**Issue:** `gltfAnim.samplers[channel.sampler]` without bounds check.
**Fix:** Bounds check before access.
**Complexity:** Trivial (~2 lines)

### H10. Cloth grid dimension unbounded — no maximum size
**File:** `engine/physics/cloth_simulator.cpp:43`
**Issue:** `initialize()` allows arbitrarily large grids. 100000x100000 = 10B particles = OOM crash.
**Fix:** Clamp to reasonable maximum (e.g., 256x256 per SECURITY.md).
**Complexity:** Trivial (~2 lines)

### H11. Sphere/cylinder collider radius not validated
**File:** `engine/physics/cloth_simulator.cpp:524-560`
**Issue:** `addSphereCollider()` doesn't validate `radius > 0`. Zero/negative radius = degenerate collision.
**Fix:** Clamp radius to minimum positive value.
**Complexity:** Trivial (~2 lines)

### H12. Model primitive indexing after negative-to-size_t cast
**File:** `engine/resource/model.cpp:88,109`
**Issue:** `static_cast<size_t>(primIdx)` without first checking `primIdx >= 0`. Negative value wraps to huge size_t.
**Fix:** Add `primIdx >= 0` guard.
**Complexity:** Trivial (~2 lines)

### H13. C-style casts in hierarchy_panel drag-drop
**File:** `engine/editor/panels/hierarchy_panel.cpp:89,583`
**Issue:** `*(const uint32_t*)payload->Data` — should use `reinterpret_cast`.
**Fix:** Replace with `*reinterpret_cast<const uint32_t*>(payload->Data)`.
**Complexity:** Trivial (~2 lines)

### H14. Morph target division-by-zero risk
**File:** `engine/utils/gltf_loader.cpp:1323`
**Issue:** `accessor.count / animChannel.timestamps.size()` — if timestamps empty, division by zero.
**Fix:** Guard with empty check.
**Complexity:** Trivial (~2 lines)

### H15. Plane normal division-by-zero in collision
**File:** `engine/physics/cloth_simulator.cpp:548`
**Issue:** `normal / len` in `addPlaneCollider()` — len could be zero/epsilon.
**Fix:** Guard against zero-length normal.
**Complexity:** Trivial (~2 lines)

### H16. GPU resource leak on shader failure in environment_map
**File:** `engine/renderer/environment_map.cpp:119-133`
**Issue:** FBO/RBO created before shader loading. If shader fails, FBO/RBO leak (never freed).
**Fix:** Validate shaders first, or cleanup on failure path.
**Complexity:** Small (~10 lines)

---

## Medium Findings

### M1. Flutter sin() recomputed on every wind query
`engine/environment/environment_forces.cpp:222-223` — Cache in `update()`.

### M2. fastInvSqrt exists but unused for normal normalization
`engine/physics/cloth_simulator.cpp:1599-1611` — Use existing `fastInvSqrt()` (defined at line 15).

### M3. calculateTangents every frame regardless of material
`engine/physics/cloth_component.cpp:90` — Skip if no normal map bound.

### M4. Duplicate gust state machine per cloth panel
`engine/physics/cloth_simulator.cpp:1645-1730` — Sample from EnvironmentForces with phase offset.

### M5. pow(x, 128) in water specular
`assets/shaders/water.frag.glsl:259` — Replace with repeated squaring (7 multiplies).

### M6. SSAO projection matrix fetch 64 times
`assets/shaders/ssao.frag.glsl:34-35` — Extract `projection[3][2]` once before sample loop.

### M7. Redundant discard in foliage shader
`assets/shaders/foliage.frag.glsl:160` — Second alpha discard redundant after first at line 110.

### M8. strncpy usage (3 locations)
`engine/editor/panels/hierarchy_panel.cpp:115,603`, `inspector_panel.cpp:201` — Use `std::string_view` or safe copy.

### M9. waterItems vector heap-allocated per frame
`engine/core/engine.cpp:999-1004` — Reuse member vector with `clear()`.

### M10. Degree-to-radian conversion every frame
`engine/renderer/water_renderer.cpp:179-188` — Pre-compute radians at config load.

### M11. SSAO noise texture fallback when disabled
`assets/shaders/ssao.frag.glsl:8` — Ensure fallback bound when SSAO off (Mesa AMD).

### M12. Contact shadow texture fallback
`engine/renderer/renderer.cpp:1075` — Ensure fallback when contact shadows disabled (Mesa AMD).

### M13. FBO bind without completeness enforcement
`engine/renderer/framebuffer.cpp:62-64` — `bind()` logs error but still binds incomplete FBO.

### M14. glGetError only in debug builds
`engine/renderer/framebuffer.cpp:235-242` — Silent GL failures in release.

### M15. Division-by-zero in constraint force via alternate path
`engine/physics/physics_world.cpp:663` — Guard deltaTime check.

### M16. Division-by-zero in motion database velocity
`engine/animation/motion_database.cpp:178` — Guard sampleRate > 0.

### M17. KD-tree variance division by zero
`engine/animation/kd_tree.cpp:100-101` — Guard count > 0.

### M18. Unchecked shader file path validation
`engine/renderer/shader.cpp:71-90` — No path traversal prevention.

### M19. Audio file size limits not enforced
`engine/audio/audio_clip.cpp:71-165` — No maximum file size check.

### M20. EnvironmentPanel unused parameter
`engine/editor/panels/environment_panel.cpp:17` — `history` parameter unused.

### M21. EnvironmentPanel const-correctness
`engine/editor/panels/environment_panel.cpp:14` — `FoliageManager&` should be const.

### M22. cppcheck: condition m_camera always true
`engine/core/engine.cpp:127` — Redundant null check.

---

## Low Findings

### L1-L3. cppcheck style: could-use-STL-algorithm (8 locations)
Various files — `std::all_of`, `std::find_if`, `std::transform` suggestions.

### L4. cppcheck: duplicateAssignExpression arkW/arkH
`engine/core/engine.cpp:2472-2473` — Both assigned `1.5f * C`. Intentional? Verify dimensions.

### L5-L7. cppcheck: functionStatic (4 locations)
`AudioAnalyzer::computeFFT`, `Timer::getElapsedTime`, `Window::pollEvents`, `Editor::setupTheme`.

### L8. cppcheck: constVariablePointer/Reference (~20 locations)
Various editor/engine files — local variables could be const.

### L9. Cloth adaptive damping uses sqrt instead of squared length
`engine/physics/cloth_simulator.cpp:244-253` — Use squared comparison.

### L10. BFS adjacency list allocation per sort
`engine/physics/cloth_simulator.cpp:1510-1519` — Cache allocation.

### L11. Test hardcoded /tmp path
`tests/test_obj_loader.cpp:27` — Use `std::filesystem::temp_directory_path()`.

### L12. Build system: lib/ directory not auto-created
`CMakeLists.txt` — `libvestige_engine.a` output directory missing, causing test build failure on clean builds.

---

## Fix Plan (Grouped by Batch)

### Batch 1: Performance — Wind/Water/Cloth (Critical)
*Addresses: C1, C2, C3, C4, C6, M1, M2, M3, M4, M9, M10, L9*
- Move wind perturbation outside substep loop (C1, C4)
- Add dirty flag to normal recomputation (C6)
- Cache flutter sin() in `update()` (M1)
- Use existing `fastInvSqrt()` for normals (M2)
- Skip tangents if no normal map (M3)
- Default water quality to APPROXIMATE (C3)
- Frustum-cull water plane before reflection/refraction passes (C2)
- Reuse waterItems vector (M9)
- Pre-compute degree-to-radian (M10)

### Batch 2: Performance — Shaders
*Addresses: C5, H4, H5, H6, M5, M6, M7*
- Reduce PCSS to 8+8 samples, cache rotation matrix (C5)
- Add quality tier for stochastic tiling (H4)
- 2-axis triplanar at lower quality (H5)
- SSAO optimization (H6, M6)
- Water specular repeated squaring (M5)
- Remove redundant foliage discard (M7)

### Batch 3: Safety — Divide-by-Zero Guards
*Addresses: C9, C10, H11, H14, H15, M15, M16, M17*
- Add zero-length guards to all identified division sites

### Batch 4: Memory — Rule-of-Five & Resource Leaks
*Addresses: C7, H3, H16*
- Add move semantics to 7 GPU resource classes
- Add event unsubscribe in Renderer destructor
- Fix environment_map shader failure leak path

### Batch 5: Security — Input Validation
*Addresses: H7, H8, H9, H10, H12, H13, M18, M19*
- glTF file size limit
- Bounds checks on array accesses
- Cloth grid size cap
- Collider validation
- C-style cast replacement

### Batch 6: Mesa AMD Driver Safety
*Addresses: C8, M11, M12*
- Bind fallback textures for water, SSAO, contact shadows

### Batch 7: Code Quality & Cleanup
*Addresses: M8, M13, M14, M20, M21, M22, L1-L12*
- strncpy replacement
- FBO bind guard
- const-correctness
- cppcheck style fixes
- Build system lib/ dir fix
