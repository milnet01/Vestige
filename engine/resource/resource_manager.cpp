// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file resource_manager.cpp
/// @brief ResourceManager implementation.
#include "resource/resource_manager.h"
#include "resource/model.h"
#include "utils/obj_loader.h"
#include "utils/gltf_loader.h"
#include "utils/path_sandbox.h"
#include "core/logger.h"

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

std::shared_ptr<Texture> ResourceManager::loadTexture(const std::string& filePath, bool linear)
{
    // Include linear flag in cache key so the same file can be loaded with different formats.
    // The cache key uses the *original* filePath (not canonical) so callers can lookup
    // by the same string they passed in; sandbox validation runs on each call regardless.
    std::string cacheKey = filePath + (linear ? ":linear" : ":srgb");

    // Check cache
    auto it = m_textures.find(cacheKey);
    if (it != m_textures.end())
    {
        return it->second;
    }

    // Path sandbox (Slice 5 D1): reject before opening file.
    std::string safePath = validatePath(filePath);
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

    m_textures[cacheKey] = texture;
    return texture;
}

void ResourceManager::processAsyncUploads()
{
    if (m_asyncLoader)
    {
        m_asyncLoader->processUploads(2);
    }
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
    // Check cache
    auto it = m_meshes.find(filePath);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    // Path sandbox (Slice 5 D1).
    std::string safePath = validatePath(filePath);
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

    m_meshes[filePath] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getCubeMesh()
{
    const std::string key = "__builtin_cube";
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createCube());
    m_meshes[key] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getPlaneMesh(float size)
{
    std::string key = "__builtin_plane_" + std::to_string(static_cast<int>(size));
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createPlane(size));
    m_meshes[key] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getSphereMesh(uint32_t sectors, uint32_t stacks)
{
    std::string key = "__builtin_sphere_" + std::to_string(sectors) + "x" + std::to_string(stacks);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createSphere(sectors, stacks));
    m_meshes[key] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getCylinderMesh(uint32_t sectors)
{
    std::string key = "__builtin_cylinder_" + std::to_string(sectors);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createCylinder(sectors));
    m_meshes[key] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getConeMesh(uint32_t sectors, uint32_t stacks)
{
    std::string key = "__builtin_cone_" + std::to_string(sectors) + "x" + std::to_string(stacks);
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createCone(sectors, stacks));
    m_meshes[key] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> ResourceManager::getWedgeMesh()
{
    const std::string key = "__builtin_wedge";
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    auto mesh = std::make_shared<Mesh>(Mesh::createWedge());
    m_meshes[key] = mesh;
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
    // Check cache
    auto it = m_models.find(filePath);
    if (it != m_models.end())
    {
        return it->second;
    }

    // Path sandbox (Slice 5 D1).
    std::string safePath = validatePath(filePath);
    if (safePath.empty())
    {
        return nullptr;
    }

    // Load new
    auto model = GltfLoader::load(safePath, *this);
    if (!model)
    {
        Logger::error("Failed to load model: " + filePath);
        return nullptr;
    }

    auto shared = std::shared_ptr<Model>(std::move(model));
    m_models[filePath] = shared;
    return shared;
}

std::string ResourceManager::findMeshKey(const std::shared_ptr<Mesh>& mesh) const
{
    for (const auto& [key, cached] : m_meshes)
    {
        if (cached == mesh)
        {
            return key;
        }
    }
    return "";
}

std::shared_ptr<Mesh> ResourceManager::getMeshByKey(const std::string& key)
{
    // Check cache first (handles exact matches for any previously created mesh)
    auto it = m_meshes.find(key);
    if (it != m_meshes.end())
    {
        return it->second;
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
                uint32_t sectors = static_cast<uint32_t>(std::stoi(key.substr(17, xPos - 17)));
                uint32_t stacks = static_cast<uint32_t>(std::stoi(key.substr(xPos + 1)));
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
            uint32_t sectors = static_cast<uint32_t>(std::stoi(key.substr(19)));
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
                uint32_t sectors = static_cast<uint32_t>(std::stoi(key.substr(15, xPos - 15)));
                uint32_t stacks = static_cast<uint32_t>(std::stoi(key.substr(xPos + 1)));
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
        if (cached == texture)
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
    m_meshes.clear();
    m_materials.clear();
    m_models.clear();
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

} // namespace Vestige
