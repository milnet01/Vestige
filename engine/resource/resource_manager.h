/// @file resource_manager.h
/// @brief Caches loaded assets (meshes, textures, materials) to prevent reloading.
#pragma once

#include "renderer/mesh.h"
#include "renderer/texture.h"
#include "renderer/material.h"

#include <memory>
#include <string>
#include <unordered_map>

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
    /// @return Shared pointer to the texture, or nullptr on failure.
    std::shared_ptr<Texture> loadTexture(const std::string& filePath);

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

    // --- Materials ---

    /// @brief Creates and registers a named material.
    /// @param name Unique material name.
    /// @return Shared pointer to the new material.
    std::shared_ptr<Material> createMaterial(const std::string& name);

    /// @brief Gets a previously created material by name.
    std::shared_ptr<Material> getMaterial(const std::string& name);

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
    std::shared_ptr<Texture> m_defaultTexture;
};

} // namespace Vestige
