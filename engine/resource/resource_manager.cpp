// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file resource_manager.cpp
/// @brief ResourceManager implementation.
#include "resource/resource_manager.h"
#include "resource/model.h"
#include "utils/obj_loader.h"
#include "utils/gltf_loader.h"
#include "utils/path_sandbox.h"
#include "utils/asset_path.h"
#include "core/logger.h"

#include <cstdio>
#include <stdexcept>

namespace Vestige
{

ResourceManager::ResourceManager() = default;
ResourceManager::~ResourceManager() = default;

void ResourceManager::setSandboxRoots(std::vector<std::filesystem::path> roots)
{
    m_sandboxRoots = std::move(roots);
}

std::string ResourceManager::validatePath(const std::string& filePath) const
{
    if (m_sandboxRoots.empty())
        return filePath;  // Sandbox disabled.

    auto canon = PathSandbox::validateInsideRoots(
        std::filesystem::path(filePath), m_sandboxRoots);
    if (canon.empty())
    {
        Logger::warning("ResourceManager: path rejected (escapes sandbox): " + filePath);
    }
    return canon;
}

std::string ResourceManager::openPathFor(const std::string& key) const
{
    // The key is already root-relative (AssetPath::toKey), so join it directly:
    // AssetPath::resolve would apply the legacy-prefix rule a second time.
    const std::filesystem::path given(key);
    if (m_assetRoot.empty() || key.empty() || given.is_absolute())
    {
        return key;
    }
    return (m_assetRoot / given).lexically_normal().string();
}

std::shared_ptr<Texture> ResourceManager::loadTexture(const std::string& filePath, bool linear)
{
    // Include linear flag in cache key so the same file can be loaded with different formats.
    // The cache key is the root-relative form (3D_E-0874), so every spelling of one
    // file shares an entry and findTexturePath returns what a scene can save. Only a
    // cache miss reaches validatePath, so a cached entry is not re-checked against
    // roots set after it was loaded.
    const std::string key = AssetPath::toKey(m_assetRoot, filePath);
    std::string cacheKey = key + (linear ? ":linear" : ":srgb");

    // Check cache
    auto it = m_textures.find(cacheKey);
    if (it != m_textures.end())
    {
        touchCache(m_textures, m_textureOrder, it);  // LRU bump (Pe5).
        return it->second.value;
    }

    // Path sandbox (Slice 5 D1): reject before opening file.
    std::string safePath = validatePath(openPathFor(key));
    if (safePath.empty())
    {
        return getDefaultTexture();
    }

    // Load new
    auto texture = std::make_shared<Texture>();
    if (!texture->loadFromFile(safePath, linear))
    {
        Logger::warning("Failed to load texture: " + filePath + " — using default");
        return getDefaultTexture();
    }

    insertAndEnforceCache(m_textures, m_textureOrder, cacheKey, texture, m_textureLimit);
    return texture;
}

std::shared_ptr<Texture> ResourceManager::getDefaultTexture()
{
    if (!m_defaultTexture)
    {
        m_defaultTexture = std::make_shared<Texture>();
        m_defaultTexture->createSolidColor(255, 255, 255);
        Logger::debug("Default white texture created");
    }
    return m_defaultTexture;
}

std::shared_ptr<Mesh> ResourceManager::loadMesh(const std::string& filePath)
{
    // Check cache (root-relative key, as loadTexture).
    const std::string key = AssetPath::toKey(m_assetRoot, filePath);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    // Path sandbox (Slice 5 D1).
    std::string safePath = validatePath(openPathFor(key));
    if (safePath.empty())
    {
        return nullptr;
    }

    // Load new
    auto mesh = std::make_shared<Mesh>();
    if (!ObjLoader::loadMesh(safePath, *mesh))
    {
        Logger::error("Failed to load mesh: " + filePath);
        return nullptr;
    }

    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getCubeMesh()
{
    const std::string key = "__builtin_cube";
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createCube());
    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getPlaneMesh(float size)
{
    // The size in full: cut to a whole number, a 0.5 plane and a 0.9 plane
    // shared one mesh. %.9g reads back to the same float in getMeshByKey and
    // prints a whole size as before ("30"), the key older scenes saved.
    char sizeText[32];
    std::snprintf(sizeText, sizeof(sizeText), "%.9g", static_cast<double>(size));
    std::string key = std::string("__builtin_plane_") + sizeText;
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createPlane(size));
    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getSphereMesh(uint32_t sectors, uint32_t stacks)
{
    std::string key = "__builtin_sphere_" + std::to_string(sectors) + "x" + std::to_string(stacks);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createSphere(sectors, stacks));
    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getCylinderMesh(uint32_t sectors)
{
    std::string key = "__builtin_cylinder_" + std::to_string(sectors);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createCylinder(sectors));
    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getConeMesh(uint32_t sectors, uint32_t stacks)
{
    std::string key = "__builtin_cone_" + std::to_string(sectors) + "x" + std::to_string(stacks);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createCone(sectors, stacks));
    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getWedgeMesh()
{
    const std::string key = "__builtin_wedge";
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createWedge());
    insertAndEnforceCache(m_meshes, m_meshOrder, key, mesh, m_meshLimit);
    return mesh;
}

std::shared_ptr<Material> ResourceManager::createMaterial(const std::string& name)
{
    auto it = m_materials.find(name);
    if (it != m_materials.end())
    {
        return it->second;
    }

    auto material = std::make_shared<Material>();
    material->name = name;
    m_materials[name] = material;
    return material;
}

std::shared_ptr<Material> ResourceManager::getMaterial(const std::string& name)
{
    auto it = m_materials.find(name);
    if (it != m_materials.end())
    {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Model> ResourceManager::loadModel(const std::string& filePath)
{
    // Check cache (root-relative key, as loadTexture).
    const std::string key = AssetPath::toKey(m_assetRoot, filePath);
    auto it = m_models.find(key);
    if (it != m_models.end())
    {
        touchCache(m_models, m_modelOrder, it);
        return it->second.value;
    }

    // Path sandbox (Slice 5 D1). The check uses the canonical path; the loader
    // gets the lexical one, so a model under a linked folder hands its textures
    // paths below the asset root, which a scene can save (3D_E-0874).
    const std::string openPath = openPathFor(key);
    if (validatePath(openPath).empty())
    {
        return nullptr;
    }

    // Load new
    auto model = GltfLoader::load(openPath, *this);
    if (!model)
    {
        Logger::error("Failed to load model: " + filePath);
        return nullptr;
    }

    auto shared = std::shared_ptr<Model>(std::move(model));
    insertAndEnforceCache(m_models, m_modelOrder, key, shared, m_modelLimit);
    return shared;
}

std::string ResourceManager::findMeshKey(const std::shared_ptr<Mesh>& mesh) const
{
    for (const auto& [key, cached] : m_meshes)
    {
        if (cached.value == mesh)
        {
            return key;
        }
    }
    return "";
}

uint32_t ResourceManager::parseBuiltinSegmentCount(const std::string& text)
{
    // A scene file supplies these keys, and a shape's vertex count grows with the
    // product of its segment counts. Zero would divide by zero in the generators.
    constexpr int MAX_SEGMENTS = 1024;
    const int segments = std::stoi(text);
    if (segments < 1 || segments > MAX_SEGMENTS)
    {
        throw std::out_of_range("built-in mesh segment count out of range");
    }
    return static_cast<uint32_t>(segments);
}

std::shared_ptr<Mesh> ResourceManager::getMeshByKey(const std::string& key)
{
    // Check cache first (handles exact matches for any previously created mesh)
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        touchCache(m_meshes, m_meshOrder, it);
        return it->second.value;
    }

    // Create built-in meshes on demand
    if (key == "__builtin_cube")
    {
        return getCubeMesh();
    }
    if (key == "__builtin_wedge")
    {
        return getWedgeMesh();
    }
    if (key.rfind("__builtin_plane_", 0) == 0)
    {
        try
        {
            float size = std::stof(key.substr(16));
            return getPlaneMesh(size);
        }
        catch (const std::invalid_argument&)
        {
            return getPlaneMesh();
        }
        catch (const std::out_of_range&)
        {
            return getPlaneMesh();
        }
    }
    if (key.rfind("__builtin_sphere_", 0) == 0)
    {
        try
        {
            auto xPos = key.find('x', 17);
            if (xPos != std::string::npos)
            {
                uint32_t sectors = parseBuiltinSegmentCount(key.substr(17, xPos - 17));
                uint32_t stacks = parseBuiltinSegmentCount(key.substr(xPos + 1));
                return getSphereMesh(sectors, stacks);
            }
        }
        catch (const std::invalid_argument&)
        {
        }
        catch (const std::out_of_range&)
        {
        }
        return getSphereMesh();
    }
    if (key.rfind("__builtin_cylinder_", 0) == 0)
    {
        try
        {
            uint32_t sectors = parseBuiltinSegmentCount(key.substr(19));
            return getCylinderMesh(sectors);
        }
        catch (const std::invalid_argument&)
        {
            return getCylinderMesh();
        }
        catch (const std::out_of_range&)
        {
            return getCylinderMesh();
        }
    }
    if (key.rfind("__builtin_cone_", 0) == 0)
    {
        try
        {
            auto xPos = key.find('x', 15);
            if (xPos != std::string::npos)
            {
                uint32_t sectors = parseBuiltinSegmentCount(key.substr(15, xPos - 15));
                uint32_t stacks = parseBuiltinSegmentCount(key.substr(xPos + 1));
                return getConeMesh(sectors, stacks);
            }
        }
        catch (const std::invalid_argument&)
        {
        }
        catch (const std::out_of_range&)
        {
        }
        return getConeMesh();
    }

    // Try loading as file path (OBJ)
    return loadMesh(key);
}

std::string ResourceManager::findTexturePath(const std::shared_ptr<Texture>& texture) const
{
    for (const auto& [key, cached] : m_textures)
    {
        if (cached.value == texture)
        {
            // Strip ":linear" or ":srgb" cache key suffix
            auto colonPos = key.rfind(':');
            if (colonPos != std::string::npos)
            {
                return key.substr(0, colonPos);
            }
            return key;
        }
    }
    return "";
}

void ResourceManager::clearAll()
{
    m_textures.clear();
    m_textureOrder.clear();
    m_meshes.clear();
    m_meshOrder.clear();
    m_materials.clear();
    m_models.clear();
    m_modelOrder.clear();
    m_defaultTexture.reset();
    Logger::info("All resources cleared");
}

size_t ResourceManager::getTextureCount() const
{
    return m_textures.size();
}

size_t ResourceManager::getMeshCount() const
{
    return m_meshes.size();
}

void ResourceManager::setTextureCacheLimit(size_t maxEntries)
{
    m_textureLimit = maxEntries;
    enforceCacheLimit(m_textures, m_textureOrder, m_textureLimit);
}

void ResourceManager::setMeshCacheLimit(size_t maxEntries)
{
    m_meshLimit = maxEntries;
    enforceCacheLimit(m_meshes, m_meshOrder, m_meshLimit);
}

void ResourceManager::setModelCacheLimit(size_t maxEntries)
{
    m_modelLimit = maxEntries;
    enforceCacheLimit(m_models, m_modelOrder, m_modelLimit);
}

} // namespace Vestige
