// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file entity_serializer.cpp
/// @brief Entity tree serialization/deserialization implementation.
#include "utils/entity_serializer.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/light_component.h"
#include "scene/particle_emitter.h"
#include "scene/water_surface.h"
#include "renderer/material.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Vestige
{
namespace EntitySerializer
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static json vec3ToJson(const glm::vec3& v)
{
    return {v.x, v.y, v.z};
}

static json vec4ToJson(const glm::vec4& v)
{
    return {v.x, v.y, v.z, v.w};
}

static glm::vec4 readVec4(const json& j, const std::string& key, const glm::vec4& defaultVal)
{
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
    {
        return glm::vec4(
            j[key][0].get<float>(),
            j[key][1].get<float>(),
            j[key][2].get<float>(),
            j[key][3].get<float>()
        );
    }
    return defaultVal;
}

static glm::vec3 readVec3(const json& j, const std::string& key, const glm::vec3& defaultVal)
{
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
    {
        return glm::vec3(
            j[key][0].get<float>(),
            j[key][1].get<float>(),
            j[key][2].get<float>()
        );
    }
    return defaultVal;
}

// ---------------------------------------------------------------------------
// Material serialization
// ---------------------------------------------------------------------------

static json serializeMaterial(const Material& mat, const ResourceManager& resources)
{
    json j;

    if (mat.getType() == MaterialType::BLINN_PHONG)
    {
        j["type"] = "BLINN_PHONG";
        j["diffuseColor"] = vec3ToJson(mat.getDiffuseColor());
        j["specularColor"] = vec3ToJson(mat.getSpecularColor());
        j["shininess"] = mat.getShininess();
    }
    else
    {
        j["type"] = "PBR";
        j["albedo"] = vec3ToJson(mat.getAlbedo());
        j["metallic"] = mat.getMetallic();
        j["roughness"] = mat.getRoughness();
        j["ao"] = mat.getAo();
        j["emissive"] = vec3ToJson(mat.getEmissive());
        j["emissiveStrength"] = mat.getEmissiveStrength();
        j["clearcoat"] = mat.getClearcoat();
        j["clearcoatRoughness"] = mat.getClearcoatRoughness();
        j["uvScale"] = mat.getUvScale();
    }

    // Texture paths (shared textures)
    if (mat.hasDiffuseTexture())
    {
        std::string path = resources.findTexturePath(mat.getDiffuseTexture());
        if (!path.empty())
        {
            j["diffuseTexture"] = path;
        }
    }
    if (mat.hasNormalMap())
    {
        std::string path = resources.findTexturePath(mat.getNormalMap());
        if (!path.empty())
        {
            j["normalMap"] = path;
        }
    }
    if (mat.hasHeightMap())
    {
        std::string path = resources.findTexturePath(mat.getHeightMap());
        if (!path.empty())
        {
            j["heightMap"] = path;
        }
    }

    j["heightScale"] = mat.getHeightScale();
    j["pomEnabled"] = mat.isPomEnabled();
    j["stochasticTiling"] = mat.isStochasticTiling();

    // PBR-specific textures
    if (mat.getType() == MaterialType::PBR)
    {
        if (mat.hasMetallicRoughnessTexture())
        {
            std::string path = resources.findTexturePath(mat.getMetallicRoughnessTexture());
            if (!path.empty())
            {
                j["metallicRoughnessTexture"] = path;
            }
        }
        if (mat.hasEmissiveTexture())
        {
            std::string path = resources.findTexturePath(mat.getEmissiveTexture());
            if (!path.empty())
            {
                j["emissiveTexture"] = path;
            }
        }
        if (mat.hasAoTexture())
        {
            std::string path = resources.findTexturePath(mat.getAoTexture());
            if (!path.empty())
            {
                j["aoTexture"] = path;
            }
        }
    }

    // Transparency
    switch (mat.getAlphaMode())
    {
        case AlphaMode::OPAQUE: j["alphaMode"] = "OPAQUE"; break;
        case AlphaMode::MASK:   j["alphaMode"] = "MASK"; break;
        case AlphaMode::BLEND:  j["alphaMode"] = "BLEND"; break;
    }
    j["alphaCutoff"] = mat.getAlphaCutoff();
    j["doubleSided"] = mat.isDoubleSided();
    j["baseColorAlpha"] = mat.getBaseColorAlpha();

    return j;
}

static std::shared_ptr<Material> deserializeMaterial(
    const json& j, ResourceManager& resources, uint32_t entityId)
{
    std::string matName = "__prefab_mat_" + std::to_string(entityId);
    auto material = resources.createMaterial(matName);

    std::string typeStr = j.value("type", std::string("PBR"));
    if (typeStr == "BLINN_PHONG")
    {
        material->setType(MaterialType::BLINN_PHONG);
        material->setDiffuseColor(readVec3(j, "diffuseColor", glm::vec3(0.8f)));
        material->setSpecularColor(readVec3(j, "specularColor", glm::vec3(1.0f)));
        material->setShininess(j.value("shininess", 32.0f));
    }
    else
    {
        material->setType(MaterialType::PBR);
        material->setAlbedo(readVec3(j, "albedo", glm::vec3(0.8f)));
        material->setMetallic(j.value("metallic", 0.0f));
        material->setRoughness(j.value("roughness", 0.5f));
        material->setAo(j.value("ao", 1.0f));
        material->setEmissive(readVec3(j, "emissive", glm::vec3(0.0f)));
        material->setEmissiveStrength(j.value("emissiveStrength", 1.0f));
        material->setClearcoat(j.value("clearcoat", 0.0f));
        material->setClearcoatRoughness(j.value("clearcoatRoughness", 0.04f));
        material->setUvScale(j.value("uvScale", 1.0f));
    }

    // Shared textures
    if (j.contains("diffuseTexture"))
    {
        auto tex = resources.loadTexture(j["diffuseTexture"].get<std::string>(), false);
        if (tex)
        {
            material->setDiffuseTexture(tex);
        }
    }
    if (j.contains("normalMap"))
    {
        auto tex = resources.loadTexture(j["normalMap"].get<std::string>(), true);
        if (tex)
        {
            material->setNormalMap(tex);
        }
    }
    if (j.contains("heightMap"))
    {
        auto tex = resources.loadTexture(j["heightMap"].get<std::string>(), true);
        if (tex)
        {
            material->setHeightMap(tex);
        }
    }

    material->setHeightScale(j.value("heightScale", 0.05f));
    material->setPomEnabled(j.value("pomEnabled", false));
    material->setStochasticTiling(j.value("stochasticTiling", false));

    // PBR textures
    if (j.contains("metallicRoughnessTexture"))
    {
        auto tex = resources.loadTexture(
            j["metallicRoughnessTexture"].get<std::string>(), true);
        if (tex)
        {
            material->setMetallicRoughnessTexture(tex);
        }
    }
    if (j.contains("emissiveTexture"))
    {
        auto tex = resources.loadTexture(
            j["emissiveTexture"].get<std::string>(), false);
        if (tex)
        {
            material->setEmissiveTexture(tex);
        }
    }
    if (j.contains("aoTexture"))
    {
        auto tex = resources.loadTexture(j["aoTexture"].get<std::string>(), true);
        if (tex)
        {
            material->setAoTexture(tex);
        }
    }

    // Transparency
    std::string alphaStr = j.value("alphaMode", std::string("OPAQUE"));
    if (alphaStr == "MASK")
    {
        material->setAlphaMode(AlphaMode::MASK);
    }
    else if (alphaStr == "BLEND")
    {
        material->setAlphaMode(AlphaMode::BLEND);
    }
    else
    {
        material->setAlphaMode(AlphaMode::OPAQUE);
    }

    material->setAlphaCutoff(j.value("alphaCutoff", 0.5f));
    material->setDoubleSided(j.value("doubleSided", false));
    material->setBaseColorAlpha(j.value("baseColorAlpha", 1.0f));

    return material;
}

// ---------------------------------------------------------------------------
// Component serialization
// ---------------------------------------------------------------------------

static json serializeMeshRenderer(const MeshRenderer& mr, const ResourceManager& resources)
{
    json j;

    if (mr.getMesh())
    {
        std::string meshKey = resources.findMeshKey(mr.getMesh());
        if (!meshKey.empty())
        {
            j["mesh"] = meshKey;
        }
    }

    if (mr.getMaterial())
    {
        j["material"] = serializeMaterial(*mr.getMaterial(), resources);
    }

    j["castsShadow"] = mr.castsShadow();
    return j;
}

static json serializeDirectionalLight(const DirectionalLightComponent& comp)
{
    json j;
    j["direction"] = vec3ToJson(comp.light.direction);
    j["ambient"] = vec3ToJson(comp.light.ambient);
    j["diffuse"] = vec3ToJson(comp.light.diffuse);
    j["specular"] = vec3ToJson(comp.light.specular);
    return j;
}

static json serializePointLight(const PointLightComponent& comp)
{
    json j;
    j["ambient"] = vec3ToJson(comp.light.ambient);
    j["diffuse"] = vec3ToJson(comp.light.diffuse);
    j["specular"] = vec3ToJson(comp.light.specular);
    j["constant"] = comp.light.constant;
    j["linear"] = comp.light.linear;
    j["quadratic"] = comp.light.quadratic;
    j["castsShadow"] = comp.light.castsShadow;
    return j;
}

static json serializeSpotLight(const SpotLightComponent& comp)
{
    json j;
    j["direction"] = vec3ToJson(comp.light.direction);
    j["ambient"] = vec3ToJson(comp.light.ambient);
    j["diffuse"] = vec3ToJson(comp.light.diffuse);
    j["specular"] = vec3ToJson(comp.light.specular);
    j["innerCutoff"] = comp.light.innerCutoff;
    j["outerCutoff"] = comp.light.outerCutoff;
    j["constant"] = comp.light.constant;
    j["linear"] = comp.light.linear;
    j["quadratic"] = comp.light.quadratic;
    return j;
}

static json serializeEmissiveLight(const EmissiveLightComponent& comp)
{
    json j;
    j["lightRadius"] = comp.lightRadius;
    j["lightIntensity"] = comp.lightIntensity;
    j["overrideColor"] = vec3ToJson(comp.overrideColor);
    return j;
}

// ---------------------------------------------------------------------------
// Particle emitter serialization
// ---------------------------------------------------------------------------

static std::string shapeToString(ParticleEmitterConfig::Shape shape)
{
    switch (shape)
    {
        case ParticleEmitterConfig::Shape::POINT: return "point";
        case ParticleEmitterConfig::Shape::SPHERE: return "sphere";
        case ParticleEmitterConfig::Shape::CONE: return "cone";
        case ParticleEmitterConfig::Shape::BOX: return "box";
    }
    return "point";
}

static ParticleEmitterConfig::Shape stringToShape(const std::string& s)
{
    if (s == "sphere") return ParticleEmitterConfig::Shape::SPHERE;
    if (s == "cone") return ParticleEmitterConfig::Shape::CONE;
    if (s == "box") return ParticleEmitterConfig::Shape::BOX;
    return ParticleEmitterConfig::Shape::POINT;
}

static json serializeParticleEmitter(const ParticleEmitterComponent& comp)
{
    json j;
    const auto& cfg = comp.getConfig();

    j["emissionRate"] = cfg.emissionRate;
    j["maxParticles"] = cfg.maxParticles;
    j["looping"] = cfg.looping;
    j["duration"] = cfg.duration;

    j["startLifetimeMin"] = cfg.startLifetimeMin;
    j["startLifetimeMax"] = cfg.startLifetimeMax;
    j["startSpeedMin"] = cfg.startSpeedMin;
    j["startSpeedMax"] = cfg.startSpeedMax;
    j["startSizeMin"] = cfg.startSizeMin;
    j["startSizeMax"] = cfg.startSizeMax;
    j["startColor"] = vec4ToJson(cfg.startColor);

    j["gravity"] = vec3ToJson(cfg.gravity);

    j["shape"] = shapeToString(cfg.shape);
    j["shapeRadius"] = cfg.shapeRadius;
    j["shapeConeAngle"] = cfg.shapeConeAngle;
    j["shapeBoxSize"] = vec3ToJson(cfg.shapeBoxSize);

    // Over-lifetime
    j["useColorOverLifetime"] = cfg.useColorOverLifetime;
    if (cfg.useColorOverLifetime)
    {
        j["colorOverLifetime"] = cfg.colorOverLifetime.toJson();
    }

    j["useSizeOverLifetime"] = cfg.useSizeOverLifetime;
    if (cfg.useSizeOverLifetime)
    {
        j["sizeOverLifetime"] = cfg.sizeOverLifetime.toJson();
    }

    j["useSpeedOverLifetime"] = cfg.useSpeedOverLifetime;
    if (cfg.useSpeedOverLifetime)
    {
        j["speedOverLifetime"] = cfg.speedOverLifetime.toJson();
    }

    j["blendMode"] = (cfg.blendMode == ParticleEmitterConfig::BlendMode::ADDITIVE)
                         ? "additive" : "alphaBlend";

    if (!cfg.texturePath.empty())
    {
        j["texturePath"] = cfg.texturePath;
    }

    // Light coupling
    if (cfg.emitsLight)
    {
        j["emitsLight"] = true;
        j["lightColor"] = vec3ToJson(cfg.lightColor);
        j["lightRange"] = cfg.lightRange;
        j["lightIntensity"] = cfg.lightIntensity;
        j["flickerSpeed"] = cfg.flickerSpeed;
    }

    return j;
}

static void deserializeParticleEmitter(const json& j, ParticleEmitterComponent& comp)
{
    auto& cfg = comp.getConfig();

    cfg.emissionRate = j.value("emissionRate", cfg.emissionRate);
    cfg.maxParticles = j.value("maxParticles", cfg.maxParticles);
    cfg.looping = j.value("looping", cfg.looping);
    cfg.duration = j.value("duration", cfg.duration);

    cfg.startLifetimeMin = j.value("startLifetimeMin", cfg.startLifetimeMin);
    cfg.startLifetimeMax = j.value("startLifetimeMax", cfg.startLifetimeMax);
    cfg.startSpeedMin = j.value("startSpeedMin", cfg.startSpeedMin);
    cfg.startSpeedMax = j.value("startSpeedMax", cfg.startSpeedMax);
    cfg.startSizeMin = j.value("startSizeMin", cfg.startSizeMin);
    cfg.startSizeMax = j.value("startSizeMax", cfg.startSizeMax);
    cfg.startColor = readVec4(j, "startColor", cfg.startColor);

    cfg.gravity = readVec3(j, "gravity", cfg.gravity);

    if (j.contains("shape"))
    {
        cfg.shape = stringToShape(j["shape"].get<std::string>());
    }
    cfg.shapeRadius = j.value("shapeRadius", cfg.shapeRadius);
    cfg.shapeConeAngle = j.value("shapeConeAngle", cfg.shapeConeAngle);
    cfg.shapeBoxSize = readVec3(j, "shapeBoxSize", cfg.shapeBoxSize);

    // Over-lifetime
    cfg.useColorOverLifetime = j.value("useColorOverLifetime", false);
    if (j.contains("colorOverLifetime"))
    {
        cfg.colorOverLifetime = ColorGradient::fromJson(j["colorOverLifetime"]);
    }

    cfg.useSizeOverLifetime = j.value("useSizeOverLifetime", false);
    if (j.contains("sizeOverLifetime"))
    {
        cfg.sizeOverLifetime = AnimationCurve::fromJson(j["sizeOverLifetime"]);
    }

    cfg.useSpeedOverLifetime = j.value("useSpeedOverLifetime", false);
    if (j.contains("speedOverLifetime"))
    {
        cfg.speedOverLifetime = AnimationCurve::fromJson(j["speedOverLifetime"]);
    }

    std::string blend = j.value("blendMode", std::string("additive"));
    cfg.blendMode = (blend == "alphaBlend")
                        ? ParticleEmitterConfig::BlendMode::ALPHA_BLEND
                        : ParticleEmitterConfig::BlendMode::ADDITIVE;

    cfg.texturePath = j.value("texturePath", std::string(""));

    // Light coupling
    cfg.emitsLight = j.value("emitsLight", false);
    cfg.lightColor = readVec3(j, "lightColor", cfg.lightColor);
    cfg.lightRange = j.value("lightRange", cfg.lightRange);
    cfg.lightIntensity = j.value("lightIntensity", cfg.lightIntensity);
    cfg.flickerSpeed = j.value("flickerSpeed", cfg.flickerSpeed);
}

// ---------------------------------------------------------------------------
// Water surface serialization
// ---------------------------------------------------------------------------

static json serializeWaterSurface(const WaterSurfaceComponent& comp)
{
    json j;
    const auto& cfg = comp.getConfig();

    j["width"] = cfg.width;
    j["depth"] = cfg.depth;
    j["gridResolution"] = cfg.gridResolution;
    j["numWaves"] = cfg.numWaves;

    json waves = json::array();
    for (int i = 0; i < cfg.numWaves && i < WaterSurfaceConfig::MAX_WAVES; ++i)
    {
        json w;
        w["amplitude"] = cfg.waves[i].amplitude;
        w["wavelength"] = cfg.waves[i].wavelength;
        w["speed"] = cfg.waves[i].speed;
        w["direction"] = cfg.waves[i].direction;
        waves.push_back(w);
    }
    j["waves"] = waves;

    j["shallowColor"] = vec4ToJson(cfg.shallowColor);
    j["deepColor"] = vec4ToJson(cfg.deepColor);
    j["depthDistance"] = cfg.depthDistance;
    j["refractionStrength"] = cfg.refractionStrength;
    j["normalStrength"] = cfg.normalStrength;
    j["dudvStrength"] = cfg.dudvStrength;
    j["flowSpeed"] = cfg.flowSpeed;
    j["specularPower"] = cfg.specularPower;
    j["reflectionResolutionScale"] = cfg.reflectionResolutionScale;
    j["causticsEnabled"] = cfg.causticsEnabled;
    j["causticsIntensity"] = cfg.causticsIntensity;
    j["causticsScale"] = cfg.causticsScale;
    j["qualityTier"] = cfg.qualityTier;

    return j;
}

static void deserializeWaterSurface(const json& j, WaterSurfaceComponent& comp)
{
    auto& cfg = comp.getConfig();

    cfg.width = j.value("width", cfg.width);
    cfg.depth = j.value("depth", cfg.depth);
    cfg.gridResolution = j.value("gridResolution", cfg.gridResolution);
    cfg.numWaves = j.value("numWaves", cfg.numWaves);

    if (j.contains("waves") && j["waves"].is_array())
    {
        int count = std::min(static_cast<int>(j["waves"].size()),
                             WaterSurfaceConfig::MAX_WAVES);
        for (int i = 0; i < count; ++i)
        {
            const auto& w = j["waves"][i];
            cfg.waves[i].amplitude = w.value("amplitude", cfg.waves[i].amplitude);
            cfg.waves[i].wavelength = w.value("wavelength", cfg.waves[i].wavelength);
            cfg.waves[i].speed = w.value("speed", cfg.waves[i].speed);
            cfg.waves[i].direction = w.value("direction", cfg.waves[i].direction);
        }
    }

    cfg.shallowColor = readVec4(j, "shallowColor", cfg.shallowColor);
    cfg.deepColor = readVec4(j, "deepColor", cfg.deepColor);
    cfg.depthDistance = j.value("depthDistance", cfg.depthDistance);
    cfg.refractionStrength = j.value("refractionStrength", cfg.refractionStrength);
    cfg.normalStrength = j.value("normalStrength", cfg.normalStrength);
    cfg.dudvStrength = j.value("dudvStrength", cfg.dudvStrength);
    cfg.flowSpeed = j.value("flowSpeed", cfg.flowSpeed);
    cfg.specularPower = j.value("specularPower", cfg.specularPower);
    cfg.reflectionResolutionScale = j.value("reflectionResolutionScale",
                                             cfg.reflectionResolutionScale);
    cfg.causticsEnabled = j.value("causticsEnabled", cfg.causticsEnabled);
    cfg.causticsIntensity = j.value("causticsIntensity", cfg.causticsIntensity);
    cfg.causticsScale = j.value("causticsScale", cfg.causticsScale);
    cfg.qualityTier = j.value("qualityTier", cfg.qualityTier);
}

// ---------------------------------------------------------------------------
// Main serialize
// ---------------------------------------------------------------------------

json serializeEntity(const Entity& entity, const ResourceManager& resources)
{
    json j;

    j["name"] = entity.getName();

    // Transform
    json transform;
    transform["position"] = vec3ToJson(entity.transform.position);
    transform["rotation"] = vec3ToJson(entity.transform.rotation);
    transform["scale"] = vec3ToJson(entity.transform.scale);
    j["transform"] = transform;

    // Entity state
    j["visible"] = entity.isVisible();
    j["locked"] = entity.isLocked();

    // Components
    json components = json::object();

    auto* mr = entity.getComponent<MeshRenderer>();
    if (mr)
    {
        components["MeshRenderer"] = serializeMeshRenderer(*mr, resources);
    }

    auto* dirLight = entity.getComponent<DirectionalLightComponent>();
    if (dirLight)
    {
        components["DirectionalLight"] = serializeDirectionalLight(*dirLight);
    }

    auto* pointLight = entity.getComponent<PointLightComponent>();
    if (pointLight)
    {
        components["PointLight"] = serializePointLight(*pointLight);
    }

    auto* spotLight = entity.getComponent<SpotLightComponent>();
    if (spotLight)
    {
        components["SpotLight"] = serializeSpotLight(*spotLight);
    }

    auto* emissive = entity.getComponent<EmissiveLightComponent>();
    if (emissive)
    {
        components["EmissiveLight"] = serializeEmissiveLight(*emissive);
    }

    auto* particleEmitter = entity.getComponent<ParticleEmitterComponent>();
    if (particleEmitter)
    {
        components["ParticleEmitter"] = serializeParticleEmitter(*particleEmitter);
    }

    auto* waterSurface = entity.getComponent<WaterSurfaceComponent>();
    if (waterSurface)
    {
        components["WaterSurface"] = serializeWaterSurface(*waterSurface);
    }

    if (!components.empty())
    {
        j["components"] = components;
    }

    // Children (recursive)
    const auto& children = entity.getChildren();
    if (!children.empty())
    {
        json childArray = json::array();
        for (const auto& child : children)
        {
            childArray.push_back(serializeEntity(*child, resources));
        }
        j["children"] = childArray;
    }

    return j;
}

// ---------------------------------------------------------------------------
// Main deserialize
// ---------------------------------------------------------------------------

static Entity* deserializeEntityRecursive(
    const json& j, Scene& scene, ResourceManager& resources, Entity* parent)
{
    if (!j.is_object())
    {
        Logger::error("EntitySerializer: expected JSON object");
        return nullptr;
    }

    std::string name = j.value("name", std::string("Entity"));

    // Create entity — as child of parent, or as root-level scene entity
    Entity* entity = nullptr;
    if (parent)
    {
        auto newEntity = std::make_unique<Entity>(name);
        entity = parent->addChild(std::move(newEntity));
    }
    else
    {
        entity = scene.createEntity(name);
    }

    // Transform
    if (j.contains("transform") && j["transform"].is_object())
    {
        const auto& t = j["transform"];
        entity->transform.position = readVec3(t, "position", glm::vec3(0.0f));
        entity->transform.rotation = readVec3(t, "rotation", glm::vec3(0.0f));
        entity->transform.scale = readVec3(t, "scale", glm::vec3(1.0f));
    }

    // Entity state
    entity->setVisible(j.value("visible", true));
    entity->setLocked(j.value("locked", false));

    // Components
    if (j.contains("components") && j["components"].is_object())
    {
        const auto& comps = j["components"];

        // MeshRenderer
        if (comps.contains("MeshRenderer") && comps["MeshRenderer"].is_object())
        {
            const auto& mrJson = comps["MeshRenderer"];

            std::shared_ptr<Mesh> mesh;
            if (mrJson.contains("mesh"))
            {
                mesh = resources.getMeshByKey(mrJson["mesh"].get<std::string>());
            }

            std::shared_ptr<Material> material;
            if (mrJson.contains("material") && mrJson["material"].is_object())
            {
                material = deserializeMaterial(
                    mrJson["material"], resources, entity->getId());
            }

            if (mesh)
            {
                auto* mr = entity->addComponent<MeshRenderer>(mesh, material);
                mr->setCastsShadow(mrJson.value("castsShadow", true));
            }
        }

        // Directional Light
        if (comps.contains("DirectionalLight") && comps["DirectionalLight"].is_object())
        {
            const auto& dl = comps["DirectionalLight"];
            auto* comp = entity->addComponent<DirectionalLightComponent>();
            comp->light.direction = readVec3(dl, "direction", glm::vec3(-0.2f, -1.0f, -0.3f));
            comp->light.ambient = readVec3(dl, "ambient", glm::vec3(0.1f));
            comp->light.diffuse = readVec3(dl, "diffuse", glm::vec3(0.8f));
            comp->light.specular = readVec3(dl, "specular", glm::vec3(1.0f));
        }

        // Point Light
        if (comps.contains("PointLight") && comps["PointLight"].is_object())
        {
            const auto& pl = comps["PointLight"];
            auto* comp = entity->addComponent<PointLightComponent>();
            comp->light.ambient = readVec3(pl, "ambient", glm::vec3(0.05f));
            comp->light.diffuse = readVec3(pl, "diffuse", glm::vec3(0.8f));
            comp->light.specular = readVec3(pl, "specular", glm::vec3(1.0f));
            comp->light.constant = pl.value("constant", 1.0f);
            comp->light.linear = pl.value("linear", 0.09f);
            comp->light.quadratic = pl.value("quadratic", 0.032f);
            comp->light.castsShadow = pl.value("castsShadow", false);
        }

        // Spot Light
        if (comps.contains("SpotLight") && comps["SpotLight"].is_object())
        {
            const auto& sl = comps["SpotLight"];
            auto* comp = entity->addComponent<SpotLightComponent>();
            comp->light.direction = readVec3(sl, "direction", glm::vec3(0.0f, -1.0f, 0.0f));
            comp->light.ambient = readVec3(sl, "ambient", glm::vec3(0.0f));
            comp->light.diffuse = readVec3(sl, "diffuse", glm::vec3(1.0f));
            comp->light.specular = readVec3(sl, "specular", glm::vec3(1.0f));
            comp->light.innerCutoff = sl.value("innerCutoff", 0.9763f);
            comp->light.outerCutoff = sl.value("outerCutoff", 0.9659f);
            comp->light.constant = sl.value("constant", 1.0f);
            comp->light.linear = sl.value("linear", 0.09f);
            comp->light.quadratic = sl.value("quadratic", 0.032f);
        }

        // Emissive Light
        if (comps.contains("EmissiveLight") && comps["EmissiveLight"].is_object())
        {
            const auto& el = comps["EmissiveLight"];
            auto* comp = entity->addComponent<EmissiveLightComponent>();
            comp->lightRadius = el.value("lightRadius", 5.0f);
            comp->lightIntensity = el.value("lightIntensity", 1.0f);
            comp->overrideColor = readVec3(el, "overrideColor", glm::vec3(0.0f));
        }

        // Particle Emitter
        if (comps.contains("ParticleEmitter") && comps["ParticleEmitter"].is_object())
        {
            auto* comp = entity->addComponent<ParticleEmitterComponent>();
            deserializeParticleEmitter(comps["ParticleEmitter"], *comp);
        }

        // Water Surface
        if (comps.contains("WaterSurface") && comps["WaterSurface"].is_object())
        {
            auto* comp = entity->addComponent<WaterSurfaceComponent>();
            deserializeWaterSurface(comps["WaterSurface"], *comp);
        }
    }

    // Children (recursive)
    if (j.contains("children") && j["children"].is_array())
    {
        for (const auto& childJson : j["children"])
        {
            deserializeEntityRecursive(childJson, scene, resources, entity);
        }
    }

    return entity;
}

Entity* deserializeEntity(const json& j, Scene& scene, ResourceManager& resources)
{
    return deserializeEntityRecursive(j, scene, resources, nullptr);
}

} // namespace EntitySerializer
} // namespace Vestige
