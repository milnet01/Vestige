# Phase 10 — Meadow Demo & Profiling-Benchmark Scene (design)

**Status:** draft — pending cold-eyes review loop (project Rule 9, global Rule 14).
**Author:** in-session 2026-07-11.
**Owner subsystem:** `engine/core` (scene setup), `engine/environment` (terrain/foliage),
`engine/scene` (water), plus asset + docs updates.

---

## 1. Goal

Replace the default demo scene — currently a row of numbered PBR **material-test
cubes** built by `Engine::setupDemoScene()` (`engine/core/engine.cpp:2166`) — with a
believable natural **meadow**: gently rolling grassy terrain under a real
photographed sky, dense wind-animated grass, a reflective **pond** with sandy
banks and lily pads, and scattered trees, rocks, wildflowers, and a fallen log.

The scene has **two jobs**:

1. **Showcase** — a launch scene that looks far better than the test cubes.
2. **Profiling benchmark** (primary driver, per user 2026-07-11) — a
   representative, deliberately load-bearing scene that exercises the heavy
   render passes (terrain LOD, dense instanced grass, planar-reflection water,
   cascaded shadows, HDRI/IBL) so the engine's **real bottlenecks surface** in
   the existing Performance panel. This scene is the fixture a subsequent
   optimization program profiles against.

