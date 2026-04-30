# Subsystem Specification — `engine/environment`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/environment` |
| Status | `shipped` (foundation; Phase 15 atmospheric rendering pending) |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (terrain + foliage from early phases; `EnvironmentForces` consolidated Phase 8, wrapped in `AtmosphereSystem` Phase 9B) |

---

## 1. Purpose

`engine/environment` owns the **world-state primitives** that the rest of the engine queries to know "what does the air do here, what does the ground look like here, what grows here." It bundles four otherwise-orphan concerns under one roof: (a) `EnvironmentForces` — the **planned** canonical source of wind / weather / buoyancy / temperature / humidity / wetness / air-density queries. **The data structure ships and the API is stable; today no live consumer has been wired in yet** — every subsystem that ought to read it (cloth / foliage / particles / water / audio reverb) currently uses its own internal wind / weather state. Reconciling those readers onto the shared source is tracked across §15 (per-consumer rows) and the Phase 9B Atmosphere & Weather rollout. (b) `Terrain` — the heightmap-backed CDLOD (Continuous Distance-Dependent Level of Detail) ground surface with splatmap, normal map, raycast, and partial-region GPU upload; (c) `FoliageManager` + `FoliageChunk` — a 16 m × 16 m chunk grid storing grass / scatter / tree instances for paint-erase-cull workflows, plus the `BiomePreset` library and `DensityMap` for spatial modulation; (d) `SplinePath` — Catmull-Rom paths for roads / streams / clear-along-path operations. It exists as a separate subsystem because the renderer, physics, scene serialiser, and editor each touch a different subset of these primitives and pushing them inward (e.g. into renderer) would force unrelated subsystems to depend on each other through an unrelated path. For the engine's primary use case — first-person walkthroughs of biblical structures — `engine/environment` is the foundation that gets the user from "the Tabernacle is sitting on a flat green plane" to "the Tabernacle is on a sand-and-rock wadi with palms swaying in a gust that the cloth tent walls *will also* feel once §15 wiring lands."

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `EnvironmentForces` — central wind / weather / buoyancy / wetness query API | Atmospheric scattering / sky rendering — `engine/renderer/atmosphere_*` (Phase 15) |
| `WeatherState` struct (temperature, humidity, precipitation, wetness, cloud cover, air density) | Precipitation rendering (rain / snow particles) — `engine/systems/particle_system` |
| Gust state machine (blow / calm cycles) + spatial wind noise | GPU-side wind sampler (foliage / cloth shaders consume but do not own) — `engine/renderer/foliage_renderer.h`, cloth solvers |
| `Terrain` — CDLOD heightmap, normal map, splatmap, raycast, partial GPU updates | Terrain rendering pipeline (shaders, draw calls) — `engine/renderer/terrain_renderer.h` |
| Bank blending + auto-texture splatmap generation | Terrain physics collider (Jolt height-field shape) — `engine/physics/` |
| `FoliageChunk` (16 m × 16 m spatial cell) + `FoliageManager` (paint / erase / cull / serialize) | Foliage rendering / GPU instancing — `engine/renderer/foliage_renderer.h` |
| `FoliageInstance` / `ScatterInstance` / `TreeInstance` data structs + type configs | Mesh / texture loading for foliage — `engine/resource/` |
| `BiomePreset` + `BiomeLibrary` (composable layer descriptions) | Biome-painting editor tool UI — `engine/editor/tools/` |
| `DensityMap` — paintable world-space mask modulating foliage spawn | Density-map texture upload to GPU — none yet (CPU-only consumer) |
| `SplinePath` — Catmull-Rom waypoints, mesh generation for paths / streams | Path / road material authoring — `engine/resource/material.h` |
| Day/night abstraction (currently only via `WeatherState::cloudCover`) | Time-of-day sun position / lighting — `engine/systems/lighting_system.h` |
| JSON serialisation of every primitive above | Scene-level binding (which terrain / foliage belongs to which scene) — `engine/scene/scene.h` |

If a reader can't tell which side of the line a feature falls on, the row needs to be sharper.

## 3. Architecture

