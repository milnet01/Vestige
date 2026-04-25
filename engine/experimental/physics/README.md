# `engine/experimental/physics/` — relocated zombie cluster

This directory holds physics subsystems that **compile and pass their unit tests but have no production caller** as of Phase 10.9 (T0 audit, Slice 0). They were relocated here from `engine/physics/` by Phase 10.9 Slice 8 W13 to make the "no caller" status visible at the path level rather than buried in audit notes.

## What's here

| Subsystem | Files | Tests | Original Phase |
|---|---|---|---|
| Ragdoll | `ragdoll.{h,cpp}`, `ragdoll_preset.{h,cpp}` | (none — covered tangentially by `test_advanced_physics.cpp`) | Phase 8 |
| Fracture / breakable | `fracture.{h,cpp}`, `breakable_component.{h,cpp}` | `test_advanced_physics.cpp`, `test_entity_serializer_registry.cpp` | Phase 8 |
| Dismemberment | `dismemberment.{h,cpp}`, `dismemberment_zones.{h,cpp}` | `test_advanced_physics.cpp` | Phase 8 |
| Grab system | `grab_system.{h,cpp}` | `test_advanced_physics.cpp` | Phase 8 |
| Stasis system | `stasis_system.{h,cpp}` | `test_stasis_system.cpp` | Phase 8 |

## What stays live in `engine/physics/`

These are consumed by production code (the Engine main loop, character controller, cloth simulation, scene serialiser):

- `physics_world` — Jolt PhysicsWorld wrapper.
- `rigid_body` — RigidBodyComponent.
- `physics_character_controller` — Jolt CharacterVirtual wrapper.
- `physics_constraint`, `physics_debug` — joint + viz APIs.
- `cloth_simulator`, `cloth_component`, `cloth_presets`, `cloth_mesh_collider`, `cloth_backend_factory`, `cloth_constraint_graph`, `gpu_cloth_simulator`, `fabric_material`, `spatial_hash`, `bvh`, `collider_generator`.
- `deformable_mesh` — used by the relocated `breakable_component` but lives separately because it's the geometry container, not the breakable behaviour.

## DestructionSystem (the empty pump)

`engine/systems/destruction_system.cpp` was previously the cluster's pump but had an empty `update()` body. After W13 it's a registered ISystem stub:

- Still constructed and registered by `Engine::initialize` (so `test_domain_systems` invariants hold).
- `getOwnedComponentTypes()` returns an empty vector — the system can't reference `BreakableComponent` (which moved to `experimental/physics/`) without violating the production-to-experimental dependency rule.
- `update()` is a no-op — same as it was before W13; the relocation just makes the empty-pump status explicit.

To restore destruction as a live subsystem: bring the cluster files back from `experimental/physics/` to `physics/`, restore the `BreakableComponent` registration, write a real `update()` that drives fracture detection + ragdoll spawn.

## Why "experimental" instead of deleted

- ~3,000 LoC of working features with passing unit tests; deleting throws that away.
- Wiring requires a destruction / ragdoll / grab gameplay push (Phase 11B territory).
- Open-source target: future contributors may want to activate any of these without re-deriving the math.
- T0 audit confirmed the zero-caller status — this isn't speculative.

## How to activate

To bring a subsystem back to production:

1. Build a real caller: a game-side death-trigger that spawns a Ragdoll, an interaction system that uses GrabSystem, an attack system that calls Dismemberment / Fracture, etc.
2. Move the consumed files back from `engine/experimental/physics/` to `engine/physics/`.
3. Update `engine/CMakeLists.txt` to remove the `experimental/physics/` prefix.
4. Update `#include` paths in the cluster's own files + their tests + DestructionSystem.
5. Re-register `BreakableComponent` (or relevant types) in `DestructionSystem::getOwnedComponentTypes()`.
6. Write at least one integration test that exercises the production call path, not just the math.

## Constraints

- **Don't add new code here unless it's deliberately a scratch / not-yet-wired prototype.** This directory is for relocated zombies, not a permanent home for incomplete work.
- **Production code must not `#include "experimental/physics/..."`.** That re-establishes the zombie's status as live, which defeats the relocation. The `DestructionSystem` no-op stub at `engine/systems/destruction_system.cpp` is the canonical example: it was rewritten during W13 to drop its experimental/ include rather than carrying it forward.
- **Tests in `tests/test_*.cpp` may include from here** — the cluster's existing tests do, and they remain in the test suite as regression pins for the math.

## Audit reference

- ROADMAP Phase 10.9 Slice 0 T0: zombie audit confirming no production caller.
- ROADMAP Phase 10.9 Slice 8 W13: this relocation.
- ROADMAP Phase 8: original "shipped" state of the subsystems (with the post-T0 caveat block noting the zombie status).
