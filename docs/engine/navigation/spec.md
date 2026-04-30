# Subsystem Specification — `engine/navigation`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/navigation` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.x` (Recast/Detour pipeline added pre-Phase 10) |

---

## 1. Purpose

`engine/navigation` is the navigation-mesh (navmesh) layer: it bakes a walkable surface from scene triangle geometry, then answers "find me a path from A to B" / "snap this point to walkable ground" / "is this point on the navmesh" queries against that surface. It exists as a separate subsystem because the cost split is dramatic — bake is heavyweight, editor-time, one-shot; query is lightweight, per-frame, hot-path — and because Recast/Detour (the upstream library) draws the same line. For the engine's primary use case — first-person walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — `engine/navigation` is what lets a guide-NPC, a roaming priest, or an interactive tour-companion follow a believable route through architecture authored in the editor. It is **not** the player's collision-resolution path (that's `engine/core/first_person_controller.h` + `engine/physics/`); it is the path NPCs and AI agents reason about.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `NavMeshBuilder` — Recast voxelisation pipeline (heightfield → compact heightfield → contours → poly mesh → detail mesh → Detour data) | Triangle source: vertex/index buffers come from `engine/scene/` `MeshRenderer` components |
| `NavMeshQuery` — A* polygon search, string-pulled waypoint extraction, nearest-poly snap, on-mesh test | Crowd / steering / boids — Recast's `DetourCrowd` is **not** wrapped (Open Q1) |
| `NavMeshBuildConfig` — agent radius / height / max-climb / slope, voxel cell size, region thresholds | Tile cache, dynamic obstacles (`dtTileCache`) — not wrapped (Open Q2) |
| `NavAgentComponent` — per-entity pathfinding parameters + current waypoint list (data only) | Path-following motion integration (advancing along waypoints, avoidance) — placeholder in `NavigationSystem::update`; not yet implemented (Open Q3) |
| `PathResult` + `findPathWithStatus` — surfaces Detour's `DT_PARTIAL_RESULT` flag for AI re-plan logic | Off-mesh connections (`dtOffMeshConnection`) — schema absent; pipeline-ready but unused (Open Q4) |
| Polygon-edge extraction for debug overlay (`extractPolygonEdges`) | Editor UI (the navmesh panel) — `engine/editor/panels/navigation_panel.h` |
| `NavigationSystem` (in `engine/systems/`) — `ISystem` host that owns the builder + query and publishes `NavMeshBakedEvent` | Persisting the baked navmesh to disk — currently re-baked on every editor request (Open Q5) |

## 3. Architecture

```
                     ┌──────────────────────────┐
                     │   NavigationSystem       │  (engine/systems/navigation_system.h:23)
                     │   ISystem; bake API +    │
                     │   query forward          │
                     └─────┬──────────────┬─────┘
                           │ owns         │ owns
                           ▼              ▼
                ┌────────────────┐  ┌─────────────────┐
                │ NavMeshBuilder │  │ NavMeshQuery    │
                │ (Recast wrap)  │  │ (Detour wrap)   │
                └───────┬────────┘  └────────┬────────┘
                        │ produces           │ reads
                        ▼                    ▼
                ┌─────────────────────────────────────┐
                │  dtNavMesh*  (Detour binary blob)   │
                │  owned by NavMeshBuilder            │
                └─────────────────────────────────────┘
                        ▲                    ▲
                        │ scene geometry      │ start / end
                        │ (vbo + ebo readback)│
                        │                    │
                ┌───────┴────────┐  ┌────────┴────────┐
                │  Scene +       │  │  AI / scripted  │
                │  MeshRenderer  │  │  consumer       │
                └────────────────┘  └─────────────────┘

Per-entity:  NavAgentComponent (radius, height, currentPath, currentPathIndex)
             attached to entities; NavigationSystem advances them (placeholder).
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `NavMeshBuilder` | class (RAII; non-copyable) | Bakes a Detour navmesh from scene geometry via the 10-step Recast pipeline. `engine/navigation/nav_mesh_builder.h:28` |
| `NavMeshBuildConfig` | struct (POD) | All knobs for a bake (cell size, agent dims, region thresholds, simplification). `engine/navigation/nav_mesh_config.h:15` |
| `NavMeshQuery` | class (RAII; non-copyable) | A\* path search + snap + on-mesh test against a built navmesh. `engine/navigation/nav_mesh_query.h:60` |
| `PathResult` | struct | `waypoints` + `partial` flag — distinguishes "arrived" from "best-guess stop short of unreachable target." `engine/navigation/nav_mesh_query.h:26` |
| `detail::isPartialPathStatus` | free function | Extracts Detour `DT_PARTIAL_RESULT` from a `dtStatus` — exposed for testability without a live navmesh. `engine/navigation/nav_mesh_query.h:52` |
| `NavAgentComponent` | `Component` (data) | Per-entity radius / height / max-speed / current path; consumed by `NavigationSystem`. `engine/navigation/nav_agent_component.h:21` |
| `NavigationSystem` | `ISystem` | Engine-side host: lifetime, bake API, `NavMeshBakedEvent` publish, runtime query forward. `engine/systems/navigation_system.h:23` |
| `NavMeshBakedEvent` | event struct | Published on successful bake (poly count + ms). Defined in `engine/core/system_events.h`. |

## 4. Public API

Small surface — four headers downstream code may include:

```cpp
// engine/navigation/nav_mesh_config.h
struct NavMeshBuildConfig
{
    float cellSize           = 0.3f;   // voxel XZ size, metres
    float cellHeight         = 0.2f;   // voxel Y size, metres
    float agentHeight        = 1.8f;   // human-scale default
    float agentRadius        = 0.4f;
    float agentMaxClimb      = 0.4f;   // max step-up the agent can climb
    float agentMaxSlope      = 45.0f;  // walkable-surface cutoff, degrees
    float regionMinSize      = 8.0f;
    float regionMergeSize    = 20.0f;
    float edgeMaxLen         = 12.0f;
    float edgeMaxError       = 1.3f;
    int   vertsPerPoly       = 6;      // Detour caps at 6
    float detailSampleDist   = 6.0f;
    float detailSampleMaxError = 1.0f;
};
```

```cpp
// engine/navigation/nav_mesh_builder.h
class NavMeshBuilder
{
    bool      buildFromScene(Scene& scene, const NavMeshBuildConfig& = {});  // editor-time, blocking
    void      clear();                                                        // frees dtNavMesh
    bool      hasMesh() const;
    dtNavMesh* getNavMesh();                                                  // borrowed pointer; owned by builder
    int       getPolyCount()      const;
    float     getLastBuildTimeMs() const;
    void      extractPolygonEdges(std::vector<glm::vec3>& out, float yLift = 0.0f) const;  // GL_LINES segments
};
```

```cpp
// engine/navigation/nav_mesh_query.h
struct PathResult { std::vector<glm::vec3> waypoints; bool partial = false; };

class NavMeshQuery
{
    bool        initialize(dtNavMesh*);          // borrows, does not take ownership
    void        shutdown();
    bool        isReady() const;
    PathResult  findPathWithStatus(const glm::vec3& start, const glm::vec3& end);
    std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end);  // legacy — drops partial flag
    glm::vec3   findNearestPoint(const glm::vec3& point);                // snap to navmesh; returns input if off-mesh
    bool        isPointOnNavMesh(const glm::vec3& point);
};
namespace detail { bool isPartialPathStatus(unsigned int dtStatus); }  // exposed for unit tests
```

```cpp
// engine/navigation/nav_agent_component.h
class NavAgentComponent : public Component
{
    float     radius           = 0.4f;
    float     height           = 1.8f;
    float     maxSpeed         = 3.5f;       // m/s
    float     maxAcceleration  = 8.0f;       // m/s^2
    std::vector<glm::vec3> currentPath;
    int       currentPathIndex = 0;
    bool      hasReachedDestination() const;
    std::unique_ptr<Component> clone() const override;
};
```

```cpp
// engine/systems/navigation_system.h — ISystem facade (lives in engine/systems/, listed here because it is the
// canonical entry point for downstream code that wants to bake / query / observe).
class NavigationSystem : public ISystem
{
    bool       bakeNavMesh(Scene&, const NavMeshBuildConfig& = {});   // delegates to builder + re-inits query + publishes event
    bool       hasNavMesh()    const;
    void       clearNavMesh();
    NavMeshBuilder& getBuilder();
    NavMeshQuery&   getQuery();
    std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end);
    glm::vec3       findNearestPoint(const glm::vec3& point);
};
```

**Non-obvious contract details:**

- `NavMeshBuilder::buildFromScene` is **synchronous and main-thread-only** today — it issues `glGetBufferSubData` against every `MeshRenderer`'s VAO/VBO/EBO. The OpenGL (GL) context is single-thread-affine, so the bake cannot move to a worker without first decoupling geometry collection from the GL readback (see §7 + Open Q6). The bake *can* be expensive on large architectural scenes; budgets are §8.
- `NavMeshBuilder::getNavMesh()` returns a **borrowed** `dtNavMesh*`. Lifetime is tied to the builder; `clear()` frees it. Callers (`NavMeshQuery::initialize`) must not call `dtFreeNavMesh` on it.
- `NavMeshQuery::initialize` borrows the navmesh — it does not extend lifetime. If the builder is cleared, the query goes invalid; `NavigationSystem::clearNavMesh` correctly shuts the query down before clearing the builder (`navigation_system.cpp:67`).
- `NavMeshQuery::findPath` is the **legacy** overload — it discards the partial-result flag for back-compat with pre-Slice-3-S8 call sites. New code calls `findPathWithStatus` and branches on `PathResult::partial`.
- `findNearestPoint` returns the **input point unchanged** when the point is not within the search extents (`{2.0, 4.0, 2.0}` m half-extents). Callers that need to distinguish "snapped" from "off-mesh" must call `isPointOnNavMesh` first.
- `extractPolygonEdges` skips off-mesh connections (`DT_POLYTYPE_OFFMESH_CONNECTION`) — those are 2-vertex links between disjoint patches and would draw misleading line segments in a polygon-outline overlay.
- Vertex ingestion assumes the engine's standard vertex layout: position vec3, normal vec3, texCoords vec2, tangent vec3, bitangent vec3 = 14 floats / 56 bytes / position at offset 0. The constant `VERTEX_STRIDE = 56` is hard-coded in `nav_mesh_builder.cpp:102` — any vertex-layout change in `engine/scene/mesh.h` breaks the bake silently. (TODO flagged Open Q7.)
- `NavAgentComponent::currentPath` is **mutable shared state**: today it is set by external callers and read by `NavigationSystem::update` (placeholder). Until the path-follower lands, no thread-safety claim is made beyond "main thread only."

**Stability:** the four public headers above are semver-frozen for `v0.x`. The legacy `findPath` overload (no status) will be retained — callers that need to migrate add the new overload alongside.

## 5. Data Flow

**Bake (editor-time, on user request — `NavigationSystem::bakeNavMesh` → `NavMeshBuilder::buildFromScene`):**

1. `NavigationPanel` (or scripted caller) → `NavigationSystem::bakeNavMesh(scene, config)`.
2. `NavMeshBuilder::clear()` releases any prior `dtNavMesh`.
3. `collectSceneGeometry` walks every `Entity` with a `MeshRenderer`; for each mesh it binds the VAO, reads back the EBO + VBO via `glGetBufferSubData`, transforms positions to world space using the entity's `worldMatrix`, and appends to flat `vertices` (3 floats / vert) + `indices` (3 ints / tri) buffers.
4. AABB derived → `rcConfig` populated from `NavMeshBuildConfig`.
5. **Recast pipeline (10 steps):** create heightfield → mark walkable triangles by slope → rasterise → filter low-hanging obstacles + ledges + low-height spans → build compact heightfield → erode by agent radius → distance field → regions → contours → poly mesh → detail mesh.
6. **Detour build:** flag every poly walkable (flags=1), `dtCreateNavMeshData` packs the binary blob, `dtAllocNavMesh` + `dtNavMesh::init(... DT_TILE_FREE_DATA)` takes ownership of the blob.
7. Builder records `m_polyCount` + `m_lastBuildTimeMs` and frees the Recast intermediates.
8. `NavigationSystem` re-initialises `NavMeshQuery` with the new mesh (`dtAllocNavMeshQuery` + `init(navMesh, 2048)` — 2048 is the max search nodes for A\*).
9. `EventBus::publish(NavMeshBakedEvent{polyCount, buildTimeMs})` — editor UI / debug overlay observes.

**Query (steady-state, can be per-frame — `NavMeshQuery::findPathWithStatus`):**

1. Caller passes world-space `start` / `end`.
2. `findNearestPoly` snaps both endpoints to navmesh poly refs using a fixed `{2, 4, 2}` m search box.
3. If either snap fails, return empty `PathResult` (no path).
4. `findPath` runs Detour A\* over polygon refs, capped at 256 polys (`MAX_PATH_POLYS`).
5. `isPartialPathStatus` extracts `DT_PARTIAL_RESULT` from the success status → `PathResult::partial`.
6. `findStraightPath` string-pulls the polygon corridor into world-space waypoints.
7. Caller branches on `partial` — re-plan, notify, or proceed.

**Exception path:** every Recast / Detour step that fails logs `Logger::error(...)` and returns `false` from `buildFromScene`; intermediates are freed in reverse order. Query failures (no path, off-mesh endpoints, uninitialised query) return empty `PathResult`. No exceptions cross the subsystem boundary.

## 6. CPU / GPU placement

Per CLAUDE.md Rule 7. **Pure CPU subsystem** — Recast/Detour is a CPU-only library by design and the engine does not stage any GPU compute fallback.

| Workload | Placement | Reason |
|----------|-----------|--------|
| Recast voxel build, region build, contour + poly + detail mesh | CPU (main thread today; bake worker viable — Open Q6) | Recast is a CPU-only library; the algorithm is sparse + branch-heavy, not data-parallel-friendly. Per CODING_STANDARDS §17 heuristic: branching / sparse / decision → CPU. |
| Geometry collection from scene (`glGetBufferSubData` readback) | CPU (main thread, GL-context-bound) | GL readback APIs require the GL context, which is main-thread-affine. Forces the *entire* current bake onto the main thread; see §7 + Open Q6 for the decoupling plan. |
| A\* polygon search + string-pulling | CPU | Detour is CPU-only; queries are sparse / branchy. |
| Polygon-edge extraction for debug overlay (`extractPolygonEdges`) | CPU produces line segments; renderer uploads + draws via `GL_LINES` | The CPU walks navmesh tiles → the renderer draws — no per-frame GPU compute originates here. |

No dual implementation. No GPU spec / parity test applies.

## 7. Threading model

Per CODING_STANDARDS §13.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** | All of `NavMeshBuilder` (bake is main-thread-only — see below), `NavMeshQuery`, `NavAgentComponent`, `NavigationSystem`. | None. |
| **Worker thread** | None today. `NavMeshQuery::findPathWithStatus` is *re-entrant-safe within Detour's own constraints* but is not synchronised here — concurrent queries on the same `dtNavMeshQuery` are undefined. (Open Q6) |

**Why main-thread-only today.** `NavMeshBuilder::buildFromScene` reads vertex / index data via `glGetBufferSubData` against entity VAOs. The OpenGL context is single-thread-affine; moving the bake to a worker requires first **separating geometry collection** (must run on the main thread, can finish in milliseconds — read every mesh's CPU-side cached vertex/index data, or readback into a staging buffer once) **from the Recast voxel build** (the heavy work — minutes-of-arc on architectural scenes). The voxel build *is* a worker-thread candidate per Recast best-practice (parallelisable per-tile, see §14 references), but the engine has not yet adopted the tile-cache (`dtTileCache`) build path that makes that natural — the current build is a single-tile mesh.

**Recast tile cache + dynamic obstacles** (the natural worker-thread shape) is tracked as Open Q2 / Q6.

**Locking:** `engine/navigation` holds **no mutexes**. Concurrency safety is delegated to "main thread only."

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. Bake is editor-time (off-budget for steady-state runtime); query is the per-frame concern.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `NavMeshQuery::findPathWithStatus` (≤ 256 polys, mid-scene) | < 0.5 ms | TBD — measure by Phase 11 audit |
| `NavMeshQuery::findNearestPoint` | < 0.1 ms | TBD — measure by Phase 11 audit |
| `NavMeshQuery::isPointOnNavMesh` | < 0.1 ms | TBD — measure by Phase 11 audit |
| `NavMeshBuilder::buildFromScene` (Tabernacle demo, ~30k tris) | < 500 ms (editor blocking acceptable) | TBD — instrument via `getLastBuildTimeMs` and capture at next bake |
| `NavMeshBuilder::extractPolygonEdges` (full mesh) | < 5 ms (debug overlay refresh) | TBD — measure by Phase 11 audit |
| `NavigationSystem::update` (placeholder) | < 0.01 ms | TBD — measure once path-follower lands |

**Honesty note:** the budgets above are placeholders. `NavMeshBuilder` already records `m_lastBuildTimeMs` per bake (`nav_mesh_builder.h:58`); the editor's navigation panel logs it. Query budgets need Tracy markers — none exist today.

Profiler markers / capture points: `NavMeshBuilder` emits two `Logger::info` lines per bake (`Building navmesh from N vertices, M triangles` and `Built navmesh: P polygons in T ms`) — greppable in capture logs. No `glPushDebugGroup` markers (no GPU passes originate here).

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap, via Recast's own allocators (`rcAllocHeightfield`, `rcAllocCompactHeightfield`, `rcAllocContourSet`, `rcAllocPolyMesh`, `rcAllocPolyMeshDetail`) and Detour's (`dtAllocNavMesh`, `dtAllocNavMeshQuery`). All Recast intermediates are freed before `buildFromScene` returns; only the final `dtNavMesh` + scratch query state survive. |
| Peak working set during bake | Tens of MB for a Tabernacle-scale scene (heightfield + compact heightfield are the two largest intermediates; both freed before return). Order of magnitude scales with `(scene AABB volume) / (cellSize² · cellHeight)`. |
| Resident working set after bake | Single-digit MB for the `dtNavMesh` blob + the 2048-node A\* search pool inside `dtNavMeshQuery`. |
| Ownership | `NavMeshBuilder` owns the `dtNavMesh*`. `NavMeshQuery` owns the `dtNavMeshQuery*` but **borrows** the `dtNavMesh*`. `NavigationSystem` owns one of each by value. |
| Lifetimes | Bake-to-rebake (each `buildFromScene` clears the prior mesh first). Query-to-shutdown (each `initialize` call replaces the prior query state). |

`std::vector<float>` + `std::vector<int>` scratch buffers in `collectSceneGeometry` are stack-allocated (function-local) — they grow during collection and free at scope exit. No long-lived per-frame transient allocator; the bake is rare enough that the cost is negligible.

## 10. Error handling

Per CODING_STANDARDS §11.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Empty scene (no `MeshRenderer` entities) | `Logger::warning("No geometry found in scene")` + `buildFromScene` returns `false` | Editor surfaces "no geometry to bake"; no navmesh produced. |
| Recast intermediate failure (heightfield / rasterise / compact / erode / regions / contours / polymesh / detail) | `Logger::error("[NavMeshBuilder] Failed to <step>")` + intermediates freed + `false` | Editor surfaces failure; previous navmesh (if any) is **already cleared** at start of `buildFromScene` — caller must accept "no navmesh now." |
| Detour data assembly failure (`dtCreateNavMeshData`) | `Logger::error` + intermediates freed + `false` | As above. |
| Detour navmesh allocation / init failure | `Logger::error` + `dtNavMesh` freed if partially allocated + `false` | As above. |
| `NavMeshQuery::initialize(nullptr)` | Returns `false` (no log) | Caller must check return; `NavigationSystem::bakeNavMesh` does, and logs at that layer. |
| `dtNavMeshQuery::init` failure | `Logger::error("Failed to initialize query")` + frees query + `false` | Caller (`NavigationSystem`) treats bake as failed. |
| `findPath` / `findPathWithStatus` on uninitialised query | Returns empty `PathResult` (no log) | Caller treats as "no path." |
| `findPath` start / end off-mesh | Returns empty `PathResult` (no log) | Caller treats as "no path." Use `findNearestPoint` first to snap. |
| `findPath` partial (best-guess stop short of unreachable target) | `PathResult{waypoints non-empty, partial = true}` | AI consumer branches on `partial` — re-plan, notify, give up. **Never** treat partial as arrival. |
| `findNearestPoint` off-mesh | Returns the input point unchanged | Use `isPointOnNavMesh` to disambiguate. |
| Programmer error (null `Scene`, etc.) | UB | Fix the caller. |
| Out of memory | `std::bad_alloc` propagates | App aborts (matches CODING_STANDARDS §11). |

`Result<T, E>` / `std::expected` not yet adopted — see Open Q8.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `isPartialPathStatus` bit-extraction (success + partial; failure + partial; success only; failure only) | `tests/test_nav_mesh_query.cpp` | Unit — no live navmesh required |
| `NavMeshQuery::findPathWithStatus` empty / uninitialised query → empty `PathResult` with `partial = false` | `tests/test_nav_mesh_query.cpp` | Unit |
| Navigation editor panel (bake / clear / debug overlay toggle) | `tests/test_navigation_panel.cpp` | Editor smoke / regression |

**Adding a test for `engine/navigation`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `NavMeshQuery` / `NavMeshBuildConfig` directly without an `Engine` instance — every primitive in this subsystem **except `NavMeshBuilder::buildFromScene`** is unit-testable headlessly. The bake path is GL-context-bound (`glGetBufferSubData`), so headless coverage of the full pipeline is impossible in CI today; that path is exercised manually via the editor's navigation panel.

**Coverage gap:** no unit test currently exercises the full Recast voxel pipeline end-to-end with a real triangle soup — closing that gap requires either decoupling geometry collection from the GL context (Open Q6) or wiring up an offscreen GL context in CI. Tracked as Open Q9.

## 12. Accessibility

`engine/navigation` produces no user-facing pixels or sound. Not applicable — pure simulation / data subsystem. The navigation **debug overlay** (polygon edges as `GL_LINES`) is rendered by the editor / debug-draw layer and inherits its colour/contrast contract from there; the line-segment data this subsystem produces carries no colour information.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/scene/scene.h`, `entity.h`, `mesh_renderer.h`, `component.h` | engine subsystem | `buildFromScene` walks entities; `NavAgentComponent` derives from `Component`. |
| `engine/core/i_system.h`, `engine.h`, `event_bus.h`, `system_events.h`, `logger.h` | engine subsystem | `NavigationSystem` is an `ISystem`; publishes `NavMeshBakedEvent`; logs build / query failures. |
| `RecastNavigation::Recast` | external (third-party, ZLib) | Voxel pipeline, walkable surface extraction, polymesh build. |
| `RecastNavigation::Detour` | external (third-party, ZLib) | Navmesh data format, A\* path search, query API. |
| `<glm/glm.hpp>` | external | `vec3` for waypoint / endpoint types. |
| `<glad/gl.h>` | external | `glGetBufferSubData` etc. for vertex / index readback during bake. (Pulled in via `nav_mesh_builder.cpp` only.) |
| `<chrono>`, `<cstring>`, `<vector>` | std | Build-time measurement; aliasing-safe `memcpy` of vertex bytes; flat geometry buffers. |