To make bottleneck analysis collaborative (user 2026-07-11: "I will need your
help significantly in figuring it out"), the profiler also **writes its
per-pass timings to a CSV log file** (§8.1) so the data can be shared and read
outside the live panel.

The **60 FPS floor remains a hard requirement** (project CLAUDE.md). For the
benchmark, any sustained sub-60 region is a **finding to fix in the follow-up
optimization pass** — not a reason to thin the scene. Density is exposed as
tunable constants so the breaking point can be found deliberately.

### 1.1 Non-goals

- **No new rendering technology.** Every element reuses a shipped subsystem
  (terrain, foliage, water, skybox/IBL, shadows, instancing). This is scene
  *content* + asset wiring, not engine features.
- **No photoreal committed trees.** Scanned trees are 60–150 MB each (§6) —
  too heavy for the public repo and a 60-FPS risk. Committed props are
  lightweight CC0 low-poly (Kenney). A documented **local-only** hook (§5.7)
  lets the maintainer drop in photoreal trees later without code changes.
- **No automated frame-time gate in this phase.** Bottleneck-hunting uses the
  existing Performance panel interactively; a CI perf-gate is future work.

---

## 2. Approach summary

Terrain, grass, and water are **already engine-owned and already enabled in the
current demo** (only `setupTabernacleScene()` disables terrain, at
`engine.cpp:2565`). So the meadow is built by:

- **Reshaping** the pre-initialized terrain (`m_terrain`) into rolling hills +
  a pond bowl, then auto-texturing it (grass splat + sand banks).
- **Painting** dense grass that follows the terrain surface.
- **Placing** a water-surface entity in the carved bowl.
- **Loading a CC0 HDRI** for sky + image-based lighting, with a matched sun.
- **Scattering** lightweight CC0 props (load-once, instantiate-many) at terrain
  height.

The old material-test scene is **preserved** behind a new `--material-demo`
flag (§5.1) so the PBR/glass/emissive/skeletal-animation test bench is not lost.

---

## 3. Verified engine APIs (citations)

All signatures below were read from current source (global Rule 13). File:line
are load-bearing and re-checked during cold-eyes.

### 3.1 Terrain (`engine/environment/terrain.h`)
- `struct TerrainConfig { int width=257, depth=257; float spacingX=1, spacingZ=1,
  heightScale=50; glm::vec3 origin{0}; ... }` (`terrain.h:23`). Default terrain
  spans 256 m × 256 m; `TerrainSystem::initialize()` sets `origin=(-128,0,-128)`
  (`terrain_system.cpp:33`).
- `void setRawHeight(int x, int z, float height)` — height **normalized 0..1**
  (`terrain.h:108`). `float getRawHeight(int,int)` (`:102`).
- `float getHeight(float worldX, float worldZ) const` — returns **world-space Y
  in world units** (already × `heightScale`; `origin.y` is 0 in default config)
  (`terrain.h:90`, body `terrain.cpp:120`). `glm::vec3 getNormal(...)` (`:96`).
- `void updateHeightmapRegion(int,int,int,int)` (`:137`),
  `void updateNormalMapRegion(...)` (`:140`), `void updateSplatmapRegion(...)`
  (`:143`), `void buildQuadtree()` (`:113`).
- `void generateAutoTexture(const AutoTextureConfig&)` — fills the splatmap
  (R=grass, G=rock, B=dirt, A=sand) from slope+altitude (`:252`, cfg `:237`).
- `void applyBankBlend(const glm::vec2& waterCenter, const glm::vec2&
  waterHalfExtent, const BankBlendConfig&)` — blends the sand channel into the
  shore; splatmap-only, does **not** edit heights (`:232`, cfg `:220`).
- `Terrain& Engine::getTerrain()` returns `*m_terrain` (`engine.h:315`). The
  cached pointer is valid in scene setup — `m_terrain` is assigned at
  `engine.cpp:239`, before the scene-selection branch at `:248`. The render loop
  draws it whenever `m_terrainEnabled && m_terrain->isInitialized()`
  (`engine.cpp:1544`); the meadow leaves `m_terrainEnabled` at its default `true`.

**No built-in noise generator exists on `Terrain`** — the canonical proc-gen
path (demonstrated in `terrain_system.cpp:38-76`) fills heights with
`setRawHeight`, then uploads + rebuilds. The meadow follows that pattern.

### 3.2 Grass / foliage (`engine/environment/foliage_manager.h`)
- `std::vector<FoliageInstanceRef> paintFoliage(uint32_t typeId, const glm::vec3&
  center, float radius, float density, float falloff, const FoliageTypeConfig&
  config, const DensityMap* = nullptr)` (`:51`).
- `eraseAllFoliage(center, radius)` (`:69`), `getTotalFoliageCount()` (`:161`).
- **`paintFoliage` does not sample terrain height** — it stamps around
  `center` (verified: no terrain reference in the header). On rolling terrain we
  therefore paint **small stamps, each at `center.y = terrain.getHeight(x,z)`**
  (§5.4), so grass follows the surface. Foliage renders instanced with wind
  animation (`foliage.vert.glsl`) and casts shadows (already wired at
  `engine.cpp:305`).

### 3.3 Water (`engine/scene/water_surface.h`)
- `WaterSurfaceComponent` is an **entity component**; the water plane sits at the
  entity's transform Y (`getLocalWaterY()` returns 0). `WaterSurfaceConfig`
  fields (`water_surface.h:27`): `width, depth, gridResolution, numWaves,
  waves[4]{amplitude,wavelength,speed,directionDeg}, shallowColor, deepColor,
  depthDistance, flowSpeed, specularPower, causticsEnabled, reflectionMode
  (default PLANAR), reflectionResolutionScale=0.25, refractionEnabled`.
- The render loop auto-discovers water entities into `m_renderData.waterSurfaces`
  and derives caustics onto terrain (`engine.cpp:1500-1528`). Reference usage:
  the demo "Water Pool" (`engine.cpp:2476-2494`).

### 3.4 Models / props (`engine/resource/`)
- `std::shared_ptr<Model> ResourceManager::loadModel(const std::string&)`
  (`resource_manager.h:83`).
- `Entity* Model::instantiate(Scene&, Entity* parent, const std::string& name)
  const` (`model.h:75`) — "instantiated multiple times without reloading GPU
  data" (`model.h:63`). Reference: `engine.cpp:2418-2423`.
- Loaders: tinygltf (`.gltf`/`.glb`) + Wavefront `.obj` (verified present).
  Kenney `.glb` are vertex-coloured, embedded-material, no external textures.
- **Do NOT** route these through `FoliageManager::addTreeDirect` /
  `TreeRenderer` — that path renders **procedural billboard placeholders**
  (`tree_renderer.h:65 createPlaceholderTree`), which is why the meadow uses
  authored `.glb` via `instantiate`.

### 3.5 CLI plumbing (three sites, mirroring `biblicalDemo`)
- Arg parse: `app/main.cpp:139` (add `--material-demo` before the unknown-arg
  `else` at `:144`).
- Struct field: `struct EngineConfig` at `engine.h:56`; `bool biblicalDemo=false`
  at `engine.h:105` (add `bool materialDemo=false`).
- Branch: `engine.cpp:248-255` (`if biblicalDemo → setupTabernacleScene else →
  setupDemoScene`) becomes a three-way (§5.1).

### 3.6 Profiling (`engine/profiler/`, `engine/editor/panels/performance_panel.*`)
- `PerformanceProfiler` (`performance_profiler.h`) owns a `GpuTimer`,
  `CpuProfiler`, `MemoryTracker`; driven by `beginFrame()` / `endFrame(dt)`.
  Getters: `getFps()`, `getFrameTimeMs()`, `getAvgFrameTimeMs()`,
  `getMin/MaxFrameTimeMs()`, `isEnabled()`/`setEnabled(bool)`.
- `GpuTimer::getResults()` → `const std::vector<GpuPassTiming>&` (per-pass
  name + ms; `gpu_timer.h:17,56`), `getTotalGpuTimeMs()`,
  `hasResults()` (true once the double/triple-buffered queries are ready).
- `CpuProfiler::getLastFrame()` → `const std::vector<ProfileEntry>&` (per-scope
  name/depth/ms; `cpu_profiler.h:16,45`), `getTotalCpuTimeMs()`.
- The editor **Performance panel** (`performance_panel.h`, F12) already renders
  Overview/GPU/CPU/Memory/DrawCalls tabs from the above. The meadow is the
  fixture; the **only** new profiling code is the CSV logger (§8.1), which reads
  these same getters — it does not add timers or change the render path.

---

## 4. Scene layout

```
        (rolling grassy hills, 256 m terrain, gentle relief ~6–10 m)
   ┌─────────────────────────────────────────────────────────────┐
   │   🌲     🌳         🌾 dense grass everywhere 🌾        🌳    │
   │      🪨        🌼🌼          🌳                 🪨            │
   │  🌳        ╭──────────────╮  reeds/cattails ring the pond    │
   │     🌼     │  POND ~14 m  │🪷 lily pads, planar reflection   │
   │   🪵 log   │  carved bowl │   of sky + trees                 │
   │            ╰──────────────╯      🌼🌼   🌲                   │
   │  🌳    🪨          🌾              🌳         🪨      🌳      │
   └─────────────────────────────────────────────────────────────┘
           camera spawns on a low rise looking down toward the pond
```

- **Terrain:** low-frequency multi-octave height field → gentle hills; a smooth
  **bowl carved** around the pond centre; auto-textured grass, with rock on
  steep slopes and sand blended onto the pond banks.
- **Pond:** one `WaterSurfaceComponent` at the bowl's water level; planar
  reflection on (mirrors sky + nearby trees), caustics on submerged terrain.
- **Props (Kenney CC0 low-poly):** trees, rocks/stones, wildflowers, lily pads,
  reeds, a log — scattered at terrain height with random yaw + scale, excluded
  from the pond interior.
- **Sky/light:** CC0 HDRI skybox + IBL; one directional sun matched to the HDRI;
  CSM shadows; existing bloom + SSAO + ACES tonemap stay on.

---

## 5. Detailed design

### 5.1 Scene selection & `--material-demo`
Rename the current builder `setupDemoScene()` → **`setupMaterialDemoScene()`**
(byte-for-byte preserved, incl. its terrain/foliage behaviour). Write a **new**
`setupDemoScene()` that builds the meadow (so "default = meadow" needs no branch
change beyond the new flag). Selection branch becomes:

```cpp
if (config.biblicalDemo)      setupTabernacleScene();
else if (config.materialDemo) setupMaterialDemoScene();
else                          setupDemoScene();   // meadow
```
Add `bool materialDemo=false` to `EngineConfig`, the `--material-demo` arg case,
and a `--help` line. Declare `void setupMaterialDemoScene();` beside the other
`setup*Scene()` decls in `engine.h`.

### 5.2 Pure, unit-testable helpers (new: `engine/environment/meadow_terrain.h/.cpp`)
To keep the GL-bound scene build out of unit tests, two **pure functions** carry
the deterministic math (reuse + testability, global Rule 3):

- `float meadowHeight01(float nx, float nz, const MeadowShape&)` — normalized
  0..1 height at grid fraction (nx,nz): summed low-freq sine octaves for hills,
  minus a smooth radial bowl (smoothstep falloff) at the pond centre. Pure,
  deterministic, no GL.
- `std::vector<ScatterPoint> scatterProps(uint32_t seed, const ScatterParams&)`
  — jittered-grid ("poisson-ish") 2D points in a region, with a min-distance
  reject and an exclusion disc (the pond). Deterministic from `seed`.

`MeadowShape`/`ScatterParams`/`ScatterPoint` are plain structs. `setupDemoScene()`
calls these, then applies results through the GL-bound terrain/scene APIs. Tests
target the pure functions (§8); the full build is covered by the visual-test
harness + manual check.

### 5.3 Terrain reshape (in `setupDemoScene`)
```cpp
Terrain& t = getTerrain();
const int W = t.getWidth(), D = t.getDepth();
MeadowShape shape{ /* octave freqs/amps, pond centre+radius in grid space */ };
for (int z=0; z<D; ++z)
  for (int x=0; x<W; ++x)
    t.setRawHeight(x, z, meadowHeight01(float(x)/(W-1), float(z)/(D-1), shape));
t.updateHeightmapRegion(0,0,W,D);
t.updateNormalMapRegion(0,0,W,D);
Terrain::AutoTextureConfig at{};                 // grass-dominant defaults
t.generateAutoTexture(at);
Terrain::BankBlendConfig bb{};                   // sand into shore
t.applyBankBlend(pondCenterXZ, pondHalfExtentXZ, bb);
t.updateSplatmapRegion(0,0,W,D);
t.buildQuadtree();
// m_terrainEnabled stays true (default)
```
Relief is tuned via the octave amplitudes so world height variation is gentle
(~6–10 m across 256 m). The bowl is carved in `meadowHeight01` so its floor sits
**below** the water level, giving visible shallow→deep shading.

### 5.4 Grass
Paint many **small** stamps over the terrain, each at the sampled surface height
so grass hugs the hills; skip stamps inside the pond and near large props:
```cpp
for (grid of stamp centres spaced ~2–3 m across the terrain interior) {
    if (inside pond radius) continue;
    float y = t.getHeight(cx, cz);
    m_foliageManager->paintFoliage(0, {cx,y,cz}, stampRadius, density, falloff, grassCfg);
}
// then eraseAllFoliage() discs around each placed prop + the pond
```
`GRASS_DENSITY` and stamp spacing are named constants (the primary benchmark
knob). Target tens of thousands of instances (the current demo already runs 10 k
comfortably; the benchmark pushes higher to find the ceiling).

### 5.5 Pond
```cpp
Entity* pond = scene->createEntity("Pond");
pond->transform.position = {pondCenter.x, WATER_LEVEL_Y, pondCenter.z};
auto* w = pond->addComponent<WaterSurfaceComponent>();
auto& c = w->getConfig();
c.width = c.depth = POND_SIZE;  c.gridResolution = 128;
c.numWaves = 3;  c.waves[0]={0.004f,3.0f,0.2f,20.0f}; /* +2 */
c.shallowColor={0.15f,0.45f,0.5f,0.8f}; c.deepColor={0.02f,0.12f,0.28f,1.0f};
c.reflectionMode = WaterReflectionMode::PLANAR;   // reflects sky + trees
```
`WATER_LEVEL_Y` is chosen so the carved bowl floor is ~1–1.5 m below it.

### 5.6 Sky, sun, shadows
Load a committed CC0 HDRI (`assets/hdri/<name>_2k.hdr`, §6) via the existing
equirectangular path (`Renderer::loadSkyboxHDRI` / `Skybox::loadEquirectangular`,
verified present) and enable the skybox (the current demo disables it and uses a
flat clear colour — the meadow enables it). Add one `DirectionalLightComponent`
"Sun" whose direction/colour matches the HDRI's sun, shadow-casting; CSM is
already active. Bloom/SSAO/ACES stay as the demo sets them.

### 5.7 Props scatter + photoreal-later hook
Load each distinct prop model **once**, then `instantiate()` per scatter point:
```cpp
auto oak = m_resourceManager->loadModel(treePath("tree_oak.glb"));
for (auto& p : scatterProps(SEED_TREES, treeParams)) {
    Entity* e = model->instantiate(*scene, nullptr, "Tree");
    e->transform.position = {p.x, t.getHeight(p.x,p.z), p.z};
    e->transform.rotation = {0, p.yawDeg, 0};
    e->transform.scale    = glm::vec3(p.scale);
}
```
Rocks, flowers, lily pads (placed at `WATER_LEVEL_Y` on the pond), reeds, and a
log follow the same pattern with their own scatter params/regions.

**Photoreal-later hook (minimal, no config system):** `treePath(name)` first
checks a git-ignored `assets/models/nature_local/<name>` and uses it if present,
else falls back to the committed Kenney model. The maintainer can drop photoreal
`.glb` files (named to match) into `nature_local/` to override trees locally
without touching code. Documented in `ASSET_LICENSES.md` + a `.gitignore` entry.
This is the "both: stylised now, photoreal later" decision (user 2026-07-11).

Finish with `scene->update(0.0f)` to compute world matrices.

---

## 6. Assets & licensing

**Committed (public repo):**
- **Kenney Nature Kit 2.1** (`kenney.nl`) — **CC0** (attribution optional). A
  curated subset of small `.glb` (each a few KB, vertex-coloured, no external
  textures): ~6 tree variants (oak/default/detailed/fat/pine A+B), rocks
  (`rock_largeA/B`, `rock_smallA–C`, `stone_tallA`), flowers
  (`flower_{purple,red,yellow}A`), `plant_bush*`, `grass`, `plant_flatTall`
  (reeds), `lily_small/large`, `log`, `mushroom_red`. Total well under ~1 MB.
  Placed in `assets/models/nature/`.
- **One CC0 Poly Haven HDRI**, 2K equirectangular (e.g. `meadow_2` or
  `syferfontein_0d_clear`), `assets/hdri/`. ~1–4 MB.

Both are consistent with the repo's existing posture (it already ships CC0/CC-BY
models — Fox, CesiumMan — and CC0 Poly Haven textures). Add rows to
`ASSET_LICENSES.md` and full attribution lines to `THIRD_PARTY_NOTICES.md`
(Kenney credit is optional but included as courtesy; Poly Haven CC0 needs none).

