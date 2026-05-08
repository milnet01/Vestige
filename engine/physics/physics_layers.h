// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_layers.h
/// @brief Jolt broadphase and object layer definitions with collision filtering.
#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Object layers — fine-grained type classification for collision filtering
// ---------------------------------------------------------------------------
namespace ObjectLayers
{
    static constexpr JPH::ObjectLayer STATIC           = 0;
    static constexpr JPH::ObjectLayer DYNAMIC          = 1;
    static constexpr JPH::ObjectLayer PLAYER_CHARACTER = 2;
    static constexpr JPH::ObjectLayer NPC_CHARACTER    = 3;
    static constexpr JPH::ObjectLayer TRIGGER          = 4;
    static constexpr JPH::ObjectLayer NUM_LAYERS       = 5;

    /// @brief Phase 10.9 Slice 7 Ph5: pre-Ph5 there was a single
    ///        `CHARACTER` layer; "character vs character — never" in
    ///        the pair filter meant that a player character and any
    ///        NPC character / NPC-driven ragdoll body assigned to the
    ///        same layer would never collide. Splitting into separate
    ///        PLAYER and NPC layers lets player-vs-NPC collide while
    ///        same-type pairs (player-vs-player is meaningless;
    ///        NPC-vs-NPC is a deliberate gameplay choice — ragdoll
    ///        crowds shouldn't push each other) stay non-colliding.
    ///
    ///        Source-compat alias retained for callers that want a
    ///        "the player character" default; new NPC code uses
    ///        `NPC_CHARACTER` directly.
    static constexpr JPH::ObjectLayer CHARACTER        = PLAYER_CHARACTER;
}

// ---------------------------------------------------------------------------
// Broadphase layers — coarse grouping for broadphase acceleration
// ---------------------------------------------------------------------------
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer STATIC(0);
    static constexpr JPH::BroadPhaseLayer DYNAMIC(1);
    static constexpr JPH::BroadPhaseLayer CHARACTER(2);  ///< Both player + NPC characters live here.
    static constexpr unsigned int NUM_LAYERS = 3;
}

/// @brief Maps object layers to broadphase layers.
class BroadPhaseLayerMapping final : public JPH::BroadPhaseLayerInterface
{
public:
    BroadPhaseLayerMapping()
    {
        m_map[ObjectLayers::STATIC]           = BroadPhaseLayers::STATIC;
        m_map[ObjectLayers::DYNAMIC]          = BroadPhaseLayers::DYNAMIC;
        m_map[ObjectLayers::PLAYER_CHARACTER] = BroadPhaseLayers::CHARACTER;
        m_map[ObjectLayers::NPC_CHARACTER]    = BroadPhaseLayers::CHARACTER;
        m_map[ObjectLayers::TRIGGER]          = BroadPhaseLayers::DYNAMIC;
    }

    unsigned int GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return m_map[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        switch (static_cast<uint8_t>(layer))
        {
        case 0: return "STATIC";
        case 1: return "DYNAMIC";
        case 2: return "CHARACTER";
        default: return "UNKNOWN";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_map[ObjectLayers::NUM_LAYERS];
};

/// @brief Determines which object layer pairs can collide.
class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        // Static vs static — never
        if (a == ObjectLayers::STATIC && b == ObjectLayers::STATIC)
        {
            return false;
        }

        // Trigger vs static — never
        if ((a == ObjectLayers::TRIGGER && b == ObjectLayers::STATIC) ||
            (a == ObjectLayers::STATIC && b == ObjectLayers::TRIGGER))
        {
            return false;
        }

        // Phase 10.9 Slice 7 Ph5: same-type character pairs never collide.
        // PLAYER vs PLAYER is meaningless (only one player); NPC vs NPC is
        // a deliberate gameplay choice (ragdolls in a crowd shouldn't
        // push each other). PLAYER vs NPC *does* collide so the player
        // can shoulder-bump or knock down NPC characters.
        if (a == ObjectLayers::PLAYER_CHARACTER && b == ObjectLayers::PLAYER_CHARACTER)
        {
            return false;
        }
        if (a == ObjectLayers::NPC_CHARACTER && b == ObjectLayers::NPC_CHARACTER)
        {
            return false;
        }

        // Trigger vs trigger — never
        if (a == ObjectLayers::TRIGGER && b == ObjectLayers::TRIGGER)
        {
            return false;
        }

        // Everything else collides
        return true;
    }
};

/// @brief Determines which broadphase layer pairs can collide.
class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer objLayer,
                       JPH::BroadPhaseLayer bpLayer) const override
    {
        // Static objects don't collide with static broadphase
        if (objLayer == ObjectLayers::STATIC)
        {
            return bpLayer != BroadPhaseLayers::STATIC;
        }

        // Everything else can collide with anything
        return true;
    }
};

} // namespace Vestige
