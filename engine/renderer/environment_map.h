/// @file environment_map.h
/// @brief IBL (Image-Based Lighting) environment map with irradiance, prefilter, and BRDF LUT.
#pragma once

#include "renderer/shader.h"

#include <glad/gl.h>

#include <string>

namespace Vestige
{

class FullscreenQuad;

/// @brief Generates and manages IBL textures from an environment cubemap.
///
/// Produces three textures for the split-sum PBR ambient approximation:
/// - Irradiance cubemap (diffuse ambient)
/// - Prefiltered cubemap with roughness mip levels (specular reflections)
/// - BRDF integration LUT (environment-independent Fresnel/geometry lookup)
class EnvironmentMap
{
public:
    EnvironmentMap();
    ~EnvironmentMap();

    // Non-copyable
    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    /// @brief Loads all IBL generation shaders.
    /// @param assetPath Base path to the assets directory.
    /// @return True if all shaders loaded successfully.
    bool initialize(const std::string& assetPath);

    /// @brief Generates all IBL maps from the current environment.
    /// @param skyboxCubemap Existing skybox cubemap texture (0 if procedural).
    /// @param hasCubemap Whether a real cubemap is provided.
    /// @param quad Fullscreen quad for BRDF LUT generation.
    /// @param skyboxShader The skybox fragment shader (for procedural capture).
    void generate(GLuint skyboxCubemap, bool hasCubemap,
                  const FullscreenQuad& quad, const Shader& skyboxShader);

    /// @brief Binds the irradiance cubemap to a texture unit.
    void bindIrradiance(int unit) const;

    /// @brief Binds the prefiltered cubemap to a texture unit.
    void bindPrefilter(int unit) const;

    /// @brief Binds the BRDF LUT to a texture unit.
    void bindBrdfLut(int unit) const;

    /// @brief Checks if all IBL maps are generated and ready.
    bool isReady() const;

    /// @brief Gets the captured environment cubemap texture ID.
    GLuint getEnvironmentCubemap() const;

    /// @brief Maximum mip level for prefiltered specular lookups.
    static constexpr int MAX_MIP_LEVELS = 5;

private:
    void createCubeVAO();
    void captureEnvironment(GLuint skyboxCubemap, bool hasCubemap,
                            const Shader& skyboxShader);
    void generateIrradiance();
    void generatePrefilter();
    void generateBrdfLut(const FullscreenQuad& quad);
    void renderCube() const;

    // IBL generation shaders
    Shader m_captureShader;
    Shader m_irradianceShader;
    Shader m_prefilterShader;
    Shader m_brdfLutShader;

    // Cube geometry for rendering to cubemap faces
    GLuint m_cubeVao = 0;
    GLuint m_cubeVbo = 0;

    // Capture FBO (reused for all cubemap face renders)
    GLuint m_captureFbo = 0;
    GLuint m_captureRbo = 0;

    // IBL textures
    GLuint m_envCubemap = 0;
    GLuint m_irradianceMap = 0;
    GLuint m_prefilterMap = 0;
    GLuint m_brdfLut = 0;

    bool m_ready = false;

    static constexpr int ENV_RESOLUTION = 512;
    static constexpr int IRRADIANCE_RESOLUTION = 32;
    static constexpr int PREFILTER_RESOLUTION = 128;
    static constexpr int BRDF_LUT_RESOLUTION = 512;
};

} // namespace Vestige
