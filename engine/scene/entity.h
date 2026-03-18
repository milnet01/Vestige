/// @file entity.h
/// @brief Entity class — a named object in the scene with components and transform.
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Transform data — position, rotation, scale with parent-child hierarchy.
struct Transform
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);  // Euler angles in degrees
    glm::vec3 scale    = glm::vec3(1.0f);

    /// @brief Computes the local model matrix from position, rotation, and scale.
    glm::mat4 getLocalMatrix() const
    {
        if (m_hasMatrixOverride)
        {
            return m_matrixOverride;
        }

        glm::mat4 mat = glm::translate(glm::mat4(1.0f), position);
        mat = glm::rotate(mat, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        mat = glm::rotate(mat, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        mat = glm::rotate(mat, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        mat = glm::scale(mat, scale);
        return mat;
    }

    /// @brief Sets a direct matrix override (bypasses TRS computation).
    /// @param matrix The local transform matrix to use directly.
    void setLocalMatrix(const glm::mat4& matrix)
    {
        m_matrixOverride = matrix;
        m_hasMatrixOverride = true;
    }

    /// @brief Checks if a matrix override is active.
    bool hasMatrixOverride() const
    {
        return m_hasMatrixOverride;
    }

    /// @brief Clears the matrix override, reverting to TRS computation.
    void clearMatrixOverride()
    {
        m_matrixOverride = glm::mat4(1.0f);
        m_hasMatrixOverride = false;
    }

private:
    glm::mat4 m_matrixOverride = glm::mat4(1.0f);
    bool m_hasMatrixOverride = false;
};

/// @brief An object in the scene graph. Has a Transform, optional Components, and children.
class Entity
{
public:
    /// @brief Creates an entity with the given name.
    /// @param name Human-readable name for identification.
    explicit Entity(const std::string& name = "Entity");
    ~Entity();

    // Non-copyable
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    /// @brief Updates this entity and all its children.
    /// @param deltaTime Time elapsed since last frame.
    /// @param parentWorldMatrix The parent's world transform (identity for root).
    void update(float deltaTime, const glm::mat4& parentWorldMatrix = glm::mat4(1.0f));

    // --- Component management ---

    /// @brief Adds a component of the given type. The entity owns it.
    /// @tparam T Component type (must derive from Component).
    /// @tparam Args Constructor argument types.
    /// @param args Arguments forwarded to T's constructor.
    /// @return Pointer to the newly added component.
    template <typename T, typename... Args>
    T* addComponent(Args&&... args)
    {
        auto id = ComponentTypeId::get<T>();
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        component->setOwner(this);
        m_components[id] = std::move(component);
        return ptr;
    }

    /// @brief Gets a component of the given type.
    /// @tparam T Component type.
    /// @return Pointer to the component, or nullptr if not present.
    template <typename T>
    T* getComponent()
    {
        auto id = ComponentTypeId::get<T>();
        auto it = m_components.find(id);
        if (it != m_components.end())
        {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    /// @brief Gets a component of the given type (const version).
    template <typename T>
    const T* getComponent() const
    {
        auto id = ComponentTypeId::get<T>();
        auto it = m_components.find(id);
        if (it != m_components.end())
        {
            return static_cast<const T*>(it->second.get());
        }
        return nullptr;
    }

    /// @brief Checks if the entity has a component of the given type.
    template <typename T>
    bool hasComponent() const
    {
        auto id = ComponentTypeId::get<T>();
        return m_components.find(id) != m_components.end();
    }

    /// @brief Removes a component of the given type.
    template <typename T>
    void removeComponent()
    {
        auto id = ComponentTypeId::get<T>();
        m_components.erase(id);
    }

    // --- Hierarchy ---

    /// @brief Adds a child entity. This entity takes ownership.
    /// @param child The child entity to add.
    /// @return Pointer to the added child.
    Entity* addChild(std::unique_ptr<Entity> child);

    /// @brief Gets all children.
    const std::vector<std::unique_ptr<Entity>>& getChildren() const;

    /// @brief Gets the parent entity (nullptr for root).
    Entity* getParent() const;

    /// @brief Finds a child entity by name (non-recursive).
    Entity* findChild(const std::string& name);

    /// @brief Finds a descendant entity by name (recursive).
    Entity* findDescendant(const std::string& name);

    // --- Properties ---

    /// @brief The entity's transform (position, rotation, scale).
    Transform transform;

    /// @brief Gets the computed world matrix (includes parent transforms).
    glm::mat4 getWorldMatrix() const;

    /// @brief Gets the world position (extracted from world matrix).
    glm::vec3 getWorldPosition() const;

    /// @brief Gets the entity name.
    const std::string& getName() const;

    /// @brief Sets whether this entity is active.
    void setActive(bool isActive);

    /// @brief Checks if this entity is active.
    bool isActive() const;

private:
    std::string m_name;
    Entity* m_parent;
    std::vector<std::unique_ptr<Entity>> m_children;
    std::unordered_map<uint32_t, std::unique_ptr<Component>> m_components;
    glm::mat4 m_worldMatrix;
    bool m_isActive;
};

} // namespace Vestige
