// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gltf_loader.h
/// @brief Loads glTF 2.0 models (.gltf / .glb) via tinygltf.
#pragma once

#include "resource/model.h"
#include "resource/resource_manager.h"

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Loads glTF 2.0 files into Model objects.
class GltfLoader
{
public:
    /// @brief Loads a glTF model from file.
    /// @param filePath Path to the .gltf or .glb file.
    /// @param resourceManager Resource manager for texture caching.
    /// @return Loaded model, or nullptr on failure.
    static std::unique_ptr<Model> load(const std::string& filePath,
                                        ResourceManager& resourceManager);
};

} // namespace Vestige
