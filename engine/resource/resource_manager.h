// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file resource_manager.h
/// @brief Caches loaded assets (meshes, textures, materials) to prevent reloading.
#pragma once

#include "renderer/mesh.h"
#include "renderer/texture.h"
#include "renderer/material.h"
#include "resource/lru_cache.h"

#include <filesystem>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

    // --- Path sandboxing (Phase 10.9 Slice 5 D1) ---

    /// @brief Sets the allowed root directories for asset loads.
    ///
    /// Every `loadTexture` / `loadMesh` / `loadModel` call canonicalises
    /// its path argument and rejects (returns default / nullptr + logs a
    /// warning) any path that does not lie inside one of these roots.
    ///
    /// Empty roots = sandbox disabled (the default). Production code wires
    /// `[install_root, project_root, asset_library_root]` once at startup.
    /// Tests typically leave it empty so the existing fixture paths keep
    /// working.
    void setSandboxRoots(std::vector<std::filesystem::path> roots);

    /// @brief Returns the configured sandbox roots (for diagnostics).
    const std::vector<std::filesystem::path>& getSandboxRoots() const { return m_sandboxRoots; }

    // --- LRU cache limits (Phase 10.9 Slice 13 Pe5) -----------------------
    //
    // Each on-disk cache (textures, meshes, models) tracks recency in a
    // doubly-linked list of keys, MRU-front. Each cache hit splices the
    // entry to front; each cache insert pushes to front. After every
    // insert, the cache evicts from the LRU tail until size ≤ limit,
    // skipping entries with `use_count() > 1` (still held by callers) so
    // the shared-instance invariant — "for a given path, at most one
    // Texture/Mesh/Model lives in the engine" — is preserved across an
    // eviction-and-reload cycle.
    //
    // Defaults are generous (large enough that typical scenes never
    // evict). Level-streaming projects should call the setters explicitly
    // with project-appropriate values.

    /// @brief Sets the texture-cache entry limit. Default `kDefaultTextureLimit`.
    void setTextureCacheLimit(size_t maxEntries);
    /// @brief Sets the mesh-cache entry limit. Default `kDefaultMeshLimit`.
    void setMeshCacheLimit(size_t maxEntries);
    /// @brief Sets the model-cache entry limit. Default `kDefaultModelLimit`.
    void setModelCacheLimit(size_t maxEntries);

    /// @brief Returns the current texture-cache entry limit.
    size_t getTextureCacheLimit() const { return m_textureLimit; }
    /// @brief Returns the current mesh-cache entry limit.
    size_t getMeshCacheLimit() const { return m_meshLimit; }
    /// @brief Returns the current model-cache entry limit.
    size_t getModelCacheLimit() const { return m_modelLimit; }

    /// @brief Default texture entry cap. Sized for a detailed level
    ///        (~256 unique textures across albedo/normal/roughness/etc).
    static constexpr size_t kDefaultTextureLimit = 1024;
    /// @brief Default mesh entry cap. Disk-loaded plus a few dozen builtins.
    static constexpr size_t kDefaultMeshLimit    = 512;
    /// @brief Default model entry cap.
    static constexpr size_t kDefaultModelLimit   = 128;

private:
    /// @brief Validates @a filePath against @a m_sandboxRoots; returns the
    ///        canonical path on success, empty string on rejection. Empty
    ///        roots → returns the input unchanged for backwards compat.
    std::string validatePath(const std::string& filePath) const;

    // LRU helpers live in `resource/lru_cache.h` so they can be unit-
    // tested against a non-GL value type.
    Cache<Texture>  m_textures;
    Cache<Mesh>     m_meshes;
    Cache<Model>    m_models;
    std::list<std::string> m_textureOrder;  ///< MRU at front.
    std::list<std::string> m_meshOrder;
    std::list<std::string> m_modelOrder;
    size_t m_textureLimit = kDefaultTextureLimit;
    size_t m_meshLimit    = kDefaultMeshLimit;
    size_t m_modelLimit   = kDefaultModelLimit;

    std::unordered_map<std::string, std::shared_ptr<Material>> m_materials;
    std::shared_ptr<Texture> m_defaultTexture;
    std::vector<std::filesystem::path> m_sandboxRoots;
};

} // namespace Vestige
