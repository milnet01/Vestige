/// @file material.h
/// @brief Surface material properties for Blinn-Phong and PBR shading.
#pragma once

#include "renderer/texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Selects the lighting model used for this material.
enum class MaterialType
{
    BLINN_PHONG,
    PBR
};

/// @brief Alpha blending mode for transparency.
enum class AlphaMode
{
    OPAQUE,
    MASK,
    BLEND
};

/// @brief Describes the visual surface properties of a mesh.
class Material
{
public:
    Material();

    // --- Material type ---

    /// @brief Sets the lighting model for this material.
    void setType(MaterialType type);

    /// @brief Gets the lighting model for this material.
    MaterialType getType() const;

    // --- Blinn-Phong properties ---

    /// @brief Sets the diffuse (base) color.
    void setDiffuseColor(const glm::vec3& color);

    /// @brief Sets the specular (highlight) color.
    void setSpecularColor(const glm::vec3& color);

    /// @brief Sets the shininess exponent (higher = sharper highlights).
    void setShininess(float shininess);

    /// @brief Gets the diffuse color.
    glm::vec3 getDiffuseColor() const;

    /// @brief Gets the specular color.
    glm::vec3 getSpecularColor() const;

    /// @brief Gets the shininess exponent.
    float getShininess() const;

    // --- Shared textures (used by both models) ---

    /// @brief Sets the diffuse/albedo texture. Pass nullptr for no texture.
    void setDiffuseTexture(std::shared_ptr<Texture> texture);

    /// @brief Gets the diffuse/albedo texture (may be nullptr).
    const std::shared_ptr<Texture>& getDiffuseTexture() const;

    /// @brief Checks if this material has a diffuse/albedo texture.
    bool hasDiffuseTexture() const;

    /// @brief Sets the normal map texture. Pass nullptr for no normal map.
    void setNormalMap(std::shared_ptr<Texture> texture);

    /// @brief Gets the normal map texture (may be nullptr).
    const std::shared_ptr<Texture>& getNormalMap() const;

    /// @brief Checks if this material has a normal map.
    bool hasNormalMap() const;

    /// @brief Sets the height map texture for parallax occlusion mapping.
    void setHeightMap(std::shared_ptr<Texture> texture);

    /// @brief Gets the height map texture (may be nullptr).
    const std::shared_ptr<Texture>& getHeightMap() const;

    /// @brief Checks if this material has a height map.
    bool hasHeightMap() const;

    /// @brief Sets the height scale for parallax occlusion mapping (clamped to [0.0, 0.2]).
    void setHeightScale(float scale);

    /// @brief Gets the height scale for parallax occlusion mapping.
    float getHeightScale() const;

    /// @brief Enables or disables parallax occlusion mapping for this material.
    void setPomEnabled(bool isEnabled);

    /// @brief Checks if parallax occlusion mapping is enabled for this material.
    bool isPomEnabled() const;

    /// @brief Enables or disables stochastic tiling (anti-tile repetition) for this material.
    void setStochasticTiling(bool isEnabled);

    /// @brief Checks if stochastic tiling is enabled for this material.
    bool isStochasticTiling() const;

    // --- PBR properties ---

    /// @brief Sets the PBR albedo (base color). Used when type is PBR.
    void setAlbedo(const glm::vec3& albedo);

    /// @brief Gets the PBR albedo.
    glm::vec3 getAlbedo() const;

    /// @brief Sets the metallic factor (clamped to [0.0, 1.0]).
    void setMetallic(float metallic);

    /// @brief Gets the metallic factor.
    float getMetallic() const;

    /// @brief Sets the roughness factor (clamped to [0.04, 1.0] to avoid GGX singularity).
    void setRoughness(float roughness);

    /// @brief Gets the roughness factor.
    float getRoughness() const;

    /// @brief Sets the ambient occlusion factor (clamped to [0.0, 1.0]).
    void setAo(float ao);

    /// @brief Gets the ambient occlusion factor.
    float getAo() const;

    /// @brief Sets the clearcoat intensity (0.0 = no clearcoat, 1.0 = full clearcoat).
    void setClearcoat(float clearcoat);

    /// @brief Gets the clearcoat intensity.
    float getClearcoat() const;

    /// @brief Sets the clearcoat roughness (clamped to [0.0, 1.0]).
    void setClearcoatRoughness(float roughness);