**Not committed:** scanned/photoreal trees (60–150 MB each; §1.1). The
`nature_local/` override dir (§5.7) is git-ignored.

**Verification:** confirm each committed `.glb` loads via the engine's glTF
loader during the assets slice (§9); confirm the HDRI loads via the skybox path.

---

## 7. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Height-field + scatter math (`meadowHeight01`, `scatterProps`) | **CPU**, once at build | Sparse, deterministic, decision/IO-like; testable pure functions. |
| Heightmap/splatmap/normal upload | CPU build → **GPU** textures | One-time; sampled per-frame on GPU. |
| Terrain CDLOD render | **GPU** | Per-vertex LOD morph + per-pixel splat shading. |
| Grass instance generation | **CPU**, once | Sparse placement decision. |
| Grass render + wind | **GPU** (instanced, vertex-shader wind) | Per-instance/per-vertex. |
| Water waves | **GPU** (vertex shader) | Per-vertex. |
| Water planar reflection/refraction | **GPU** (extra scene passes) | Per-pixel; **the key bottleneck to watch** (§8). |
| Prop placement | **CPU**, once (static) | Sparse. |
| Prop render | **GPU** (batched/instanced) | Per-vertex. |
| Shadows (CSM), IBL prefilter, bloom/SSAO/tonemap | **GPU** | Per-pixel/per-texel. |
| Profiling capture | **CPU** timers + **GPU** timer queries | Existing profiler. |
| Profiler CSV logging (§8.1) | **CPU / IO**, throttled ~1 Hz | File writing + interval averaging; pure row-format function. |

