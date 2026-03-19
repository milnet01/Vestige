/// @file scene.h
/// @brief A complete 3D scene containing entities, lights, and a camera.
#pragma once

#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "renderer/camera.h"
#include "renderer/light.h"

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

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
    };

    std::vector<RenderItem> renderItems;
    std::vector<RenderItem> transparentItems;
    DirectionalLight directionalLight;
    bool hasDirectionalLight = false;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;
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

    /// @brief Removes an entity by ID (and all its descendants).
    /// @return True if the entity was found and removed.
    bool removeEntity(uint32_t id);

    /// @brief Moves an entity to a new parent.
    /// @param entityId Entity to move.
    /// @param newParentId New parent (0 = scene root).
    /// @return True if reparenting succeeded.
    bool reparentEntity(uint32_t entityId, uint32_t newParentId);

private:
    void collectRenderDataRecursive(const Entity& entity, SceneRenderData& data) const;
    void collectCollidersRecursive(const Entity& entity, std::vector<AABB>& colliders) const;
    Entity* findEntityByIdRecursive(Entity& entity, uint32_t id);

    std::string m_name;
    std::unique_ptr<Entity> m_root;
};

} // namespace Vestige