```
                 ┌────────────────────────────────────┐
                 │     AtmosphereSystem (ISystem)     │
                 │  engine/systems/atmosphere_system  │
                 │  owns ⇩, force-active, drives Wx   │
                 └─────────────┬──────────────────────┘
                               │ owns by-value
                               ▼
                 ┌────────────────────────────────────┐
                 │       EnvironmentForces            │  ← cached pointer in
                 │  wind / weather / buoyancy queries │    Engine::m_environmentForces
                 │  gust state machine + RNG          │    (engine/core/engine.h:224)
                 └─────────────┬──────────────────────┘
                               │ queried (read-only) by — *planned*
   ┌──────────────────┬────────┼────────┬───────────────────┬──────────────┐
   ▼                  ▼        ▼        ▼                   ▼              ▼
ClothSimulator   FoliageRenderer  WaterRenderer  ParticleSystem   (future) AudioReverb
(getWindVel)     (wind uniforms)  (wetness/buoy) (wind+turb)      (humidity → reverb)


                Independent primitives owned by Engine / Scene:

                ┌────────────────────────────────────┐
                │            Terrain                 │
                │  CDLOD quadtree + heightmap +      │
                │  normalmap + splatmap (GPU tex)    │
                │  raycast, auto-texture, bank blend │
                └─────────────┬──────────────────────┘
                              │ ground reference for
                              ▼
            ┌──────────────────────────────────────────────┐
            │            FoliageManager                    │
            │  std::unordered_map<chunkKey, FoliageChunk>  │
            │   16m × 16m cells   (chunk_size = 16.0 m)    │
            │     ├── foliage by typeId                    │
            │     ├── scatter (rocks / debris)             │
            │     └── trees                                │
            │  + paint / erase / cull / clear-along-path   │
            └─────┬────────────────────────────────────────┘
                  │ optionally modulated by
                  ▼
              ┌──────────────────────┐         ┌──────────────────┐
              │     DensityMap       │         │   SplinePath     │
              │  world-space mask    │         │  Catmull-Rom +   │
              │  paint / sample()    │         │  mesh generator  │
              └──────────────────────┘         └──────────────────┘

              ┌──────────────────────┐
              │   BiomeLibrary       │  (catalogue; pure data)
              │   built-in + user    │
              │   FoliageLayer /     │
              │   ScatterLayer /     │
              │   TreeLayer presets  │
              └──────────────────────┘
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `EnvironmentForces` | class | Single source of truth for wind / weather / buoyancy queries; owns gust state machine. `engine/environment/environment_forces.h:36` |
| `WeatherState` | struct | POD weather snapshot — temperature / humidity / precipitation / wetness / cloud cover / air density. `engine/environment/environment_forces.h:20` |
| `Terrain` | class | Heightmap + splatmap + CDLOD quadtree; owns CPU data and GPU textures. `engine/environment/terrain.h:61` |
| `TerrainConfig` | struct | Cold-start config — width / depth / spacing / height scale / LOD depth. `engine/environment/terrain.h:23` |
| `CDLODNode` | struct | One quadtree node — center / half-extent / min-max height / LOD level / 4 children. `engine/environment/terrain.h:37` |
| `TerrainDrawNode` | struct | One LOD-selected draw entry — world offset + scale + LOD + morph factor. `engine/environment/terrain.h:48` |
| `Terrain::BankBlendConfig` | struct | Width / channel / strength for blending a bank material near water edges. `engine/environment/terrain.h:212` |
| `Terrain::AutoTextureConfig` | struct | Slope + altitude thresholds + noise for auto-generated splatmaps. `engine/environment/terrain.h:229` |
| `FoliageManager` | class | Paint / erase / cull / serialise across the chunk grid. `engine/environment/foliage_manager.h:37` |
| `FoliageChunk` | class | 16 m × 16 m cell holding foliage / scatter / tree instances. `engine/environment/foliage_chunk.h:22` |
| `FoliageInstance` / `ScatterInstance` / `TreeInstance` | struct | Per-instance data (position / rotation / scale / type or species id). `engine/environment/foliage_instance.h:18,27,36` |
| `FoliageTypeConfig` / `ScatterTypeConfig` / `TreeSpeciesConfig` | struct | Type-level palette entry (mesh / texture / scale range / wind params / spacing). `engine/environment/foliage_instance.h:45,57,67` |
| `FoliageInstanceRef` | struct | `(chunkKey, typeId, instance)` triple for undo / redo. `engine/environment/foliage_manager.h:26` |
| `BiomePreset` / `BiomeLibrary` | struct + class | Composable layer mix (foliage + scatter + tree) with built-in and user presets. `engine/environment/biome_preset.h:40,57` |
| `DensityMap` | class | Paintable world-space grayscale mask modulating foliage spawn probability. `engine/environment/density_map.h:25` |
| `SplinePath` | class | Catmull-Rom waypoint chain with path / stream mesh generation. `engine/environment/spline_path.h:30` |
| `PathMeshData` | struct | Generated triangle-strip data (positions / normals / UVs / indices). `engine/environment/spline_path.h:18` |

## 4. Public API

`engine/environment` exposes eight public headers — past the seven-header cutoff for the inline-listing pattern (CODING_STANDARDS section 18), so this section uses the **per-header grouped form**. Each block summarises the public surface; the headers are the canonical reference.

```cpp
// engine/environment/environment_forces.h — central wind / weather / buoyancy queries.
struct WeatherState
{
    float temperature   = 20.0f;   // Celsius
    float humidity      = 0.5f;    // [0, 1]
    float precipitation = 0.0f;    // [0, 1]   (0 = none, 1 = heavy rain)
    float wetness       = 0.0f;    // [0, 1]   (surface wetness, integrated)
    float cloudCover    = 0.3f;    // [0, 1]
    float airDensity    = 1.225f;  // kg/m³ (sea level, 15 °C default)
};

class EnvironmentForces
{
public:
    EnvironmentForces();

    // -- Wind queries (position-dependent, thread-safe read-only) --
    glm::vec3 getWindVelocity (const glm::vec3& worldPos) const;            // m/s
    glm::vec3 getWindForce    (const glm::vec3& worldPos,
                               float surfaceArea_m2,
                               const glm::vec3& surfaceNormal) const;       // Newtons
    float     getWindSpeed         (const glm::vec3& worldPos) const;       // m/s scalar
    glm::vec3 getBaseWindDirection() const;                                 // unit vector
    float     getBaseWindStrength () const;                                 // m/s, pre-gust
    float     getGustIntensity    () const;                                 // [0, 1]
    glm::vec3 getWindDirectionOffset() const;                               // current jitter

    // -- Weather queries --
    float getTemperature           (const glm::vec3& worldPos) const;       // °C
    float getHumidity              (const glm::vec3& worldPos) const;       // [0, 1]
    float getWetness               (const glm::vec3& worldPos) const;       // [0, 1]
    float getPrecipitationIntensity() const;                                // [0, 1]
    float getAirDensity            (const glm::vec3& worldPos) const;       // kg/m³

    // -- Fluid queries --
    glm::vec3 getBuoyancy(const glm::vec3& worldPos,
                          float submergedVolume_m3,
                          float objectDensity_kg_m3) const;                 // upward Newtons

    // -- Configuration (main thread only — see §7) --
    void  setWindDirection (const glm::vec3& dir);  // normalised internally
    void  setWindStrength  (float m_per_s);
    void  setGustsEnabled  (bool);
    void  setTurbulenceScale(float);                // larger = slower spatial variation
    void  setWeather       (const WeatherState&);
    void  setWaterLevel    (float y_world);
    const WeatherState& getWeather() const;
    float getWaterLevel    () const;

    void  update         (float dt);                // once per frame, AtmosphereSystem only
    float getElapsedTime () const;
    void  reset          ();
};
```

```cpp
// engine/environment/terrain.h — heightmap + CDLOD + splatmap.
class Terrain
{
public:
    bool initialize  (const TerrainConfig&);
    void shutdown    ();
    bool isInitialized() const;

