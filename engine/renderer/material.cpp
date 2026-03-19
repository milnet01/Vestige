/// @file material.cpp
/// @brief Material implementation.
#include "renderer/material.h"

namespace Vestige
{

Material::Material()
    : name("default")
    , m_type(MaterialType::BLINN_PHONG)
    , m_diffuseColor(0.8f, 0.8f, 0.8f)
    , m_specularColor(1.0f, 1.0f, 1.0f)
    , m_shininess(32.0f)
    , m_diffuseTexture(nullptr)
    , m_normalMap(nullptr)
    , m_heightMap(nullptr)
    , m_heightScale(0.05f)
    , m_pomEnabled(true)
    , m_stochasticTiling(false)
    , m_albedo(0.8f, 0.8f, 0.8f)
    , m_metallic(0.0f)
    , m_roughness(0.5f)
    , m_ao(1.0f)
    , m_clearcoat(0.0f)
    , m_clearcoatRoughness(0.04f)
    , m_emissive(0.0f, 0.0f, 0.0f)
    , m_emissiveStrength(1.0f)
    , m_uvScale(1.0f)
    , m_metallicRoughnessTexture(nullptr)
    , m_emissiveTexture(nullptr)
    , m_aoTexture(nullptr)
    , m_alphaMode(AlphaMode::OPAQUE)
    , m_alphaCutoff(0.5f)
    , m_doubleSided(false)
    , m_baseColorAlpha(1.0f)
{
}

void Material::setType(MaterialType type)
{
    m_type = type;
}

MaterialType Material::getType() const
{
    return m_type;
}

void Material::setDiffuseColor(const glm::vec3& color)
{
    m_diffuseColor = color;
}

void Material::setSpecularColor(const glm::vec3& color)
{
    m_specularColor = color;
}

void Material::setShininess(float shininess)
{
    m_shininess = shininess;
}

void Material::setDiffuseTexture(std::shared_ptr<Texture> texture)
{
    m_diffuseTexture = texture;
}

glm::vec3 Material::getDiffuseColor() const
{
    return m_diffuseColor;
}

glm::vec3 Material::getSpecularColor() const
{
    return m_specularColor;
}

float Material::getShininess() const
{
    return m_shininess;
}

const std::shared_ptr<Texture>& Material::getDiffuseTexture() const
{
    return m_diffuseTexture;
}

bool Material::hasDiffuseTexture() const
{
    return m_diffuseTexture != nullptr && m_diffuseTexture->isLoaded();
}

void Material::setNormalMap(std::shared_ptr<Texture> texture)
{
    m_normalMap = texture;
}

const std::shared_ptr<Texture>& Material::getNormalMap() const
{
    return m_normalMap;
}

bool Material::hasNormalMap() const
{
    return m_normalMap != nullptr && m_normalMap->isLoaded();
}

void Material::setHeightMap(std::shared_ptr<Texture> texture)
{
    m_heightMap = texture;
}

const std::shared_ptr<Texture>& Material::getHeightMap() const
{
    return m_heightMap;
}

bool Material::hasHeightMap() const
{
    return m_heightMap != nullptr && m_heightMap->isLoaded();
}

void Material::setHeightScale(float scale)
{
    if (scale < 0.0f)
    {
        scale = 0.0f;
    }
    if (scale > 0.2f)
    {
        scale = 0.2f;
    }
    m_heightScale = scale;
}

float Material::getHeightScale() const
{
    return m_heightScale;
}

void Material::setPomEnabled(bool isEnabled)
{
    m_pomEnabled = isEnabled;
}

bool Material::isPomEnabled() const
{
    return m_pomEnabled;
}

void Material::setStochasticTiling(bool isEnabled)
{
    m_stochasticTiling = isEnabled;
}

bool Material::isStochasticTiling() const
{
    return m_stochasticTiling;
}

void Material::setUvScale(float scale)
{
    if (scale < 0.01f)
    {
        scale = 0.01f;
    }
    if (scale > 100.0f)
    {
        scale = 100.0f;
    }
    m_uvScale = scale;
}

float Material::getUvScale() const
{
    return m_uvScale;
}

// --- PBR properties ---

void Material::setAlbedo(const glm::vec3& albedo)
{
    m_albedo = albedo;
}

glm::vec3 Material::getAlbedo() const
{
    return m_albedo;
}

void Material::setMetallic(float metallic)
{
    if (metallic < 0.0f)
    {
        metallic = 0.0f;
    }
    if (metallic > 1.0f)
    {
        metallic = 1.0f;
    }
    m_metallic = metallic;
}

float Material::getMetallic() const
{
    return m_metallic;
}

void Material::setRoughness(float roughness)
{
    if (roughness < 0.04f)
    {
        roughness = 0.04f;
    }
    if (roughness > 1.0f)
    {
        roughness = 1.0f;
    }
    m_roughness = roughness;
}

float Material::getRoughness() const
{
    return m_roughness;
}

void Material::setAo(float ao)
{
    if (ao < 0.0f)
    {
        ao = 0.0f;
    }
    if (ao > 1.0f)
    {
        ao = 1.0f;
    }
    m_ao = ao;
}

float Material::getAo() const
{
    return m_ao;
}

void Material::setClearcoat(float clearcoat)
{
    if (clearcoat < 0.0f) clearcoat = 0.0f;
    if (clearcoat > 1.0f) clearcoat = 1.0f;
    m_clearcoat = clearcoat;
}

float Material::getClearcoat() const
{
    return m_clearcoat;
}

void Material::setClearcoatRoughness(float roughness)
{
    if (roughness < 0.0f) roughness = 0.0f;
    if (roughness > 1.0f) roughness = 1.0f;
    m_clearcoatRoughness = roughness;
}

float Material::getClearcoatRoughness() const
{
    return m_clearcoatRoughness;
}

void Material::setEmissive(const glm::vec3& emissive)
{
    m_emissive = emissive;
}

glm::vec3 Material::getEmissive() const
{
    return m_emissive;
}

void Material::setEmissiveStrength(float strength)
{
    if (strength < 0.0f)
    {
        strength = 0.0f;
    }
    if (strength > 100.0f)
    {
        strength = 100.0f;
    }
    m_emissiveStrength = strength;
}

float Material::getEmissiveStrength() const
{
    return m_emissiveStrength;
}

void Material::setMetallicRoughnessTexture(std::shared_ptr<Texture> texture)
{
    m_metallicRoughnessTexture = texture;
}

const std::shared_ptr<Texture>& Material::getMetallicRoughnessTexture() const
{
    return m_metallicRoughnessTexture;
}

bool Material::hasMetallicRoughnessTexture() const
{
    return m_metallicRoughnessTexture != nullptr && m_metallicRoughnessTexture->isLoaded();
}

void Material::setEmissiveTexture(std::shared_ptr<Texture> texture)
{
    m_emissiveTexture = texture;
}

const std::shared_ptr<Texture>& Material::getEmissiveTexture() const
{
    return m_emissiveTexture;
}

bool Material::hasEmissiveTexture() const
{
    return m_emissiveTexture != nullptr && m_emissiveTexture->isLoaded();
}

void Material::setAoTexture(std::shared_ptr<Texture> texture)
{
    m_aoTexture = texture;
}

const std::shared_ptr<Texture>& Material::getAoTexture() const
{
    return m_aoTexture;
}

bool Material::hasAoTexture() const
{
    return m_aoTexture != nullptr && m_aoTexture->isLoaded();
}

// --- Transparency ---

void Material::setAlphaMode(AlphaMode mode)
{
    m_alphaMode = mode;
}

AlphaMode Material::getAlphaMode() const
{
    return m_alphaMode;
}

void Material::setAlphaCutoff(float cutoff)
{
    if (cutoff < 0.0f)
    {
        cutoff = 0.0f;
    }
    if (cutoff > 1.0f)
    {
        cutoff = 1.0f;
    }
    m_alphaCutoff = cutoff;
}

float Material::getAlphaCutoff() const
{
    return m_alphaCutoff;
}

void Material::setDoubleSided(bool doubleSided)
{
    m_doubleSided = doubleSided;
}

bool Material::isDoubleSided() const
{
    return m_doubleSided;
}

void Material::setBaseColorAlpha(float alpha)
{
    if (alpha < 0.0f)
    {
        alpha = 0.0f;
    }
    if (alpha > 1.0f)
    {
        alpha = 1.0f;
    }
    m_baseColorAlpha = alpha;
}

float Material::getBaseColorAlpha() const
{
    return m_baseColorAlpha;
}

} // namespace Vestige
