// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_json.cpp
/// @brief The one material <-> JSON mapping (3D_E-0871).
#include "utils/material_json.h"
#include "renderer/material.h"
#include "resource/resource_manager.h"
#include "core/logger.h"
#include "utils/path_sandbox.h"

#include <memory>
#include <string>

using json = nlohmann::json;

namespace Vestige
{
namespace MaterialJson
{
namespace
{

json vec3ToJson(const glm::vec3& v)
{
    return {v.x, v.y, v.z};
}

glm::vec3 readVec3(const json& j, const std::string& key, const glm::vec3& def)
{
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
    {
        return def;
    }
    return glm::vec3(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
}

using TexturePtr = std::shared_ptr<Texture>;

/// One texture slot: its JSON key, whether it holds linear data, and how the
/// material reads and sets it.
struct TextureSlot
{
    const char* key;
    bool linear;
    bool pbrOnly;
    bool (Material::*has)() const;
    const TexturePtr& (Material::*get)() const;
    void (Material::*set)(TexturePtr);
};

const TextureSlot TEXTURE_SLOTS[] = {
    {"diffuseTexture", false, false,
     &Material::hasDiffuseTexture, &Material::getDiffuseTexture, &Material::setDiffuseTexture},
    {"normalMap", true, false,
     &Material::hasNormalMap, &Material::getNormalMap, &Material::setNormalMap},
    {"heightMap", true, false,
     &Material::hasHeightMap, &Material::getHeightMap, &Material::setHeightMap},
    {"metallicRoughnessTexture", true, true,
     &Material::hasMetallicRoughnessTexture, &Material::getMetallicRoughnessTexture,
     &Material::setMetallicRoughnessTexture},
    {"emissiveTexture", false, true,
     &Material::hasEmissiveTexture, &Material::getEmissiveTexture, &Material::setEmissiveTexture},
    {"aoTexture", true, true,
     &Material::hasAoTexture, &Material::getAoTexture, &Material::setAoTexture},
};

bool pathAllowed(const std::string& path, const char* key, TexturePaths paths)
{
    if (paths == TexturePaths::Any || PathSandbox::isSafeRelativeAsset(path))
    {
        return true;
    }
    Logger::warning(std::string("scene: unsafe path rejected for ") + key + ": " + path);
    return false;
}

} // namespace

json toJson(const Material& mat, const ResourceManager& resources)
{
    json j;
    const bool pbr = mat.getType() != MaterialType::BLINN_PHONG;

    if (!pbr)
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
        j["subsurfaceStrength"] = mat.getSubsurfaceStrength();
        j["subsurfaceColor"] = vec3ToJson(mat.getSubsurfaceColor());
        j["subsurfaceThickness"] = mat.getSubsurfaceThickness();
    }

    for (const auto& slot : TEXTURE_SLOTS)
    {
        if ((slot.pbrOnly && !pbr) || !(mat.*slot.has)())
        {
            continue;
        }
        const std::string path = resources.findTexturePath((mat.*slot.get)());
        if (!path.empty())
        {
            j[slot.key] = path;
        }
    }

    j["heightScale"] = mat.getHeightScale();
    j["pomEnabled"] = mat.isPomEnabled();
    j["stochasticTiling"] = mat.isStochasticTiling();

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

void apply(const json& j, Material& mat, ResourceManager& resources, TexturePaths paths)
{
    if (j.value("type", std::string("PBR")) == "BLINN_PHONG")
    {
        mat.setType(MaterialType::BLINN_PHONG);
        mat.setDiffuseColor(readVec3(j, "diffuseColor", glm::vec3(0.8f)));
        mat.setSpecularColor(readVec3(j, "specularColor", glm::vec3(1.0f)));
        mat.setShininess(j.value("shininess", 32.0f));
    }
    else
    {
        mat.setType(MaterialType::PBR);
        mat.setAlbedo(readVec3(j, "albedo", glm::vec3(0.8f)));
        mat.setMetallic(j.value("metallic", 0.0f));
        mat.setRoughness(j.value("roughness", 0.5f));
        mat.setAo(j.value("ao", 1.0f));
        mat.setEmissive(readVec3(j, "emissive", glm::vec3(0.0f)));
        mat.setEmissiveStrength(j.value("emissiveStrength", 1.0f));
        mat.setClearcoat(j.value("clearcoat", 0.0f));
        mat.setClearcoatRoughness(j.value("clearcoatRoughness", 0.04f));
        mat.setUvScale(j.value("uvScale", 1.0f));
        mat.setSubsurfaceStrength(j.value("subsurfaceStrength", 0.0f));
        mat.setSubsurfaceColor(readVec3(j, "subsurfaceColor", glm::vec3(1.0f)));
        mat.setSubsurfaceThickness(j.value("subsurfaceThickness", 0.5f));
    }

    // Loads read every slot, PBR-only ones included, as both old copies did.
    for (const auto& slot : TEXTURE_SLOTS)
    {
        if (!j.contains(slot.key))
        {
            continue;
        }
        const std::string path = j[slot.key].get<std::string>();
        if (!pathAllowed(path, slot.key, paths))
        {
            continue;
        }
        if (auto tex = resources.loadTexture(path, slot.linear))
        {
            (mat.*slot.set)(tex);
        }
    }

    mat.setHeightScale(j.value("heightScale", 0.05f));
    mat.setPomEnabled(j.value("pomEnabled", false));
    mat.setStochasticTiling(j.value("stochasticTiling", false));

    const std::string alphaStr = j.value("alphaMode", std::string("OPAQUE"));
    if (alphaStr == "MASK")       mat.setAlphaMode(AlphaMode::MASK);
    else if (alphaStr == "BLEND") mat.setAlphaMode(AlphaMode::BLEND);
    else                          mat.setAlphaMode(AlphaMode::OPAQUE);

    mat.setAlphaCutoff(j.value("alphaCutoff", 0.5f));
    mat.setDoubleSided(j.value("doubleSided", false));
    mat.setBaseColorAlpha(j.value("baseColorAlpha", 1.0f));
}

} // namespace MaterialJson
} // namespace Vestige