**Recast/Detour version posture (CLAUDE.md Rule 5):** pinned at `v1.6.0` via `external/CMakeLists.txt:378`. Verified 2026-04-28 against <https://github.com/recastnavigation/recastnavigation/releases> — **v1.6.0 remains the latest stable release** ("includes a number of bug fixes and improvements from the past few years and maintains backwards compatibility with 1.x versions"). No upstream successor as of this spec date; the pin is current.

**Direction:** consumed by `engine/editor/panels/navigation_panel.h`, `engine/systems/navigation_system.h` (host), and downstream AI / scripted callers (none in tree yet — Open Q3). `engine/navigation` itself depends only on `engine/scene/` (read-only) and `engine/core/` (logger / events). It must **not** depend on the editor or any concrete AI consumer.

## 14. References

Cited research / authoritative external sources:

- Recast Navigation. *Recast Navigation — Industry-standard navigation-mesh toolset for games* (current docs, 2026). <https://recastnav.com/> and <https://github.com/recastnavigation/recastnavigation>
- Recast Navigation. *Introduction* (current docs) — single-tile vs tiled navmesh trade-offs, agent-config best practice (radius / height / max-slope / max-step). <https://recastnav.com/md_Docs_2__1__Introduction.html>
- Recast Navigation. *Releases — v1.6.0 (latest)*. <https://github.com/recastnavigation/recastnavigation/releases>
- Recast Navigation maintainers. *Detour: Pathfinding and Navigation* (DeepWiki, indexed Dec 2025). Tile cache, off-mesh connections, dynamic obstacles via `dtTileCache` + `addCylinderObstacle` / `addBoxObstacle` / `removeObstacle` + `tileCache.update(navMesh)`. <https://deepwiki.com/recastnavigation/recastnavigation/3-detour:-pathfinding-and-navigation>
- Isaac Mason. *recast-navigation-js — Worker-thread navmesh build* (2025–2026). Pattern: build tiles in a worker, transfer the `Uint8Array` result to the main thread, hot-swap into the live navmesh — informs the Open Q6 plan for decoupling geometry collection from the voxel build. <https://recast-navigation-js-docs.isaacmason.com/>
- Recast Navigation. *Development Roadmap* (current docs). <http://recastnav.com/md_Docs_2__99__Roadmap.html>
- Mikko Mononen. *Recast Navigation — original Recast paper / talk materials* (foundational, still authoritative for the voxel pipeline this spec implements).

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §27 (units: metres / Y-up / RH).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Adopt Recast's `DetourCrowd` for steering / avoidance, or roll a thinner per-agent follower against `NavMeshQuery`? Crowd buys avoidance + corridor smoothing; cost is another upstream surface to track. | milnet01 | Phase 11 entry |
| 2 | Adopt `dtTileCache` (tile-based navmesh + temporary box / cylinder obstacles) for dynamic-obstacle scenarios — needed once destruction / movable doors / placed props affect routes. | milnet01 | Phase 11 entry |
| 3 | Wire `NavigationSystem::update` to actually advance `NavAgentComponent::currentPath` (path-follower: pop reached waypoints, feed velocity into the entity's transform / character controller). Currently a no-op placeholder. | milnet01 | Phase 11 entry |
| 4 | Off-mesh connections (`dtOffMeshConnection`) — author-tagged jumps / ladders / portal links. Pipeline supports them (the edge extractor already skips `DT_POLYTYPE_OFFMESH_CONNECTION`); needs a scene-side authoring representation. | milnet01 | Phase 11 entry |
| 5 | Persist baked navmesh to disk so editor sessions don't re-bake on every load (Detour's `dtNavMesh` serialises to a binary blob; needs versioning + a `.navmesh` sidecar). | milnet01 | Phase 11 entry |
| 6 | Decouple geometry collection (GL-context-bound, must stay on main thread) from the Recast voxel build (CPU-only, worker-thread candidate). Pattern: snapshot vertex / index data once on the main thread, then run Recast on a job-system worker. References §7 and the recast-navigation-js worker model in §14. | milnet01 | Phase 11 entry |
| 7 | Hard-coded `VERTEX_STRIDE = 56` in `nav_mesh_builder.cpp:102` couples this subsystem to the engine's standard vertex layout. Pull stride from `engine/scene/mesh.h` (single source of truth) so a layout change cannot break the bake silently. | milnet01 | Phase 11 entry |
| 8 | Migrate `NavMeshBuilder::buildFromScene` and `NavMeshQuery::initialize` from `bool` returns to `Result<T, NavError>` once the codebase-wide `Result` / `std::expected` policy lands. | milnet01 | post-MIT release (Phase 12) |
| 9 | No unit test exercises the full Recast voxel pipeline end-to-end (CI lacks an offscreen GL context for the readback path). Closes naturally with Open Q6 — once geometry collection is decoupled, a synthetic `(vertices, indices)` triangle soup feeds the test. | milnet01 | Phase 11 entry |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/navigation` Recast/Detour-backed pipeline, formalised post-Phase 10.9 audit. Recast pin verified at v1.6.0 (current upstream stable). |
