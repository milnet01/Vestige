# Vestige Engine — Comprehensive Code Audit Report

**Date:** 2026-03-25
**Scope:** Full codebase (74K lines, 240 C++ files, 62 GLSL shaders)
**Audited by:** 7 parallel agents covering renderer, core/scene/environment, editor, shaders, tests/build, and online research

---

## Summary

| Category | Critical | High | Medium | Low | Total |
|----------|----------|------|--------|-----|-------|
| Bugs | 2 | 6 | 8 | 5 | 21 |
| Memory / Resources | 0 | 0 | 0 | 5 | 5 |
| Performance | 0 | 1 | 5 | 4 | 10 |
| Security | 0 | 0 | 4 | 3 | 7 |
| Dead Code | 0 | 0 | 5 | 6 | 11 |
| Standards Compliance | 0 | 1 | 1 | 8 | 10 |
| Build / Test | 0 | 2 | 6 | 2 | 10 |
| **Total** | **2** | **10** | **29** | **33** | **74** |

---

## Fixes Implemented (28 files, +132/-100 lines)

### Critical (Fixed)
1. **ParticleData::resize wipes live particles** — `resize()` reset `count = 0`, killing all active particles when config changed at runtime. Fixed to preserve count up to new capacity.
2. **Foliage deserialization crash on malformed JSON** — `chunkJson["gridX"].get<int>()` throws if key missing. Fixed with `.value()` and `.contains()` checks across foliage_manager.cpp and foliage_chunk.cpp (foliage, scatter, tree instances).

### High (Fixed)
3. **EventBus copies entire listener vector per publish** — Every `publish()` call heap-allocated a vector copy. Fixed with index-based iteration using captured size.
4. **Timer FPS uses clamped delta** — FPS counter accumulated clamped deltaTime (max 0.25s), giving wrong FPS after breakpoint stalls. Fixed to use raw elapsed time.
5. **PerformanceProfiler divides by HISTORY_SIZE** — Average frame time divided by 300 even when only 10 slots filled, giving artificially low averages. Fixed to count valid entries.
6. **GpuTimer pass names from wrong frame** — Single `m_passNames[]` array shared across triple-buffered frames. If pass order changed, names would mismatch timing data. Fixed with per-buffer pass names.
7. **scene.vert.glsl #version 460** — Originally flagged as incompatible with GL 4.5 target, but `gl_BaseInstance` requires GLSL 460 on Mesa even with `GL_ARB_shader_draw_parameters`. Kept at 460 (user's GPU supports GL 4.6).

### Medium (Fixed)
8. **Command history dirty tracking corruption** — `m_savedVersion -= trimCount` was mathematically wrong (version counter ≠ command index). Fixed with `m_savedVersionLost` flag.
9. **Version string mismatch** — main.cpp said "v0.3.0", engine.cpp said "v0.5.0". Fixed main.cpp to match.
10. **Prefab filename sanitization incomplete** — Missing null byte removal, `..` traversal check, leading/trailing dot/space stripping. Fixed.
11. **Texture size_t→int truncation** — `stbi_load_from_memory` receives `int` size; could silently wrap for >2GB data. Added bounds check.
12. **Dead uniforms wasting CPU calls** — `u_maxLodLevels` (2 shaders + 2 C++ calls), `u_cascadeTexelSize[4]` (1 shader + 1 C++ call), `u_texelSize` (1 shader). Removed all.
13. **Dead code in contact_shadows.frag.glsl** — 3 linearized depth computations (3 wasted texture fetches per fragment) never used. Removed.
14. **Dead template functions in gltf_loader.cpp** — `getAccessorData<T>` and `readStrided<T>` never called. Removed.
15. **CpuProfiler copies frame vector** — `m_lastFrame = m_currentFrame` copied; fixed to `std::move()`.

### Low (Fixed)
16. **Shader naming conventions** — `debug_line` shaders used `aPos`/`vColor` instead of `a_position`/`v_color`. `material_preview` shaders used `aPos`/`vWorldPos` instead of `a_position`/`v_worldPos`. All fixed to match codebase conventions.

