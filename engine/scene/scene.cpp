/// @file scene.cpp
/// @brief Scene implementation.
#include "scene/scene.h"
#include "core/logger.h"

namespace Vestige
{

Scene::Scene(const std::string& name)
    : m_name(name)
    , m_root(std::make_unique<Entity>("Root"))
{
}

Scene::~Scene() = default;

Entity* Scene::createEntity(const std::string& name)
{
    auto entity = std::make_unique<Entity>(name);
    return m_root->addChild(std::move(entity));
}

void Scene::update(float deltaTime)
{
    m_root->update(deltaTime);
}

SceneRenderData Scene::collectRenderData() const
{
    SceneRenderData data;
    collectRenderDataRecursive(*m_root, data);
    return data;
}

void Scene::collectRenderData(SceneRenderData& data) const
{
    data.renderItems.clear();
    data.transparentItems.clear();
    data.pointLights.clear();
    data.spotLights.clear();
    data.hasDirectionalLight = false;
    collectRenderDataRecursive(*m_root, data);
}

std::vector<AABB> Scene::collectColliders() const
{
    std::vector<AABB> colliders;
    collectCollidersRecursive(*m_root, colliders);
    return colliders;
}

void Scene::collectColliders(std::vector<AABB>& colliders) const
{
    colliders.clear();
    collectCollidersRecursive(*m_root, colliders);
}

Entity* Scene::findEntity(const std::string& name)
{
    if (m_root->getName() == name)
    {
        return m_root.get();
    }
    return m_root->findDescendant(name);
}

Entity* Scene::findEntityById(uint32_t id)
{
    return findEntityByIdRecursive(*m_root, id);
}

Entity* Scene::getRoot()
{
    return m_root.get();
}

const std::string& Scene::getName() const
{
    return m_name;
}

void Scene::setName(const std::string& name)
{
    m_name = name;
}

void Scene::clearEntities()
{
    m_root = std::make_unique<Entity>("Root");
}

bool Scene::removeEntity(uint32_t id)
{
    Entity* entity = findEntityById(id);
    if (!entity || entity == m_root.get())
    {
        return false;
    }

    Entity* parent = entity->getParent();
    if (!parent)
    {
        return false;
    }

    parent->removeChild(entity);
    return true;
}

Entity* Scene::duplicateEntity(uint32_t entityId)
{
    Entity* original = findEntityById(entityId);
    if (!original || original == m_root.get())
    {
        return nullptr;
    }

    auto clone = original->clone();

    // Place as sibling (same parent as original)
    Entity* parent = original->getParent();
    if (!parent)
    {
        parent = m_root.get();
    }

    return parent->addChild(std::move(clone));
}

bool Scene::reparentEntity(uint32_t entityId, uint32_t newParentId)
{
    Entity* entity = findEntityById(entityId);
    if (!entity || entity == m_root.get())
    {
        return false;
    }

    // newParentId 0 means reparent to root
    Entity* newParent = (newParentId == 0) ? m_root.get() : findEntityById(newParentId);
    if (!newParent)
    {
        return false;
    }

    if (entity == newParent)
    {
        return false;
    }

    Entity* oldParent = entity->getParent();
    if (oldParent == newParent)
    {
        return false;
    }

    // Check for cycle: newParent must not be a descendant of entity
    Entity* check = newParent;
    while (check)
    {
        if (check == entity)
        {
            return false;
        }
        check = check->getParent();
    }

    auto detached = oldParent->removeChild(entity);
    if (!detached)
    {
        return false;
    }

    newParent->addChild(std::move(detached));
    return true;
}

void Scene::collectRenderDataRecursive(const Entity& entity, SceneRenderData& data) const
{
    if (!entity.isActive())
    {
        return;
    }

    // Invisible entities (and their children) are skipped for rendering
    if (!entity.isVisible())
    {
        return;
    }

    // Check for MeshRenderer
    auto* meshRenderer = entity.getComponent<MeshRenderer>();
    if (meshRenderer && meshRenderer->isEnabled() && meshRenderer->getMesh() && meshRenderer->getMaterial())
    {
        SceneRenderData::RenderItem item;
        item.mesh = meshRenderer->getMesh().get();
        item.material = meshRenderer->getMaterial().get();
        item.worldMatrix = entity.getWorldMatrix();
        item.worldBounds = meshRenderer->getCullingBounds().transformed(item.worldMatrix);
        item.entityId = entity.getId();
        item.castsShadow = meshRenderer->castsShadow();
        item.isLocked = entity.isLocked();

        // BLEND materials go to the transparent list; OPAQUE and MASK go to opaque
        if (item.material->getAlphaMode() == AlphaMode::BLEND)
        {
            data.transparentItems.push_back(item);
        }
        else
        {
            data.renderItems.push_back(item);
        }
    }

    // Check for light components
    auto* dirLight = entity.getComponent<DirectionalLightComponent>();
    if (dirLight && dirLight->isEnabled())
    {
        data.directionalLight = dirLight->light;
        data.hasDirectionalLight = true;
    }

    auto* pointLight = entity.getComponent<PointLightComponent>();
    if (pointLight && pointLight->isEnabled())
    {
        PointLight pl = pointLight->light;
        pl.position = entity.getWorldPosition();
        data.pointLights.push_back(pl);
    }

    auto* spotLight = entity.getComponent<SpotLightComponent>();
    if (spotLight && spotLight->isEnabled())
    {
        SpotLight sl = spotLight->light;
        sl.position = entity.getWorldPosition();
        data.spotLights.push_back(sl);
    }

    // Emissive light auto-generation: create a synthetic point light from emissive material
    auto* emissiveLight = entity.getComponent<EmissiveLightComponent>();
    if (emissiveLight && emissiveLight->isEnabled() && meshRenderer)
    {
        auto mat = meshRenderer->getMaterial();
        if (mat)
        {
            glm::vec3 emColor = emissiveLight->overrideColor;
            if (emColor == glm::vec3(0.0f))
            {
                emColor = mat->getEmissive() * mat->getEmissiveStrength();
            }

            if (emColor != glm::vec3(0.0f) && static_cast<int>(data.pointLights.size()) < MAX_POINT_LIGHTS)
            {
                PointLight pl;
                pl.position = entity.getWorldPosition();
                pl.ambient = emColor * 0.05f * emissiveLight->lightIntensity;
                pl.diffuse = emColor * emissiveLight->lightIntensity;
                pl.specular = emColor * emissiveLight->lightIntensity;

                // Attenuation based on radius
                float r = emissiveLight->lightRadius;
                pl.constant = 1.0f;
                pl.linear = 2.0f / r;
                pl.quadratic = 1.0f / (r * r);
                pl.castsShadow = false;

                data.pointLights.push_back(pl);
            }
        }
    }

    // Recurse into children
    for (const auto& child : entity.getChildren())
    {
        collectRenderDataRecursive(*child, data);
    }
}

Entity* Scene::findEntityByIdRecursive(Entity& entity, uint32_t id)
{
    if (entity.getId() == id)
    {
        return &entity;
    }
    for (auto& child : entity.getChildren())
    {
        Entity* found = findEntityByIdRecursive(*child, id);
        if (found)
        {
            return found;
        }
    }
    return nullptr;
}

void Scene::collectCollidersRecursive(const Entity& entity, std::vector<AABB>& colliders) const
{
    if (!entity.isActive())
    {
        return;
    }

    auto* meshRenderer = entity.getComponent<MeshRenderer>();
    if (meshRenderer && meshRenderer->isEnabled())
    {
        AABB bounds = meshRenderer->getBounds();
        // Only add if bounds are non-zero (has collision)
        if (bounds.getSize() != glm::vec3(0.0f))
        {
            colliders.push_back(bounds.transformed(entity.getWorldMatrix()));
        }
    }

    for (const auto& child : entity.getChildren())
    {
        collectCollidersRecursive(*child, colliders);
    }
}

} // namespace Vestige
