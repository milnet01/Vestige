// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file entity_factory.h
/// @brief Static helper functions for spawning entities from the editor Create menu.
#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Vestige
{

class Entity;
class Scene;
class ResourceManager;

/// @brief Static factory for creating entities from the editor Create menu.
/// Each method creates an entity, positions it, attaches relevant components,
/// and returns a raw pointer (owned by the Scene). Designed so each function
/// can later be wrapped in an UndoableCommand (Phase 5D).
class EntityFactory
{
public:
    // --- Empty ---

    /// @brief Creates an empty entity (useful as a group node).
    static Entity* createEmptyEntity(Scene& scene, const glm::vec3& position);

    // --- Primitives ---

    /// @brief Creates a 1m cube with default PBR material.
    static Entity* createCube(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a 1m diameter sphere with default PBR material.
    static Entity* createSphere(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a 2m x 2m plane with default PBR material.
    static Entity* createPlane(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a 1m capped cylinder with default PBR material.
    static Entity* createCylinder(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a 1m capped cone with default PBR material.
    static Entity* createCone(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a 1m wedge (ramp) with default PBR material.
    static Entity* createWedge(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    // --- Lights ---

    /// @brief Creates an entity with a DirectionalLightComponent.
    static Entity* createDirectionalLight(Scene& scene, const glm::vec3& position);

    /// @brief Creates an entity with a PointLightComponent (warm white).
    static Entity* createPointLight(Scene& scene, const glm::vec3& position);

    /// @brief Creates an entity with a SpotLightComponent (pointing down).
    static Entity* createSpotLight(Scene& scene, const glm::vec3& position);

    // --- Effects ---

    /// @brief Creates an entity with a ParticleEmitterComponent (default upward fountain).
    static Entity* createParticleEmitter(Scene& scene, const glm::vec3& position);

    /// @brief Creates a particle emitter from a named preset.
    /// @param presetName One of: "torch", "candle", "campfire", "smoke", "dust", "incense", "sparks"
    static Entity* createParticlePreset(Scene& scene, const glm::vec3& position,
                                        const std::string& presetName);

    /// @brief Creates an entity with a WaterSurfaceComponent (default pool preset).
    static Entity* createWaterSurface(Scene& scene, const glm::vec3& position);

    // --- Architectural ---

    /// @brief Creates a wall entity (3m wide, 3m high, 0.3m thick) with PBR material.
    static Entity* createWall(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a wall with a door opening.
    static Entity* createWallWithDoor(Scene& scene, ResourceManager& resources,
                                       const glm::vec3& position);

    /// @brief Creates a wall with a window opening.
    static Entity* createWallWithWindow(Scene& scene, ResourceManager& resources,
                                         const glm::vec3& position);

    /// @brief Creates a 4-wall room with floor (4m x 4m x 3m) grouped under a parent.
    static Entity* createRoom(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a gable roof (4m x 4m, 1.5m peak).
    static Entity* createRoof(Scene& scene, ResourceManager& resources, const glm::vec3& position);

    /// @brief Creates a straight staircase (3m height, 10 steps, 1m wide).
    static Entity* createStairs(Scene& scene, ResourceManager& resources,
                                 const glm::vec3& position);

    /// @brief Creates a spiral staircase (3m height, 360 degrees).
    static Entity* createSpiralStairs(Scene& scene, ResourceManager& resources,
                                       const glm::vec3& position);

    /// @brief Creates a floor slab (4m x 4m, 0.15m thick).
    static Entity* createFloorSlab(Scene& scene, ResourceManager& resources,
                                    const glm::vec3& position);
};

} // namespace Vestige
