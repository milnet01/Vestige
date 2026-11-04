// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_json.h
/// @brief The one material <-> JSON mapping, shared by scene files and presets.
#pragma once

#include <nlohmann/json.hpp>

namespace Vestige
{

class Material;
class ResourceManager;

namespace MaterialJson
{

/// @brief Which texture paths a load accepts.
enum class TexturePaths
{
    Any,                ///< Presets: the user's own files, any path.
    SandboxedRelative,  ///< Scene files: relative, no "..", else skipped with a warning.
};

/// @brief Writes every material field and each texture's path.
/// The material's name is not written; a caller that keeps one adds it.
nlohmann::json toJson(const Material& material, const ResourceManager& resources);

/// @brief Sets @a material from @a j. A missing field takes its default; a
///        texture path @a paths rejects, or that fails to load, is skipped.
void apply(const nlohmann::json& j, Material& material, ResourceManager& resources,
           TexturePaths paths);

} // namespace MaterialJson
} // namespace Vestige