    // Height + normal queries (bilinear).
    float     getHeight (float worldX, float worldZ) const;
    glm::vec3 getNormal (float worldX, float worldZ) const;
    float     getRawHeight(int x, int z) const;
    void      setRawHeight(int x, int z, float normalised01);

    // CDLOD selection.
    void buildQuadtree();
    void selectNodes(const Camera&, float aspect, std::vector<TerrainDrawNode>& out) const;

    // Partial GPU updates (sculpting / painting hot path).
    void updateHeightmapRegion (int x, int z, int w, int h);
    void updateNormalMapRegion (int x, int z, int w, int h);
    void updateSplatmapRegion  (int x, int z, int w, int h);

    // Splatmap, GPU handles, raycast.
    void      setSplatWeight(int x, int z, int channel, float weight);
    glm::vec4 getSplatWeight(int x, int z) const;
    GLuint    getHeightmapTexture () const;  // R32F
    GLuint    getNormalMapTexture () const;  // RGB8
    GLuint    getSplatmapTexture  () const;  // RGBA8
    bool      raycast(const Ray&, float maxDist, glm::vec3& outHit) const;

    // Authoring helpers.
    void applyBankBlend     (const glm::vec2& center, const glm::vec2& halfExtent,
                             const BankBlendConfig&);
    void generateAutoTexture(const AutoTextureConfig&);

    // Persistence.
    nlohmann::json serializeSettings() const;
    bool           deserializeSettings(const nlohmann::json&);
    bool           saveHeightmap (const std::filesystem::path&) const;
    bool           loadHeightmap (const std::filesystem::path&);
    bool           saveSplatmap  (const std::filesystem::path&) const;
    bool           loadSplatmap  (const std::filesystem::path&);
};
// see terrain.h:61 for the full surface; nested config structs above + getters.
```

```cpp
// engine/environment/foliage_chunk.h — 16 m × 16 m cell.
class FoliageChunk
{
public:
    static constexpr float CHUNK_SIZE = 16.0f;     // metres, axis-aligned
    FoliageChunk(int gridX, int gridZ);

    // Foliage by type id, scatter, trees — symmetric add / remove-in-radius / get.
    // See foliage_chunk.h:32 for full surface (≈ 30 LOC public).
    AABB           getBounds()              const;
    bool           isEmpty()                const;
    int            getTotalInstanceCount()  const;
    nlohmann::json serialize()              const;
    void           deserialize(const nlohmann::json&);
};
```

```cpp
// engine/environment/foliage_manager.h — paint / erase / cull / serialise.
class FoliageManager
{
public:
    // Paint / erase (all return undo-records).
    std::vector<FoliageInstanceRef> paintFoliage(uint32_t typeId, const glm::vec3& center,
                                                 float radius, float density, float falloff,
                                                 const FoliageTypeConfig&,
                                                 const DensityMap* = nullptr);
    std::vector<FoliageInstanceRef> eraseFoliage   (uint32_t typeId, const glm::vec3&, float);
    std::vector<FoliageInstanceRef> eraseAllFoliage(const glm::vec3&, float);
    void restoreFoliage(const std::vector<FoliageInstanceRef>&);
    void removeFoliage (const std::vector<FoliageInstanceRef>&);

    // Scatter / trees — symmetric API. See foliage_manager.h:104 for full surface.

    // Path clearing.
    int clearAlongPath(const SplinePath&, float margin = 0.5f);

    // Frustum / shadow culling — out-param overloads avoid per-frame allocations.
    std::vector<const FoliageChunk*> getVisibleChunks(const glm::mat4& viewProj) const;
    void                              getVisibleChunks(const glm::mat4&,
                                                       std::vector<const FoliageChunk*>& out) const;
    std::vector<const FoliageChunk*> getAllChunks() const;
    void                              getAllChunks(std::vector<const FoliageChunk*>& out) const;
    const FoliageChunk*               getChunk(int gridX, int gridZ) const;

    int            getTotalFoliageCount() const;
    int            getChunkCount       () const;
    nlohmann::json serialize           () const;
    void           deserialize         (const nlohmann::json&);
    void           clear               ();

    static uint64_t packChunkKey  (int gridX, int gridZ);
    static void     unpackChunkKey(uint64_t key, int& gridX, int& gridZ);
};
```

```cpp
// engine/environment/foliage_instance.h — pure data structs.
struct FoliageInstance     { glm::vec3 position; float rotation_rad, scale; glm::vec3 colorTint; };
struct ScatterInstance     { glm::vec3 position; glm::quat rotation; float scale; uint32_t meshIndex; };
struct TreeInstance        { glm::vec3 position; float rotation_rad, scale; uint32_t speciesIndex; };
struct FoliageTypeConfig   { /* name, texturePath, scale range, wind amp/freq, tintVariation */ };
struct ScatterTypeConfig   { /* name, meshPath,  scale range, surfaceAlignment ∈ [0,1]      */ };
struct TreeSpeciesConfig   { /* name, meshPath,  billboardTexturePath, scale range, minSpacing */ };
```

```cpp
// engine/environment/biome_preset.h — composable presets.
struct FoliageLayer { uint32_t typeId; float density_per_m2; };
struct ScatterLayer { uint32_t typeId; float density_per_m2; };
struct TreeLayer    { uint32_t speciesId; float density_per_m2; };

struct BiomePreset
{
    std::string name, groundMaterialPath;
    std::vector<FoliageLayer> foliageLayers;
    std::vector<ScatterLayer> scatterLayers;
    std::vector<TreeLayer>    treeLayers;
    nlohmann::json serialize() const;
    static BiomePreset deserialize(const nlohmann::json&);
};

