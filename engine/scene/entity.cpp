/// @file entity.cpp
/// @brief Entity implementation.
#include "scene/entity.h"

namespace Vestige
{

Entity::Entity(const std::string& name)
    : m_name(name)
    , m_parent(nullptr)
    , m_worldMatrix(1.0f)
    , m_isActive(true)
{
}

Entity::~Entity() = default;

void Entity::update(float deltaTime, const glm::mat4& parentWorldMatrix)
{
    if (!m_isActive)
    {
        return;
    }

    // Compute world matrix from parent and local transform
    m_worldMatrix = parentWorldMatrix * transform.getLocalMatrix();

    // Update all components
    for (auto& [id, component] : m_components)
    {
        if (component->isEnabled())
        {
            component->update(deltaTime);
        }
    }

    // Update children
    for (auto& child : m_children)
    {
        child->update(deltaTime, m_worldMatrix);
    }
}

Entity* Entity::addChild(std::unique_ptr<Entity> child)
{
    child->m_parent = this;
    Entity* ptr = child.get();
    m_children.push_back(std::move(child));
    return ptr;
}

const std::vector<std::unique_ptr<Entity>>& Entity::getChildren() const
{
    return m_children;
}

Entity* Entity::getParent() const
{
    return m_parent;
}

Entity* Entity::findChild(const std::string& name)
{
    for (auto& child : m_children)
    {
        if (child->m_name == name)
        {
            return child.get();
        }
    }
    return nullptr;
}

Entity* Entity::findDescendant(const std::string& name)
{
    for (auto& child : m_children)
    {
        if (child->m_name == name)
        {
            return child.get();
        }
        Entity* found = child->findDescendant(name);
        if (found)
        {
            return found;
        }
    }
    return nullptr;
}

glm::mat4 Entity::getWorldMatrix() const
{
    return m_worldMatrix;
}

glm::vec3 Entity::getWorldPosition() const
{
    return glm::vec3(m_worldMatrix[3]);
}

const std::string& Entity::getName() const
{
    return m_name;
}

void Entity::setActive(bool isActive)
{
    m_isActive = isActive;
}

bool Entity::isActive() const
{
    return m_isActive;
}

} // namespace Vestige