No new dual CPU/GPU implementations → no parity test needed.

---

## 8. Performance & benchmark methodology

**Target:** 60 FPS at 1280×720 on the dev RX 6600. The scene intentionally runs
all heavy passes at once.

**Expected hotspots to watch (hypotheses to confirm in the panel):**
1. **Planar-reflection water** — re-renders the scene into the reflection FBO;
   with dense grass + props in view this can dominate. `reflectionResolutionScale`
   (0.25) and reflection-list culling are the first levers.
2. **Grass instance count / overdraw** — the main tunable load.
3. **Shadow cascades** — terrain + grass + props all cast; cascade resolution
   and caster culling are levers.
4. **Terrain LOD / draw-node count**; **IBL + SSAO** fixed overhead.

**Method:** open the editor's **Performance panel** (CPU + GPU per-pass timers),
walk/fly the meadow, and read the per-pass GPU cost. Density constants (§5.4) let
us push to the sub-60 point deliberately. Findings feed the follow-up
optimization phase (not in scope here). No CI perf-gate is added this phase.

**Determinism:** scatter + height field are seeded (fixed `SEED_*` constants) so
the scene is identical run-to-run — a stable fixture for before/after profiling.

### 8.1 Profiler CSV logging (new: `engine/profiler/profile_log.{h,cpp}`)

