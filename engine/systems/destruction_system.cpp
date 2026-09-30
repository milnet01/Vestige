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
/// After W13 the system is kept registered but does nothing.
///
/// To activate destruction in a future phase: bring the
/// breakable / fracture / ragdoll work back from
/// `engine/experimental/physics/` to `engine/physics/` and write a
/// real `update()` that pumps fracture detection + ragdoll spawn.
#include "systems/destruction_system.h"
#include "core/engine.h"
#include "core/logger.h"

namespace Vestige
{

bool DestructionSystem::initialize(Engine& /*engine*/)
{
    // Physics managed by PhysicsWorld (shared infrastructure in Engine).
    // This system has no per-frame work after W13 — it exists only
    // because Engine still constructs it.
    Logger::info("[DestructionSystem] Initialized (W13 stub)");
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

} // namespace Vestige
