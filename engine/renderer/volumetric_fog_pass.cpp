// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog_pass.cpp
/// @brief Implementation of the volumetric fog GPU subsystem (slice 11.6).
#include "renderer/volumetric_fog_pass.h"

#include "core/logger.h"

#include <glm/vec4.hpp>

#include <algorithm>
#include <array>

namespace Vestige
{

namespace
{

// Configure a froxel 3D texture: trilinear filtering (the composite samples
// between froxel centres) and clamp-to-edge on all three axes.
void configureFroxelSampling(GLuint tex)
{
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

// std430 SSBO element for a mist volume (slice 11.11): 4×vec4 = 64 B, matching
// the `FogVolumeGpu` struct in volumetric_inject.comp.glsl.
struct GpuFogVolume
{
    glm::vec4 centerShape;        // xyz center, w shape (0 Box, 1 Sphere)
    glm::vec4 halfExtentsDensity; // xyz halfExtents, w density
    glm::vec4 colourEdge;         // xyz colour, w edgeSoftness
    glm::vec4 animMisc;           // x animSpeed, yzw pad
};

GpuFogVolume packVolume(const FogVolume& v)
{
    return {glm::vec4(v.center, static_cast<float>(static_cast<int>(v.shape))),
            glm::vec4(v.halfExtents, v.density),
            glm::vec4(v.colour, v.edgeSoftness),
            glm::vec4(v.animSpeed, 0.0f, 0.0f, 0.0f)};
}

} // namespace

VolumetricFogPass::VolumetricFogPass() = default;

VolumetricFogPass::~VolumetricFogPass()
{
    destroy();
}

bool VolumetricFogPass::init(const std::string& shaderDir, const FroxelGridConfig& cfg)
{
    m_cfg = cfg;

    const bool ok =
        m_inject.loadComputeShader(shaderDir + "/volumetric_inject.comp.glsl")
        && m_scatter.loadComputeShader(shaderDir + "/volumetric_scatter.comp.glsl")
        && m_integrate.loadComputeShader(shaderDir + "/volumetric_integrate.comp.glsl");
    if (!ok)
    {
        Logger::error("VolumetricFogPass: failed to load compute shaders from " + shaderDir);
        return false;
    }

    createTextures();
    m_initialized = true;
    Logger::info("Volumetric fog initialized ("
                 + std::to_string(m_cfg.resX) + "x"
                 + std::to_string(m_cfg.resY) + "x"
                 + std::to_string(m_cfg.resZ) + " froxels)");
    return true;
}

void VolumetricFogPass::createTextures()
{
    glCreateTextures(GL_TEXTURE_3D, 1, &m_volumeTex);
    glTextureStorage3D(m_volumeTex, 1, GL_RGBA16F, m_cfg.resX, m_cfg.resY, m_cfg.resZ);
    configureFroxelSampling(m_volumeTex);

    glCreateTextures(GL_TEXTURE_3D, 1, &m_integratedTex);
    glTextureStorage3D(m_integratedTex, 1, GL_RGBA16F, m_cfg.resX, m_cfg.resY, m_cfg.resZ);
    configureFroxelSampling(m_integratedTex);

    // 1×1×1 "fully lit" depth array. The scatter shader declares a
    // sampler2DArray for the CSM; Mesa requires a complete texture bound to a
    // declared sampler's unit even on dispatches that never sample it (same
    // constraint the scene pass documents). Cleared to depth 1.0 so any
    // stray sample reads as unshadowed.
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_fallbackShadowTex);
    glTextureStorage3D(m_fallbackShadowTex, 1, GL_DEPTH_COMPONENT32F, 1, 1, 1);
    const float lit = 1.0f;
    glClearTexImage(m_fallbackShadowTex, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &lit);
    glTextureParameteri(m_fallbackShadowTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_fallbackShadowTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Mist-volume SSBO (slice 11.11): fixed MAX_FOG_VOLUMES capacity, refilled
    // per frame. Always bound (binding 1) so the declared buffer stays complete
    // even on frames with zero volumes.
    glCreateBuffers(1, &m_volumeSsbo);
    glNamedBufferData(m_volumeSsbo,
                      static_cast<GLsizeiptr>(MAX_FOG_VOLUMES * sizeof(GpuFogVolume)),
                      nullptr, GL_DYNAMIC_DRAW);
}

void VolumetricFogPass::destroy()
{
    if (m_volumeTex != 0)
    {
        glDeleteTextures(1, &m_volumeTex);
        m_volumeTex = 0;
    }
    if (m_integratedTex != 0)
    {
        glDeleteTextures(1, &m_integratedTex);
        m_integratedTex = 0;
    }
    if (m_fallbackShadowTex != 0)
    {
        glDeleteTextures(1, &m_fallbackShadowTex);
        m_fallbackShadowTex = 0;
    }
    if (m_volumeSsbo != 0)
    {
        glDeleteBuffers(1, &m_volumeSsbo);
        m_volumeSsbo = 0;
    }
    m_initialized = false;
}

void VolumetricFogPass::dispatch(const FrameParams& params)
{
    if (!m_initialized)
    {
        return;
    }

    const glm::vec3 res{static_cast<float>(m_cfg.resX),
                        static_cast<float>(m_cfg.resY),
                        static_cast<float>(m_cfg.resZ)};
    const glm::vec2 nearFar{m_cfg.near, m_cfg.far};

    const GLuint groupsX = static_cast<GLuint>((m_cfg.resX + 7) / 8);
    const GLuint groupsY = static_cast<GLuint>((m_cfg.resY + 7) / 8);
    const GLuint groupsZ = static_cast<GLuint>((m_cfg.resZ + 7) / 8);

    // Pass 1 — inject the participating medium (+ density noise, slice 11.8).
    m_inject.use();
    m_inject.setVec3("u_froxelRes", res);
    m_inject.setVec3("u_scattering", params.scattering);
    m_inject.setFloat("u_extinction", params.extinction);
    m_inject.setVec2("u_froxelNearFar", nearFar);
    m_inject.setMat4("u_invProjection", params.invProjection);
    m_inject.setMat4("u_invView", params.invView);
    m_inject.setFloat("u_elapsed", params.elapsedSeconds);
    m_inject.setInt("u_noiseEnabled", params.noise.enabled ? 1 : 0);
    m_inject.setFloat("u_noiseFreq", params.noise.frequency);
    m_inject.setFloat("u_noiseStrength", params.noise.strength);
    m_inject.setInt("u_noiseOctaves", params.noise.octaves);
    m_inject.setVec3("u_noiseWind", params.noise.windVelocity);

    // Mist / ground-fog volumes (slice 11.11). Clamp to capacity; the drop is
    // logged once when the over-cap count changes (CLAUDE.md "no silent caps")
    // rather than every frame.
    const int totalVolumes = static_cast<int>(params.volumes.size());
    const int volumeCount   = std::min(totalVolumes, MAX_FOG_VOLUMES);
    if (totalVolumes > MAX_FOG_VOLUMES)
    {
        if (m_lastOverCap != totalVolumes)
        {
            Logger::warning("VolumetricFogPass: " + std::to_string(totalVolumes)
                            + " fog volumes exceed cap " + std::to_string(MAX_FOG_VOLUMES)
                            + "; dropping " + std::to_string(totalVolumes - MAX_FOG_VOLUMES));
            m_lastOverCap = totalVolumes;
        }
    }
    else
    {
        m_lastOverCap = -1;
    }
    if (volumeCount > 0)
    {
        std::array<GpuFogVolume, MAX_FOG_VOLUMES> packed{};
        for (int i = 0; i < volumeCount; ++i)
        {
            packed[static_cast<size_t>(i)] = packVolume(params.volumes[static_cast<size_t>(i)]);
        }
        glNamedBufferSubData(m_volumeSsbo, 0,
                             static_cast<GLsizeiptr>(static_cast<size_t>(volumeCount)
                                                     * sizeof(GpuFogVolume)),
                             packed.data());
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_volumeSsbo);
    m_inject.setInt("u_volumeCount", volumeCount);

    glBindImageTexture(0, m_volumeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(groupsX, groupsY, groupsZ);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Pass 2 — single-scatter sun inscattering, written back in place.
    m_scatter.use();
    m_scatter.setVec3("u_froxelRes", res);
    m_scatter.setVec2("u_froxelNearFar", nearFar);
    m_scatter.setMat4("u_invProjection", params.invProjection);
    m_scatter.setVec3("u_sunDirViewSpace", params.sunDirViewSpace);
    m_scatter.setVec3("u_sunRadiance", params.sunRadiance);
    m_scatter.setVec3("u_ambient", params.ambient);
    m_scatter.setFloat("u_anisotropy", params.anisotropy);

    // CSM sun shadowing. With csmCascadeCount == 0 the shader takes the
    // unshadowed path and never samples, but the sampler still needs a
    // complete texture bound (Mesa) — fall back to the 1×1 lit array.
    m_scatter.setMat4("u_invView", params.invView);
    m_scatter.setInt("u_csmCascadeCount", params.csmCascadeCount);
    m_scatter.setFloat("u_csmDepthBias", params.csmDepthBias);
    const int cascades = std::min(params.csmCascadeCount, 4);
    for (int i = 0; i < cascades; ++i)
    {
        const std::string idx = std::to_string(i);
        m_scatter.setFloat("u_csmCascadeSplits[" + idx + "]",
                           params.csmCascadeSplits[static_cast<size_t>(i)]);
        m_scatter.setMat4("u_csmLightSpaceMatrices[" + idx + "]",
                          params.csmLightSpaceMatrices[static_cast<size_t>(i)]);
    }
    const GLuint shadowTex = (cascades > 0 && params.csmShadowTexture != 0)
                                 ? params.csmShadowTexture
                                 : m_fallbackShadowTex;
    glBindTextureUnit(0, shadowTex);
    m_scatter.setInt("u_csmShadowMap", 0);

    glBindImageTexture(0, m_volumeTex, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glDispatchCompute(groupsX, groupsY, groupsZ);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Pass 3 — front-to-back ray-march accumulation (one thread per tile).
    m_integrate.use();
    m_integrate.setVec3("u_froxelRes", res);
    m_integrate.setVec2("u_froxelNearFar", nearFar);
    glBindImageTexture(0, m_volumeTex, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
    glBindImageTexture(1, m_integratedTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(groupsX, groupsY, 1);

    // Result is consumed by the composite (texture fetch) and by test readback
    // (glGetTextureImage); cover both.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
                    | GL_TEXTURE_FETCH_BARRIER_BIT
                    | GL_TEXTURE_UPDATE_BARRIER_BIT);
}

} // namespace Vestige
