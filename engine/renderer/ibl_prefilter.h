// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ibl_prefilter.h
/// @brief Shared mip-chain prefilter loop for IBL environment / light probes.
///
/// EnvironmentMap and LightProbe both render the same convolved GGX mip chain
/// into a prefiltered cubemap. This helper captures the loop body so both
/// call sites stay identical — caller supplies the shader + draw callback
/// and the helper drives the mip×face iteration, per-mip RBO resize, viewport
/// change, and FBO layer attachment.
#pragma once

#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <functional>

namespace Vestige
{

/// @brief Inputs required by the prefilter loop. `drawCube` is the per-face
/// draw call; it runs after `glClear` and with the face layer already bound.
struct IblPrefilterParams
{
    const Shader* shader = nullptr;         ///< Bound before the loop starts.
    GLuint captureFbo = 0;
    GLuint captureRbo = 0;                  ///< Resized per-mip.
    GLuint prefilterMap = 0;                ///< The destination cubemap.
    const glm::mat4* captureViews = nullptr; ///< 6 face view matrices.
    int resolution = 0;                     ///< Mip 0 edge length.
    int mipLevels = 0;
    std::function<void()> drawCube;         ///< Per-face draw call.
};

/// @brief Runs the prefilter loop described above.
///
/// Preconditions:
///  - `params.shader` is already `use()`-d and has `u_projection`,
///    `u_envResolution`, `u_environmentMap` set.
///  - Caller has already bound the source cubemap to texture unit 0.
///
/// Postconditions:
///  - FBO 0 is bound, the original viewport is restored, and `GL_CULL_FACE`
///    is re-enabled (the helper disables it for the back-face cube draws).
inline void runIblPrefilterLoop(const IblPrefilterParams& params)
{
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, params.captureFbo);
    glDisable(GL_CULL_FACE);

    for (int mip = 0; mip < params.mipLevels; mip++)
    {
        int mipWidth = params.resolution >> mip;
        int mipHeight = params.resolution >> mip;
        if (mipWidth < 1) mipWidth = 1;
        if (mipHeight < 1) mipHeight = 1;

        glNamedRenderbufferStorage(params.captureRbo, GL_DEPTH_COMPONENT24,
                                   mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = static_cast<float>(mip)
                        / static_cast<float>(params.mipLevels - 1);
        params.shader->setFloat("u_roughness", roughness);

        for (int face = 0; face < 6; face++)
        {
            params.shader->setMat4("u_view", params.captureViews[face]);
            glNamedFramebufferTextureLayer(params.captureFbo, GL_COLOR_ATTACHMENT0,
                                           params.prefilterMap, mip, face);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (params.drawCube) params.drawCube();
        }
    }

    glEnable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

} // namespace Vestige