class BiomeLibrary
{
public:
    BiomeLibrary();                                    // populates built-ins
    int                       getPresetCount() const;
    const BiomePreset&        getPreset(int idx) const;
    std::vector<std::string>  getPresetNames() const;
    void                      addPreset(const BiomePreset&);
    nlohmann::json            serializeUserPresets() const;     // built-ins skipped
    void                      deserializeUserPresets(const nlohmann::json&);
};
```

```cpp
// engine/environment/density_map.h — paintable spawn-probability mask.
class DensityMap
{
public:
    void  initialize(float originX, float originZ, float worldW, float worldD,
                     float texelsPerMetre = 1.0f);
    bool  isInitialized() const;
    float sample(float worldX, float worldZ) const;            // bilinear, OOB → 1.0
    void  paint  (const glm::vec3& center, float radius,
                  float value, float strength, float falloff);
    void  clearAlongPath(const SplinePath&, float margin = 0.5f);
    void  fill   (float value = 1.0f);
    // Texel access + dimensions + JSON round-trip — see density_map.h:67.
};
```

```cpp
// engine/environment/spline_path.h — Catmull-Rom waypoints + mesh gen.
struct PathMeshData
{
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t>  indices;
};

class SplinePath
{
public:
    // Waypoint editing (add / insert / remove / move) + count / list accessors.
    glm::vec3    evaluate       (float t01) const;            // clamped, passes through waypoints
    glm::vec3    evaluateTangent(float t01) const;            // unit vector
    float        getLength      (int samples = 100) const;
    PathMeshData generatePathMesh  (float halfWidth, float spacing = 0.5f) const;
    PathMeshData generateStreamMesh(float halfWidth, float spacing = 0.5f) const;
    nlohmann::json serialize() const;
    void           deserialize(const nlohmann::json&);

