# Subsystem Specification — `engine/resource`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/resource` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (foundation since Phase 1; sandbox added Phase 10.9 Slice 5 D1) |

---

## 1. Purpose

`engine/resource` is the asset cache. It owns the lifetime of every loaded texture, mesh, material, and glTF (Graphics Language Transmission Format) `Model` so they're loaded once, looked up by path, and shared across the scene as `std::shared_ptr`. It also enforces a path sandbox so a hostile or sloppy caller cannot trick the engine into opening arbitrary files. It exists as its own subsystem because three independent consumers — the renderer (textures + meshes), the scene loader (models + materials), and the editor (everything) — all need the same de-duplicating cache, and pushing the cache inward to any one of them would force the others to depend on that subsystem through an unrelated path. For the engine's primary use case — first-person walkthroughs of the Tabernacle and Solomon's Temple — `engine/resource` is what keeps the same red-brick texture from being uploaded ten times when the Outer Court reuses it across ten walls.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `ResourceManager` — the cache + facade | Per-pixel rendering of cached resources — `engine/renderer/` |
| Texture caching + sRGB / linear distinction at the cache-key level | `Texture::loadFromFile` actual stb_image decode + GL upload — `engine/renderer/texture.cpp` |
| Mesh caching (file-loaded OBJ + procedural built-ins: cube, plane, sphere, cylinder, cone, wedge) | OBJ parsing — `engine/utils/obj_loader.{h,cpp}` |
| Material registry by name | Material PBR (Physically Based Rendering) shading — `engine/renderer/material.{h,cpp}` |
| `Model` — glTF scene-graph container + scene-instantiation entry point | glTF parsing + accessor decoding — `engine/utils/gltf_loader*.cpp` |
| Reverse lookup (`findMeshKey`, `findTexturePath`, `getMeshByKey`) for serialization | The serializer itself — `engine/scene/scene_serializer.{h,cpp}` |
| Path sandbox (`setSandboxRoots`, `validatePath`) — root-allow-list with canonicalisation | Generic path-canonicalisation primitive — `engine/utils/path_sandbox.{h,cpp}` |
| Default fallback texture (solid white) for missing-file recovery | Renderer-side missing-shader / missing-mesh handling |
| `clearAll` lifecycle hook called from `Engine::shutdown` | Per-frame eviction / streaming budgets (deferred — see §15 Q1) |

## 3. Architecture

```
                ┌───────────────────────────────────────────┐
                │            ResourceManager                │
                │     (engine/resource/resource_manager.h)  │
                │                                           │
                │  m_textures   m_meshes   m_materials      │
                │  m_models     m_defaultTexture            │
                │  m_sandboxRoots                           │
                └──────┬─────────────┬──────────────┬───────┘
                       │             │              │
                       │             │              │
            validatePath(...)        │              │
                       │             │              │
                       ▼             ▼              ▼
              ┌──────────────┐  ┌──────────┐  ┌──────────────┐
              │ PathSandbox  │  │ ObjLoader│  │ GltfLoader   │
              │ (utils)      │  │ (utils)  │  │ (utils)      │
              └──────────────┘  └──────────┘  └──────────────┘
                       │             │              │
                       ▼             ▼              ▼
                 std::filesystem  Texture/Mesh   Model + nested
                 (canonicalise)   (stb_image,    Mesh/Material/
                                   GL upload)    Texture loads
                                                 (recursive into
                                                  ResourceManager)
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `ResourceManager` | class | Owns every cache map; sole legal entry point for asset loading. `engine/resource/resource_manager.h:24` |
| `loadTexture(path, linear)` | method | Sandbox-validate → cache-lookup → `Texture::loadFromFile` → cache. Cache key includes `:srgb`/`:linear`. `engine/resource/resource_manager.cpp:40` |
| `loadMesh(path)` / `loadModel(path)` | method | Same shape as `loadTexture`, dispatches to OBJ / glTF loader. `engine/resource/resource_manager.cpp:84,220` |
| `getCubeMesh` / `getPlaneMesh` / `getSphereMesh` / `getCylinderMesh` / `getConeMesh` / `getWedgeMesh` | method | Procedural built-ins keyed by `__builtin_<shape>[_<params>]`. `engine/resource/resource_manager.cpp:112+` |
| `findMeshKey` / `findTexturePath` / `getMeshByKey` | method | Reverse-lookup for `engine/scene/scene_serializer` (round-trip preservation of built-in vs. file-loaded primitives). `engine/resource/resource_manager.cpp:249,261,356` |
| `setSandboxRoots` / `getSandboxRoots` / `validatePath` | method | Path-allow-list enforcement; `validatePath` is the choke-point every public load runs through. `engine/resource/resource_manager.cpp:21,26` |
| `Model` | class | glTF scene-graph container — primitives + nodes + materials + textures + skeleton + animation clips; `instantiate(Scene&, parent, name)` walks the node tree to materialise entities. `engine/resource/model.h:65` |
| `ModelNode` | struct | One glTF node — TRS (translation/rotation/scale) decomposition or direct matrix; child + primitive indices. `engine/resource/model.h:39` |
| `ModelPrimitive` | struct | One renderable (mesh + material index + AABB (Axis-Aligned Bounding Box) + morph-target data). `engine/resource/model.h:30` |

## 4. Public API

Two header subsystem (≤ 7) — the small-surface §4 pattern applies. The `#include` targets for downstream code are `engine/resource/resource_manager.h` and `engine/resource/model.h`.

