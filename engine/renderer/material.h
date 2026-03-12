/// @file material.h
/// @brief Surface material properties for Blinn-Phong shading.
#pragma once

#include "renderer/texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Describes the visual surface properties of a mesh.
class Material
{
public:
    Material();

    /// @brief Sets the diffuse (base) color.
    void setDiffuseColor(const glm::vec3& color);

    /// @brief Sets the specular (highlight) color.
    void setSpecularColor(const glm::vec3& color);

    /// @brief Sets the shininess exponent (higher = sharper highlights).
    void setShininess(float shininess);

    /// @brief Sets the diffuse texture. Pass nullptr for no texture.
    void setDiffuseTexture(std::shared_ptr<Texture> texture);

    /// @brief Gets the diffuse color.
    glm::vec3 getDiffuseColor() const;

    /// @brief Gets the specular color.
    glm::vec3 getSpecularColor() const;

    /// @brief Gets the shininess exponent.
    float getShininess() const;

    /// @brief Gets the diffuse texture (may be nullptr).
    std::shared_ptr<Texture> getDiffuseTexture() const;

    /// @brief Checks if this material has a diffuse texture.
    bool hasDiffuseTexture() const;

    /// @brief Name of this material (for debugging/identification).
    std::string name;

private:
    glm::vec3 m_diffuseColor;
    glm::vec3 m_specularColor;
    float m_shininess;
    std::shared_ptr<Texture> m_diffuseTexture;
};

} // namespace Vestige
