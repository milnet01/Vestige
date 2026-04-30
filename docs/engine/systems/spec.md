# Subsystem Specification — `engine/systems`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/systems` |
| Status | `shipped` (foundation; some systems are empty-pump stubs — see §3 status table) |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.6.0+` (Phase 9A introduced `ISystem` / `SystemRegistry`; Phase 9B-onwards wrapped concrete domain systems) |

---

## 1. Purpose

`engine/systems` is the layer that *implements* the engine's domain `ISystem` (Interface for an engine subsystem) contracts — the concrete `TerrainSystem`, `AtmosphereSystem`, `AudioSystem`, `UISystem`, `LightingSystem`, `NavigationSystem`, `ParticleVfxSystem`, `Physics2DSystem`, `SpriteSystem`, `VegetationSystem`, `WaterSystem`, `ClothSystem`, `CharacterSystem`, `DestructionSystem` classes that `engine/core`'s `SystemRegistry` instantiates, sorts, ticks, and tears down each frame. Each class in this directory is a thin façade that:

- holds the *primitive* subsystems that actually do the work (e.g. `TerrainSystem` owns the `Terrain` heightfield + `TerrainRenderer`; `AudioSystem` owns the `AudioEngine` OpenAL wrapper; `NavigationSystem` owns the `NavMeshBuilder` + `NavMeshQuery` Recast/Detour wrappers),
- declares its `UpdatePhase` placement (`PreUpdate` / `Update` / `PostCamera` / `PostPhysics` / `Render`) and `getOwnedComponentTypes()` so `SystemRegistry` can stable-sort and auto-activate it,
- forwards per-frame `update(dt)` to the underlying primitive(s) so the registry's per-system metrics + budget enforcement applies uniformly.

The directory exists as its own subsystem because the alternative — letting every domain primitive register itself directly with `SystemRegistry` — would duplicate the activation / ordering / metrics boilerplate across `environment/`, `audio/`, `navigation/`, `physics/`, `renderer/`, `ui/`, scattering the registry contract across the whole tree. Centralising the wrappers here keeps the include graph one-way (`core → systems → primitive subsystems`) and gives `SystemRegistry` a single, audit-able list of every domain system that runs each frame. For the engine's primary use case — first-person walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — this is what gets terrain, sky, water, vegetation, audio, UI, and navmesh all stepping in lock-step at 60 frames per second (FPS).

> **Distinction from `engine/core/i_system.h`.** That header defines the *interface* (the abstract base class + `UpdatePhase` enum). This subsystem ships the *implementations*. A new domain system goes here, not next to `i_system.h`.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| Concrete `ISystem` implementations: `AtmosphereSystem`, `AudioSystem`, `CharacterSystem`, `ClothSystem`, `DestructionSystem`, `LightingSystem`, `NavigationSystem`, `ParticleVfxSystem`, `Physics2DSystem`, `SpriteSystem`, `TerrainSystem`, `UISystem`, `VegetationSystem`, `WaterSystem` | The `ISystem` interface, `UpdatePhase` enum, `SystemRegistry` lifecycle / dispatch — `engine/core/i_system.h`, `engine/core/system_registry.h` |
| Per-system declaration of update phase, force-active flag, owned component types, opt-in lifecycle hooks (`onSceneLoad`, `onSceneUnload`, `drawDebug`) | Cross-system orchestration, init-prefix rollback, per-frame phase walk — `engine/core/system_registry.cpp` |
| Per-system thin forwarders that call into the primitive subsystem(s) they own (e.g. `AtmosphereSystem::update` → `EnvironmentForces::update`) | The primitive subsystems themselves (`Terrain`, `TerrainRenderer`, `EnvironmentForces`, `AudioEngine`, `NavMeshBuilder`, `FoliageManager`, `ParticleRenderer`, `WaterRenderer`, `WaterFbo`, `SpriteRenderer`, `SpriteBatchRenderer`, `PhysicsCharacterController`, `JPH::PhysicsSystem`, `UICanvas`, `UITheme`, `NotificationQueue`, …) — those live in `engine/{environment,audio,navigation,physics,renderer,ui}/` |
| `UISystem`'s screen-stack state machine, modal capture, focus / tab navigation, accessibility-toggle plumbing, notification queue tick | The pure `GameScreen` state machine + ImGui editor panels — `engine/ui/`, `engine/editor/` |
| `NavigationSystem`'s editor-facing `bakeNavMesh` + runtime `findPath` / `findNearestPoint` query API | The Recast / Detour wrappers themselves — `engine/navigation/` |
| `Physics2DSystem`'s 2D body lifecycle (`ensureBody` / `removeBody`), impulse / velocity / transform helpers, collision-event publishing on the shared `EventBus` | The shared 3D Jolt `PhysicsWorld`, broadphase, layer table — `engine/physics/physics_world.h` |
| `SpriteSystem`'s headless `collectVisible` / `sortDrawList` / `buildBatches` static helpers (testable without a Graphics-Library context) | The instanced quad shader + `SpriteRenderer` GPU pump — `engine/renderer/sprite_renderer.h` |
| `AudioSystem`'s per-frame listener sync, ducking advance, `AudioSourceComponent` auto-acquire + per-frame `AL_GAIN` / pitch / position push | OpenAL device + buffer cache, Head-Related Transfer Function (HRTF) chain, mixer struct — `engine/audio/` |

If a reader cannot tell whether a piece of behaviour belongs to the wrapper here or to the primitive in another subsystem, the rule of thumb is: *if `SystemRegistry` cares about it (phase, force-active, owned components, per-frame timing, scene activation), it lives here; if the GPU or the audio thread cares about it, it lives next to its primitive.*

## 3. Architecture

```
                             ┌──────────────────────────┐
                             │   SystemRegistry         │
                             │ (engine/core)            │
                             └──────────┬───────────────┘
                                        │ owns std::unique_ptr<ISystem>
              ┌──────────┬──────────┬───┴──────┬───────────┬─────────────┬───────────┐
              ▼          ▼          ▼          ▼           ▼             ▼           ▼
        ┌─────────┐ ┌────────┐ ┌────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
        │Atmos.   │ │Audio   │ │Terrain │ │Veget.   │ │Water     │ │Particle  │ │Cloth     │
        │ System  │ │System  │ │System  │ │ System  │ │ System   │ │VfxSystem │ │ System   │
        └────┬────┘ └───┬────┘ └────┬───┘ └────┬────┘ └────┬─────┘ └────┬─────┘ └──────────┘
             │          │           │          │           │            │   (no owned prim;
             ▼          ▼           ▼          ▼           ▼            ▼    pumps Cloth-
        EnvForces  AudioEngine  Terrain    FoliageMgr  WaterRenderer ParticleRen Component)
                              + Renderer + Foliage   + WaterFbo
                                          + TreeRen.
        ┌──────────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌───────────┐ ┌──────────┐
        │Lighting  │ │Charact.  │ │Naviga-  │ │UI        │ │Sprite     │ │Physics2D │
        │ System   │ │ System   │ │tion Sys │ │System    │ │System     │ │ System   │
        └──────────┘ └────┬─────┘ └───┬─────┘ └────┬─────┘ └─────┬─────┘ └─────┬────┘
        (Renderer-       │           │            │              │             │
         embedded)       ▼           ▼            ▼              ▼             ▼
                    PhysicsChar  NavMeshBuilder Canvas+Theme  SpriteRenderer  PhysicsWorld
                    Controller   +NavMeshQuery  +Notification               (shared 3D Jolt
                                                +Subtitle/                   + Plane2D DOF)
                                                 Renderer
        ┌────────────┐
        │Destruction │
        │ System     │  ← W13 stub (no-op; kept for invariants)
        └────────────┘