```cpp
/// Cache + sandbox lifecycle.
ResourceManager();
~ResourceManager();
void                       setSandboxRoots(std::vector<std::filesystem::path> roots);
const std::vector<std::filesystem::path>& getSandboxRoots() const;
void                       clearAll();
size_t                     getTextureCount() const;
size_t                     getMeshCount() const;
```

```cpp
/// Texture / mesh / material / model loaders.
std::shared_ptr<Texture>   loadTexture(const std::string& path, bool linear = false);
std::shared_ptr<Texture>   getDefaultTexture();        // solid white, lazy-created
std::shared_ptr<Mesh>      loadMesh(const std::string& path);  // OBJ
std::shared_ptr<Mesh>      getCubeMesh();
std::shared_ptr<Mesh>      getPlaneMesh(float size = 10.0f);
std::shared_ptr<Mesh>      getSphereMesh(uint32_t sectors = 32, uint32_t stacks = 16);
std::shared_ptr<Mesh>      getCylinderMesh(uint32_t sectors = 32);
std::shared_ptr<Mesh>      getConeMesh(uint32_t sectors = 32, uint32_t stacks = 4);
std::shared_ptr<Mesh>      getWedgeMesh();
std::shared_ptr<Material>  createMaterial(const std::string& name);
std::shared_ptr<Material>  getMaterial(const std::string& name);
std::shared_ptr<Model>     loadModel(const std::string& path);  // .gltf / .glb
```

```cpp
/// Reverse lookup (used by SceneSerializer).
std::string                findMeshKey(const std::shared_ptr<Mesh>& mesh) const;
std::shared_ptr<Mesh>      getMeshByKey(const std::string& key);
std::string                findTexturePath(const std::shared_ptr<Texture>& tex) const;
```

```cpp
/// Model — see engine/resource/model.h:65 for the full surface.
Entity*                    Model::instantiate(Scene&, Entity* parent, const std::string& name) const;
const std::string&         Model::getName() const;
size_t                     Model::getMeshCount() const;
size_t                     Model::getMaterialCount() const;
size_t                     Model::getTextureCount() const;
size_t                     Model::getNodeCount() const;
AABB                       Model::getBounds() const;
// Public data members populated by GltfLoader: m_primitives, m_materials,
// m_textures, m_nodes, m_rootNodes, m_skeleton, m_animationClips.
```

**Non-obvious contract details:**