---

## Issues Identified But NOT Fixed (Deferred)

### Performance (Deferred — requires architectural changes)
- **Per-frame heap allocations in shadow pass** — Multiple `std::vector` and `std::unordered_map` constructions per frame in `renderShadowPass()` and `buildInstanceBatches()`. Fix: use PMR arena (planned in Batch 5).
- **Per-glyph draw calls in text rendering** — One `glNamedBufferSubData` + `glDrawArrays` per character. Fix: batch all glyphs into single draw call. Medium effort.
- **Full heightmap copy on brush stroke begin** — TerrainBrush copies entire heightmap (up to 16MB). Fix: snapshot only affected region.

### Dead Code (Deferred — low risk, future use planned)
- **ShadowMap class unused** — Replaced by CascadedShadowMap. Still in CMakeLists. Harmless.
- **SSR/Contact shadow members disabled** — FBO allocated but feature disabled (needs G-buffer). Wastes some VRAM.
- **computeSplitDistances() static function** — Split computation done inline; may be used by tests.

### Standards Compliance (Deferred — massive rename)
- **Enum values use UPPER_SNAKE_CASE** instead of PascalCase per coding standards. Affects: AntiAliasMode, MaterialType, AlphaMode, EditorMode, AssetType, EntityProperty, PendingAction, TerrainBrushMode, BrushTool::Mode, ParticleEmitterConfig::Shape/BlendMode. Fixing requires codebase-wide rename.

### Build / Dependencies (Deferred — needs coordination)
- **3 floating dependency branches** — ImGui (`docking`), ImGuizmo (`master`), imgui-filebrowser (`master`). Should pin to specific commit hashes per SECURITY.md.
- **Test target lacks warning flags** — Tests compile without `-Wall -Wextra` etc.
- **30+ subsystems with zero test coverage** — Camera math, Selection, Entity Serializer, OBJ Loader are best candidates for new tests without GL context.
- **No headless GL test infrastructure** — Many tests are placeholders (`EXPECT_TRUE(true)`) because GL context is unavailable.

### Latent Bugs (Low risk, deferred)
- **Entity ID counter overflow** — `uint32_t s_nextId` wraps to 0 (sentinel value) after ~4 billion entities. Practically unreachable.
- **EventBus SubscriptionId wraparound** — Same overflow concern with `uint32_t`. Consider `uint64_t`.
- **Cubemap face dimension mismatch** — `loadCubemap` doesn't verify all 6 faces have same dimensions. Could corrupt GPU memory with mismatched images.
- **Environment map capture doesn't save/restore depth state** — Works because IBL is generated during init, but would break if regenerated at runtime with reverse-Z active.
- **Water surface floating-point equality** — `m_builtWidth == m_config.width` exact float comparison.

---

## Research Findings

Two comprehensive research documents were generated:
- `docs/COMPREHENSIVE_TECH_RESEARCH.md` — OpenGL 4.5+ best practices, AMD Mesa driver issues, CSM/SDSM, TAA, PBR correctness, SMAA, particle systems, terrain, water rendering
- `docs/CUTTING_EDGE_FEATURES_RESEARCH.md` — Experimental features: mesh shaders, bindless textures, FSR, volumetric lighting, GI, procedural generation, physics, audio

### Key Research Highlights
- **AMD Mesa driver**: Confirmed "all samplers must have valid textures bound" requirement
- **Persistent mapped buffers**: Fastest streaming approach for dynamic data (planned in Batch 4)
- **PBR energy conservation**: Kulla-Conty compensation needed at roughness > 0.5 (up to 60% energy loss)
- **Bindless textures**: Mesa supports `GL_ARB_bindless_texture` on RDNA2 — eliminates texture binding overhead
- **FSR 1.0**: Portable OpenGL GLSL implementation available — spatial upscaling for free performance

---

## Verification

- **Build:** Zero errors, warnings are pre-existing `-Wconversion` in terrain.h (not introduced by audit)
- **Tests:** 408 passed, 1 skipped (GL context), 0 failed
- **Files modified:** 28 (see `git diff --stat`)