```

Status by system (shipping behaviour as of Phase 10.9):

| System | File | Status | Owned primitives | Update phase | Force-active | Owned components |
|--------|------|--------|------------------|--------------|--------------|-------------------|
| `AtmosphereSystem` | `atmosphere_system.{h,cpp}` | active | `EnvironmentForces` | `Update` | yes | none (global wind state) |
| `AudioSystem` | `audio_system.{h,cpp}` | active | `AudioEngine` | `PostCamera` | yes | `AudioSourceComponent` |
| `CharacterSystem` | `character_system.{h,cpp}` | active (init-only) | `PhysicsCharacterController` | `Update` | no | none (controller stepped in `Engine::run`) |
| `ClothSystem` | `cloth_system.{h,cpp}` | empty-pump (active per-component) | none directly | `Update` | no | `ClothComponent` |
| `DestructionSystem` | `destruction_system.{h,cpp}` | **W13 stub** (no-op after Phase 10.9 W13) | none | `Update` | no | none (was `BreakableComponent`) |
| `LightingSystem` | `lighting_system.{h,cpp}` | empty-pump (Renderer-embedded) | none | `Update` | yes | none |
| `NavigationSystem` | `navigation_system.{h,cpp}` | active (editor-driven bake; runtime queries) | `NavMeshBuilder`, `NavMeshQuery` | `Update` | no | `NavAgentComponent` |
| `ParticleVfxSystem` | `particle_system.{h,cpp}` | empty-pump (component-driven) | `ParticleRenderer` | `Update` | no | `ParticleEmitterComponent`, `GPUParticleEmitter` |
| `Physics2DSystem` | `physics2d_system.{h,cpp}` | active | shares `PhysicsWorld` (3D Jolt) | `Update` | no | (uses scene-load hook; bodies tracked by entity id) |
| `SpriteSystem` | `sprite_system.{h,cpp}` | active | `SpriteRenderer` | `Update` | no | inherits empty default — `SpriteComponent` ticking happens via scene traversal, not via `getOwnedComponentTypes()` registration |
| `TerrainSystem` | `terrain_system.{h,cpp}` | active | `Terrain`, `TerrainRenderer` | `Update` | yes | none (global heightfield) |
| `UISystem` | `ui_system.{h,cpp}` | active | `SpriteBatchRenderer`, `UICanvas`, `UITheme`, `NotificationQueue` | `Render` | yes | none (HUD / menus are global) |
| `VegetationSystem` | `vegetation_system.{h,cpp}` | active | `FoliageManager`, `FoliageRenderer`, `TreeRenderer` | `Update` | no | none (environment-based) |
| `WaterSystem` | `water_system.{h,cpp}` | active | `WaterRenderer`, `WaterFbo` | `Update` | no | `WaterSurfaceComponent` |

Key abstractions (small per-class surface; one-line each):

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `AtmosphereSystem` | `ISystem` impl | Wraps `EnvironmentForces` for global wind / weather. `engine/systems/atmosphere_system.h:21` |
| `AudioSystem` | `ISystem` impl | Wraps `AudioEngine`; per-frame listener sync + ducking + per-source state push. `engine/systems/audio_system.h:23` |
| `CharacterSystem` | `ISystem` impl | Wraps `PhysicsCharacterController`; lifecycle only — controller is stepped from `Engine::run`. `engine/systems/character_system.h:22` |
| `ClothSystem` | `ISystem` impl | Domain grouping for `ClothComponent`; per-component update via scene traversal. `engine/systems/cloth_system.h:21` |
| `DestructionSystem` | `ISystem` impl (stub) | No-op shell; production cluster relocated to `engine/experimental/physics/` per W13. `engine/systems/destruction_system.h:20` |
| `LightingSystem` | `ISystem` impl | Empty pump; lighting lives inside `Renderer` (Forward+ pass). `engine/systems/lighting_system.h:21` |
| `NavigationSystem` | `ISystem` impl | Recast bake + Detour query façade + `NavMeshBakedEvent` publisher. `engine/systems/navigation_system.h:23` |
| `ParticleVfxSystem` | `ISystem` impl | Owns `ParticleRenderer`; emitters are entity components. `engine/systems/particle_system.h:21` |
| `Physics2DSystem` | `ISystem` impl | 2D rigid-body sim on top of shared 3D Jolt with `Plane2D` Degrees-Of-Freedom (DOF). `engine/systems/physics2d_system.h:44` |
| `SpriteSystem` | `ISystem` impl | 2D sprite pass; headless sort + batch helpers + per-frame instanced draw. `engine/systems/sprite_system.h:59` |
| `TerrainSystem` | `ISystem` impl | Owns `Terrain` + `TerrainRenderer`; default demo heightfield seeded at init. `engine/systems/terrain_system.h:21` |
| `UISystem` | `ISystem` impl | Screen stack, modal capture, theme + accessibility, focus/tab nav, toast queue. `engine/systems/ui_system.h:33` |
| `VegetationSystem` | `ISystem` impl | Owns foliage + tree managers and renderers. `engine/systems/vegetation_system.h:23` |
| `WaterSystem` | `ISystem` impl | Owns `WaterRenderer` + reflection / refraction Frame-Buffer-Object (FBO). `engine/systems/water_system.h:22` |
| `SpriteDrawEntry` | struct | Pre-sort entry for the sprite pass — testable without a Graphics-Library context. `engine/systems/sprite_system.h:42` |
| `SpritePass` | enum | Opaque vs Transparent split for the 2D pass. `engine/systems/sprite_system.h:53` |
| `SpriteSystem::Batch` | struct | Atlas-keyed batch pack for one instanced draw. `engine/systems/sprite_system.h:98` |
| `UISystem::ScreenBuilder` | type alias | Pluggable prefab callback letting game code override engine menus. `engine/systems/ui_system.h:177` |

## 4. Public API

This subsystem has 14 public headers (≥ 8 → facade pattern: one block per header, headline functions only). Every `engine/systems/<name>_system.h` is a legitimate `#include` target; downstream code typically reaches them via `Engine::getSystemRegistry().getSystem<T>()` rather than direct construction.

```cpp
// engine/systems/atmosphere_system.h — global wind / weather.
class AtmosphereSystem : public ISystem {
    bool initialize(Engine&) override;        // EnvironmentForces ctor-init.
    void update(float dt) override;            // EnvironmentForces::update(dt).
    bool isForceActive() const override { return true; }
    EnvironmentForces& getEnvironmentForces();
};
```

```cpp
// engine/systems/audio_system.h — OpenAL listener + ducking + per-source push.
class AudioSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float dt) override;            // listener sync → ducking → mixer → per-component AL state.
    UpdatePhase getUpdatePhase() const override { return UpdatePhase::PostCamera; }
    bool isForceActive() const override { return true; }
    bool isAvailable() const;                  // false if no audio hardware.
    AudioEngine& getAudioEngine();
    const std::unordered_map<uint32_t, unsigned int>& activeSources() const;
};
```

