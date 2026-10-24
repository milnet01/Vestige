// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skybox.h
/// @brief Skybox rendering with cubemap or procedural gradient.
#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Renders a skybox using either a cubemap texture or a procedural gradient.
class Skybox
{
public:
    Skybox();
    ~Skybox();

    // Non-copyable (owns GPU resources)
    Skybox(const Skybox&) = delete;
    Skybox& operator=(const Skybox&) = delete;

    /// @brief Loads a cubemap texture from 6 face images.
    /// @param faces File paths in order: +X, -X, +Y, -Y, +Z, -Z.
    /// @return True if all faces loaded successfully.
    bool loadCubemap(const std::vector<std::string>& faces);

    /// @brief Loads an equirectangular HDR/LDR image and converts to cubemap on CPU.
    /// @param path File path to equirectangular image (.hdr, .jpg, .png, etc.).
    /// @return True if loaded and converted successfully.
    bool loadEquirectangular(const std::string& path);

    /// @brief Draws the skybox. Uses reverse-Z with GL_GEQUAL depth func.
    void draw() const;

    /// @brief Checks if a cubemap texture is loaded.
    bool hasTexture() const;

    /// @brief Gets the cubemap texture ID.
    GLuint getTextureId() const;

    /// @brief Brightest radiance the IBL capture keeps (3D_E-0772). The sun
    ///        lights the scene through its directional light, so letting the
    ///        HDRI's sun into the IBL as well would count it twice and seed
    ///        fireflies in the prefilter. The visible sky is not capped.
    static constexpr float IBL_MAX_RADIANCE = 50.0f;

private:
    void createCubeVAO();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_cubemapTexture = 0;
    bool m_hasTexture = false;
};

} // namespace Vestige