    /// @brief Gets the clearcoat roughness.
    float getClearcoatRoughness() const;

    /// @brief Sets the emissive color.
    void setEmissive(const glm::vec3& emissive);

    /// @brief Gets the emissive color.
    glm::vec3 getEmissive() const;

    /// @brief Sets the emissive strength multiplier (clamped to [0.0, 100.0]).
    void setEmissiveStrength(float strength);

    /// @brief Gets the emissive strength multiplier.
    float getEmissiveStrength() const;

    // --- PBR textures ---

    /// @brief Sets the metallic-roughness map (G=roughness, B=metallic).
    void setMetallicRoughnessTexture(std::shared_ptr<Texture> texture);

    /// @brief Gets the metallic-roughness map (may be nullptr).
    const std::shared_ptr<Texture>& getMetallicRoughnessTexture() const;

    /// @brief Checks if this material has a metallic-roughness map.
    bool hasMetallicRoughnessTexture() const;

    /// @brief Sets the emissive texture.
    void setEmissiveTexture(std::shared_ptr<Texture> texture);

    /// @brief Gets the emissive texture (may be nullptr).
    const std::shared_ptr<Texture>& getEmissiveTexture() const;

    /// @brief Checks if this material has an emissive texture.
    bool hasEmissiveTexture() const;

    /// @brief Sets the ambient occlusion texture.
    void setAoTexture(std::shared_ptr<Texture> texture);

    /// @brief Gets the ambient occlusion texture (may be nullptr).
    const std::shared_ptr<Texture>& getAoTexture() const;

    /// @brief Checks if this material has an ambient occlusion texture.
    bool hasAoTexture() const;

    /// @brief Sets the IBL (environment lighting) multiplier.
    /// Use < 1.0 for interior surfaces that shouldn't receive full sky irradiance.
    /// @param multiplier Clamped to [0.0, 1.0]. Default is 1.0 (full IBL).
    void setIblMultiplier(float multiplier);

    /// @brief Gets the IBL multiplier.
    float getIblMultiplier() const;

    /// @brief Sets the UV tiling scale (1.0 = default, 0.5 = bricks twice as big, 2.0 = twice as many).
    void setUvScale(float scale);

    /// @brief Gets the UV tiling scale.
    float getUvScale() const;

    // --- Transparency ---

    /// @brief Sets the alpha blending mode.
    void setAlphaMode(AlphaMode mode);

    /// @brief Gets the alpha blending mode.
    AlphaMode getAlphaMode() const;

    /// @brief Sets the alpha cutoff for MASK mode (clamped to [0.0, 1.0]).
    void setAlphaCutoff(float cutoff);

    /// @brief Gets the alpha cutoff.
    float getAlphaCutoff() const;

    /// @brief Sets whether this material is double-sided (disables face culling).
    void setDoubleSided(bool doubleSided);

    /// @brief Checks if this material is double-sided.
    bool isDoubleSided() const;

    /// @brief Sets the base color alpha (opacity) factor (clamped to [0.0, 1.0]).
    void setBaseColorAlpha(float alpha);

    /// @brief Gets the base color alpha factor.
    float getBaseColorAlpha() const;

    /// @brief Name of this material (for debugging/identification).
    std::string name;

private:
    MaterialType m_type;

    // Blinn-Phong
    glm::vec3 m_diffuseColor;
    glm::vec3 m_specularColor;
    float m_shininess;

    // Shared textures
    std::shared_ptr<Texture> m_diffuseTexture;
    std::shared_ptr<Texture> m_normalMap;
    std::shared_ptr<Texture> m_heightMap;
    float m_heightScale;
    bool m_pomEnabled;
    bool m_stochasticTiling;

    // PBR
    glm::vec3 m_albedo;
    float m_metallic;
    float m_roughness;
    float m_ao;
    float m_clearcoat;
    float m_clearcoatRoughness;
    glm::vec3 m_emissive;
    float m_emissiveStrength;
    float m_uvScale;
    float m_iblMultiplier;
    std::shared_ptr<Texture> m_metallicRoughnessTexture;
    std::shared_ptr<Texture> m_emissiveTexture;
    std::shared_ptr<Texture> m_aoTexture;

    // Transparency
    AlphaMode m_alphaMode;
    float m_alphaCutoff;
    bool m_doubleSided;
    float m_baseColorAlpha;
};

} // namespace Vestige