So bottlenecks can be analysed off-panel and shared (user 2026-07-11), a small
`ProfileLog` class writes the profiler's timings to a CSV file.

- **API:** `bool open(const std::string& path)`, `void close()`, `bool
  isOpen()`, and `void sample(const PerformanceProfiler&, double elapsedSec)`.
  The engine calls `sample(...)` once per frame *after* `endFrame(dt)`; the class
  **throttles internally** to ~1 write/second, emitting **averages over the
  interval** (so the file is small and readable over a long session, and
  single-frame noise is smoothed).
- **Format — long CSV** (robust to a variable pass set; trivial for an agent to
  pivot). Header written once on `open`:
  ```
  time_s,category,name,ms,fps
  12.0,frame,total,15.9,62.9
  12.0,gpu,total,9.8,
  12.0,gpu,ShadowPass,2.1,
  12.0,gpu,WaterReflection,3.4,
  12.0,gpu,Foliage,1.9,
  12.0,cpu,total,4.2,
  12.0,cpu,Culling,0.8,
  12.0,mem,gpu_mb,512,
  ```
  `category ∈ {frame,gpu,cpu,mem}`; `name` is the pass/scope; `ms` the interval
  average; `fps` filled only on the `frame,total` row. Per-pass rows come
  straight from `GpuTimer::getResults()` / `CpuProfiler::getLastFrame()`.
