/// @file material.cpp
/// @brief Material implementation.
#include "renderer/material.h"

namespace Vestige
{

Material::Material()
    : name("default")
    , m_diffuseColor(0.8f, 0.8f, 0.8f)
    , m_specularColor(1.0f, 1.0f, 1.0f)
    , m_shininess(32.0f)
    , m_diffuseTexture(nullptr)
{
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

std::shared_ptr<Texture> Material::getDiffuseTexture() const
{
    return m_diffuseTexture;
}

bool Material::hasDiffuseTexture() const
{
    return m_diffuseTexture != nullptr && m_diffuseTexture->isLoaded();
}

} // namespace Vestige
