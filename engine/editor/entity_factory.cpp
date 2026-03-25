/// @file entity_factory.cpp
/// @brief EntityFactory implementation — spawns entities from the editor Create menu.
#include "editor/entity_factory.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "scene/particle_emitter.h"
#include "scene/particle_presets.h"
#include "scene/water_surface.h"
#include "resource/resource_manager.h"
#include "renderer/material.h"
#include "core/logger.h"

namespace Vestige
{

namespace
{

/// @brief Creates a unique PBR material with default editor settings.
/// Each entity gets its own material instance so inspector edits don't bleed.
std::shared_ptr<Material> createDefaultMaterial(ResourceManager& resources, uint32_t entityId)
{
    std::string name = "__editor_mat_" + std::to_string(entityId);
    auto material = resources.createMaterial(name);
    material->setType(MaterialType::PBR);
    material->setAlbedo(glm::vec3(0.7f));
    material->setMetallic(0.0f);
    material->setRoughness(0.5f);
    return material;
}

} // anonymous namespace

// --- Empty ---

Entity* EntityFactory::createEmptyEntity(Scene& scene, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Empty Entity");
    entity->transform.position = position;
    Logger::info("Created: " + entity->getName());
    return entity;
}

// --- Primitives ---

Entity* EntityFactory::createCube(Scene& scene, ResourceManager& resources, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Cube");
    entity->transform.position = position;

    auto mesh = resources.getCubeMesh();
    auto material = createDefaultMaterial(resources, entity->getId());
    entity->addComponent<MeshRenderer>(mesh, material);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createSphere(Scene& scene, ResourceManager& resources, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Sphere");
    entity->transform.position = position;

    auto mesh = resources.getSphereMesh();
    auto material = createDefaultMaterial(resources, entity->getId());
    entity->addComponent<MeshRenderer>(mesh, material);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createPlane(Scene& scene, ResourceManager& resources, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Plane");
    entity->transform.position = position;

    auto mesh = resources.getPlaneMesh(1.0f);  // half-size 1.0 = 2m x 2m
    auto material = createDefaultMaterial(resources, entity->getId());
    entity->addComponent<MeshRenderer>(mesh, material);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createCylinder(Scene& scene, ResourceManager& resources, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Cylinder");
    entity->transform.position = position;

    auto mesh = resources.getCylinderMesh();
    auto material = createDefaultMaterial(resources, entity->getId());
    entity->addComponent<MeshRenderer>(mesh, material);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createCone(Scene& scene, ResourceManager& resources, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Cone");
    entity->transform.position = position;

    auto mesh = resources.getConeMesh();
    auto material = createDefaultMaterial(resources, entity->getId());
    entity->addComponent<MeshRenderer>(mesh, material);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createWedge(Scene& scene, ResourceManager& resources, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Wedge");
    entity->transform.position = position;

    auto mesh = resources.getWedgeMesh();
    auto material = createDefaultMaterial(resources, entity->getId());
    entity->addComponent<MeshRenderer>(mesh, material);

    Logger::info("Created: " + entity->getName());
    return entity;
}

// --- Lights ---

Entity* EntityFactory::createDirectionalLight(Scene& scene, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Directional Light");
    entity->transform.position = position;
    entity->transform.rotation = glm::vec3(-45.0f, -30.0f, 0.0f);  // Angled downward

    entity->addComponent<DirectionalLightComponent>();

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createPointLight(Scene& scene, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Point Light");
    entity->transform.position = position;

    auto* comp = entity->addComponent<PointLightComponent>();
    comp->light.diffuse = glm::vec3(1.0f, 0.9f, 0.8f);   // Warm white
    comp->light.specular = glm::vec3(1.0f, 0.9f, 0.8f);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createSpotLight(Scene& scene, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Spot Light");
    entity->transform.position = position;

    entity->addComponent<SpotLightComponent>();

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createParticleEmitter(Scene& scene, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Particle Emitter");
    entity->transform.position = position;

    auto* emitter = entity->addComponent<ParticleEmitterComponent>();
    // Default: upward fountain effect
    auto& cfg = emitter->getConfig();
    cfg.emissionRate = 50.0f;
    cfg.maxParticles = 500;
    cfg.shape = ParticleEmitterConfig::Shape::CONE;
    cfg.shapeConeAngle = 15.0f;
    cfg.startSpeedMin = 2.0f;
    cfg.startSpeedMax = 4.0f;
    cfg.startSizeMin = 0.05f;
    cfg.startSizeMax = 0.15f;
    cfg.startLifetimeMin = 1.0f;
    cfg.startLifetimeMax = 2.5f;
    cfg.startColor = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f);  // Warm orange
    cfg.gravity = glm::vec3(0.0f, -3.0f, 0.0f);

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createParticlePreset(Scene& scene, const glm::vec3& position,
                                             const std::string& presetName)
{
    ParticleEmitterConfig preset;
    std::string entityName = "Particle Emitter";

    if (presetName == "torch")
    {
        preset = ParticlePresets::torchFire();
        entityName = "Torch Fire";
    }
    else if (presetName == "candle")
    {
        preset = ParticlePresets::candleFlame();
        entityName = "Candle Flame";
    }
    else if (presetName == "campfire")
    {
        preset = ParticlePresets::campfire();
        entityName = "Campfire";
    }
    else if (presetName == "smoke")
    {
        preset = ParticlePresets::smoke();
        entityName = "Smoke";
    }
    else if (presetName == "dust")
    {
        preset = ParticlePresets::dustMotes();
        entityName = "Dust Motes";
    }
    else if (presetName == "incense")
    {
        preset = ParticlePresets::incense();
        entityName = "Incense Smoke";
    }
    else if (presetName == "sparks")
    {
        preset = ParticlePresets::sparks();
        entityName = "Sparks";
    }
    else
    {
        // Unknown preset, fall back to torch
        preset = ParticlePresets::torchFire();
        entityName = "Torch Fire";
    }

    Entity* entity = scene.createEntity(entityName);
    entity->transform.position = position;

    auto* emitter = entity->addComponent<ParticleEmitterComponent>();
    emitter->getConfig() = preset;

    Logger::info("Created: " + entity->getName());
    return entity;
}

Entity* EntityFactory::createWaterSurface(Scene& scene, const glm::vec3& position)
{
    Entity* entity = scene.createEntity("Water Surface");
    entity->transform.position = position;

    auto* water = entity->addComponent<WaterSurfaceComponent>();
    // Default: calm pool preset
    auto& cfg = water->getConfig();
    cfg.width = 10.0f;
    cfg.depth = 10.0f;
    cfg.gridResolution = 128;
    cfg.numWaves = 2;
    cfg.waves[0] = {0.005f, 3.0f, 0.2f, 0.0f};
    cfg.waves[1] = {0.003f, 2.0f, 0.15f, 90.0f};
    cfg.shallowColor = {0.15f, 0.5f, 0.6f, 0.7f};
    cfg.deepColor = {0.02f, 0.15f, 0.3f, 1.0f};
    cfg.flowSpeed = 0.2f;
    cfg.specularPower = 256.0f;

    Logger::info("Created: " + entity->getName());
    return entity;
}

} // namespace Vestige
