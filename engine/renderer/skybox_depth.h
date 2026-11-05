// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skybox_depth.h
/// @brief Where the skybox sits in depth, and whether its sun is capped, for
/// the depth convention that is active now.
#pragma once

#include "renderer/skybox.h"

#include <glad/gl.h>

namespace Vestige
{

/// @brief The two skybox uniforms that depend on the depth convention.
struct SkyboxPassState
{
    float farDepth = 0.0f;     ///< z/w the skybox vertex shader writes.
    float maxRadiance = 0.0f;  ///< Sun brightness cap; 0 means no cap.
};

/// @brief Chooses the skybox depth and sun cap from the live depth test.
///
/// The engine draws reverse-Z (GL_GEQUAL, cleared to 0), so the sky goes at
/// 0, the far plane. Cubemap captures run inside ScopedForwardZ (GL_LESS,
/// cleared to 1): there the sky goes at 0.99999, just inside the cleared far
/// value so GL_LESS still passes, and the sun is capped as the IBL caps it,
/// because captures feed lighting (3D_E-0772). The choice reads the live
/// depth function, not the caller's geometry-only flag: the water reflection
/// and refraction passes are geometry-only but reverse-Z, and 0.99999 there
/// drew the sky over every surface (3D_E-0874).
inline SkyboxPassState skyboxPassStateForCurrentDepthTest()
{
    GLint depthFunc = GL_GEQUAL;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);

    SkyboxPassState state;
    if (depthFunc == GL_LESS || depthFunc == GL_LEQUAL)
    {
        state.farDepth = 0.99999f;
        state.maxRadiance = Skybox::IBL_MAX_RADIANCE;
    }
    return state;
}

} // namespace Vestige
