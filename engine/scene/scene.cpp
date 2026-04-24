// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file scene.cpp
/// @brief Scene implementation.
#include "scene/scene.h"
#include "physics/cloth_component.h"
#include "scene/camera_component.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>
#include <utility>

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
    Entity* ptr = entity.get();

    // S2: index the spawn eagerly so a subsequent `findEntityById` (e.g.
    // a SpawnEntity node writing the new id onto its "entity" output
    // pin, then another node reading it back in the same frame) resolves
    // while the attach-to-tree step is still queued.
    m_entityIndex[ptr->getId()] = ptr;

    if (m_updateDepth > 0)
    {
        m_pendingAdds.emplace_back(std::move(entity), m_root.get());
    }
    else
    {
        m_root->addChild(std::move(entity));
    }
    return ptr;
}

void Scene::update(float deltaTime)
{
    // S2: wrap the traversal so a component's update (typically a script
    // node) that calls `createEntity` / `removeEntity` gets deferred-
    // until-drain semantics instead of invalidating the walk.
    ScopedUpdate guard(*this);
    m_root->update(deltaTime);
}

void Scene::beginUpdate()
{
    ++m_updateDepth;
}

void Scene::endUpdate()
{
    assert(m_updateDepth > 0 && "endUpdate without matching beginUpdate");
    --m_updateDepth;
    if (m_updateDepth == 0)
    {
        drainPendingMutations();
    }
}

void Scene::drainPendingMutations()
{
    // Adds first: a spawned-then-removed entity in the same frame must
    // be materialised into the tree so the subsequent removal pass can
    // use the normal `unregisterEntityRecursive` + `removeChild` path
    // (which also handles the S1 active-camera null-out and recurses
    // into descendants).
    //
    // We move out of `m_pendingAdds` into a local and clear the member
    // up-front: `addChild` re-entering the scene (e.g. via a
    // component's onAttach that spawns another entity) would otherwise
    // mutate the vector we're iterating.
    auto adds = std::move(m_pendingAdds);
    m_pendingAdds.clear();
    for (auto& pa : adds)
    {
        if (pa.first && pa.second)
        {
            pa.second->addChild(std::move(pa.first));
        }
    }

    // Removals: dedupe ids so two handlers destroying the same target
    // don't double-destroy (second lookup would return nullptr anyway,
    // but the set makes the intent explicit).
    auto removals = std::move(m_pendingRemovals);
    m_pendingRemovals.clear();
    std::unordered_set<uint32_t> seen;
    seen.reserve(removals.size());
    for (uint32_t id : removals)
    {
        if (!seen.insert(id).second)
        {
            continue;
        }
        Entity* entity = findEntityById(id);
        if (!entity || entity == m_root.get())
        {
            continue;
        }
        Entity* parent = entity->getParent();
        if (!parent)
        {
            continue;
        }
        unregisterEntityRecursive(entity);
        parent->removeChild(entity);
    }
}

SceneRenderData Scene::collectRenderData(
    bool photosensitiveEnabled,
    const PhotosensitiveLimits& limits) const
{
    SceneRenderData data;
    collectRenderDataRecursive(*m_root, data, photosensitiveEnabled, limits);
    return data;
}

void Scene::collectRenderData(
    SceneRenderData& data,
    bool photosensitiveEnabled,
    const PhotosensitiveLimits& limits) const
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

    collectRenderDataRecursive(*m_root, data, photosensitiveEnabled, limits);
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

void Scene::setActiveCamera(CameraComponent* camera)
{
    m_activeCamera = camera;
}

CameraComponent* Scene::getActiveCamera() const
{
    return m_activeCamera;
}

void Scene::clearEntities()
{
    m_activeCamera = nullptr;
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

    if (m_updateDepth > 0)
    {
        // S2: defer. The entity stays alive and reachable (in the tree,
        // in m_entityIndex) until the outermost `ScopedUpdate` releases.
        // The walk in flight above us still sees this entity — removal
        // semantics are "apply after the current traversal", not "skip
        // during it".
        m_pendingRemovals.push_back(id);
        return true;
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

    Entity* result = clone.get();

    // Index the clone eagerly so findEntityById resolves from the
    // instant duplicateEntity returns, whether we attach now or defer.
    registerEntityRecursive(result);

    if (m_updateDepth > 0)
    {
        // S2: defer attachment (iterator invalidation hazard on the
        // parent's m_children otherwise) — owner stays alive in the
        // pending-adds list until drain.
        m_pendingAdds.emplace_back(std::move(clone), parent);
    }
    else
    {
        parent->addChild(std::move(clone));
    }
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

void Scene::collectRenderDataRecursive(
    const Entity& entity,
    SceneRenderData& data,
    bool photosensitiveEnabled,
    const PhotosensitiveLimits& limits) const
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
        if (animator && animator->isEnabled() && !animator->getMorphWeights().empty())
        {
            item.morphWeights = &animator->getMorphWeights();
        }

        // Morph target SSBO from mesh
        auto mesh = meshRenderer->getMesh();
        if (mesh && mesh->getMorphSSBO() != 0)
        {
            item.morphSSBO = mesh->getMorphSSBO();
            item.morphTargetCount = mesh->getMorphTargetCount();
            item.morphVertexCount = mesh->getMorphVertexCount();
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

        // Collect coupled light from fire emitters. Phase 10.7 slice
        // C2 threads photosensitive state here so the flicker-speed
        // clamp takes effect at every render-data collection — camera
        // reflection / refraction / id passes all see the same
        // clamped value as the primary view.
        glm::vec3 emitterPos = glm::vec3(entity.getWorldMatrix()[3]);
        auto coupledLight = particleEmitter->getCoupledLight(
            emitterPos, photosensitiveEnabled, limits);
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
        collectRenderDataRecursive(*child, data,
                                   photosensitiveEnabled, limits);
    }
}

void Scene::forEachEntity(const std::function<void(Entity&)>& fn)
{
    if (!m_root) return;
    // S2: wrap the traversal so `fn` can safely call `createEntity` /
    // `removeEntity` — those mutations are queued and drained when the
    // guard releases. The `visit` recursion still uses a bare range-for
    // because the underlying `m_children` vectors do not shrink during
    // the walk (all removes are deferred).
    ScopedUpdate guard(*this);
    std::function<void(Entity&)> visit = [&](Entity& entity)
    {
        fn(entity);
        for (auto& child : entity.getChildren())
        {
            visit(*child);
        }
    };
    for (auto& child : m_root->getChildren())
    {
        visit(*child);
    }
}

void Scene::forEachEntity(const std::function<void(const Entity&)>& fn) const
{
    if (!m_root) return;
    std::function<void(const Entity&)> visit = [&](const Entity& entity)
    {
        fn(entity);
        for (const auto& child : entity.getChildren())
        {
            visit(*child);
        }
    };
    for (const auto& child : m_root->getChildren())
    {
        visit(*child);
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

    // Phase 10.9 Slice 3 S1 — null the active-camera pointer if this
    // entity owns it. m_activeCamera is a raw CameraComponent* with
    // no ownership; destroying the owning entity without nulling the
    // pointer would leave renderer code dereferencing freed memory
    // every frame.
    if (auto* cam = entity->getComponent<CameraComponent>())
    {
        if (cam == m_activeCamera)
        {
            m_activeCamera = nullptr;
        }
    }

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
