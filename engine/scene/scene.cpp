/// @file scene.cpp
/// @brief Scene implementation.
#include "scene/scene.h"
#include "physics/cloth_component.h"
#include "core/logger.h"

#include <algorithm>

namespace Vestige
{

Scene::Scene(const std::string& name)
    : m_name(name)
    , m_root(std::make_unique<Entity>("Root"))
{
    m_entityIndex[m_root->getId()] = m_root.get();
}

Scene::~Scene() = default;

Entity* Scene::createEntity(const std::string& name)
{
    auto entity = std::make_unique<Entity>(name);
    Entity* ptr = m_root->addChild(std::move(entity));
    m_entityIndex[ptr->getId()] = ptr;
    return ptr;
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
    // Preserve previous capacity hints for first-frame pre-allocation.
    // After the first frame, clear() preserves capacity so no reallocation occurs.
    size_t prevRenderCount = std::max(data.renderItems.capacity(), size_t(64));
    size_t prevTransparentCount = std::max(data.transparentItems.capacity(), size_t(16));

    data.renderItems.clear();
    data.transparentItems.clear();
    data.pointLights.clear();
    data.spotLights.clear();
    data.particleEmitters.clear();
    data.waterSurfaces.clear();
    data.clothItems.clear();
    data.hasDirectionalLight = false;

    data.renderItems.reserve(prevRenderCount);
    data.transparentItems.reserve(prevTransparentCount);

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
    auto it = m_entityIndex.find(id);
    return (it != m_entityIndex.end()) ? it->second : nullptr;
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
    m_entityIndex.clear();
    m_entityIndex[m_root->getId()] = m_root.get();
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

    unregisterEntityRecursive(entity);
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

    Entity* result = parent->addChild(std::move(clone));
    registerEntityRecursive(result);
    return result;
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

        // Check for skeletal animation — animator may be on this entity or a parent
        auto* animator = entity.getComponent<SkeletonAnimator>();
        if (!animator)
        {
            const Entity* parent = entity.getParent();
            while (parent)
            {
                animator = parent->getComponent<SkeletonAnimator>();
                if (animator) break;
                parent = parent->getParent();
            }
        }
        if (animator && animator->isEnabled() && animator->hasBones())
        {
            item.boneMatrices = &animator->getBoneMatrices();
        }

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

    // Check for particle emitter
    auto* particleEmitter = entity.getComponent<ParticleEmitterComponent>();
    if (particleEmitter && particleEmitter->isEnabled())
    {
        data.particleEmitters.emplace_back(particleEmitter, entity.getWorldMatrix());

        // Collect coupled light from fire emitters
        glm::vec3 emitterPos = glm::vec3(entity.getWorldMatrix()[3]);
        auto coupledLight = particleEmitter->getCoupledLight(emitterPos);
        if (coupledLight.has_value())
        {
            data.pointLights.push_back(coupledLight.value());
        }
    }

    // Check for water surface
    auto* waterSurface = entity.getComponent<WaterSurfaceComponent>();
    if (waterSurface && waterSurface->isEnabled())
    {
        data.waterSurfaces.emplace_back(waterSurface, entity.getWorldMatrix());
    }

    // Check for cloth component
    auto* cloth = entity.getComponent<ClothComponent>();
    if (cloth && cloth->isEnabled() && cloth->isReady())
    {
        SceneRenderData::ClothRenderItem item;
        item.mesh = &cloth->getMesh();
        item.material = cloth->getMaterial().get();
        item.worldMatrix = entity.getWorldMatrix();
        item.worldBounds = cloth->getMesh().getLocalBounds().transformed(item.worldMatrix);
        item.entityId = entity.getId();
        data.clothItems.push_back(item);
    }

    // Recurse into children
    for (const auto& child : entity.getChildren())
    {
        collectRenderDataRecursive(*child, data);
    }
}

void Scene::rebuildEntityIndex()
{
    m_entityIndex.clear();
    registerEntityRecursive(m_root.get());
}

void Scene::registerEntityRecursive(Entity* entity)
{
    if (!entity) return;
    m_entityIndex[entity->getId()] = entity;
    for (const auto& child : entity->getChildren())
    {
        registerEntityRecursive(child.get());
    }
}

void Scene::unregisterEntityRecursive(Entity* entity)
{
    if (!entity) return;
    m_entityIndex.erase(entity->getId());
    for (const auto& child : entity->getChildren())
    {
        unregisterEntityRecursive(child.get());
    }
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
