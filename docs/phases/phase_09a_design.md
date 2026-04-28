# Phase 9A Design: System Infrastructure

## Overview

Phase 9A introduces the formal domain system abstraction for Vestige. It creates three
components: `ISystem` (base class), `SystemRegistry` (lifecycle manager), and typed
cross-system events (using the existing EventBus).

This is **infrastructure only** -- no existing subsystems are wrapped in this phase (that's
Phase 9B). After 9A, the registry is ready to accept domain system registrations.

## Research Summary

**Industry patterns reviewed:**
- **Unreal Engine** -- `USubsystem` hierarchy (Engine/GameInstance/World scoping), auto-registration via reflection, lifecycle tied to outer object
- **Unity** -- PlayerLoop phases (EarlyUpdate, FixedUpdate, Update, LateUpdate) with `[UpdateBefore/After]` attributes for fine ordering
- **Godot** -- Autoload singletons, `_process()` vs `_physics_process()` split
- **Flecs/entt** -- Topological sort on declared read/write dependencies; flecs modules for wrapping legacy code

**Key takeaways applied to Vestige:**
1. Two-level update ordering: coarse phases (FixedUpdate, Update, Render) + fine dependency declarations
2. Auto-activation from scene contents (Unreal pattern) -- don't force manual system lists
3. Performance budgets as first-class API (Unreal scalability settings pattern)
4. Systems declare owned component types (flecs module pattern)

## Architecture

### ISystem Interface

```
engine/core/i_system.h
```

Abstract base class. All domain systems inherit from this.

**Pure virtuals (every system MUST implement):**
- `getSystemName()` -- Human-readable name for logging/profiling
- `initialize(Engine&)` -- One-time setup (called by SystemRegistry during init)
- `shutdown()` -- Cleanup (called in reverse registration order)
- `update(float deltaTime)` -- Per-frame variable-rate update

**Opt-in virtual no-ops (override only if needed):**
- `fixedUpdate(float fixedDt)` -- Fixed-timestep update (physics, simulation)
- `submitRenderData(SceneRenderData&)` -- Push render items before rendering
- `onSceneLoad(Scene&)` -- React to scene changes
- `onSceneUnload(Scene&)` -- Cleanup scene-specific state
- `drawDebug()` -- Debug visualization
- `reportMetrics(PerformanceProfiler&)` -- Submit profiling data

**Performance budget API:**
- `getFrameBudgetMs() / setFrameBudgetMs(float)` -- Per-system time budget
- `getLastUpdateTimeMs()` -- Actual time spent in last update
- `isOverBudget()` -- Convenience check

**Component ownership:**
- `getOwnedComponentTypes()` -- Returns `std::vector<uint32_t>` of ComponentTypeId values
- Used by SystemRegistry for auto-activation (if scene has entities with these components, activate the system)

**Activation state:**
- `isActive() / setActive(bool)` -- Systems can be deactivated (skipped in update loop)
- `isForceActive()` -- Returns true for always-on systems (Lighting, Atmosphere)

### SystemRegistry

```
engine/core/system_registry.h
engine/core/system_registry.cpp
```

Manages all domain system instances. Owned by Engine as a value member.

**Registration:**
```cpp
template<typename T, typename... Args>
T* registerSystem(Args&&... args);

template<typename T>
T* getSystem();
```

Systems are stored as `unique_ptr<ISystem>` in registration order. A type-indexed map
(`unordered_map<type_index, ISystem*>`) provides O(1) lookup by type.

**Lifecycle methods:**
- `initializeAll(Engine&)` -- Calls `initialize()` on each registered system in registration order
- `shutdownAll()` -- Calls `shutdown()` in **reverse** registration order
- `updateAll(float dt)` -- Calls `update()` on all active systems, measures time per system
- `fixedUpdateAll(float fixedDt)` -- Calls `fixedUpdate()` on all active systems
- `submitRenderDataAll(SceneRenderData&)` -- Calls `submitRenderData()` on all active systems
- `onSceneLoadAll(Scene&)` / `onSceneUnloadAll(Scene&)` -- Scene transition hooks