    std::string name = "Path";
    float       width = 1.5f;
    std::string materialPath;
};
// see spline_path.h:30 for the full surface.
```

**Non-obvious contract details:**

- `EnvironmentForces::update(dt)` is the **single mutator on the per-frame hot path** — every other query is `const`. `AtmosphereSystem::update` (force-active) is the sole legitimate caller; do not call it twice in one frame or the gust state machine advances at 2× rate.
- `EnvironmentForces` query results are **frame-coherent** within an `update()` cycle: `m_cachedFlutter` is computed once per `update` and re-used by every `getWindVelocity()` call until the next `update`, so two systems that query the same world position in the same frame see the same wind vector (deterministic).
- `getTemperature`, `getHumidity`, `getWetness`, `getAirDensity` accept a `worldPos` but currently return the global field — the position parameter is **forward-compat** (Phase 15 will introduce position-varying weather). Treat the signature as load-bearing, not the implementation.
- `EnvironmentForces::reset()` re-seeds the LCG (Linear Congruential Generator) RNG (Random Number Generator) with the fixed seed `54321u` — gust-state replays are deterministic across runs.
- `Terrain::initialize` requires `(width − 1)` and `(depth − 1)` to be **powers of two** — see `TerrainConfig` defaults `257 × 257`. The CDLOD quadtree assumes power-of-two-plus-one; non-conforming dimensions return `false` with a warning.
- `Terrain::deserializeSettings` **reinitialises** the terrain (destructive). Heightmap data is reset to flat; load the `.r32` heightmap separately via `loadHeightmap`.
- `Terrain::raycast` uses uniform stepping then bilinear-height refinement — `maxDist` is metres, not heightmap texels.
- `FoliageChunk::CHUNK_SIZE = 16.0` m is **load-bearing** for the packed-key format (`packChunkKey(x, z)` assumes int32 grid coords); changing it without a versioned save migration breaks scene files.
- `FoliageChunk::getFoliage(typeId)` returns `EMPTY_FOLIAGE` (a static empty vector) when the type id is not present — never returns a dangling reference. Mutable equivalent (`getFoliageMutable`) returns `nullptr`.
- `FoliageManager::paintScatter` / `placeTree` enforce **minimum spacing** (`TreeSpeciesConfig::minSpacing`) — too-close placement returns an empty undo record, not an error.
- `FoliageManager::getVisibleChunks` (out-param overload) preserves caller-owned vector capacity across frames; the by-value overload allocates each call (audit-flagged H9). Render callers must use the out-param form.
- `DensityMap::sample` returns **`1.0` for out-of-bounds** queries (forgiving; foliage paint then proceeds at full requested density). This is intentional — an uninitialised density map is "all on" so callers don't need a null-check around every sample.
- `BiomeLibrary::serializeUserPresets` deliberately **skips built-ins** — built-ins live in code, not on disk. `m_builtInCount` is the dividing line.
- `SplinePath::evaluate(t)` clamps `t ∈ [0, 1]`. The spline **passes through** all waypoints (Catmull-Rom interpolating form, not B-spline). Endpoints are duplicated internally for the first/last segment.

**Stability:** the public API above is semver-frozen for `v0.x` with two known evolution points: (a) the `worldPos` arg on weather queries (Phase 15 will start honouring it — additive); (b) `EnvironmentForces` will gain a per-zone weather override mechanism (planned for Phase 15, again additive — current global-state queries continue to work).

## 5. Data Flow

**Steady-state per-frame:**

1. `AtmosphereSystem::update(dt)` (registered in `UpdatePhase::Update` group, force-active per `engine/systems/atmosphere_system.h:31`) → `EnvironmentForces::update(dt)`:
   1. `m_elapsed += dt`.
   2. `updateGustState(dt)` advances the gust state machine (calm ↔ blow), the direction-shift state machine, and uses the LCG RNG.
   3. `m_cachedFlutter` is recomputed from `m_elapsed` (two-sine combination).
   4. `m_weather.wetness` integrates: precipitation > 0 saturates in ~30 s; otherwise dries in ~120 s.
2. Read-side, in any later phase / system / shader-uniform pack: `getWindVelocity(pos)` / `getWindForce(pos, A, n)` / `getWetness(pos)` / `getBuoyancy(pos, V, ρ)` — all `const`, cheap.
3. **Planned (not yet wired):** cloth / foliage / particle / water consumers will pull per-frame `windVel = environmentForces.getWindVelocity(centroid)`. Today none of them do — `ClothSimulator::getWindVelocity` (`engine/physics/cloth_simulator.cpp:633`) returns its **own** internal `m_windDirection * m_windStrength` rather than calling into `EnvironmentForces`. Phase 9B Atmosphere & Weather rollout reconciles each consumer; until then the readers in this step are aspirational.
4. Foliage / water / particle renderers similarly query per draw / per frame; the cached flutter ensures consistency.

**Terrain hot path (per-frame render):**

1. `Terrain::selectNodes(camera, aspect, out)` walks the CDLOD quadtree, frustum-culling against `FrustumPlanes` and selecting LOD by camera distance into `m_lodRanges`.
2. `TerrainRenderer` (out-of-scope) reads `getHeightmapTexture()` / `getNormalMapTexture()` / `getSplatmapTexture()` and the `out` draw list.
3. Editor sculpting calls `setRawHeight` + `updateHeightmapRegion(x, z, w, h)` to push a region to GPU without re-uploading the whole heightmap.

**Foliage paint / erase (editor-driven):**

1. Editor brush → `FoliageManager::paintFoliage(typeId, center, r, density, falloff, config, densityMap?)` →
2. For each candidate position (Poisson-ish sampling within radius), sample density map (if any), apply falloff, decide accept / reject.
3. `worldToGrid(pos)` → `getOrCreateChunk(gx, gz)` → `chunk.addFoliage(typeId, instance)`.
4. Returns `vector<FoliageInstanceRef>` for the editor's undo stack.
5. Symmetric: `eraseFoliage` returns the same shape; `restoreFoliage` / `removeFoliage` are the undo / redo handlers.

**Scene save / load:**

1. Save: `terrain.serializeSettings()`, `foliageManager.serialize()`, density-map / spline arrays serialised through `nlohmann::json` — written via `engine/utils/atomic_write.h` so a partial write never corrupts a scene.
2. Load: deserialise inverse; on schema mismatch, defaults populate and a `Logger::warning` is emitted.

**Cold start:**

1. `AtmosphereSystem::initialize()` is a no-op aside from constructing `EnvironmentForces` (which calls `reset()` itself); no GL resources.
2. `Terrain::initialize(config)` allocates CPU buffers and creates GL textures (R32F heightmap, RGB8 normal, RGBA8 splat) on the main thread.
3. Scene load populates the foliage chunk grid via `FoliageManager::deserialize`.

**Exception path:** every primitive returns `bool` / empty-vector / `false` on failure, logs a warning, and leaves prior state intact. No exceptions are thrown.

## 6. CPU / GPU placement

Per CLAUDE.md Rule 7. `engine/environment` is a **mostly-CPU** subsystem with `Terrain` straddling CPU + GPU as **infrastructure plumbing** (it owns the GPU textures the renderer samples).

| Workload | Placement | Reason |
|----------|-----------|--------|
| `EnvironmentForces` queries (wind / weather / buoyancy / wetness) | **CPU** (main + worker thread reads) | Branching, data-light, query API by design — CODING_STANDARDS section 17 default for sparse work. |
| `EnvironmentForces::update` (gust state machine + flutter cache + wetness integration) | **CPU** (main thread, `AtmosphereSystem`) | Per-frame, sequential state machine; no per-pixel/per-particle workload to vectorise. |
| Wind sampling **inside** cloth / foliage / water shaders | **GPU** (consumer-side) | The CPU hands a sparse uniform set (`baseDir`, `strength`, `gustIntensity`, `flutter`) and the shader does the per-vertex / per-particle eval. Spatial noise in the shader would re-implement `hashNoise` from `environment_forces.cpp:50` — not yet shared; flagged as Open Q1. |
| `Terrain` heightmap / normal / splat data (CPU side) | **CPU** | Edited via raycast / sculpt / paint — sparse, branching, undo-tracked. |
| Heightmap / normal / splatmap **textures** | **GPU** (R32F / RGB8 / RGBA8 owned by `Terrain`) | Sampled per-vertex (terrain morph) and per-pixel (terrain shading) by `TerrainRenderer`. CPU edits are pushed to GPU via `glTexSubImage2D` regions (`Terrain::updateHeightmapRegion` etc.) on the main thread. |
| CDLOD node selection | **CPU** | Camera-frustum + distance — sparse decision; output is a small draw-list (≤ 64 nodes typical). |
| `Terrain::raycast` | **CPU** | Editor picking; sparse, branching. |
| `FoliageManager` paint / erase / cull / serialize | **CPU** | Editor authoring + per-frame frustum cull (small `n`); GPU side lives in `engine/renderer/foliage_renderer.h`. |
| `DensityMap::sample` / `paint` | **CPU** | Brush authoring (rare) + foliage-paint sampling (paint-time, not per-frame). |
| `SplinePath` mesh generation | **CPU** (one-shot) | Authoring time; output is a static mesh handed to the renderer. |

**Dual implementation:** none. The wind hash noise is not currently mirrored on the GPU — Phase 15 will introduce a shared GLSL helper if shaders need spatial wind variation. When that lands, a parity test (`tests/test_environment_forces_gpu_parity.cpp`) will pin bit-equivalence to within `1e-4` tolerance (RGBA8 sampler-level error budget).

## 7. Threading model

Per CODING_STANDARDS section 13.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** | All of `EnvironmentForces`, `Terrain`, `FoliageManager`, `FoliageChunk`, `BiomeLibrary`, `DensityMap`, `SplinePath`. | None. |
| **Worker thread** (job system, async loaders) | `EnvironmentForces` `const` queries (`getWindVelocity`, `getWeather`, `getWetness`, `getBuoyancy`, etc.). `Terrain::getHeight` / `getNormal`. `DensityMap::sample`. `SplinePath::evaluate` / `evaluateTangent`. `FoliageChunk::getFoliage` / `getScatter` / `getTrees` (read-only). | None — these methods touch only `const` member data. |
| **Worker thread** (mutators) | **Forbidden.** `EnvironmentForces::update`, `Terrain::setRawHeight` / `update*Region`, `FoliageManager::paint*` / `erase*`, `DensityMap::paint`, `SplinePath::addWaypoint` — all main-thread-only. | n/a |

**Thread-safety guarantees (explicit):**

1. `EnvironmentForces` const queries are **safe to call from any thread** **provided no thread is concurrently calling `update()` or `set*()`**. There is **no internal lock** — the AtmosphereSystem update + reader pattern matches "single writer (main thread, between frames) → many readers (workers, during their own update)" and exploits the engine's frame-phase barrier rather than a runtime mutex. If a worker reads while `update()` is mid-flight on the main thread, the result is racy on `m_cachedFlutter` / `m_gustCurrent` / `m_windDirOffset` and the read may tear.
2. `Terrain` const queries (`getHeight`, `getNormal`, `getSplatWeight`) are similarly safe from worker threads provided no editor mutation is in flight.
3. `FoliageChunk` and `FoliageManager` are **not** thread-safe — concurrent paint / cull races on `std::unordered_map<uint64_t, std::unique_ptr<FoliageChunk>>` (rehash invalidates iterators).
4. The phrase "thread-safe read-only" in `EnvironmentForces` doc-comments means exactly the const-during-frame-phase contract above — not "lock-free wait-free under any access pattern."

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. `engine/environment` is a query / authoring / per-frame-bookkeeping subsystem; the bulk of GPU cost lives in `engine/renderer`.

Not yet measured — will be filled by Phase 11 audit (Tracy capture); tracked as Open Q3 in §15.

Profiler markers / capture points (per `engine/profiler/performance_profiler.h`):

- `AtmosphereSystem::update` is one of the registered `ISystem` entries; its per-frame timing falls out of `SystemRegistry` metrics under the system name `"Atmosphere"`.
- Terrain partial uploads emit `glPushDebugGroup("Terrain heightmap region")` markers (verify with RenderDoc capture against `terrain.cpp` once Phase 11 audit confirms — currently TBD pending audit).
- No `engine/environment` code emits its own `glPushDebugGroup` markers today — the GPU work is upstream in `engine/renderer/`.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (`std::vector` / `std::unordered_map` for chunk grid, foliage instances, terrain CPU buffers); GL textures on GPU. No arena, no per-frame transient allocator. |
| Reusable per-frame buffers | `FoliageManager::getVisibleChunks(out)` and `getAllChunks(out)` accept caller-owned vectors so render callers retain capacity (audit H9). `Terrain::selectNodes(out)` likewise. |
| Peak working set (terrain, default 257 × 257) | CPU: heightmap 257² × 4 B ≈ 256 KB; normal map 257² × 12 B ≈ 768 KB; splatmap 257² × 16 B ≈ 1 MB. GPU: heightmap R32F 256 KB, normal map RGB8 ≈ 192 KB, splatmap RGBA8 ≈ 256 KB. Total ≈ ~3 MB CPU + ~1 MB GPU per terrain. |
| Peak working set (foliage) | Per `FoliageInstance` ≈ 28 B; a "generous" scene (5 K instances) ≈ 140 KB CPU. Tree + scatter add proportionally. The GPU instance buffer (in `foliage_renderer`) is the upper bound, not this subsystem. |
| `EnvironmentForces` working set | < 200 B (a handful of floats + RNG state); negligible. |
| Ownership | `Engine` owns `Terrain` + `FoliageManager` (raw-pointer cached, allocated by registered systems / scene). `AtmosphereSystem` owns `EnvironmentForces` by value. `FoliageManager::m_chunks` owns its `std::unique_ptr<FoliageChunk>` entries. |
| Lifetimes | `EnvironmentForces` engine-lifetime. `Terrain` + `FoliageManager` typically scene-lifetime (`SceneManager::loadScene` rebuilds them). Density maps / splines: scene-lifetime, edited in place. |

No `new` / `delete` in feature code (CODING_STANDARDS section 12).

## 10. Error handling

Per CODING_STANDARDS section 11 — no exceptions in steady state.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| `Terrain::initialize` invalid config (non-power-of-two-plus-one, zero spacing, etc.) | `Logger::warning` + `return false` | Caller falls back to defaults / aborts scene load. |
| `Terrain::loadHeightmap` size mismatch with current config | `Logger::warning` + `return false`; existing heights untouched | Caller logs + retries with correct dimensions. |
| `Terrain::raycast` no intersection within `maxDist` | `return false`; `outHit` unchanged | Caller treats as a miss. |
| `Terrain` query out-of-bounds (`getHeight(worldX, worldZ)` outside heightmap) | Returns clamped-edge height (no error); same for `getNormal` (clamped). | Caller may pre-check `getWorldWidth()` / `getWorldDepth()` if it wants to detect off-map queries. |
| `EnvironmentForces` query at any position | Always defined (no OOB concept — wind is global); positions far from origin yield large hash inputs but still finite floats. | n/a — no failure path. |
| `EnvironmentForces::setWindDirection({0,0,0})` (degenerate) | Silently ignored (length check `< 0.0001f` in `environment_forces.cpp:314`); previous direction retained. | Caller passes a non-zero vector. |
| `EnvironmentForces::getWeather()` when never set | Returns `WeatherState{}` defaults (20 °C, 0.5 humidity, no precipitation, 0 wetness, 30% cloud cover, 1.225 kg/m³). | None — defaults are by design. |
| `FoliageManager::placeTree` too close to existing tree | Returns empty undo-record vector; no `Logger` noise (paint-rate operation). | Caller treats empty as "rejected, try elsewhere." |
| `FoliageManager::getChunk(gx, gz)` no such chunk | Returns `nullptr`. | Caller null-checks. |
| `FoliageChunk::getFoliage(typeId)` unknown id | Returns `EMPTY_FOLIAGE` (static empty vector — never dangling). | Caller iterates the empty range as a no-op. |
| `DensityMap::sample` outside bounds | Returns `1.0` (forgiving — uninitialised mask = "no modulation"). | Caller relies on the documented OOB behaviour. |
| `DensityMap::paint` before `initialize` | No-op (writes to an empty buffer); `Logger::warning` not currently emitted. | Caller checks `isInitialized()` first. **Open Q2: should this assert?** |
| JSON deserialisation of any primitive | Schema-mismatched fields fall back to defaults; unknown fields ignored; `Logger::warning` for major fields. | Caller logs + continues with the partially-loaded result. |
| Save failure (`saveHeightmap` / `saveSplatmap` disk full / permission) | `return false` (atomic-write didn't commit; old file intact via `engine/utils/atomic_write.h`). | Caller surfaces the error to the user. |
| Programmer error (negative texel, OOB `setRawHeight`) | `assert` (debug) / UB (release) | Fix the caller. |

`Result<T, E>` / `std::expected` not yet used in `engine/environment` (predates the engine-wide policy). Migration is on the broader engine debt list, not specific to this subsystem.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `EnvironmentForces` wind / gust / weather query API + determinism | `tests/test_environment_forces.cpp` | Public API contract + LCG-seeded replay |
| `Terrain` config caps + memory upper bound | `tests/test_terrain_size_caps.cpp` | Size validation guard |
| `FoliageChunk` add / remove / serialize round-trip | `tests/test_foliage_chunk.cpp` | Public API contract |
| `BiomePreset` + `BiomeLibrary` JSON round-trip + built-in invariance | `tests/test_biome_preset.cpp` | Schema |
| `DensityMap` paint / sample / clear-along-path / serialize | `tests/test_density_map.cpp` | Public API contract |
| `SplinePath` Catmull-Rom evaluation, mesh generation, serialize | `tests/test_spline_path.cpp` | Public API contract |
| Catmull-Rom math (focused) | `tests/test_catmull_rom_spline.cpp` | Numerical correctness |
| Cubic spline reference (math primitives) | `tests/test_cubicspline.cpp` | Reference / sanity |

**Coverage gaps:**

- `Terrain` GPU upload / partial-region update is not unit-tested — requires a GL context. Visual coverage via `engine/testing/visual_test_runner.h` (terrain sculpt tools).
- `FoliageManager::paintFoliage` / `paintScatter` density-map modulation **integration** — covered in spirit by `test_density_map.cpp` + `test_foliage_chunk.cpp` separately, but no end-to-end paint-with-mask test exists. Open Q4.
- CDLOD node selection (`Terrain::selectNodes`) has no dedicated unit test (camera-frustum integration is implicit). Visual-test only. Open Q5.

**Adding a test for `engine/environment`:** drop a new `tests/test_<thing>.cpp` next to its peers, link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `EnvironmentForces` / `FoliageManager` / `DensityMap` / `SplinePath` directly without an `Engine` instance — every primitive **except `Terrain`'s GPU side and any code path that calls `glTexSubImage2D`** is unit-testable headlessly. Determinism: `EnvironmentForces` re-seeds via `reset()`; tests should call it explicitly.

## 12. Accessibility

`engine/environment` itself produces no user-facing pixels, sound, or focus rings. **However**, several settings flow through it indirectly:

- **Photosensitive caps:** wind-driven foliage motion respects `Settings::accessibility::reducedMotion` not directly here but via the **consumer** (foliage shader scales `windAmplitude` in `FoliageTypeConfig` by the photosensitive-safety multiplier when reduced-motion is enabled — that multiplication happens in `engine/renderer/foliage_renderer`, sourced from `engine/accessibility/photosensitive_safety.h`).
- **Reduced motion:** `EnvironmentForces` does **not** itself attenuate gust intensity when reduced-motion is enabled — the consumers do. This is intentional: the simulation-side state stays canonical; the rendering-side decides how much to display. If a future use case demands sim-side attenuation (e.g. cloth tearing thresholds), add a `getReducedMotionScalar()` accessor rather than mutating `m_gustCurrent`.
- **Subtitle / caption hooks:** weather state changes (rain start, thunderstorm) emit `WeatherChangedEvent` (`engine/core/system_events.h`) which the subtitle / audio systems may pick up for caption-friendly weather announcements. `engine/environment` is the publisher; `engine/ui` owns the caption-rendering side.
- **Color encoding:** the splatmap's RGBA channel-to-material mapping (R = grass, G = rock, B = dirt, A = sand by convention in `Terrain::generateAutoTexture`) is internal — never surfaced to the user as a color-only signal. Editor splat-painting UI must label channels with text + icon, not colour alone.

Constraint summary for downstream UIs that consume `engine/environment`:

- Foliage / cloth / particle shaders that read `EnvironmentForces` MUST scale visible motion by the photosensitive-safety multiplier when reduced-motion is on.
- Editor brush UI for `DensityMap::paint` and `Terrain` sculpting MUST surface the brush radius / strength textually (mouse-position metres + numeric strength), not solely as a translucent visual ring.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/utils/deterministic_lcg_rng.h` | engine subsystem | Seeded LCG for the gust state machine; deterministic replay. |
| `engine/utils/aabb.h` | engine subsystem | `FoliageChunk::getBounds` and frustum culling; `Terrain` raycast. |
| `engine/utils/frustum.h` | engine subsystem | Frustum culling for terrain CDLOD + foliage chunks. |
| `engine/renderer/camera.h` | engine subsystem | Camera + ray types for `Terrain::raycast` / `selectNodes`. |
| `engine/editor/tools/brush_tool.h` | engine subsystem | `Ray` type used by `Terrain::raycast` (header-only `Ray` typedef — circular-dep risk; flagged as Open Q6). |
| `engine/core/i_system.h` | engine subsystem | `AtmosphereSystem` (in `engine/systems/`) implements `ISystem`; the wrap lives outside this subsystem, but the `EnvironmentForces` API is shaped around once-per-frame `update`. |
| `<glm/glm.hpp>` + `<glm/gtc/quaternion.hpp>` | external | Math primitives (`vec2`, `vec3`, `vec4`, `quat`, `mat4`). |
| `<glad/gl.h>` | external | `GLuint` for terrain texture handles. |
| `<nlohmann/json.hpp>` + `<nlohmann/json_fwd.hpp>` | external | JSON serialisation for every primitive. |
| `<filesystem>`, `<vector>`, `<unordered_map>`, `<memory>`, `<cstdint>`, `<string>`, `<algorithm>`, `<cmath>`, `<cstring>` | std | Containers, paths, math, hashing. |

