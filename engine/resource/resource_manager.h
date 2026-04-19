// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file resource_manager.h
/// @brief Caches loaded assets (meshes, textures, materials) to prevent reloading.
#pragma once

#include "renderer/mesh.h"
#include "renderer/texture.h"
#include "renderer/material.h"
#include "resource/async_texture_loader.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Vestige { class Model; }  // Forward declaration

namespace Vestige
{

/// @brief Manages loading and caching of all engine assets.
class ResourceManager
{
public:
    ResourceManager();
    ~ResourceManager();

    // --- Textures ---

    /// @brief Loads a texture from file, or returns cached version.
    /// @param filePath Path to the image file.
    /// @param linear If true, load as linear data (normal/height maps). If false (default), load as sRGB.
    /// @return Shared pointer to the texture, or nullptr on failure.
    std::shared_ptr<Texture> loadTexture(const std::string& filePath, bool linear = false);

    /// @brief Processes completed async texture uploads (call once per frame).
    void processAsyncUploads();

    /// @brief Gets a solid-color fallback texture.
    std::shared_ptr<Texture> getDefaultTexture();

    // --- Meshes ---

    /// @brief Loads a mesh from an OBJ file, or returns cached version.
    /// @param filePath Path to the OBJ file.
    /// @return Shared pointer to the mesh, or nullptr on failure.
    std::shared_ptr<Mesh> loadMesh(const std::string& filePath);

    /// @brief Gets or creates a shared cube mesh.
    std::shared_ptr<Mesh> getCubeMesh();

    /// @brief Gets or creates a shared plane mesh.
    std::shared_ptr<Mesh> getPlaneMesh(float size = 10.0f);

    /// @brief Gets or creates a shared sphere mesh.
    std::shared_ptr<Mesh> getSphereMesh(uint32_t sectors = 32, uint32_t stacks = 16);

    /// @brief Gets or creates a shared cylinder mesh.
    std::shared_ptr<Mesh> getCylinderMesh(uint32_t sectors = 32);

    /// @brief Gets or creates a shared cone mesh.
    std::shared_ptr<Mesh> getConeMesh(uint32_t sectors = 32, uint32_t stacks = 4);

    /// @brief Gets or creates a shared wedge mesh.
    std::shared_ptr<Mesh> getWedgeMesh();

    // --- Materials ---

    /// @brief Creates and registers a named material.
    /// @param name Unique material name.
    /// @return Shared pointer to the new material.
    std::shared_ptr<Material> createMaterial(const std::string& name);

    /// @brief Gets a previously created material by name.
    std::shared_ptr<Material> getMaterial(const std::string& name);

    // --- Models ---

    /// @brief Loads a glTF model from file, or returns cached version.
    /// @param filePath Path to the .gltf or .glb file.
    /// @return Shared pointer to the model, or nullptr on failure.
    std::shared_ptr<Model> loadModel(const std::string& filePath);

    // --- Reverse lookup (for serialization) ---

    /// @brief Finds the cache key for a mesh (e.g. "__builtin_cube" or file path).
    /// @return The key string, or empty if the mesh is not in the cache.
    std::string findMeshKey(const std::shared_ptr<Mesh>& mesh) const;

    /// @brief Gets or creates a mesh by its cache key (builtin name or file path).
    /// @return The mesh, or nullptr if the key is invalid.
    std::shared_ptr<Mesh> getMeshByKey(const std::string& key);

    /// @brief Finds the file path for a cached texture (strips cache key suffix).
    /// @return The original file path, or empty if not found.
    std::string findTexturePath(const std::shared_ptr<Texture>& texture) const;

    // --- Management ---

    /// @brief Releases all cached resources.
    void clearAll();

    /// @brief Gets the number of cached textures.
    size_t getTextureCount() const;

    /// @brief Gets the number of cached meshes.
    size_t getMeshCount() const;

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> m_meshes;
    std::unordered_map<std::string, std::shared_ptr<Material>> m_materials;
    std::unordered_map<std::string, std::shared_ptr<Model>> m_models;
    std::shared_ptr<Texture> m_defaultTexture;
    std::unique_ptr<AsyncTextureLoader> m_asyncLoader;
};

} // namespace Vestige
