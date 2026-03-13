/// @file resource_manager.cpp
/// @brief ResourceManager implementation.
#include "resource/resource_manager.h"
#include "utils/obj_loader.h"
#include "core/logger.h"

namespace Vestige
{

ResourceManager::ResourceManager() = default;
ResourceManager::~ResourceManager() = default;

std::shared_ptr<Texture> ResourceManager::loadTexture(const std::string& filePath)
{
    // Check cache
    auto it = m_textures.find(filePath);
    if (it != m_textures.end())
    {
        return it->second;
    }

    // Load new
    auto texture = std::make_shared<Texture>();
    if (!texture->loadFromFile(filePath))
    {
        Logger::warning("Failed to load texture: " + filePath + " — using default");
        return getDefaultTexture();
    }

    m_textures[filePath] = texture;
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
    // Check cache
    auto it = m_meshes.find(filePath);
    if (it != m_meshes.end())
    {
        return it->second;
    }

    // Load new
    auto mesh = std::make_shared<Mesh>();
    if (!ObjLoader::loadMesh(filePath, *mesh))
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

void ResourceManager::clearAll()
{
    m_textures.clear();
    m_meshes.clear();
    m_materials.clear();
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