**Direction:** `engine/environment` may `#include` from `engine/utils`, `engine/renderer/camera.h`, `engine/editor/tools/brush_tool.h`. It must **not** include from `engine/scene`, `engine/physics`, `engine/audio`, or any concrete `ISystem`. Domain consumers (cloth simulator, foliage renderer, water renderer, particle system) `#include` `environment_forces.h` directly. The `AtmosphereSystem` wrap (`engine/systems/atmosphere_system.h`) is the one place where `engine/environment` data crosses into the `ISystem` execution model.

## 14. References

Cited research / authoritative external sources:

- Filip Strugar. *Continuous Distance-Dependent Level of Detail for Rendering Heightmaps* (CDLOD), Journal of Graphics, GPU & Game Tools, 2009 (canonical paper, still actively cited 2025). <https://aggrobird.com/files/cdlod_latest.pdf>
- Filip Strugar et al. *CDLOD reference implementation.* GitHub. <https://github.com/fstrugar/CDLOD>
- Andrew Maximov / Sucker Punch. *Using Vorticles to Simulate Wind in Ghost of Tsushima* — GDC 2020 / Game Developer write-up; the inspiration for the time-varying Perlin-noise wind field this engine adapts (`hashNoise`-based, no vorticles yet). <https://www.gamedeveloper.com/design/using-vorticles-to-simulate-wind-in-i-ghost-of-tsushima-i->
- Yelzkizi. *Wind in Unreal Engine 5: WindDirectionalSource, Foliage Wind, Niagara Forces, Cloth and Groom Hair Setup* (2025) — current-idiom reference for engine-wide wind parameter set (Strength, Speed, Min/Max Gust, Turbulence). <https://yelzkizi.org/wind-in-unreal-engine-5-winddirectionalsource-foliage-wind-niagara-forces-cloth-and-groom-hair-setup/>
- 80 Level / community. *Realistic Wind in Unreal Engine 5 Tutorial* (2025) — Niagara-based 2D wind system with controllable direction + force + gusts + turbulence; matches the `EnvironmentForces` query-shape (sparse parameters consumed by GPU). <https://80.lv/articles/a-new-tutorial-explains-how-to-make-realistic-wind-in-unreal-engine-5>
- Oscar Rehnberg. *Static vs Dynamic Weather Systems in Video Games* (DiVA 2021, refreshed 2024) — weather-FSM (Finite State Machine) state catalogue + transition probabilities; informs the `WeatherState` design and the in-progress Phase 15 transition logic. <https://www.diva-portal.org/smash/get/diva2:1524012/FULLTEXT02>
- StraySpark. *UE5.7 Nanite Foliage + Procedural Placement: Performance Guide for Open Worlds* (2025) — current-best-practice for chunk grids + indirect instancing; benchmarks the chunk-size + density-map combination this engine uses. <https://www.strayspark.studio/blog/ue5-nanite-foliage-procedural-placement-performance>
- Terrain3D Foliage Instancer documentation (Godot, 2025) — 32 m × 32 m cell sizing rationale + paint-density UX patterns. Comparison point for the 16 m × 16 m choice this engine made (denser cells = finer cull granularity at higher per-frame map cost). <https://terrain3d.readthedocs.io/en/stable/docs/instancer.html>
- Eastshade Studios. *Foliage Optimization in Unity* — chunk-grid + per-chunk visibility article, archetype for the approach. <https://eastshade.com/foliage-optimization-in-unity/>

