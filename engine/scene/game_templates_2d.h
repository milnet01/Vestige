// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file game_templates_2d.h
/// @brief 2D game-type templates — Side-Scroller and Shmup (Phase 9F-5).
///
/// Each template populates a Scene with the minimal set of entities
/// wired for that genre: player with physics + character controller,
/// world geometry, a 2D camera configured to follow, and optional
/// genre-specific helpers (e.g. a bullet spawner for the shmup).
///
/// The templates do NOT load real art assets — they attach sprite /
/// tilemap components with atlases passed in by the caller (or
/// unassigned, for designers to fill in). Art binding happens in the
/// editor's 2D panel (Phase 9F-6).
#pragma once

#include <memory>
#include <string>

namespace Vestige
{

class Entity;
class Scene;
class SpriteAtlas;

/// @brief Configuration knobs shared by all 2D templates. Defaults are
/// designer-friendly — platformer with 32 px tiles, pixels-per-unit 100
/// (Unity convention), gravity scale 1.
struct GameTemplate2DConfig
{
    /// @brief Optional sprite atlas the template binds to sprites /
    /// tilemaps. Templates still instantiate all entities when this is
    /// null; the SpriteComponents simply have no atlas yet.
    std::shared_ptr<SpriteAtlas> atlas;

    /// @brief Optional frame name for the player sprite. Ignored if
    /// `atlas` is null.
    std::string playerFrameName = "player";

    /// @brief Optional frame name for a generic ground / platform tile.
    std::string platformFrameName = "platform";

    /// @brief Optional frame name for a shmup bullet.
    std::string bulletFrameName = "bullet";

    /// @brief Pixels per world unit for spawned sprites.
    float pixelsPerUnit = 100.0f;
};

/// @brief Populates @p scene with a 2D side-scroller starter:
///
///   "SideScrollerWorld"  (root)
///     ├── "Player"       (sprite + RB2D dynamic + Collider2D capsule
///     │                   + CharacterController2D)
///     ├── "Ground"       (sprite-less tilemap + RB2D static +
///     │                   Collider2D edge-chain, 40×1 floor)
///     ├── "Platform_A"   (RB2D static + Collider2D box, 3×0.5 unit)
///     ├── "Platform_B"   (as above)
///     └── "Camera"       (Camera2D — follows Player, deadzone, bounds).
///
/// Returns the root entity (owned by the scene). Fails soft: nullptr if
/// scene is null.
Entity* createSideScrollerTemplate(Scene& scene, const GameTemplate2DConfig& config = {});

/// @brief Populates @p scene with a 2D vertical-shmup starter:
///
///   "ShmupWorld"        (root)
///     ├── "Player"      (sprite + RB2D kinematic + Collider2D box;
///     │                  no gravity, no character controller — player
///     │                  moves with direct velocity from input)
///     ├── "ScrollingBackdrop" (tilemap — star field, scrolls downward)
///     └── "Camera"      (Camera2D — fixed, no follow, world-bounded).
///
/// The bullet entity is *not* created — shmup logic spawns bullets at
/// runtime via a scripted node (Phase 9E's SpawnEntity node), seeded
/// from the Player entity's `playerFrameName`.
///
/// Returns the root entity. Fails soft.
Entity* createShmupTemplate(Scene& scene, const GameTemplate2DConfig& config = {});

} // namespace Vestige
