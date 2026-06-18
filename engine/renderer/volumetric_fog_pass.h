// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog_pass.h
/// @brief Phase 10 slice 11.6 — the volumetric fog GPU subsystem.
///
/// Owns the froxel 3D textures and drives the three compute passes
/// (inject → scatter → integrate) that produce the integrated
/// (inscatter, transmittance) volume the HDR composite samples. The froxel
/// coordinate math lives GL-free in `volumetric_fog.h`; this class is the
/// GL-resource owner that the design doc (§4.2) defers to "a later step of
/// slice 11.6".
///
/// Lifecycle mirrors the other compute subsystems (`DepthReducer`): construct,
/// `init()` once a GL context exists, `dispatch()` per frame, `destroy()`
/// before context teardown. Non-copyable (owns GL handles).
#pragma once

#include "renderer/shader.h"
#include "renderer/volumetric_fog.h"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <string>

namespace Vestige
{

/// @brief Drives the three froxel compute passes and owns their textures.
class VolumetricFogPass
{
public:
    /// @brief Per-frame inputs for the inject + scatter passes.
    ///
    /// Slice 11.6 evaluates the unshadowed sun lobe; CSM shadowing, density
    /// noise (11.8) and mist volumes (11.11) extend this later. `scattering`
    /// (sigma_s) and `extinction` (sigma_t) are the uniform base medium.
    struct FrameParams
    {
        glm::mat4 invProjection{1.0f};        ///< Clip → view, to place froxels.
        glm::vec3 sunDirViewSpace{0.0f, 1.0f, 0.0f}; ///< Normalised, toward the sun.
        glm::vec3 sunRadiance{1.0f};          ///< Linear HDR sun radiance.
        glm::vec3 ambient{0.0f};              ///< Ambient inscatter floor.
        glm::vec3 scattering{0.02f};          ///< sigma_s per channel, 1/m.
        float     extinction{0.02f};          ///< sigma_t, 1/m.
        float     anisotropy{0.0f};           ///< Henyey-Greenstein g.
    };

    VolumetricFogPass();
    ~VolumetricFogPass();

    VolumetricFogPass(const VolumetricFogPass&)            = delete;
    VolumetricFogPass& operator=(const VolumetricFogPass&) = delete;

    /// @brief Loads the three compute shaders and allocates the froxel
    ///        textures. @p shaderDir is the directory holding
    ///        `volumetric_{inject,scatter,integrate}.comp.glsl`.
    /// @return True on success; false (with a logged error) on shader-load
    ///         failure, leaving the object uninitialised.
    bool init(const std::string& shaderDir, const FroxelGridConfig& cfg = {});

    /// @brief Releases GL resources. Safe to call repeatedly / before teardown.
    void destroy();

    bool isInitialized() const { return m_initialized; }

    /// @brief Runs inject → scatter → integrate for this frame. No-op if not
    ///        initialised. Leaves the integrated volume in @ref integratedTexture.
    void dispatch(const FrameParams& params);

    /// @brief The integrated froxel volume: RGBA16F, rgb = accumulated
    ///        inscatter, a = transmittance. 0 until `init()` succeeds.
    GLuint integratedTexture() const { return m_integratedTex; }

    const FroxelGridConfig& config() const { return m_cfg; }

private:
    void createTextures();

    Shader m_inject;
    Shader m_scatter;
    Shader m_integrate;

    GLuint m_volumeTex     = 0;  ///< Scratch: inject writes, scatter rewrites in place.
    GLuint m_integratedTex = 0;  ///< Final integrated volume the composite samples.

    FroxelGridConfig m_cfg{};
    bool m_initialized = false;
};

} // namespace Vestige