Internal cross-references:

- `CODING_STANDARDS.md` section 11 (errors), section 12 (memory), section 13 (threading), section 17 (CPU/GPU), section 18 (public API), section 27 (units).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).
- `docs/phases/phase_09b_design.md` — `AtmosphereSystem` wrap of `EnvironmentForces`.
- `docs/ROADMAP.md` Phase 15 — atmospheric rendering (volumetric clouds, sky, position-varying weather).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Wind hash noise (`environment_forces.cpp:50`) is CPU-only. Should consumers (foliage / particle shaders) implement an identical GLSL helper for GPU-side spatial wind variation, with a parity test pinning bit-equivalence? | milnet01 | Phase 15 entry |
| 2 | `DensityMap::paint` before `initialize` is a silent no-op. Should it `assert` (debug) and `Logger::warning` (release) instead? | milnet01 | Phase 11 audit |
| 3 | `engine/environment` performance budget (§8) is unmeasured. One Tracy + RenderDoc capture covering `AtmosphereSystem::update` + `Terrain::selectNodes` + `FoliageManager::getVisibleChunks` would close it. | milnet01 | Phase 11 audit |
| 4 | No end-to-end test for `FoliageManager::paintFoliage` × `DensityMap` modulation. Density-mask spawn-count test would close it. | milnet01 | Phase 11 audit |
| 5 | `Terrain::selectNodes` (CDLOD node selection) has no dedicated unit test — relies on visual coverage + camera-integration smoke. A frustum + camera-distance unit test would harden the morph-factor / LOD-step contract. | milnet01 | Phase 11 audit |
| 6 | `terrain.h` includes `editor/tools/brush_tool.h` to pull in the `Ray` typedef — flips the engine→editor dependency direction for a one-line typedef. Move `Ray` to `engine/utils/ray.h` and break the cycle. | milnet01 | Phase 11 audit |
| 7 | Position-varying weather queries (`getTemperature(pos)` etc.) currently ignore `worldPos`. Phase 15 will introduce per-zone overrides; document the migration plan + backward-compat policy before the change lands. | milnet01 | Phase 15 entry |
| 8 | `EnvironmentForces` const queries are documented as thread-safe under "no concurrent mutator" but enforced by frame-phase convention rather than an explicit lock or atomic. Should we adopt `std::shared_mutex` for explicit protection, or formalise the convention with a `VESTIGE_ASSERT_MAIN_THREAD()` on mutators? | milnet01 | triage (no scheduled phase) |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/environment` foundation (terrain + foliage from early phases; `EnvironmentForces` consolidated Phase 8, wrapped by `AtmosphereSystem` Phase 9B), formalised post-Phase 10.9 audit. Phase 15 atmospheric-rendering hooks listed as forward-compat in §4 / §15. |
