/// @file cascaded_shadow_map.h
/// @brief Cascaded shadow maps for directional lights with frustum-fitted cascades.
#pragma once

#include "renderer/light.h"
#include "renderer/camera.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <array>
#include <vector>

namespace Vestige
{

/// @brief Configuration for cascaded shadow mapping.
struct CascadedShadowConfig
{
    int resolution = 2048;          // Shadow map texture size per cascade (square)
    int cascadeCount = 4;           // Number of cascades (1-4)
    float shadowDistance = 150.0f;   // Maximum shadow distance from camera
    float splitLambda = 0.5f;       // Blend between logarithmic (1.0) and linear (0.0) splits
};

/// @brief Manages cascaded shadow maps for a directional light using a 2D texture array.
class CascadedShadowMap
{
public:
    /// @brief Creates a cascaded shadow map with the given configuration.
    explicit CascadedShadowMap(const CascadedShadowConfig& config = CascadedShadowConfig());
    ~CascadedShadowMap();

    // Non-copyable
    CascadedShadowMap(const CascadedShadowMap&) = delete;
    CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;

    /// @brief Updates all cascade light-space matrices from the camera frustum.
    /// @param light The directional light source.
    /// @param camera The main camera (defines the view frustum).
    /// @param aspectRatio Window aspect ratio for projection.
    void update(const DirectionalLight& light, const Camera& camera, float aspectRatio);

    /// @brief Sets tight depth bounds from SDSM analysis to optimize cascade distribution.
    /// When set, cascade splits are computed within [near, far] instead of
    /// [cameraNear, shadowDistance], giving better shadow resolution.
    /// @param near Nearest geometry distance (view-space).
    /// @param far Farthest geometry distance (view-space).
    void setDepthBounds(float near, float far);

    /// @brief Clears SDSM depth bounds, reverting to default cascade distribution.
    void clearDepthBounds();

    /// @brief Returns true if SDSM depth bounds are currently active.
    bool hasDepthBounds() const;

    /// @brief Begins rendering to a specific cascade layer.
    /// @param cascade Cascade index (0 = nearest).
    void beginCascade(int cascade);

    /// @brief Ends the current cascade render pass.
    void endCascade();

    /// @brief Binds the shadow depth texture array for sampling.
    /// @param textureUnit The texture unit to bind to.
    void bindShadowTexture(int textureUnit);

    /// @brief Gets the light-space matrix for a specific cascade.
    const glm::mat4& getLightSpaceMatrix(int cascade) const;

    /// @brief Gets the view-space split distance for a cascade's far boundary.
    float getCascadeSplit(int cascade) const;

    /// @brief Gets the number of cascades.
    int getCascadeCount() const;

    /// @brief Gets the world-space texel size for a cascade (for normal offset bias).
    float getTexelWorldSize(int cascade) const;

    /// @brief Gets the configuration (read-only).
    const CascadedShadowConfig& getConfig() const;

    // --- Static helpers (also used in tests) ---

    /// @brief Computes split distances using a logarithmic-linear blend.
    /// @param nearPlane Camera near plane distance.
    /// @param farPlane Shadow far distance.
    /// @param cascadeCount Number of cascades.
    /// @param lambda Blend factor: 0.0 = linear, 1.0 = logarithmic.
    /// @return View-space distances for each cascade's far boundary.
    static std::vector<float> computeSplitDistances(
        float nearPlane, float farPlane, int cascadeCount, float lambda);

    /// @brief Computes the 8 corners of a frustum in world space from a view-projection matrix.
    /// @param viewProjection Combined view-projection matrix for the sub-frustum.
    /// @return 8 world-space corner positions (stack-allocated, no heap).
    static std::array<glm::vec3, 8> computeFrustumCorners(
        const glm::mat4& viewProjection);

    /// @brief Computes a tight orthographic light-space matrix from frustum corners.
    /// @param light The directional light defining shadow direction.
    /// @param frustumCorners 8 world-space frustum corners.
    /// @param resolution Texture resolution for texel snapping.
    /// @return Orthographic light-space matrix (projection * view).
    static glm::mat4 computeCascadeMatrix(
        const DirectionalLight& light,
        const std::array<glm::vec3, 8>& frustumCorners,
        int resolution);

private:
    CascadedShadowConfig m_config;
    GLuint m_fbo = 0;
    GLuint m_depthTextureArray = 0;
    std::vector<glm::mat4> m_lightSpaceMatrices;
    std::vector<float> m_cascadeSplits;
    std::vector<float> m_texelWorldSizes;

    // SDSM tight depth bounds (set externally from depth buffer analysis)
    bool m_hasDepthBounds = false;
    float m_depthBoundsNear = 0.0f;
    float m_depthBoundsFar = 0.0f;
};

} // namespace Vestige
