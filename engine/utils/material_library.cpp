// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_library.cpp
/// @brief Material preset save/load implementation.
#include "utils/material_library.h"
#include "renderer/material.h"
#include "core/logger.h"
#include "utils/atomic_write.h"
#include "utils/json_size_cap.h"
#include "utils/material_json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

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

/// A preset name becomes "<name>.json" inside materials/, so it must be one
/// plain file name: a separator or a dot-only name would reach outside it.
bool isPlainPresetName(const std::string& name)
{
    return !name.empty() && name != "." && name != ".."
        && name.find_first_of("/\\") == std::string::npos;
}

} // anonymous namespace

namespace MaterialLibrary
{

bool saveMaterial(const std::string& name, const Material& material,
                  const ResourceManager& resources, const std::string& assetPath)
{
    if (!isPlainPresetName(name))
    {
        Logger::error("MaterialLibrary: Preset name '" + name + "' is not a plain file name");
        return false;
    }

    std::string filepath = materialsDir(assetPath) + "/" + name + ".json";
    json j = MaterialJson::toJson(material, resources);
    j["name"] = material.name;

    // writeFile creates materials/ and never leaves a half-written preset.
    const AtomicWrite::Status status = AtomicWrite::writeFile(filepath, j.dump(2));
    if (status != AtomicWrite::Status::Ok)
    {
        Logger::error("MaterialLibrary: Failed to write " + filepath + ": "
                      + AtomicWrite::describe(status));
        return false;
    }
    Logger::info("MaterialLibrary: Saved preset '" + name + "' to " + filepath);
    return true;
}

bool loadMaterial(const std::string& name, Material& target,
                  ResourceManager& resources, const std::string& assetPath)
{
    if (!isPlainPresetName(name))
    {
        Logger::error("MaterialLibrary: Preset name '" + name + "' is not a plain file name");
        return false;
    }

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