- **Cache key ≠ canonical path.** `loadTexture` keys on the *original* `filePath` argument plus `:srgb` / `:linear`. Two callers that pass `models/foo.glb` and `models/./foo.glb` produce two cache entries (sandbox canonicalisation only feeds `Texture::loadFromFile`). This is deliberate — the cache trades a small duplication risk for ABI (Application Binary Interface) stability of the lookup string.
- **`loadTexture` never returns `nullptr`.** Sandbox rejection or load failure both fall back to `getDefaultTexture()` (solid white). `loadMesh` and `loadModel` *do* return `nullptr` on failure — caller must check.
- **`getDefaultTexture()` is lazy.** First call creates the white 1x1; subsequent calls return the cached shared_ptr. The texture is created with a live GL context — calling it from a no-context test environment (e.g. `tests/test_resource_manager_sandbox.cpp`) returns a `Texture` whose GPU handle is 0; that's fine for the tests' purposes.
- **`loadModel` recursively re-enters `ResourceManager`.** `GltfLoader::load(path, *this)` calls back into `loadTexture` for embedded base-color / normal / metallic-roughness maps and `createMaterial` for each glTF material — sharing the same cache so two models that reference the same texture upload it once.
- **Sandbox empty = disabled.** Default `m_sandboxRoots` is empty → `validatePath` returns the input unchanged. Production wires `[install_root, project_root, asset_library_root]` from `Engine::initialize`. Tests typically leave it empty.
- **`clearAll` is idempotent** — safe to call from `Engine::shutdown` regardless of state. It does *not* explicitly release GPU handles; the `~Texture` / `~Mesh` destructors do, triggered when the last `shared_ptr` drops. Caller responsibility: drop scene references before calling `clearAll`.
- **`Model::instantiate` is `const`** — instantiating multiple times into different scenes shares the same GPU resources (meshes, materials, textures). Per-instance state (transforms, animation playback) lives on the `Entity`s the call creates, never on `Model`.

**Stability:** the facade above is semver-frozen for `v0.x`. Two known evolution points: (a) async loading is not yet present (see §15 Q1) — when it lands it will be additive (`loadTextureAsync` / `awaitTextures`), not a breaking change to the existing sync API; (b) hot-reload (§15 Q2) likewise lands as new methods, not a contract change.

## 5. Data Flow

**Cold load — texture (steady-state during scene load):**

1. Caller (e.g. `Engine::initialize` material creation, scene loader, editor file dialog) → `ResourceManager::loadTexture("assets/textures/red_brick_diff_2k.jpg", false)`.
2. Compose cache key `"<path>:srgb"` (or `:linear`).
3. `m_textures.find(cacheKey)` — hit returns immediately (the steady-state path during walkthrough; a 100-wall room is one decode + 99 hits).
4. Miss → `validatePath(filePath)` → `PathSandbox::validateInsideRoots(...)` returns canonical path or empty.
5. Empty (sandbox reject) → `Logger::warning(...)` + `return getDefaultTexture()`.
6. `Texture::loadFromFile(safePath, linear)` → stb_image decode (CPU) → `glGenTextures` + `glTexImage2D` (CPU-driver-GPU sync upload, no PBO today).
7. Store in `m_textures[cacheKey]`; return `shared_ptr`.

**Cold load — model (glTF):**

1. Caller → `loadModel("assets/models/CesiumMan.glb")`.
2. Cache check + sandbox validate (as above).
3. `GltfLoader::load(safePath, *this)` parses the glTF, then for every embedded image calls back into `loadTexture` (recursive cache use), and for every material calls `createMaterial`.
4. Skeleton + animation clips populated into `Model::m_skeleton` / `m_animationClips`.
5. `Model` stored in `m_models[filePath]`; returned as `shared_ptr<Model>`.
6. Caller typically calls `model->instantiate(scene, parent, name)` to materialise entities.

**Reverse lookup — serialize a scene:**

1. `SceneSerializer` walks every `Entity`, finds its `MeshComponent::mesh` (a `shared_ptr<Mesh>`).
2. → `ResourceManager::findMeshKey(mesh)` walks `m_meshes`, returns the cache key (e.g. `"__builtin_cube"` or `"assets/models/door.obj"`).
3. Serializer writes the key string. On deserialize: `getMeshByKey(key)` reconstructs (built-in mesh procedurally, file-loaded mesh via `loadMesh`).

**Shutdown:** `Engine::shutdown` calls `m_resourceManager.reset()` (destruction). The `~ResourceManager` runs every map's destructor, dropping shared_ptrs; `~Texture`, `~Mesh`, etc. release GL handles only if the GL context is still live (CODING_STANDARDS §24 — that's why `SystemRegistry::shutdownAll` runs *before* `Window` is destroyed).

**Exception path:** sandbox rejection logs a warning and returns the default fallback (texture) or `nullptr` (mesh / model). Decode failure logs an error and returns the same. No exceptions propagate out of `loadTexture` / `loadMesh` / `loadModel` in steady state. `Texture::loadFromFile` swallows `stbi_load` failures and returns `false`; the manager then logs and falls back. The two `try/catch` blocks at `resource_manager.cpp:286,307` are scoped to `std::stof` / `std::stoi` parsing inside `getMeshByKey` for `__builtin_plane_<size>` keys — converting parse failure to "use the default size" rather than propagating.

