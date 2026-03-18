/// @file point_shadow_map.h
/// @brief Omnidirectional shadow mapping using a depth cubemap for point lights.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Configuration for point light shadow mapping.
struct PointShadowConfig
{
    int resolution = 1024;         // Cubemap face size (square)
    float nearPlane = 0.1f;
    float farPlane = 25.0f;
};

/// @brief Maximum number of point lights that can cast shadows simultaneously.
constexpr int MAX_POINT_SHADOW_LIGHTS = 2;

/// @brief Manages an omnidirectional shadow map (depth cubemap FBO) for a point light.
class PointShadowMap
{
public:
    /// @brief Creates a point shadow map with the given configuration.
    /// @param config Shadow map settings.
    explicit PointShadowMap(const PointShadowConfig& config = PointShadowConfig());
    ~PointShadowMap();

    // Non-copyable
    PointShadowMap(const PointShadowMap&) = delete;
    PointShadowMap& operator=(const PointShadowMap&) = delete;

    /// @brief Computes the 6 light-space matrices for a given light position.
    /// @param lightPos World-space position of the point light.
    void update(const glm::vec3& lightPos);

    /// @brief Begins rendering to a specific cubemap face.
    /// @param face Cubemap face index (0-5: +X, -X, +Y, -Y, +Z, -Z).
    void beginFace(int face);

    /// @brief Ends rendering to the current face.
    void endFace();

    /// @brief Binds the shadow cubemap texture for sampling.
    /// @param textureUnit The texture unit to bind to.
    void bindShadowTexture(int textureUnit) const;

    /// @brief Gets the light-space matrix for a specific face.
    /// @param face Cubemap face index (0-5).
    const glm::mat4& getLightSpaceMatrix(int face) const;

    /// @brief Gets the configuration.
    const PointShadowConfig& getConfig() const;

private:
    PointShadowConfig m_config;
    GLuint m_fbo = 0;
    GLuint m_depthCubemap = 0;
    glm::mat4 m_lightSpaceMatrices[6];
    glm::mat4 m_shadowProjection;
};

} // namespace Vestige
