// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file entity_serializer.h
/// @brief Serializes/deserializes entity trees to/from JSON (reusable for prefabs and scenes).
#pragma once

#include <nlohmann/json_fwd.hpp>

namespace Vestige
{

class Entity;
class ResourceManager;
class Scene;

namespace EntitySerializer
{

/// @brief Recursively serializes an entity and all descendants to JSON.
/// @param entity The root entity to serialize.
/// @param resources ResourceManager for resolving mesh/texture cache keys.
/// @return JSON representation of the entity tree.
nlohmann::json serializeEntity(const Entity& entity, const ResourceManager& resources);

/// @brief Deserializes a JSON entity tree and adds it to the scene.
/// @param j JSON data for the entity (and children).
/// @param scene Target scene to create entities in.
/// @param resources ResourceManager for loading meshes/textures/materials.
/// @return Pointer to the root entity created, or nullptr on failure.
Entity* deserializeEntity(const nlohmann::json& j, Scene& scene, ResourceManager& resources);

} // namespace EntitySerializer
} // namespace Vestige