**Auto-activation:**
- `activateSystemsForScene(Scene&)` -- Scans all entities for component types, matches against
  each system's `getOwnedComponentTypes()`. Activates systems that have matching components.
- Systems with `isForceActive() == true` are always activated.
- Called automatically on scene load.

**Performance monitoring:**
- `getSystemMetrics()` -- Returns per-system timing data
- `getTotalUpdateTimeMs()` -- Sum of all system update times
- If a system exceeds its budget, the registry logs a warning (but does not force quality reduction -- that's the system's responsibility).

### Cross-System Events

```
engine/core/system_events.h
```

Typed event structs for discrete inter-system communication via existing EventBus.
Systems subscribe in `initialize()` and unsubscribe in `shutdown()`.

**Initial event types:**
- `SceneLoadedEvent { Scene* scene }` -- Scene finished loading
- `SceneUnloadedEvent { Scene* scene }` -- Scene about to unload
- `WeatherChangedEvent { WeatherState newState }` -- Weather parameters changed
- `EntityDestroyedEvent { uint32_t entityId }` -- Entity removed from scene

**Rule (from roadmap):** Events for discrete occurrences, direct queries for continuous data.
Systems query `EnvironmentForces` and `Terrain` directly -- no per-frame broadcast events.

## Design Decisions

### Why not full ECS?

Vestige uses inheritance-based components (`Component` base class, virtual `update()`,
type-indexed map per entity). Converting to data-oriented ECS would require rewriting
every component and all entity iteration code. The domain system pattern wraps the existing
component model without changing it.

### Why registration order for update?

Topological sorting from declared dependencies adds complexity and is hard to debug. The
existing engine already has a well-defined update order (environment -> physics -> scene ->
render). Registration order matches this existing sequence. If future systems need explicit
ordering, we can add `getUpdatePriority()` or `[UpdateAfter]`-style declarations.

### Why not auto-registration via static constructors?

Static initialization order is undefined across translation units in C++. Explicit registration
in `Engine::initialize()` is predictable, debuggable, and matches the existing pattern.

### Why systems take Engine& in initialize()?

Systems need access to shared infrastructure (EventBus, ResourceManager, EnvironmentForces,
etc.) during setup. Passing `Engine&` gives access to everything without requiring each system
to declare individual dependencies. This matches how existing subsystems already work (they're
constructed with references to Engine-owned objects).

## Performance Considerations

- **Zero overhead when no systems registered** -- updateAll() early-returns if empty
- **Per-system timing** via `std::chrono::steady_clock` in updateAll/fixedUpdateAll
- **Budget enforcement is advisory** -- the registry reports overruns; systems self-manage quality
- **No dynamic allocation in hot path** -- systems vector is fixed after init; metrics are pre-allocated

## Implementation Steps

1. Create `engine/core/i_system.h` -- ISystem abstract base class
2. Create `engine/core/system_registry.h/.cpp` -- SystemRegistry implementation
3. Create `engine/core/system_events.h` -- Cross-system event types
4. Modify `engine/core/engine.h` -- Add `SystemRegistry m_systemRegistry` member
5. Modify `engine/core/engine.cpp` -- Wire registry lifecycle into init/run/shutdown
6. Create `tests/test_system_registry.cpp` -- Unit tests
7. Add new files to `engine/CMakeLists.txt` and `tests/CMakeLists.txt`

## File Summary

| File | Type | Description |
|------|------|-------------|
| `engine/core/i_system.h` | New | ISystem abstract base class |
| `engine/core/system_registry.h` | New | SystemRegistry class declaration |
| `engine/core/system_registry.cpp` | New | SystemRegistry implementation |
| `engine/core/system_events.h` | New | Cross-system event struct definitions |
| `engine/core/engine.h` | Modify | Add SystemRegistry member |
| `engine/core/engine.cpp` | Modify | Wire registry into update loop |
| `tests/test_system_registry.cpp` | New | Unit tests |
| `engine/CMakeLists.txt` | Modify | Add source file |
| `tests/CMakeLists.txt` | Modify | Add test file |
