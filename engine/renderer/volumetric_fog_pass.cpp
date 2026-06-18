// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog_pass.cpp
/// @brief Implementation of the volumetric fog GPU subsystem (slice 11.6).
#include "renderer/volumetric_fog_pass.h"

#include "core/logger.h"

#include <algorithm>

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

    // Pass 1 — inject the participating medium.
    m_inject.use();
    m_inject.setVec3("u_froxelRes", res);
    m_inject.setVec3("u_scattering", params.scattering);
    m_inject.setFloat("u_extinction", params.extinction);
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