- **Row assembly is a pure function** — `formatSampleRows(const ProfileSample&)
  → std::vector<std::string>` where `ProfileSample` is a plain struct of the
  averaged values. This is unit-tested headlessly (§11); `sample()` just
  aggregates into a `ProfileSample` and calls it.
- **Triggers (two thin entry points over one `ProfileLog`):**
  1. **CLI** `--profile-log[=PATH]` — sets `EngineConfig::profileLogPath`;
     `Engine::initialize()` calls `profiler.setEnabled(true)` + `ProfileLog::open`.
     Default path `vestige_profile_<unix_ts>.csv` in the CWD. This lets a
     headless / `--play` run capture data without the editor (so it can be
     driven and the log read back for analysis).
  2. **Panel toggle** — a "⏺ Log to CSV" checkbox in the Performance panel
     opens/closes the same `ProfileLog` at the default path for interactive runs.
- **Off by default:** no file, no throttled sampling, and (unless already on)
  no forced profiler-enable when neither trigger is used — zero overhead on
  normal launches. GPU timer queries carry a small cost, hence enabled only when
  logging is requested.
- **Placement:** file writing + averaging is **CPU/IO** (§7).

---

## 9. Accessibility

- **Reduce-motion:** grass wind and water wave animation are motion. If a global
  reduce-motion setting exists (Settings), the meadow honours it by damping wind
  amplitude + water `flowSpeed` to ~0; otherwise this is noted as a follow-up
  (do not invent a setting here). Verify the setting's presence during impl
  (Rule 13) before wiring.
- Scene readability does not depend on colour alone (geometry + lighting carry
  it). No text is added.

---

## 10. Implementation slices (each: build + verify)

1. **Flag rename** — `setupDemoScene`→`setupMaterialDemoScene`; add
   `materialDemo` field, arg case, help line, three-way branch; stub new
   `setupDemoScene()` that (for now) calls the material demo. *Verify:* builds;
   `--material-demo` and default both open the old scene; flag parse unit test.