```cpp
// engine/systems/character_system.h — PhysicsCharacterController owner.
class CharacterSystem : public ISystem {
    bool initialize(Engine&) override;         // builds controller at camera-pos − eyeHeight.
    void update(float) override;               // no-op; controller stepped in Engine::run.
    PhysicsCharacterController& getPhysicsCharController();
};
```

```cpp
// engine/systems/cloth_system.h — ClothComponent domain grouping.
class ClothSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op; per-component update via scene traversal.
    std::vector<uint32_t> getOwnedComponentTypes() const override;  // ClothComponent.
};
```

```cpp
// engine/systems/destruction_system.h — W13 stub.
class DestructionSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op.
    std::vector<uint32_t> getOwnedComponentTypes() const override;  // empty after W13.
};
```

```cpp
// engine/systems/lighting_system.h — Renderer-embedded lighting placeholder.
class LightingSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op; lighting lives in Renderer.
    bool isForceActive() const override { return true; }
};
```

```cpp
// engine/systems/navigation_system.h — Recast bake + Detour query.
class NavigationSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op (Phase 11A: agent path advance).
    void drawDebug() override;                 // no-op (Phase 11A: navmesh wireframe).
    std::vector<uint32_t> getOwnedComponentTypes() const override;  // NavAgentComponent.

    bool bakeNavMesh(Scene&, const NavMeshBuildConfig& = {});
    bool hasNavMesh() const;
    void clearNavMesh();
    std::vector<glm::vec3> findPath(const glm::vec3& start, const glm::vec3& end);
    glm::vec3 findNearestPoint(const glm::vec3&);
};
```

```cpp
// engine/systems/particle_system.h — ParticleRenderer lifecycle + emitter component grouping.
class ParticleVfxSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op; emitters tick via SceneManager.
    std::vector<uint32_t> getOwnedComponentTypes() const override;  // ParticleEmitter, GPUParticleEmitter.
    ParticleRenderer& getParticleRenderer();
};
```

```cpp
// engine/systems/physics2d_system.h — 2D rigid-body sim on shared 3D Jolt.
class Physics2DSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float dt) override;            // body-velocity-cache writeback to components.
    void onSceneLoad(Scene&) override;         // ensureBody for every entity with RigidBody2D + Collider2D.
    void onSceneUnload(Scene&) override;       // removeBody for all entities.

    JPH::BodyID ensureBody(Entity&);           // idempotent.
    void removeBody(Entity&);
    void applyImpulse(Entity&, const glm::vec2&);
    void setLinearVelocity(Entity&, const glm::vec2&);
    glm::vec2 getLinearVelocity(const Entity&) const;
    void setTransform(Entity&, const glm::vec2&, float rotationRadians);
    bool isInitialized() const;
    std::size_t liveBodyCount() const;
    void setPhysicsWorldForTesting(PhysicsWorld*);  // test seam.
};
```

```cpp
// engine/systems/sprite_system.h — 2D pass with headless sort + batch helpers.
class SpriteSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op; SpriteComponent tick via scene.
    void render(const glm::mat4& viewProj);    // call once per frame after 3D post.

    // Headless static helpers (test without a GL context).
    static std::size_t collectVisible(const Scene&, std::vector<SpriteDrawEntry>& out);
    static void        sortDrawList  (std::vector<SpriteDrawEntry>&);
    static std::vector<Batch> buildBatches(const std::vector<SpriteDrawEntry>&);
    static void        buildBatches  (const std::vector<SpriteDrawEntry>&,
                                       std::vector<Batch>& outBatches);
    SpriteRenderer& getRenderer();
};
```

```cpp
// engine/systems/terrain_system.h — heightfield + LOD owner.
class TerrainSystem : public ISystem {
    bool initialize(Engine&) override;         // builds 257×257 demo heightfield.
    void update(float) override;               // no-op; renderer pumped by render loop.
    bool isForceActive() const override { return true; }
    Terrain& getTerrain();
    TerrainRenderer& getTerrainRenderer();
};
```

```cpp
// engine/systems/ui_system.h — game UI / HUD / menus / accessibility (large surface).
class UISystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float dt) override;            // ticks NotificationQueue with theme transitionDuration.
    UpdatePhase getUpdatePhase() const override { return UpdatePhase::Render; }
    bool isForceActive() const override { return true; }

    void renderUI(int screenW, int screenH);
    bool wantsCaptureInput() const;            // modal OR cursor-over-interactive.
    void setModalCapture(bool);
    void updateMouseHit(const glm::vec2& cursor, int w, int h);

    // Accessibility (Phase 10).
    void setBaseTheme(const UITheme&);
    void setScalePreset(UIScalePreset);        // 1.0× / 1.25× / 1.5× / 2.0×.
    void setHighContrastMode(bool);
    void setReducedMotion(bool);
    void applyAccessibilityBatch(UIScalePreset, bool highContrast, bool reducedMotion);

    // Screen stack (Phase 10 slice 12.2).
    void setScreenBuilder(GameScreen, ScreenBuilder);   // pluggable prefab.
    void setRootScreen(GameScreen);
    void pushModalScreen(GameScreen);
    void popModalScreen();
    void applyIntent(GameScreenIntent);

    // Focus / tab nav (Phase 10.9 Slice 3 S4).
    UIElement* getFocusedElement() const;
    void       setFocusedElement(UIElement*);
    bool       handleKey(int glfwKey, int mods);

    NotificationQueue& getNotifications();
    Signal<GameScreen> onRootScreenChanged, onModalPushed, onModalPopped;
    // see engine/systems/ui_system.h:33 for full surface (~50 entry points).
};
```

```cpp
// engine/systems/vegetation_system.h — foliage + trees.
class VegetationSystem : public ISystem {
    bool initialize(Engine&) override;
    void update(float) override;               // no-op; rendering driven by render loop.
    FoliageManager&  getFoliageManager();
    FoliageRenderer& getFoliageRenderer();
    TreeRenderer&    getTreeRenderer();
};
```

```cpp
// engine/systems/water_system.h — water surfaces + reflection/refraction FBO.
class WaterSystem : public ISystem {
    bool initialize(Engine&) override;         // 25%-resolution reflection / refraction FBOs.
    void update(float) override;               // no-op; rendering driven by render loop.
    std::vector<uint32_t> getOwnedComponentTypes() const override;  // WaterSurfaceComponent.
    WaterRenderer& getWaterRenderer();
    WaterFbo&      getWaterFbo();
};
```

**Non-obvious contract details:**