## 6. CPU / GPU placement

| Workload | Placement | Reason |
|----------|-----------|--------|
| Cache lookup, hash table maintenance | CPU (main thread) | Branching, sparse, decision-heavy — exactly the CODING_STANDARDS §17 default for CPU work. |
| Path sandbox canonicalisation (`std::filesystem::canonical`) | CPU (main thread) | Filesystem I/O; per-load one-shot, never per-frame. |
| Image decode (stb_image, called via `Texture::loadFromFile`) | CPU (main thread today) | One-shot per asset; Phase 11 may move decode to a worker thread (see §15 Q1). |
| OBJ / glTF parse (called via `ObjLoader` / `GltfLoader`) | CPU (main thread today) | Same — one-shot, candidate for worker thread. |
| GL texture upload (`glTexImage2D` inside `Texture::loadFromFile`) | CPU-driver-GPU (main thread) | OpenGL context affinity is single-thread; today the upload is **synchronous** (CPU memcpy → driver staging → GPU). PBO (Pixel Buffer Object) async upload is a deferred optimisation flagged in §15 Q1. |
| Mesh VBO/EBO (Vertex Buffer Object / Element Buffer Object) upload | CPU-driver-GPU (main thread) | Same — synchronous today. |

`engine/resource` is fundamentally a **CPU subsystem**. The only GPU touch is the synchronous upload that `Texture::loadFromFile` and `Mesh::upload` perform internally; `ResourceManager` itself never issues a GL call. No dual implementation; no GPU compute; no per-frame GPU cost (cache lookup is `unordered_map::find`, ≈ ns).

## 7. Threading model

Per CODING_STANDARDS §13.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the only thread that calls `Engine::run`) | All of `ResourceManager` and `Model`. GL context affinity lives here. | None — main-thread-only by contract. |
| **Worker threads** | None. `ResourceManager` is **not** thread-safe. | N/A |

**Main-thread-only.** No mutex inside `ResourceManager`. `m_textures` / `m_meshes` / `m_materials` / `m_models` are plain `std::unordered_map`s; concurrent `loadTexture` from two threads is undefined behaviour. This is intentional (§17 below) — GL context affinity makes a worker-thread upload path non-trivial, and the current scene-load + editor flow does not need parallelism.

**Async / worker pool.** None today. The header search shows no `AsyncTextureLoader` class, no `std::thread` / `std::jthread` / `std::async` use, no internal worker pool. Phase 11 is the planned slot for an async loader — see §15 Q1. When that lands the threading row above will gain a "worker thread → CPU decode + I/O + queue → main thread → GL upload" entry.

**Lock-free / atomic:** none required today.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. `engine/resource` is **steady-state-cheap, init-heavy**. Cache hits are the per-frame cost; cache misses (decoded + uploaded textures, parsed glTFs) are the cold-start / scene-transition cost.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `loadTexture` cache hit | < 0.001 ms | TBD — measure by Phase 11 audit (low-risk: `unordered_map::find` + one cache-key concat) |
| `loadTexture` cold (2K JPEG decode + GL upload, sync) | < 30 ms | TBD — measure by Phase 11 audit |
| `loadMesh` cold (OBJ parse + VBO upload, ≤ 100k tris) | < 50 ms | TBD — measure by Phase 11 audit |
| `loadModel` cold (CesiumMan-class glTF, < 5 MB) | < 150 ms | TBD — measure by Phase 11 audit |
| `validatePath` (sandbox configured) | < 0.05 ms | TBD — measure by Phase 11 audit (one `std::filesystem::canonical` call per load — disk-cache-friendly after first hit) |
| `clearAll` | < 5 ms | TBD — measure by Phase 11 audit |

