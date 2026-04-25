// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sampler_fallback.h
/// @brief Shared 1×1 fallback textures for Mesa sampler-binding safety.
///
/// **Mesa AMD driver constraint:** every sampler uniform a shader
/// declares must have a valid texture of the matching type bound at
/// the sampler's unit at draw time, even if the shader never samples
/// from it. Failure mode: `GL_INVALID_OPERATION` at draw call. Two
/// flavours of the bug surface in this engine:
///
/// 1. **Sampler unit unbound.** A shader path conditionally uses a
///    sampler (e.g. `u_cascadeShadowMap` only when shadows are on);
///    the conditional skips both the `glBindTextureUnit` and the
///    `glUniform1i(sampler, unit)` calls. The sampler defaults to
///    unit 0 and reads whatever's there — type mismatch on Mesa
///    (sampler2DArray reading from unit 0 which has a sampler2D
///    bound).
/// 2. **Type mismatch at the same unit.** A previous pass left a
///    sampler2D bound at unit 0; the new pass declares a samplerCube
///    at unit 0 without an explicit rebind. Mesa rejects.
///
/// `Renderer` already addresses the scene path (renderer.cpp:749-768
/// binds fallback textures of every declared sampler type before each
/// frame). This header lifts those fallbacks into a shared helper so
/// foliage / water / gpu_particle / skybox paths can also bind them
/// without each subsystem needing its own private fallback set.
///
/// Tests inject a `MockTextureCreator` so the lazy-init + caching
/// contract is verifiable without a GL context. Production uses
/// `GlTextureCreator` which calls `glCreateTextures` etc.
///
/// **Memory:** four 1×1 textures total (sampler2D RGBA8 + samplerCube
/// 6-face RGBA8 + sampler2DArray 1-layer RGBA8 + sampler3D 1-voxel
/// RGBA16F). ~30 bytes of GPU memory; trivial.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief State-IO interface for `SamplerFallbackImpl`. Production
/// uses `GlTextureCreator`; tests inject a recording mock.
struct GlTextureCreator
{
    /// Create a 1×1 RGBA8 white sampler2D, return its name.
    static GLuint createSampler2D();
    /// Create a 1×1×6 RGBA8 black samplerCube (all faces black).
    static GLuint createSamplerCube();
    /// Create a 1×1×1 RGBA8 black sampler2DArray (one layer).
    static GLuint createSampler2DArray();
    /// Create a 1×1×1 RGBA16F black sampler3D (one voxel).
    static GLuint createSampler3D();
    /// Delete a texture; called from shutdown.
    static void deleteTexture(GLuint name);
};

/// @brief Lazy-initialised shared fallback textures. Templated on a
/// `TextureCreator` for test injection.
template <typename Creator = GlTextureCreator>
class SamplerFallbackImpl
{
public:
    /// Returns a sampler2D 1×1 white texture handle. Creates on first
    /// call; subsequent calls return the cached handle.
    GLuint getSampler2D()
    {
        if (m_sampler2D == 0)
        {
            m_sampler2D = Creator::createSampler2D();
        }
        return m_sampler2D;
    }

    /// Returns a samplerCube 1×1 black texture handle.
    GLuint getSamplerCube()
    {
        if (m_samplerCube == 0)
        {
            m_samplerCube = Creator::createSamplerCube();
        }
        return m_samplerCube;
    }

    /// Returns a sampler2DArray 1×1×1 black texture handle.
    GLuint getSampler2DArray()
    {
        if (m_sampler2DArray == 0)
        {
            m_sampler2DArray = Creator::createSampler2DArray();
        }
        return m_sampler2DArray;
    }

    /// Returns a sampler3D 1×1×1 black texture handle.
    GLuint getSampler3D()
    {
        if (m_sampler3D == 0)
        {
            m_sampler3D = Creator::createSampler3D();
        }
        return m_sampler3D;
    }

    /// Releases all created textures. Idempotent. Safe after no
    /// `get*` calls (no-op).
    void shutdown()
    {
        if (m_sampler2D != 0)      { Creator::deleteTexture(m_sampler2D);      m_sampler2D = 0; }
        if (m_samplerCube != 0)    { Creator::deleteTexture(m_samplerCube);    m_samplerCube = 0; }
        if (m_sampler2DArray != 0) { Creator::deleteTexture(m_sampler2DArray); m_sampler2DArray = 0; }
        if (m_sampler3D != 0)      { Creator::deleteTexture(m_sampler3D);      m_sampler3D = 0; }
    }

    SamplerFallbackImpl() = default;
    ~SamplerFallbackImpl() = default;

    SamplerFallbackImpl(const SamplerFallbackImpl&) = delete;
    SamplerFallbackImpl& operator=(const SamplerFallbackImpl&) = delete;
    SamplerFallbackImpl(SamplerFallbackImpl&&) = delete;
    SamplerFallbackImpl& operator=(SamplerFallbackImpl&&) = delete;

private:
    GLuint m_sampler2D = 0;
    GLuint m_samplerCube = 0;
    GLuint m_sampler2DArray = 0;
    GLuint m_sampler3D = 0;
};

/// @brief Production typedef.
using SamplerFallback = SamplerFallbackImpl<GlTextureCreator>;

/// @brief Process-wide singleton accessor. The first call creates the
/// instance; subsequent calls return the same. Thread-safe via
/// function-local-static (C++11 magic statics). Production sites call
/// this; tests construct their own `SamplerFallbackImpl<MockCreator>`
/// directly.
SamplerFallback& sharedSamplerFallback();

} // namespace Vestige
