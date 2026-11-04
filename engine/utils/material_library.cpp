// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_library.cpp
/// @brief Material preset save/load implementation.
#include "utils/material_library.h"
#include "renderer/material.h"
#include "core/logger.h"
#include "utils/json_size_cap.h"
#include "utils/material_json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vestige
{
namespace
{

std::string materialsDir(const std::string& assetPath)
{
    return assetPath + "/materials";
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
    json j = MaterialJson::toJson(material, resources);
    j["name"] = material.name;

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
    auto j = JsonSizeCap::loadJsonWithSizeCap(filepath, "MaterialLibrary");
    if (!j)
    {
        return false;
    }

    MaterialJson::apply(*j, target, resources, MaterialJson::TexturePaths::Any);
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
