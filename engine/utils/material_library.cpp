// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_library.cpp
/// @brief Material preset save/load implementation.
#include "utils/material_library.h"
#include "renderer/material.h"
#include "resource/resource_manager.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vestige
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

std::string materialsDir(const std::string& assetPath)
{
    return assetPath + "/materials";
}

json serializeMat(const Material& mat, const ResourceManager& resources)
{
    json j;
    j["name"] = mat.name;

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

    if (mat.hasDiffuseTexture())
    {
        std::string path = resources.findTexturePath(mat.getDiffuseTexture());
        if (!path.empty()) j["diffuseTexture"] = path;
    }
    if (mat.hasNormalMap())
    {
        std::string path = resources.findTexturePath(mat.getNormalMap());
        if (!path.empty()) j["normalMap"] = path;
    }
    if (mat.hasHeightMap())
    {
        std::string path = resources.findTexturePath(mat.getHeightMap());
        if (!path.empty()) j["heightMap"] = path;
    }

    j["heightScale"] = mat.getHeightScale();
    j["pomEnabled"] = mat.isPomEnabled();
    j["stochasticTiling"] = mat.isStochasticTiling();

    if (mat.getType() == MaterialType::PBR)
    {
        if (mat.hasMetallicRoughnessTexture())
        {
            std::string path = resources.findTexturePath(mat.getMetallicRoughnessTexture());
            if (!path.empty()) j["metallicRoughnessTexture"] = path;
        }
        if (mat.hasEmissiveTexture())
        {
            std::string path = resources.findTexturePath(mat.getEmissiveTexture());
            if (!path.empty()) j["emissiveTexture"] = path;
        }
        if (mat.hasAoTexture())
        {
            std::string path = resources.findTexturePath(mat.getAoTexture());
            if (!path.empty()) j["aoTexture"] = path;
        }
    }

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

void applyMat(const json& j, Material& mat, ResourceManager& resources)
{
    std::string typeStr = j.value("type", std::string("PBR"));
    if (typeStr == "BLINN_PHONG")
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
    }

    if (j.contains("diffuseTexture"))
    {
        auto tex = resources.loadTexture(j["diffuseTexture"].get<std::string>(), false);
        if (tex) mat.setDiffuseTexture(tex);
    }
    if (j.contains("normalMap"))
    {
        auto tex = resources.loadTexture(j["normalMap"].get<std::string>(), true);
        if (tex) mat.setNormalMap(tex);
    }
    if (j.contains("heightMap"))
    {
        auto tex = resources.loadTexture(j["heightMap"].get<std::string>(), true);
        if (tex) mat.setHeightMap(tex);
    }

    mat.setHeightScale(j.value("heightScale", 0.05f));
    mat.setPomEnabled(j.value("pomEnabled", false));
    mat.setStochasticTiling(j.value("stochasticTiling", false));

    if (j.contains("metallicRoughnessTexture"))
    {
        auto tex = resources.loadTexture(j["metallicRoughnessTexture"].get<std::string>(), true);
        if (tex) mat.setMetallicRoughnessTexture(tex);
    }
    if (j.contains("emissiveTexture"))
    {
        auto tex = resources.loadTexture(j["emissiveTexture"].get<std::string>(), false);
        if (tex) mat.setEmissiveTexture(tex);
    }
    if (j.contains("aoTexture"))
    {
        auto tex = resources.loadTexture(j["aoTexture"].get<std::string>(), true);
        if (tex) mat.setAoTexture(tex);
    }

    std::string alphaStr = j.value("alphaMode", std::string("OPAQUE"));
    if (alphaStr == "MASK") mat.setAlphaMode(AlphaMode::MASK);
    else if (alphaStr == "BLEND") mat.setAlphaMode(AlphaMode::BLEND);
    else mat.setAlphaMode(AlphaMode::OPAQUE);

    mat.setAlphaCutoff(j.value("alphaCutoff", 0.5f));
    mat.setDoubleSided(j.value("doubleSided", false));
    mat.setBaseColorAlpha(j.value("baseColorAlpha", 1.0f));
}

} // anonymous namespace

namespace MaterialLibrary
{

bool saveMaterial(const std::string& name, const Material& material,
                  const ResourceManager& resources, const std::string& assetPath)
{
    std::string dir = materialsDir(assetPath);
    fs::create_directories(dir);

    std::string filepath = dir + "/" + name + ".json";
    json j = serializeMat(material, resources);

    std::ofstream file(filepath);
    if (!file.is_open())
    {
        Logger::error("MaterialLibrary: Failed to write " + filepath);
        return false;
    }
    file << j.dump(2);
    Logger::info("MaterialLibrary: Saved preset '" + name + "' to " + filepath);
    return true;
}

bool loadMaterial(const std::string& name, Material& target,
                  ResourceManager& resources, const std::string& assetPath)
{
    std::string filepath = materialsDir(assetPath) + "/" + name + ".json";
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        Logger::error("MaterialLibrary: Failed to read " + filepath);
        return false;
    }

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded())
    {
        Logger::error("MaterialLibrary: Invalid JSON in " + filepath);
        return false;
    }

    applyMat(j, target, resources);
    Logger::info("MaterialLibrary: Loaded preset '" + name + "'");
    return true;
}

std::vector<std::string> listPresets(const std::string& assetPath)
{
    std::vector<std::string> names;
    std::string dir = materialsDir(assetPath);
    if (!fs::exists(dir))
    {
        return names;
    }

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace MaterialLibrary
} // namespace Vestige
