/// @file scene.h
/// @brief A complete 3D scene containing entities, lights, and a camera.
#pragma once

#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "scene/camera_component.h"
#include "scene/particle_emitter.h"
#include "scene/water_surface.h"
#include "animation/skeleton_animator.h"
#include "renderer/camera.h"
#include "renderer/light.h"

#include <glad/gl.h>

#include <memory>
#include <string>

#include <unordered_map>
#include <vector>

namespace Vestige
{

// Forward declarations for cloth rendering
class DynamicMesh;
class ClothComponent;

/// @brief Collected render data from a scene — used by the renderer.
struct SceneRenderData
{
    struct RenderItem
    {
        const Mesh* mesh;
        const Material* material;
        glm::mat4 worldMatrix;
        AABB worldBounds;
        uint32_t entityId = 0;
        bool castsShadow = true;
        bool isLocked = false;
        const std::vector<glm::mat4>* boneMatrices = nullptr;  ///< Skeletal animation (nullptr for static)
        const std::vector<float>* morphWeights = nullptr;      ///< Morph target weights (nullptr if no morphs)
        GLuint morphSSBO = 0;                                  ///< Morph target delta SSBO (0 if no morphs)
        int morphTargetCount = 0;                              ///< Number of active morph targets
        int morphVertexCount = 0;                              ///< Vertex count for SSBO indexing
    };

    std::vector<RenderItem> renderItems;
    std::vector<RenderItem> transparentItems;
    DirectionalLight directionalLight;
    bool hasDirectionalLight = false;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;

    /// @brief Particle emitters with their world matrices.
    std::vector<std::pair<const ParticleEmitterComponent*, glm::mat4>> particleEmitters;

    /// @brief Water surfaces with their world matrices.
    std::vector<std::pair<const WaterSurfaceComponent*, glm::mat4>> waterSurfaces;

    /// @brief Cloth items for dynamic mesh rendering.
    struct ClothRenderItem
    {
        const DynamicMesh* mesh;
        const Material* material;
        glm::mat4 worldMatrix;
        AABB worldBounds;
        uint32_t entityId = 0;
    };
    std::vector<ClothRenderItem> clothItems;
};

/// @brief A complete scene — owns a hierarchy of entities.
class Scene
{
public:
    /// @brief Creates a scene with the given name.
    explicit Scene(const std::string& name = "Untitled Scene");
    ~Scene();

    /// @brief Creates a new entity and adds it to the scene root.
    /// @param name Entity name.
    /// @return Pointer to the new entity (owned by the scene).
    Entity* createEntity(const std::string& name = "Entity");

    /// @brief Updates all entities in the scene.
    /// @param deltaTime Time elapsed since last frame.
    void update(float deltaTime);

    /// @brief Collects all renderable data for the renderer.
    /// @return Render data containing meshes, materials, transforms, and lights.
    SceneRenderData collectRenderData() const;

    /// @brief Fills existing render data (clears and reuses allocated memory).
    /// @param data Output render data — vectors are cleared but capacity is preserved.
    void collectRenderData(SceneRenderData& data) const;

    /// @brief Collects all world-space AABBs for collision detection.
    /// @return List of world-space bounding boxes.
    std::vector<AABB> collectColliders() const;

    /// @brief Fills existing collider list (clears and reuses allocated memory).
    /// @param colliders Output vector — cleared but capacity is preserved.
    void collectColliders(std::vector<AABB>& colliders) const;

    /// @brief Finds an entity by name (searches entire hierarchy).
    Entity* findEntity(const std::string& name);

    /// @brief Finds an entity by unique ID (searches entire hierarchy).
    /// @return Pointer to the entity, or nullptr if not found.
    Entity* findEntityById(uint32_t id);

    /// @brief Gets the root entity.
    Entity* getRoot();

    /// @brief Gets the scene name.
    const std::string& getName() const;

    /// @brief Sets the scene name.
    void setName(const std::string& name);

    /// @brief Sets the active camera component for rendering.
    /// @param camera Pointer to a CameraComponent in this scene (nullptr to clear).
    void setActiveCamera(CameraComponent* camera);

    /// @brief Gets the active camera component (nullptr if none set).
    CameraComponent* getActiveCamera() const;

    /// @brief Removes all entities from the scene (keeps the root node).
    void clearEntities();

    /// @brief Removes an entity by ID (and all its descendants).
    /// @return True if the entity was found and removed.
    bool removeEntity(uint32_t id);

    /// @brief Duplicates an entity and all its descendants.
    /// The clone is placed as a sibling (same parent) with new IDs.
    /// @param entityId ID of the entity to duplicate.
    /// @return Pointer to the clone, or nullptr if the entity was not found.
    Entity* duplicateEntity(uint32_t entityId);

    /// @brief Moves an entity to a new parent.
    /// @param entityId Entity to move.
    /// @param newParentId New parent (0 = scene root).
    /// @return True if reparenting succeeded.
    bool reparentEntity(uint32_t entityId, uint32_t newParentId);

    /// @brief Rebuilds the entity ID lookup index from the current hierarchy.
    /// Call after bulk operations (e.g., deserialization) that bypass Scene methods.
    void rebuildEntityIndex();

    /// @brief Registers an entity (and all descendants) in the lookup index.
    void registerEntityRecursive(Entity* entity);

    /// @brief Unregisters an entity (and all descendants) from the lookup index.
    void unregisterEntityRecursive(Entity* entity);

private:
    void collectRenderDataRecursive(const Entity& entity, SceneRenderData& data) const;
    void collectCollidersRecursive(const Entity& entity, std::vector<AABB>& colliders) const;

    std::string m_name;
    std::unique_ptr<Entity> m_root;
    std::unordered_map<uint32_t, Entity*> m_entityIndex;
    CameraComponent* m_activeCamera = nullptr;
};

} // namespace Vestige
