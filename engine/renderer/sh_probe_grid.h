// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sh_probe_grid.h
/// @brief Spherical Harmonics probe grid for smooth diffuse ambient lighting.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class Renderer;
class Shader;
struct SceneRenderData;
class Camera;

/// @brief Configuration for an SH probe grid.
struct SHGridConfig
{
    glm::vec3 worldMin = glm::vec3(0.0f);   ///< Grid AABB minimum corner.
    glm::vec3 worldMax = glm::vec3(1.0f);   ///< Grid AABB maximum corner.
    glm::ivec3 resolution = glm::ivec3(4);  ///< Number of probes per axis.
};

/// @brief A 3D grid of L2 Spherical Harmonic probes for diffuse ambient lighting.
///
/// Each probe stores 9 SH coefficients per RGB channel (27 floats) encoding
/// the local irradiance. Stored in 7 RGBA16F 3D textures with hardware
/// trilinear interpolation for smooth spatial blending.
///
/// The grid replaces cubemap probes for diffuse ambient. Specular reflections
/// still use the global IBL prefilter cubemap.
class SHProbeGrid
{
public:
    SHProbeGrid();
    ~SHProbeGrid();

    // Non-copyable
    SHProbeGrid(const SHProbeGrid&) = delete;
    SHProbeGrid& operator=(const SHProbeGrid&) = delete;

    /// @brief Initializes the grid with the given configuration.
    bool initialize(const SHGridConfig& config);

    /// @brief Sets the irradiance SH coefficients for a specific probe.
    /// @param x, y, z Grid indices.
    /// @param coeffs Array of 9 vec3 (pre-convolved irradiance SH).
    void setProbeIrradiance(int x, int y, int z, const glm::vec3 coeffs[9]);

    /// @brief Gets the irradiance SH coefficients for a specific probe.
    void getProbeIrradiance(int x, int y, int z, glm::vec3 coeffs[9]) const;

    /// @brief Projects a cubemap into L2 SH coefficients (radiance).
    /// @param cubemapData Float RGB data for 6 faces, each faceSize x faceSize.
    /// @param faceSize Width/height of each cubemap face.
    /// @param outCoeffs Output: 9 vec3 SH coefficients (radiance, not yet convolved).
    static void projectCubemapToSH(const float* cubemapData, int faceSize,
                                    glm::vec3 outCoeffs[9]);

    /// @brief Applies cosine convolution to convert radiance SH → irradiance SH (in-place).
    ///
    /// **Legacy / unused after Phase 10.9 Slice 4 R7.** The shader's
    /// `evaluateSHGridIrradiance` in `scene.frag.glsl` uses the
    /// Ramamoorthi-Hanrahan Eq. 13 form, whose `c1..c5` constants
    /// already fold in the per-band cosine-lobe weights `A_ℓ`.
    /// Calling this function on the CPU before upload double-applies
    /// `A_ℓ`, leaving band-0 ambient ≈ π× over-bright. Production
    /// no longer calls it; kept declared for unit-test coverage of
    /// the helper's math.
    static void convolveRadianceToIrradiance(glm::vec3 coeffs[9]);

    /// @brief CPU pipeline that turns a captured cubemap into the SH
    /// coefficients ready to upload via `setProbeIrradiance`.
    ///
    /// This is the function `Renderer::captureSHGrid` calls per probe;
    /// extracted from the renderer so the contract is unit-testable
    /// without a GL context (and so the bug surface from R7 is
    /// pinned). The output is **radiance**-SH — the shader's
    /// Ramamoorthi-Hanrahan Eq. 13 evaluator folds in the cosine lobe
    /// `A_ℓ` weights at evaluation time.
    static void computeProbeShFromCubemap(const float* cubemapData,
                                           int faceSize,
                                           glm::vec3 outCoeffs[9]);

    /// @brief CPU mirror of `evaluateSHGridIrradiance` in
    /// `scene.frag.glsl`. Must stay byte-for-byte equivalent to the
    /// GLSL implementation; pinned by parity tests.
    ///
    /// Inputs: `coeffs` are radiance-SH (output of
    /// `computeProbeShFromCubemap`); `normal` is the surface normal
    /// in world space. Returns the diffuse-IBL convention value
    /// `irradiance / π`, matching what the shader returns and what
    /// the downstream `diffuseIBL = albedo × irradiance × ao` term
    /// expects.
    static glm::vec3 evaluateIrradianceCpu(const glm::vec3 coeffs[9],
                                            const glm::vec3& normal);

    /// @brief Uploads all probe data to GPU (7 RGBA16F 3D textures).
    void upload();

    /// @brief Binds the 7 SH textures to texture units 17-23.
    void bind() const;

    /// @brief Checks if the grid is uploaded and ready for rendering.
    bool isReady() const;

    /// @brief Checks if the grid has been initialized (CPU storage allocated).
    /// True after initialize(), even before upload().
    bool isInitialized() const;

    // --- Accessors ---
    glm::vec3 getWorldMin() const { return m_config.worldMin; }
    glm::vec3 getWorldMax() const { return m_config.worldMax; }
    glm::ivec3 getResolution() const { return m_config.resolution; }
    int getProbeCount() const;

    /// @brief Number of SH textures used.
    static constexpr int SH_TEXTURE_COUNT = 7;

    /// @brief First texture unit for SH grid textures.
    static constexpr int FIRST_TEXTURE_UNIT = 17;

    /// @brief L2 SH: 9 coefficients per probe per color channel.
    static constexpr int SH_COEFF_COUNT = 9;

private:
    int probeIndex(int x, int y, int z) const;

    SHGridConfig m_config;

    // CPU storage: m_probeData[probeIndex * 9 + coeffIndex] = vec3(R, G, B)
    std::vector<glm::vec3> m_probeData;

    // GPU: 7 RGBA16F 3D textures
    GLuint m_textures[SH_TEXTURE_COUNT] = {};

    bool m_ready = false;
};

} // namespace Vestige