2. **Assets in** — download + curate Kenney subset + HDRI into `assets/`;
   `ASSET_LICENSES.md` + `THIRD_PARTY_NOTICES.md` rows; `.gitignore`
   `nature_local/`. *Verify:* each `.glb` + the HDRI load without error (a small
   load smoke check / manual).
3. **Terrain + pond** — `meadow_terrain.{h,cpp}` pure helpers + reshape/carve +
   auto-texture + bank blend + water entity + HDRI sky + sun. *Verify:* rolling
   terrain + reflective pond render; height-field unit tests green.
4. **Grass** — terrain-following stamps + exclusions. *Verify:* dense grass
   hugs terrain, none in the pond; foliage count > threshold.
5. **Props** — scatter trees/rocks/flowers/lily/reeds/log via `instantiate`;
   `treePath` local-override hook. *Verify:* props sit on the surface, none in
   the pond; scatter unit tests green.
6. **Profiler CSV log** — `profile_log.{h,cpp}` + `--profile-log` flag +
   panel toggle; wire `sample()` after `endFrame`. *Verify:* `--profile-log`
   run produces a well-formed CSV with per-pass rows; row-format unit test
   green; no file / no overhead when the flag/toggle is off.
7. **Docs/tests/CHANGELOG/ROADMAP** — finalize; full local-CI incl. Windows.

---

## 11. Testing & verification

- **Unit (headless, no GL):** `meadowHeight01` (range 0..1, bowl floor below rim,
  gentle slopes, determinism); `scatterProps` (count, min-distance, exclusion
  disc honoured, determinism from seed); `--material-demo` + `--profile-log` arg
  parse set their fields; `formatSampleRows` (§8.1) emits a correct header +
  one row per pass/scope with the right category/name/ms/fps columns.
- **Visual-test harness** (`--visual-test`): meadow renders without GL errors /
  crashes (headless-capable path already used by CI).
- **Manual (the one un-automated step):** launch the editor, confirm the meadow
  looks right, open the **Performance panel**, and confirm/measure the 60-FPS
  behaviour + read initial hotspots.

---

## 12. Invariants

- **INV-1** `--material-demo` reproduces the *exact* pre-change scene (the old
  builder is moved verbatim, not re-authored).
- **INV-2** Default launch (no flags) opens the meadow; `--biblical-demo` still
  opens the Tabernacle; `--scene PATH` still overrides either.
- **INV-3** No grass or solid prop is placed inside the pond's water footprint.
- **INV-4** Every prop's Y equals `terrain.getHeight(x,z)` (lily pads excepted —
  they sit at `WATER_LEVEL_Y`).
- **INV-5** `meadowHeight01` returns values in [0,1]; the pond-bowl floor is
  strictly below the rim height (so water has depth).
- **INV-6** Committed assets are CC0/CC-BY with a matching `ASSET_LICENSES.md`
  row; nothing over ~5 MB enters the repo.
- **INV-7** Scene is deterministic given fixed `SEED_*` (stable profiling
  fixture).
- **INV-8** No new render pass or shader; terrain stays enabled
  (`m_terrainEnabled == true`) for the meadow.
- **INV-9** Profiler CSV logging is **off unless** `--profile-log` or the panel
  toggle is used: no file created and no added per-frame cost on a normal
  launch. When on, rows are interval **averages** (throttled ~1 Hz), not
  per-frame spam.
- **INV-10** `ProfileLog` never throws into the frame loop — a failed `open`
  (bad path/permissions) logs a warning and disables logging; the frame loop is
  unaffected.

---

## 13. Cold-eyes loop log
_(to be filled as loops run — project Rule 9 / global Rule 14)_

---

## 14. Sources

- Verified engine source (file:line) as cited in §3.
- Kenney Nature Kit 2.1 — kenney.nl, CC0 (`License.txt` in kit).
- Poly Haven — CC0 HDRIs/models (api.polyhaven.com).
- Existing scene builders: `setupDemoScene` (`engine.cpp:2166`),
  `setupTabernacleScene` (`engine.cpp:2545`), demo water/foliage
  (`engine.cpp:2476-2537`), `TerrainSystem::initialize` (`terrain_system.cpp:38`).