- `AudioSystem` runs in **`UpdatePhase::PostCamera`** and `UISystem` in **`UpdatePhase::Render`** (every other system uses the default `UpdatePhase::Update`). Order matters: AudioSystem reads camera pos/forward/up *after* the camera is stepped (closes the listener-after-camera dependency, Slice 11 W6); UISystem prepares ImGui-frame state *after* every other domain system has settled.
- `AudioSystem`, `AtmosphereSystem`, `LightingSystem`, `TerrainSystem`, `UISystem` are **force-active** (`isForceActive() == true`) — they own global state (mixer, wind, lighting, heightfield, screen stack) that the scene-driven activation heuristic would otherwise deactivate when the scene has zero matching components.
- `DestructionSystem` is a deliberate **no-op stub** post-W13: it is registered solely so `test_domain_systems` invariants (`name`, `forceActive`) still hold. The fracture/dismemberment cluster lives at `engine/experimental/physics/` and is not built into shipping configurations.
- `Physics2DSystem` shares the **single** `PhysicsWorld` with the 3D physics path (Phase 9F-2). 2D bodies are extruded along Z by `zThickness` (clamped to ≥ 0.06 m for Jolt's convex radius) with `JPH::EAllowedDOFs::Plane2D`. There is no separate broadphase.
- `SpriteSystem`'s static helpers (`collectVisible`, `sortDrawList`, `buildBatches`) are **deliberately Graphics-Library-free** so the sort + pack logic is unit-testable without a window. Only `render()` touches the GPU.
- `NavigationSystem::bakeNavMesh` is **editor-driven** — there is no implicit auto-bake on scene load. After bake it publishes `NavMeshBakedEvent` on the shared `EventBus`.
- `UISystem::setScreenBuilder(screen, {})` clears any override and falls back to the built-in `menu_prefabs` default. Passing `GameScreen::None` to `setRootScreen` clears the canvas (used by editor / headless tests).
- `CharacterSystem::update` is **intentionally empty** — the controller step is in `Engine::run` because it is tightly coupled with `Camera` and `FirstPersonController`. The system exists for lifecycle + future animation subsystems.
- Accessor `getOwnedComponentTypes()` returns by value (small vectors of `uint32_t`); `SystemRegistry` walks them on scene load to compute auto-activation. Force-active systems may legally return an empty list.

**Stability:** every `<name>_system.h` header in this directory is part of the engine's public façade and respects semantic versioning for v0.x. Two evolution points to flag explicitly: (a) `UISystem`'s screen-stack and accessibility setters are still settling (multiple sub-slice revisions through Phase 10.9) — additive only, but the prefab override contract may change; (b) `NavigationSystem::update` and `drawDebug` are placeholders pending Phase 11A agent advance + navmesh wireframe.

## 5. Data Flow

**Steady-state per-frame (driven from `SystemRegistry::updateAll(dt)` in `Engine::run`):**

1. `SystemRegistry` walks `m_systems` in stable phase order (`PreUpdate` → `Update` → `PostCamera` → `PostPhysics` → `Render`).
2. For each *active* system: start `steady_clock` → call `update(dt)` → stop clock → record `m_lastUpdateTimeMs` → log warning if `> m_frameBudgetMs`.
3. **`Update`** phase pumps (registration order, all default phase): `AtmosphereSystem` advances `EnvironmentForces`; `Physics2DSystem` writes back per-body velocities to components; `SpriteSystem`/`ParticleVfxSystem`/`ClothSystem`/`WaterSystem`/`VegetationSystem`/`TerrainSystem`/`LightingSystem`/`CharacterSystem`/`DestructionSystem`/`NavigationSystem` are no-ops at this layer (their primitives tick via scene-traversal or render-loop callbacks).
4. **`PostCamera`** phase: `AudioSystem` syncs the listener to the freshly-stepped camera, advances `DuckingState`, pushes mixer snapshot, walks `AudioSourceComponent`s in the active scene (auto-acquires sources for `autoPlay=true` clips, pushes per-frame `AudioSourceAlState` for tracked sources, reaps stopped sources / vanished entities).
5. **`Render`** phase: `UISystem` advances `NotificationQueue` by `dt × m_theme.transitionDuration`; modal capture state is sticky.
6. After the registry walk, `SystemRegistry::submitRenderDataAll(renderData)` lets opt-in systems push drawables. The actual GPU draws for sprites / UI / terrain / water / vegetation / particles are issued from the renderer's pass list, not from `update()`.

**Scene load / unload:**

1. `SystemRegistry::activateSystemsForScene(scene)` walks every entity, collects component-type ids, activates each non-force-active system whose `getOwnedComponentTypes()` intersects the scene set.
2. `Physics2DSystem::onSceneLoad(scene)` calls `ensureBody` for every entity with both `RigidBody2DComponent` + `Collider2DComponent`. `onSceneUnload` removes them all.
3. (Other systems with scene-load hooks are reserved for Phase 11A — `NavigationSystem` agent activation, `AudioSystem` ambience prebake.)

**Cold start / shutdown:**

1. `Engine::initialize` registers every system in a fixed order (atmosphere, particle, water, vegetation, terrain, cloth, destruction, character, lighting, audio, UI, navigation, sprite, physics2D — `engine/core/engine.cpp:153`).
2. `SystemRegistry::initializeAll(engine)` stable-sorts by `UpdatePhase`, calls `initialize(engine)` in order, rolls back the prefix in reverse on first failure.
3. `SystemRegistry::shutdownAll()` then `clear()` runs at `Engine::shutdown` *while* GL / Window / Renderer are still alive (per Audit §H17 — GL-bound primitives like `WaterFbo` and `ParticleRenderer` need a live context to release GPU resources).

**Exception path:** none of the systems throw in steady state. Init failures inside a primitive (e.g. `m_particleRenderer.init(...)` returning false) are downgraded to `Logger::warning` with a "feature unavailable" message and the system's `initialize` still returns `true` — the system stays registered but its primitive is inert. The narrow exception is a hard `Engine::initialize` failure (e.g. shader load), which is reported by `engine/core` not by these systems.

## 6. CPU / GPU placement

Per CLAUDE.md Rule 7. This subsystem is mostly thin CPU forwarders into primitives that are themselves CPU-or-GPU per their own specs. The placement choice for *the wrapper layer* is uniformly CPU; placement for the *underlying work* varies and is summarised below.

| Workload (where it actually runs) | Placement | Reason |
|-----------------------------------|-----------|--------|
| `ISystem` lifecycle + per-frame phase walk + budget check | CPU (main thread) | Branching, sparse, decision-heavy — exactly the CODING_STANDARDS §17 default. |
| `EnvironmentForces` wind tick (AtmosphereSystem) | CPU (main thread) | Tiny per-frame state machine; a few floats. |
| OpenAL listener sync, ducking, mixer push, per-source state push (AudioSystem) | CPU (main thread) — driver thread inside OpenAL | Driver-side work happens off-thread inside OpenAL; the engine-side state push is CPU/main only. |
| `AudioSourceComponent` scene walk + auto-acquire (AudioSystem) | CPU (main thread) | Sparse iteration over a scene-sized component list. |
| `Terrain` chunk LOD selection / dirty-region heightmap upload (TerrainSystem) | CPU decision (LOD), GPU upload (heightmap / normalmap textures) | LOD is a sparse decision tree → CPU. The heightfield + normal map textures themselves live in GPU memory and are streamed via `glTexSubImage2D` from the main thread. |
| Terrain rasterization (TerrainRenderer, owned but not pumped here) | GPU (vertex + tessellation + fragment shaders) | Per-vertex / per-pixel — §17 default. |
| Foliage / tree / water / particle / sprite rasterization | GPU (instanced draws) | Per-vertex / per-pixel / per-instance — §17 default. |
| Sprite sort + batch pack (SpriteSystem static helpers) | CPU (main thread) | Sparse decision (atlas grouping, layer sort) — §17 default. Stable sort + linear pack are not data-parallel enough to benefit from GPU. |
| Recast navmesh bake (NavigationSystem) | CPU (main thread; one-shot, editor-driven) | Heavy, infrequent, branching geometric algorithm. Off the per-frame budget by design. |
| Detour pathfinding query (`findPath`, `findNearestPoint`) | CPU (main thread) | A* with priority queue; sparse, branching. |
| Jolt 2D rigid-body integration (Physics2DSystem) | CPU (Jolt's job system; main-thread by default in this engine) | Jolt's design: SIMD-friendly per-island steps on CPU. |
| Per-body velocity writeback to components (Physics2DSystem update) | CPU (main thread) | Iteration over `m_bodyByEntity`; small. |
| `UISystem` screen-stack rebuild, focus / tab walk, notification advance | CPU (main thread) | Branching, sparse. |
| `UISystem::renderUI` sprite-batch draw | GPU (instanced quads) | Per-pixel — §17 default. |
| `WaterFbo` 25 %-resolution reflection / refraction render targets | GPU (FBO) | Off-screen rasterization. The wrapper just allocates the textures + binds them at init. |

**Dual-implementation parity tests:** none specific to this subsystem; the sprite sort/pack helpers are CPU-only (no GPU equivalent), and per-system primitives carry their own parity tests where applicable (e.g. `tests/test_gpu_cloth_simulator.cpp` vs `tests/test_cloth_simulator.cpp` for cloth).

## 7. Threading model

Per CODING_STANDARDS §13. The wrapper layer is **main-thread-only** by contract; primitives may have their own off-thread work that the wrapper does not directly enter.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the only thread that drives `SystemRegistry::updateAll`) | every public method on every system in this directory | none — main thread is single-threaded by contract |
| **Audio thread** (owned by OpenAL inside `AudioEngine`) | does not enter `engine/systems` directly; consumes the snapshot pushed each frame by `AudioSystem::update` | none at this layer |
| **Job-system worker** (Jolt's pool, owned by `PhysicsWorld`) | does not enter `engine/systems`; bodies created/destroyed via `BodyInterface` from the main thread | none at this layer |
| **Async-texture-loader thread** (owned by `engine/resource`) | does not enter `engine/systems` | none at this layer |

**Main-thread-only:** every method on every system in this directory. Calling them from a worker is undefined.

**Lock-free / atomic:** none required at the wrapper layer. The sole shared state between threads is the snapshot `AudioSystem` pushes to `AudioEngine::setMixerSnapshot` / `setDuckingSnapshot` / `applySourceState` each frame; the audio thread reads those without further synchronisation because the OpenAL driver provides the boundary.

**Future:** `TerrainSystem` is a likely candidate for off-thread chunk streaming when terrain chunking lands (project memory `project_terrain_chunking.md`). When that happens, the chunk-load worker will write into a staging buffer and the main-thread `update` will publish atomically; the wrapper is the right place for that handoff.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame total. Each system in this directory contributes a slice. The wrapper layer itself is overhead-only; the budgets below are the *visible* per-system cost as measured by `SystemRegistry::m_systemMetrics`.

| System | Budget | Measured (RX 6600, 1080p) |
|--------|--------|----------------------------|
| `AtmosphereSystem::update` | < 0.05 ms | TBD — measure by Phase 11 audit |
| `AudioSystem::update` (≤ 64 active sources) | < 0.5 ms | TBD — measure by Phase 11 audit |
| `CharacterSystem::update` | < 0.01 ms (no-op) | TBD — measure by Phase 11 audit |
| `ClothSystem::update` | < 0.01 ms (wrapper no-op; cost is per-`ClothComponent`) | TBD — measure by Phase 11 audit |
| `DestructionSystem::update` | 0 ms (no-op stub) | n/a — W13 stub |
| `LightingSystem::update` | < 0.01 ms (no-op) | TBD — measure by Phase 11 audit |
| `NavigationSystem::update` | < 0.05 ms (no-op until Phase 11A agent advance) | TBD — measure by Phase 11 audit |
| `NavigationSystem::bakeNavMesh` (one-shot, editor-driven) | < 2000 ms (full Tabernacle scene) | TBD — measure by Phase 11 audit |
| `ParticleVfxSystem::update` | < 0.01 ms (wrapper no-op; cost is per-emitter) | TBD — measure by Phase 11 audit |
| `Physics2DSystem::update` (≤ 256 bodies) | < 0.3 ms | TBD — measure by Phase 11 audit |
| `SpriteSystem::render` (≤ 1024 sprites) | < 1.0 ms | TBD — measure by Phase 11 audit |
| `TerrainSystem::update` | < 0.01 ms (wrapper no-op; LOD pump in render loop) | TBD — measure by Phase 11 audit |
| `UISystem::update` (steady HUD, no modals) | < 0.1 ms | TBD — measure by Phase 11 audit |
| `UISystem::renderUI` | < 0.5 ms | TBD — measure by Phase 11 audit |
| `VegetationSystem::update` | < 0.01 ms (wrapper no-op) | TBD — measure by Phase 11 audit |
| `WaterSystem::update` | < 0.01 ms (wrapper no-op; refl/refr in render loop) | TBD — measure by Phase 11 audit |

**Cold-start budgets (one-shot from `Engine::initialize`):**

| Path | Budget | Measured |
|------|--------|----------|
| `TerrainSystem::initialize` (257×257 demo heightfield seed + normalmap + quadtree) | < 50 ms | TBD — measure by Phase 11 audit |
| `AudioSystem::initialize` (OpenAL device + context) | < 100 ms | TBD — measure by Phase 11 audit |
| `WaterSystem::initialize` (FBO allocation at 25 % resolution) | < 5 ms | TBD — measure by Phase 11 audit |
| All other systems' `initialize` combined | < 20 ms | TBD — measure by Phase 11 audit |

Profiler markers / capture points: per-system entries appear in the `SystemRegistry::m_systemMetrics` table by name (e.g. `"Audio"`, `"Terrain"`, `"Sprite"`); over-budget systems emit a `Logger::warning` containing the system name (greppable in capture logs). Renderer-side draws issued by primitives have their own `glPushDebugGroup` markers (`"TerrainPass"`, `"WaterReflectPass"`, `"SpritePass"`, …) — see each primitive's spec.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (each system is constructed via `SystemRegistry::registerSystem<T>` → `std::make_unique`); per-system primitives own their own buffers. `SpriteSystem` retains `m_entriesScratch` + `m_batchesScratch` across frames so the per-frame draw-list pass does not heap-alloc (Phase 10.9 Slice 13 Pe2). |
| Per-frame transients | None at the wrapper layer for shipping systems; `SpriteSystem` clears its scratch vectors in place (capacity preserved). `AudioSystem` walks `m_activeSources` in place (no transient containers). `Physics2DSystem` walks `m_bodyByEntity` in place. |
| Peak working set | Wrapper-layer overhead is negligible (≤ ~1 MB across all 14 systems for the wrapper structs themselves). The dominant memory belongs to primitives: `Terrain` heightfield ≈ 257 × 257 × 4 B ≈ 264 KB on the CPU side + matching GPU textures; `WaterFbo` reflection/refraction render targets at 25 % screen ≈ 480 × 270 × 4 B × 2 ≈ 1 MB; `AudioEngine` buffer cache scales with loaded clips; `UICanvas` + `NotificationQueue` ≈ tens of KB; `ParticleRenderer` GPU instance buffer scales with active emitters. |
| Ownership | `SystemRegistry::m_systems` owns every `ISystem` via `std::unique_ptr`. Each system owns its primitives by value (e.g. `TerrainSystem::m_terrain`, `AudioSystem::m_audioEngine`). `Physics2DSystem::m_bodyByEntity` keys onto `JPH::BodyID`s owned by the shared `PhysicsWorld`. |
| Lifetimes | Engine-lifetime — every wrapper is constructed at `Engine::initialize` and destroyed at `Engine::shutdown`. Primitives follow the same lifetime by composition. The single exception is `Physics2DSystem`'s body table, which is scene-scoped (cleared in `onSceneUnload`). |

No `new` / `delete` in feature code (CODING_STANDARDS §12). The legitimate exceptions are GLFW, GL, Jolt, OpenAL, Recast/Detour internal allocations behind RAII handles owned by their respective primitives.

## 10. Error handling

Per CODING_STANDARDS §11. Wrapper-layer policy is **uniform across every system**: primitive-init failures are downgraded to `Logger::warning`, the system stays registered with an inert primitive, and `initialize()` still returns `true`. The engine continues with that domain feature unavailable rather than refusing to start.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Primitive renderer init failed (`m_particleRenderer.init`, `m_foliageRenderer.init`, `m_treeRenderer.init`, `m_terrainRenderer.init`, `m_waterRenderer.init`, `m_renderer.initialize`, `m_spriteBatch.initialize`) | `Logger::warning("[XSystem] X init failed — feature unavailable")` + system's `initialize` still returns `true` | Engine continues; affected feature is inert (no draws) but the system stays registered. |
| OpenAL device unavailable (`AudioSystem::initialize`) | `Logger::warning` + `AudioEngine::isAvailable()` returns false | Engine continues silently; `AudioSystem::update` returns early when not available. |
| Physics not initialised (`CharacterSystem`, `Physics2DSystem`) | `Logger::info` / early return; `Physics2DSystem::isInitialized()` returns false | Engine continues; physics-dependent features inert. |
| `Physics2DSystem::ensureBody` malformed collider (e.g. polygon < 3 verts) | `Logger::warning` + return invalid `JPH::BodyID` | Body skipped; gameplay code must check `BodyID::IsInvalid()`. |
| `NavigationSystem::bakeNavMesh` build failed | `false` return + (Recast / Detour internal log via `Logger::error`) | Editor surfaces failure; previous navmesh (if any) retained on `m_builder`. |
| `NavigationSystem::bakeNavMesh` build succeeded but query init failed | `Logger::error` + `false` return | Same — navmesh data retained, queries unavailable. |
| `SpriteSystem::buildBatches` missing atlas frame | silent skip (per-instance) | Keeps the render loop robust against stale frame names during asset hot-reload. |
| `UISystem::handleKey` unhandled key | returns `false` | Game input handler processes the key normally. |
| `UISystem::popModalScreen` empty stack | no-op | Caller need not pre-check. |
| Subscriber callback throws inside `EventBus::publish` (e.g. `NavMeshBakedEvent`) | propagates to publisher (no wrapper) | **Policy: callbacks must not throw** — fix the callback (matches `engine/core` policy). |
| Programmer error (null entity, double-init) | `assert` (debug) / undefined behaviour (release) | Fix the caller. |
| Out of memory | `std::bad_alloc` propagates | App aborts (matches CODING_STANDARDS §11 — fatal during init; steady-state allocations are bounded). |

`Result<T, E>` / `std::expected` is **not** used in this subsystem; primitive-init returns `bool` and falls through to `Logger::warning`. Migration is on the broader engine-wide debt list (engine/core spec §15 Q4).

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `ISystem` invariants for every concrete system (name, force-active, owned components) | `tests/test_domain_systems.cpp` | All 14 systems — pinned contract |
| `SystemRegistry` registration / phase sort / init-prefix rollback | `tests/test_system_registry.cpp` | Lifecycle (in `engine/core`'s test set, listed here for cross-reference) |
| `AudioSystem` listener / mixer / per-source state push | `tests/test_audio_attenuation.cpp`, `test_audio_doppler.cpp`, `test_audio_occlusion.cpp`, `test_audio_source_state.cpp`, `test_audio_source_component.cpp`, `test_audio_engine_sandbox.cpp`, `test_audio_mixer.cpp`, `test_audio_music.cpp`, `test_audio_music_stream.cpp`, `test_audio_reverb.cpp`, `test_audio_hrtf.cpp`, `test_audio_ambient.cpp`, `test_audio_panel.cpp`, `test_audio_stop_sound.cpp` | Full mixer + per-component pipeline |
| `Physics2DSystem` body lifecycle + impulse / velocity / transform | `tests/test_physics2d_system.cpp` | Body create / destroy, scene-load auto-spawn, helpers |
| `SpriteSystem` collect / sort / pack | `tests/test_sprite_renderer.cpp`, `test_sprite_atlas.cpp`, `test_sprite_animation.cpp`, `test_sprite_panel.cpp` | Headless sort + batch helpers; renderer pump |
| `UISystem` accessibility / focus / hit / theme / runtime / world projection / screen stack | `tests/test_ui_system_input.cpp`, `test_ui_system_screen_stack.cpp`, `test_ui_focus_navigation.cpp`, `test_ui_hit_test.cpp`, `test_ui_widgets.cpp`, `test_ui_design_widgets.cpp`, `test_ui_layout_panel.cpp`, `test_ui_runtime_panel.cpp`, `test_ui_theme_accessibility.cpp`, `test_ui_world_projection.cpp`, `test_ui_accessible.cpp` | Modal / focus / hit / accessibility batch / state machine |
| `NavigationSystem` bake + query | `tests/test_nav_mesh_query.cpp`, `test_navigation_panel.cpp` | Query API correctness + editor panel smoke |
| `WaterSystem` water surface component | `tests/test_water_surface.cpp` | Component round-trip |
| `CharacterSystem` (via physics character controller suite) | `tests/test_physics_character_controller.cpp`, `test_character_controller_2d.cpp` | Controller behaviour |
| `ClothSystem` (via cloth simulator suite) | `tests/test_cloth_simulator.cpp`, `test_cloth_collision.cpp`, `test_cloth_constraint_graph.cpp`, `test_cloth_presets.cpp`, `test_cloth_solver_backend.cpp`, `test_cloth_solver_improvements.cpp`, `test_cloth_backend_factory.cpp`, `test_gpu_cloth_simulator.cpp` | Component simulation + solver parity |
| `ParticleVfxSystem` (via particle-data + GPU-particle suites) | `tests/test_particle_data.cpp`, `test_gpu_particle_system.cpp` | Emitter component + GPU pipeline |
| `TerrainSystem` size caps | `tests/test_terrain_size_caps.cpp` | Heightfield bounds |
| `AtmosphereSystem` / `LightingSystem` / `VegetationSystem` / `DestructionSystem` | `tests/test_domain_systems.cpp` | Name / force-active / owned-components only — no per-frame body to test |
| Lighting effects (emissive surfaces) | `tests/test_emissive_lighting.cpp` | Renderer-side emissive contribution |

**Adding a test for this subsystem:** drop a new `tests/test_<system>_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use the system class directly without an `Engine` instance — every wrapper in this directory **except `UISystem::renderUI`, `SpriteSystem::render`, and the `*Renderer::init` calls inside `initialize()`** is unit-testable headlessly because the wrapper is pure forwarding logic. GPU / GLFW-bound paths exercise via `engine/testing/visual_test_runner.h`. Headless `Physics2DSystem` tests use `setPhysicsWorldForTesting(...)` to bypass `Engine::initialize`.

**Coverage gap:** the `update()` body of force-active systems (`AtmosphereSystem`, `AudioSystem`, `LightingSystem`, `TerrainSystem`, `UISystem`) is exercised end-to-end only through the visual-test runner — `test_domain_systems` pins names / flags / component lists but does not pump frames. The wrapper bodies are short enough that this is a deliberate trade-off, not a defect.

## 12. Accessibility

Several systems in this directory produce user-facing output and carry first-class accessibility constraints:

- **`UISystem`** is the primary accessibility surface. It owns the three Phase 10 toggles end-to-end:
  - **UI scale preset** (`UIScalePreset::X1_0` / `X1_25` / `X1_5` / `X2_0`) — partially-sighted users (per project memory) should select 1.5× minimum or 2.0×. The active theme is recomputed from `m_baseTheme` so per-field artist overrides survive a base-theme update only if baked in via `setBaseTheme(...)`.
  - **High-contrast mode** — swaps the palette for black-on-white with saturated accents; sizing stays under scale-preset control so `2.0× + high-contrast` is a legal combination.
  - **Reduced motion** — zeros `transitionDuration` so UI transitions snap; composes with the other two toggles.
  - `applyAccessibilityBatch(scale, highContrast, reducedMotion)` is the single-rebuild path used by the Settings apply chain so a fresh load / Apply / Restore Defaults pushes all three values without three theme-rebuilds.
  - **Keyboard navigation** (`handleKey` + `setFocusedElement`): Tab / Shift-Tab cycles interactive elements; arrow keys do the same; Enter / Space fires `onClick`. Tab order is built by depth-first canvas walk, skipping invisible subtrees and non-interactive elements. Modal-active traversal traps focus inside the modal canvas (root unreachable until modal closes). The contract that `UISystem` consumes from `engine/core` is `KeyPressedEvent::mods` so it can distinguish Shift+Tab from Tab without re-querying GLFW.
  - **No color-only encoding.** UI element styling must back colour with text labels per the partially-sighted-user constraint (memory).
- **`AudioSystem`** routes the audio accessibility chain: mixer (music / voice / SFX / UI buses), HRTF on/off, subtitles via the `SubtitleQueueApplySink` pushed by `engine/core`. Game code triggers captions through `engine.getCaptionMap()` / `engine.getSubtitleQueue()`; `AudioSystem` itself does not read settings — it consumes the snapshot pushed each frame.
- **`UISystem`** also drives the **photosensitive safety** caps (`maxFlashAlpha`, `maxStrobeHz`, `bloomIntensityScale`) applied via `RendererAccessibilityApplySink`, which is built around `UISystemAccessibilityApplySink` / `PhotosensitiveStoreApplySink` in `engine/core/settings_apply.h`.
- **`NavigationSystem`** produces no user-facing output; debug-draw is off by default (Phase 11A wireframe will be a debug-overlay and must respect the existing in-game / dev-mode gate).
- **`SpriteSystem`** produces user-facing pixels but no game-state-conveying colour-only encoding. Sprite tint defaults respect the global UI-theme palette only when used for HUD widgets (in which case they should route through `UISystem`'s sprite batch instead).
- All other systems in this directory (`AtmosphereSystem`, `CharacterSystem`, `ClothSystem`, `DestructionSystem`, `LightingSystem`, `ParticleVfxSystem`, `Physics2DSystem`, `TerrainSystem`, `VegetationSystem`, `WaterSystem`) produce no direct user-facing UX — accessibility is a non-issue at the wrapper layer.

Constraint summary for downstream code that consumes `engine/systems`:

- Game UI code must use `UISystem`'s screen-stack / theme / focus APIs rather than rolling its own ImGui — accessibility toggles apply only to widgets created through `UICanvas` + `UITheme`.
- Captions: gameplay code triggers via `Engine::getCaptionMap()` and `Engine::getSubtitleQueue()`; `AudioSystem` does not own caption state.
- Photosensitive defaults flow from `engine/core`'s `PhotosensitiveSafetyWire` (`maxFlashAlpha = 0.25`, `maxStrobeHz = 2.0`, `bloomIntensityScale = 0.6`) through the apply-sink chain — wrappers here only consume the snapshot.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/i_system.h` | engine subsystem | Base interface every class here implements. |
| `engine/core/engine.h` | engine subsystem | `initialize(Engine&)` receives the engine for shared infrastructure access (camera, physics, scene manager, asset path). |
| `engine/core/logger.h` | engine subsystem | Init / shutdown breadcrumbs and primitive-init-failed warnings. |
| `engine/core/system_events.h` | engine subsystem | `NavMeshBakedEvent` (NavigationSystem); collision events (Physics2DSystem). |
| `engine/scene/component.h`, `entity.h`, `scene.h`, `scene_manager.h` | engine subsystem | Component-id lookup (`ComponentTypeId::get<T>()`); per-frame scene traversal (Audio, Sprite, Physics2D). |
| `engine/audio/audio_engine.h`, `audio_source_component.h`, `audio_source_state.h` | engine subsystem | Owned by `AudioSystem`. |
| `engine/environment/environment_forces.h`, `terrain.h`, `foliage_manager.h` | engine subsystem | Owned by Atmosphere / Terrain / Vegetation. |
| `engine/navigation/nav_mesh_builder.h`, `nav_mesh_query.h`, `nav_mesh_config.h`, `nav_agent_component.h` | engine subsystem | Owned by `NavigationSystem`. |
| `engine/physics/physics_character_controller.h`, `physics_world.h`, `cloth_component.h` | engine subsystem | Owned by Character / Physics2D / Cloth. |
| `engine/renderer/camera.h`, `particle_renderer.h`, `terrain_renderer.h`, `foliage_renderer.h`, `tree_renderer.h`, `water_renderer.h`, `water_fbo.h`, `sprite_renderer.h`, `sprite_atlas.h`, `text_renderer.h`, `renderer.h` | engine subsystem | Owned by the respective wrappers; AudioSystem reads camera transform. |
| `engine/scene/water_surface.h`, `particle_emitter.h`, `gpu_particle_emitter.h`, `sprite_component.h`, `rigid_body_2d_component.h`, `collider_2d_component.h` | engine subsystem | `getOwnedComponentTypes()` and per-frame component walk. |
| `engine/ui/game_screen.h`, `sprite_batch_renderer.h`, `ui_canvas.h`, `ui_notification_toast.h`, `ui_signal.h`, `ui_theme.h`, `subtitle_renderer.h`, `menu_prefabs.h` | engine subsystem | Owned by `UISystem`. |
| `<glm/glm.hpp>` | external | Math primitives. |
| `<Jolt/...>` (`Body/BodyCreationSettings.h`, `Body/BodyInterface.h`, `Body/AllowedDOFs.h`, `Collision/Shape/{BoxShape,SphereShape,CapsuleShape,CylinderShape,ConvexHullShape,MeshShape,StaticCompoundShape}.h`) | external (Jolt 5+) | 2D body creation with `Plane2D` DOF. |
| `<GLFW/glfw3.h>`, `<glad/gl.h>` | external | Key codes (`UISystem::handleKey`); GL state for sprite/UI passes. |
| `<chrono>`, `<algorithm>`, `<cmath>`, `<vector>`, `<unordered_map>`, `<functional>`, `<string>`, `<cstdint>` | std | Per-frame timing, sort, terrain seeding, scratch containers, body-id table, screen-builder callback. |

**Direction:** `engine/systems` is depended on by `engine/core` (forward-declared registration target) and by `engine/editor` (panels reach systems via `m_systemRegistry.getSystem<T>()`). It depends on `engine/{audio,environment,navigation,physics,renderer,scene,ui}` and `engine/core`. It must **not** depend on `engine/editor`, `engine/experimental`, or any concrete game project — the include graph is one-way (`core → systems → primitives`). The post-W13 `DestructionSystem` rule is explicit: production code in this directory does not include from `engine/experimental/`.

## 14. References

Cited research / authoritative external sources (current 2025-2026 sources marked):

- Sander Mertens. *ECS FAQ* (2024–2025, ongoing) — registry-of-systems vs archetype storage trade-offs that informed the wrapper-around-primitives shape used here. <https://github.com/SanderMertens/ecs-faq>
- skypjack. *EnTT — fast and reliable Entity Component System (ECS) for modern C++* (2025) — type-indexed system lookup reference for `SystemRegistry::getSystem<T>()`. <https://github.com/skypjack/entt>
- Unity Technologies. *Entities 1.x — System Groups* (Unity DOTS 2025-2026 stable) — three-root-group scheduling (Initialization / Simulation / Presentation) and `UpdateInGroup` / `OrderFirst` / `OrderLast` informed the `UpdatePhase` enum's coarse-then-stable-sort design. <https://docs.unity3d.com/Packages/com.unity.entities@1.0/manual/systems-update-order.html>
- Unity Forum. *ECS Development Status / Milestones — December 2025* — current state of Unity's ECS roadmap; informs decision to keep registry-of-systems pattern instead of full archetype storage in v0.x. <https://discussions.unity.com/t/ecs-development-status-december-2025/1699284>
- PulseGeek. *ECS Architecture in Game Development: Core Patterns* (2025) — modern patterns including phase-pinned write ownership and accumulation buffers when two systems must touch the same component. <https://pulsegeek.com/articles/ecs-architecture-in-game-development-core-patterns/>
- Voxagon. *Thoughts on ECS* (2025-03-28) — current-state critique informing why this engine kept a registry-of-systems pattern instead of full archetype storage. <https://blog.voxagon.se/2025/03/28/thoughts-on-ecs.html>
- Recast Navigation Project. *Recast & Detour — Industry-standard navigation-mesh toolset* (2025) — bake (Recast) + query (Detour) split, source-integration recommendation, DetourTileCache for streaming, DetourCrowd for agent simulation. <https://github.com/recastnavigation/recastnavigation>
- Recast Navigation. *Building & Integrating* (current) — integration patterns for engine source trees. <http://recastnav.com/md_Docs_2__2__BuildingAndIntegrating.html>
- Jolt Physics Project. *Jolt 5.x — `EAllowedDOFs::Plane2D` and 2D-on-3D-broadphase patterns* (2025). <https://github.com/jrouwe/JoltPhysics>
- OpenAL Soft. *Effects-extension + listener / source state model* (current). <https://openal-soft.org/>
- ISO C++ Core Guidelines, *Concurrency* (CP.20–CP.43) — threading conventions referenced from CODING_STANDARDS §13. <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-concurrency>

Internal cross-references:

- `docs/engine/core/spec.md` — `ISystem`, `UpdatePhase`, `SystemRegistry` contracts every wrapper here implements.
- `docs/phases/phase_09a_design.md` — original system-infrastructure design (research summary, Unreal `USubsystem` / Unity `PlayerLoop` / Godot autoload / flecs module patterns reviewed).
- `docs/phases/phase_09b_design.md`, `phase_09c_design.md`, `phase_09e_design.md`, `phase_09e3_design.md`, `phase_09f_design.md` — per-wave domain-system rollouts.
- `CODING_STANDARDS.md` §11 (errors), §13 (threading), §17 (CPU/GPU), §18 (public API).
- `ARCHITECTURE.md` §1–6 (subsystem map, engine loop, event bus).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | `DestructionSystem` is a no-op stub; do we revive it in Phase 11A by re-promoting the experimental physics cluster, or remove it entirely? Currently kept for `test_domain_systems` invariants. | milnet01 | Phase 11A entry |
| 2 | `LightingSystem` is an empty pump (lighting lives in `Renderer`). Does Phase 11 lift the global-illumination probe grid (per `docs/GI_ROADMAP.md`) into this wrapper, or keep lighting renderer-embedded? | milnet01 | Phase 11 entry |
| 3 | `NavigationSystem::update` is a no-op (agents have no path-advance yet). Phase 11A is expected to land agent advance + `drawDebug` wireframe. | milnet01 | Phase 11A |
| 4 | Performance budgets in §8 are placeholders. Need a one-shot Tracy / RenderDoc capture across a populated Tabernacle scene to fill measured numbers. | milnet01 | Phase 11 audit (concrete: end of Phase 10.9) |
| 5 | `TerrainSystem` seeds its own demo heightfield in `initialize()` — should that be moved to a demo-scene loader so `TerrainSystem` is content-agnostic? Tied to terrain chunking (project memory `project_terrain_chunking.md`). | milnet01 | Phase 11 entry |
| 6 | `Physics2DSystem::ensureBody` extracts `zRadians = 0.0f` from the entity transform (TODO at `physics2d_system.cpp:275`). Pending a quaternion-to-Z-rotation helper. | milnet01 | Phase 11A |
| 7 | `CharacterSystem::update` is empty because the controller step is in `Engine::run`. Eventually move the step into `update(dt)` so the system is self-contained — blocked on decoupling the controller from `FirstPersonController`. | milnet01 | Phase 11 entry |
| 8 | Wrappers downgrade primitive-init failures to `Logger::warning` and return `true` from `initialize`. Should any of these be promoted to hard-fail (e.g. terrain renderer missing → cannot render world)? Tied to `engine/core` Q1. | milnet01 | Phase 11 entry |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/systems` as the domain-`ISystem` implementation layer, post-Phase 10.9 audit. Captures W13 stub status of `DestructionSystem`, force-active flags introduced in Slice 8 W5, `UpdatePhase::PostCamera` / `Render` overrides introduced in Slice 11 Sy1. |
