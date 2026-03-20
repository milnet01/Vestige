/// @file entity_factory.h
/// @brief Static helper functions for spawning entities from the editor Create menu.
#pragma once

#include <glm/glm.hpp>

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
};

} // namespace Vestige
