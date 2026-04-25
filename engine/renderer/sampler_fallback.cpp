// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "renderer/sampler_fallback.h"

namespace Vestige
{

GLuint GlTextureCreator::createSampler2D()
{
    GLuint name = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &name);
    glTextureStorage2D(name, 1, GL_RGBA8, 1, 1);
    const unsigned char white[4] = { 255, 255, 255, 255 };
    glTextureSubImage2D(name, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    return name;
}

GLuint GlTextureCreator::createSamplerCube()
{
    GLuint name = 0;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &name);
    glTextureStorage2D(name, 1, GL_RGBA8, 1, 1);
    const unsigned char black[4] = { 0, 0, 0, 255 };
    for (int face = 0; face < 6; ++face)
    {
        glTextureSubImage3D(name, 0, 0, 0, face, 1, 1, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, black);
    }
    return name;
}

GLuint GlTextureCreator::createSampler2DArray()
{
    GLuint name = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &name);
    glTextureStorage3D(name, 1, GL_RGBA8, 1, 1, 1);
    const unsigned char black[4] = { 0, 0, 0, 255 };
    glTextureSubImage3D(name, 0, 0, 0, 0, 1, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, black);
    return name;
}

GLuint GlTextureCreator::createSampler3D()
{
    GLuint name = 0;
    glCreateTextures(GL_TEXTURE_3D, 1, &name);
    glTextureStorage3D(name, 1, GL_RGBA16F, 1, 1, 1);
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glTextureSubImage3D(name, 0, 0, 0, 0, 1, 1, 1,
                        GL_RGBA, GL_FLOAT, zero);
    return name;
}

void GlTextureCreator::deleteTexture(GLuint name)
{
    if (name != 0)
    {
        glDeleteTextures(1, &name);
    }
}

SamplerFallback& sharedSamplerFallback()
{
    static SamplerFallback s_instance;
    return s_instance;
}

} // namespace Vestige