Profiler markers / capture points: none today (`engine/resource` does not emit `glPushDebugGroup` markers — the `Texture::loadFromFile` GL calls run unscoped). When async loading lands, mark every upload boundary so RenderDoc captures show the streaming pattern.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (`std::shared_ptr` + `std::unordered_map` nodes). No arena; no per-frame transient allocator (the maps grow on cache miss and stay grown until `clearAll`). |
| Peak working set | Dominated by GPU texture memory, not CPU: a 2048x2048 RGBA8 texture is 16 MiB GPU + ~4 MiB CPU (kept by `Texture` for re-uploads). The Tabernacle demo scene (≈ 30 unique textures + 5 glTF models) settles around 200–400 MiB of GPU texture memory; the CPU-side `ResourceManager` itself is single-digit MiB (cache-key strings + map nodes + the small `Mesh` / `Material` structs). |
| Ownership | `ResourceManager` owns the **cache entry** via `shared_ptr`; every caller (renderer, scene, editor) holds a copy of the same `shared_ptr`. Last release destroys the resource — usually `Engine::shutdown` via `m_resourceManager.reset()`. |
| Lifetimes | Engine-lifetime by default. Scene-load duration is the same as engine-lifetime today (no per-scene eviction). `clearAll` is the only explicit teardown hook. Reusable across scene transitions: assets used by the next scene survive the load (they're already in cache). |

No `new`/`delete` (CODING_STANDARDS §12). Built-in mesh procedural generation allocates inside `Mesh` (vertex/index vectors → VBO upload → vectors freed on cache eviction).

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady-state hot paths.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| File missing (path doesn't exist) | `loadTexture`: `Logger::warning` + `getDefaultTexture()` (solid white). `loadMesh` / `loadModel`: `Logger::error` + `nullptr`. | Caller must null-check mesh / model results; texture callers can render the fallback. |
| Malformed asset (corrupt JPEG, malformed glTF, OBJ syntax error) | Same as missing — the underlying loader returns `false`, manager logs + falls back. | Same. |
| Sandbox rejection (path canonicalises outside any root) | `Logger::warning("ResourceManager: path rejected (escapes sandbox): <path>")` + fallback (default texture / nullptr). | Treat as caller bug — fix the path, or extend `setSandboxRoots`. |
| OOM (Out Of Memory) on large texture allocation | `std::bad_alloc` propagates from `stb_image` / `Texture` / `std::make_shared`. | App aborts (matches CODING_STANDARDS §11 — OOM is fatal). |
| GL context absent during `Texture::loadFromFile` (test environment) | `glGenTextures` returns 0; texture is in cache but unusable on GPU. | Test code does not exercise the GL path; sandbox-only tests check that `getTextureCount` did not increment when sandbox rejected. |
| `std::stof` / `std::stoi` parse failure inside `getMeshByKey` for `__builtin_plane_<size>` etc. | Caught locally (`resource_manager.cpp:286,307`) → use default param value. | None — the deserializer key was malformed, the default is the safest substitute. |
| Programmer error (null mesh in `findMeshKey`) | Returns empty string — non-asserting (the call is search-shaped, not lookup-by-key). | Caller treats empty as "not in cache." |

`Result<T, E>` / `std::expected` not yet used here (predates the engine-wide policy). The current `shared_ptr<T>` + `nullptr`-on-failure / `getDefaultTexture()` pattern is the migration target for Phase 12 — see `engine/core/spec.md` Q4 for the codebase-wide migration plan; `engine/resource` will follow the same shape.

No `try/catch` outside the two `std::stof`/`std::stoi` blocks documented above.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Path sandbox accept / reject / empty-roots-disabled / canonical comparison | `tests/test_resource_manager_sandbox.cpp` | Public API contract for `setSandboxRoots` + every `load*` choke-point |
| glTF loader bounds checks (accessor index OOB, primitive count overflow) | `tests/test_gltf_bounds_checks.cpp` | Defensive parsing — feeds into `loadModel` |
| glTF loader filesystem sandbox | `tests/test_gltf_fs_sandbox.cpp` | Embedded URI resolution must stay inside the sandbox |
| Texture filtering / mipmap | `tests/test_texture_filter.cpp` | Renderer-side, but loaded via `ResourceManager` |

**Adding a test for `engine/resource`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `ResourceManager` directly without an `Engine` instance — every primitive in this subsystem **except the GL upload inside `Texture::loadFromFile` / `Mesh::upload`** is unit-testable headlessly. Visual / GL-bound paths exercise via `engine/testing/visual_test_runner.h`. The sandbox tests at `tests/test_resource_manager_sandbox.cpp` are the canonical pattern: they observe sandbox rejection by checking that the returned texture is the default fallback (not `nullptr`) and that `getTextureCount()` did not increment, all without a GL context.

**Coverage gap:**
- `loadTexture` / `loadMesh` / `loadModel` *success* paths cannot be unit-tested headlessly — the GL upload requires a context. They run inside the visual-test harness and through the demo scenes (every `Engine::initialize` exercises ≈ 30 texture loads).
- Cache-hit fast path is implicitly covered (every duplicate `loadTexture` call) but has no dedicated unit test. Add one when `loadTextureAsync` lands so the hit-vs-miss accounting is observable.
- Concurrent-load behaviour: no test, by design — see §7 (main-thread-only).

## 12. Accessibility

Not applicable — `engine/resource` produces no user-facing pixels or sound directly. The cache is upstream of every renderer / UI surface; accessibility constraints (color-vision filter, captions, photosensitive caps) are applied **downstream** (in `engine/renderer/` and `engine/ui/`) on the resources this subsystem hands out. A regression here cannot break accessibility downstream — the worst case is "asset failed to load," which is already handled with the default-texture fallback.

The one indirect connection: when a texture load fails, `Logger::warning` writes to the editor console, where the `LogLevel` colour cue must be backed by a text label per the `engine/core` spec §12 (partially-sighted-user constraint). That's an `engine/core` concern, not `engine/resource`.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/renderer/mesh.h` | engine subsystem | `Mesh` type stored in cache. |
| `engine/renderer/texture.h` | engine subsystem | `Texture` type stored in cache; `loadFromFile` does the actual decode + upload. |
| `engine/renderer/material.h` | engine subsystem | `Material` type stored in cache. |
| `engine/animation/skeleton.h` | engine subsystem | `Model::m_skeleton` storage for glTF rigs. |
| `engine/animation/animation_clip.h` | engine subsystem | `Model::m_animationClips` storage. |
| `engine/animation/morph_target.h` | engine subsystem | `ModelPrimitive::morphTargets` storage. |
| `engine/utils/aabb.h` | engine subsystem | `ModelPrimitive::bounds` + `Model::getBounds()`. |
| `engine/utils/obj_loader.h` | engine subsystem | OBJ parser called from `loadMesh`. |
| `engine/utils/gltf_loader.h` | engine subsystem | glTF parser called from `loadModel`. |
| `engine/utils/path_sandbox.h` | engine subsystem | `PathSandbox::validateInsideRoots` — the canonicalisation primitive. |
| `engine/core/logger.h` | engine subsystem | Warning / error logging on sandbox reject + load failure. |
| `engine/scene/scene.h`, `entity.h` | engine subsystem (consumed by `Model::instantiate`) | Scene + entity types the model instantiates into. |
| `<glm/glm.hpp>`, `<glm/gtc/quaternion.hpp>` | external | TRS storage on `ModelNode`. |
| `<filesystem>`, `<memory>`, `<string>`, `<unordered_map>`, `<vector>` | std | Cache containers + path types + shared ownership. |

**Direction:** depended on by `engine/scene/`, `engine/renderer/` (indirectly — renderer reads materials / textures the cache hands out), `engine/editor/`, `engine/core/engine.cpp` (asset wiring during `initialize`). `engine/resource` itself depends on `engine/renderer` (type definitions only, not behaviour), `engine/utils`, `engine/animation`, `engine/core/logger`. It must **not** depend on `engine/scene/scene_manager.h` — `Model::instantiate` takes a `Scene&` parameter, never reaches into a global scene registry.

## 14. References

Cited research / authoritative external sources:

- Khronos Group. *OpenGL Wiki — Pixel Buffer Object.* — canonical reference for asynchronous texture upload via PBO + `GL_PIXEL_UNPACK_BUFFER`. <https://www.khronos.org/opengl/wiki/Pixel_Buffer_Object>
- Song Ho Ahn. *OpenGL Pixel Buffer Object (PBO).* — multi-PBO ping-pong streaming pattern (`GL_STREAM_DRAW` hint, `glMapBuffer` / `glUnmapBuffer`), the design `loadTextureAsync` will follow when §15 Q1 is closed. <https://www.songho.ca/opengl/gl_pbo.html>
- spnda. *fastgltf — modern C++17 glTF 2.0 library focused on speed, correctness, and usability* (2025) — reference design for the glTF parse path; ~24x faster than tinygltf with RapidJSON. Considered for the Phase 11 loader rewrite. <https://github.com/spnda/fastgltf>
- syoyo. *tinygltf v3* (2025, ongoing) — current-stream glTF 2.0 loader; v3 is a C-centric arena-based redesign with opt-in `TINYGLTF3_ENABLE_FS` / `TINYGLTF3_ENABLE_STB_IMAGE`. The engine's current loader uses v2; v3 migration is on the Phase 11 list. <https://github.com/syoyo/tinygltf>
- jkuhlmann. *cgltf — single-file glTF 2.0 loader and writer (C99).* — alternate parser; deferred buffer / image loading via `cgltf_load_buffers` is the model `GltfLoader` follows for the recursive `ResourceManager::loadTexture` callback. <https://github.com/jkuhlmann/cgltf>
- Bevy contributors. *Bevy Cheat Book — Hot-Reloading Assets* (2025) — design reference for the `notify`-style filesystem-watcher pattern that would underpin a future hot-reload feature (§15 Q2). <https://bevy-cheatbook.github.io/assets/hot-reload.html>
- Snyk. *Exploring 3 types of directory traversal vulnerabilities in C/C++.* — threat model behind `setSandboxRoots`; the canonicalise-then-prefix-check pattern in `PathSandbox::validateInsideRoots` follows their recommendation. <https://snyk.io/blog/exploring-3-types-of-directory-traversal-vulnerabilities-in-c-c/>
- MITRE. *CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal').* — the vulnerability class the sandbox defends against. <https://cwe.mitre.org/data/definitions/22.html>
- cppreference. *std::filesystem::canonical / weakly_canonical.* — the standard-library primitive `PathSandbox` builds on. <https://en.cppreference.com/w/cpp/filesystem/canonical.html>
- Khronos Group. *glTF 2.0 specification* — the asset format `loadModel` consumes. <https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html>

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §24 (GL resource lifetimes), §32 (asset paths — `EnginePaths::assetRoot()` once it lands).
- `engine/core/spec.md` §13 (FPC / Camera / ResourceManager dependency direction), §15 Q4 (`Result<T,E>` migration this subsystem will follow).
- `ARCHITECTURE.md` §1–6 (subsystem map).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Async texture loading is not yet implemented (no `AsyncTextureLoader` class today, despite earlier plan references). Add `loadTextureAsync` + multi-PBO ping-pong upload pipeline (worker thread for stb_image decode + pinned-buffer staging; main thread does the `glTexSubImage2D` from the PBO). Keep sync `loadTexture` as the first-frame / required-for-this-frame path. | milnet01 | Phase 11 entry |
| 2 | Hot-reload is not implemented. A `notify` / `inotify` / `ReadDirectoryChangesW` file watcher feeding back into `loadTexture` (cache-key invalidate + reload) would close the editor "tweak texture, see in viewport" loop. Touches GL state — must run main-thread on the watcher's poll tick, not the watcher thread itself. | milnet01 | Phase 11 entry |
| 3 | Per-scene eviction is not implemented — `clearAll` is the only teardown hook. A scene transition from Tabernacle → Solomon's Temple keeps every Tabernacle asset in cache. Acceptable today (memory headroom on the dev hardware) but won't scale to streaming worlds. Add a `releaseUnreferenced()` sweep that drops cache entries with `use_count() == 1` after scene unload. | milnet01 | Phase 11 entry |
| 4 | Cache-key inconsistency: textures key on `<original-path>:<format>`; meshes / models key on `<original-path>` (no canonicalisation). Two callers passing `models/foo.glb` and `models/./foo.glb` produce two cache entries. Fix: key on `path::lexically_normal()` per CODING_STANDARDS §32. Coordinate with `SceneSerializer` (the reverse-lookup callers) so on-disk scene files don't break. | milnet01 | Phase 11 entry |
| 5 | `Result<T, E>` / `std::expected` migration — `loadTexture` returning the default fallback on every failure mode is silent-failure-shaped. Switch to `Result<shared_ptr<Texture>, LoadError>` for explicit caller-side diagnosis; existing callers that want the fallback call a new `loadTextureOrDefault` helper. Coordinated with the engine-wide migration in `engine/core/spec.md` Q4. | milnet01 | post-MIT release (Phase 12) |
| 6 | Performance budgets in §8 are placeholders. Need a one-shot capture (cold-start the demo + Tabernacle) with `tracy` / `chrome://tracing` to fill in measured numbers. | milnet01 | Phase 11 audit |
| 7 | `loadModel` cache key uses the *original* path string; does not deduplicate two scene refs to the same `.glb` opened via different relative paths. Same fix as Q4 (`lexically_normal()`). | milnet01 | Phase 11 entry |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/resource` foundation since Phase 1, sandbox added Phase 10.9 Slice 5 D1, formalised post-Phase 10.9 audit. Notes async / hot-reload absence as open questions, not spec gaps. |
