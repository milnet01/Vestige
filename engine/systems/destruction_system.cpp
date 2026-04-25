// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file destruction_system.cpp
/// @brief DestructionSystem implementation — no-op stub after Phase 10.9 W13.
///
/// Phase 10.9 Slice 8 W13 relocated the destruction / ragdoll /
/// fracture / dismemberment / grab / stasis cluster to
/// `engine/experimental/physics/` because the entire cluster had
/// no production caller (T0 audit, Phase 10.9 Slice 0). This system
/// previously registered `BreakableComponent` as an owned type but
/// its `update()` body was already empty (the Jolt PhysicsWorld
/// handles rigid-body dynamics in Engine's main loop, not here).
///
/// After W13 the system is kept registered (so the ISystem
/// `name` / `forceActive` invariants tested by `test_domain_systems`
/// still hold) but `getOwnedComponentTypes()` returns an empty
/// vector — the ComponentTypeId::get<BreakableComponent>() call
/// would have required an `#include` from `experimental/`, which
/// would re-establish the production-to-experimental dependency
/// W13 just removed.
///
/// To activate destruction in a future phase: bring the
/// breakable / fracture / ragdoll work back from
/// `engine/experimental/physics/` to `engine/physics/`, restore
/// the `BreakableComponent` registration here, and write a real
/// `update()` that pumps fracture detection + ragdoll spawn.
#include "systems/destruction_system.h"
#include "core/engine.h"
#include "core/logger.h"

namespace Vestige
{

bool DestructionSystem::initialize(Engine& /*engine*/)
{
    // Physics managed by PhysicsWorld (shared infrastructure in Engine).
    // This system has no per-frame work after W13 — it exists only
    // because Engine still constructs it and `test_domain_systems`
    // pins its name / forceActive invariants.
    Logger::info("[DestructionSystem] Initialized (W13 stub — no owned components)");
    return true;
}

void DestructionSystem::shutdown()
{
    Logger::info("[DestructionSystem] Shut down");
}

void DestructionSystem::update(float /*deltaTime*/)
{
    // No-op — see file-header comment.
}

std::vector<uint32_t> DestructionSystem::getOwnedComponentTypes() const
{
    // Empty after W13. The previously-registered BreakableComponent
    // type lives in `engine/experimental/physics/`; production code
    // (this file) must not include from there.
    return {};
}

} // namespace Vestige
