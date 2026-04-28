# Phase 9B Design: Wrap Existing Code into Domain Systems

## Overview

Phase 9B wraps 9 existing subsystem groups into formal `ISystem` domain system classes
managed by the `SystemRegistry` infrastructure created in Phase 9A. Each domain system owns
its subsystem instances, manages their lifecycle, and exposes typed accessors.

## Research Summary

### Unreal Engine Subsystems (USubsystem)
- Subsystems have explicitly-defined lifecycles managed by `FSubsystemCollection`
- `Initialize()` / `Deinitialize()` called automatically
- `ShouldCreateSubsystem()` for conditional creation (similar to our auto-activation)
- Scoped lifetimes: GameInstance, World, LocalPlayer

**Source:** [Unreal Engine Programming Subsystems Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine)

### Facade Pattern
- Provides a unified interface over complex subsystem interactions
- Hides implementation details while preserving full functionality
- Commonly used in game engines to wrap rendering, physics, audio subsystems

**Source:** [Refactoring Guru - Facade Pattern](https://refactoring.guru/design-patterns/facade)

### Design Decision: Ownership Transfer vs Facade
**Chosen: Ownership Transfer.** Domain systems hold subsystem instances as value members
(moved from Engine). This matches the roadmap's intent ("have it own the existing subsystem
instances") and gives systems full lifecycle control.

Engine stores cached raw pointers for hot-path render loop access. These are set once during
registration and avoid per-frame type_index map lookups.

## Architecture

```
Engine
  |-- SystemRegistry (manages lifecycle)
  |     |-- AtmosphereSystem --> owns EnvironmentForces
  |     |-- ParticleVfxSystem --> owns ParticleRenderer
  |     |-- WaterSystem --> owns WaterRenderer, WaterFbo
  |     |-- VegetationSystem --> owns FoliageManager, FoliageRenderer, TreeRenderer
  |     |-- TerrainSystem --> owns Terrain, TerrainRenderer
  |     |-- ClothSystem --> (facade, entity-component based)
  |     |-- DestructionSystem --> (facade, entity-component based)
  |     |-- CharacterSystem --> owns PhysicsCharacterController
  |     |-- LightingSystem --> (facade, embedded in Renderer)
  |
  |-- m_environmentForces* (cached pointer -> AtmosphereSystem)
  |-- m_particleRenderer*  (cached pointer -> ParticleVfxSystem)
  |-- m_waterRenderer*     (cached pointer -> WaterSystem)
  |-- ... etc
```

## Domain Systems

### Systems with Full Ownership (6)
| System | Owns | update() | isForceActive |
|--------|------|----------|---------------|
| AtmosphereSystem | EnvironmentForces | Wind state advance | Yes |
| ParticleVfxSystem | ParticleRenderer | No-op (components) | No |
| WaterSystem | WaterRenderer, WaterFbo | No-op (render loop) | No |
| VegetationSystem | FoliageManager, FoliageRenderer, TreeRenderer | No-op (render loop) | No |
| TerrainSystem | Terrain, TerrainRenderer | No-op (render loop) | No |
| CharacterSystem | PhysicsCharacterController | No-op (main loop) | No |

### Facade Systems (3)
| System | Purpose | isForceActive |
|--------|---------|---------------|
| ClothSystem | Auto-activation for ClothComponent | No |
| DestructionSystem | Auto-activation for BreakableComponent, RigidBody | No |
| LightingSystem | Future lighting control API | Yes |

## Engine Changes

### Members Removed (moved to domain systems)
- `EnvironmentForces m_environmentForces` -> AtmosphereSystem
- `ParticleRenderer m_particleRenderer` -> ParticleVfxSystem
- `WaterRenderer m_waterRenderer`, `WaterFbo m_waterFbo` -> WaterSystem
- `FoliageManager m_foliageManager`, `FoliageRenderer m_foliageRenderer`, `TreeRenderer m_treeRenderer` -> VegetationSystem
- `Terrain m_terrain`, `TerrainRenderer m_terrainRenderer` -> TerrainSystem
- `PhysicsCharacterController m_physicsCharController` -> CharacterSystem

### Members Added
- Cached raw pointers for each moved subsystem
- `std::string m_assetPath` (stored from config for domain system initialization)

### Accessors Added
- `getAssetPath()`, `getWindow()`, `getCamera()`

### Main Loop Changes
- Environment update removed (handled by AtmosphereSystem::update via SystemRegistry)
- All member accesses changed from `.` to `->` (pointer dereference)
- Shutdown: direct subsystem shutdown calls removed (handled by SystemRegistry::shutdownAll)

## Key Design Decisions

1. **PhysicsWorld stays in Engine** -- shared infrastructure used by multiple systems
2. **Rendering stays in engine.cpp** -- too intertwined with the render pipeline to extract now
3. **Cached pointers** -- avoids type_index overhead in hot render loop
4. **Registration order = update order** -- Atmosphere first (drives wind), Lighting last
5. **Flags stay in Engine** -- `m_terrainEnabled`, `m_usePhysicsController` toggled by key handlers

## Test Results
- 38 new domain system tests (names, types, force-active, component ownership, polymorphism)
- 1434 existing tests pass (no regressions)
- Total: 1472 tests, 100% pass
