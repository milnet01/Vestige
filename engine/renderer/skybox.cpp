// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skybox.cpp
/// @brief Skybox implementation.
#include "renderer/skybox.h"
#include "core/logger.h"

#include <glm/glm.hpp>

#include <stb_image.h>

#include <cmath>
#include <filesystem>
#include <system_error>
#include <vector>

namespace Vestige
{

// Skybox cube vertices (36 vertices, inward-facing)
static const float SKYBOX_VERTICES[] = {
    // Back face
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,

    // Front face
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    // Left face
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,

    // Right face
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,

    // Top face
    -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,

    // Bottom face
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,
};

Skybox::Skybox()
{
    createCubeVAO();
}

Skybox::~Skybox()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
    }
    if (m_cubemapTexture != 0)
    {
        glDeleteTextures(1, &m_cubemapTexture);
    }
}

void Skybox::createCubeVAO()
{
    // Create buffer with DSA (immutable storage for static geometry)
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo, sizeof(SKYBOX_VERTICES), SKYBOX_VERTICES, 0);

    // Create VAO with DSA
    glCreateVertexArrays(1, &m_vao);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, 3 * sizeof(float));

    // Position attribute (location 0)
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_vao, 0, 0);

    Logger::debug("Skybox cube VAO created");
}

bool Skybox::loadCubemap(const std::vector<std::string>& faces)
{
    if (faces.size() != 6)
    {
        Logger::error("Cubemap requires exactly 6 face images");
        return false;
    }

    stbi_set_flip_vertically_on_load_thread(0);  // Cubemaps are not flipped

    // Load first face to determine dimensions for immutable storage allocation
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* firstData = stbi_load(faces[0].c_str(), &width, &height, &channels, 0);
    if (!firstData)
    {
        Logger::error("Failed to load cubemap face: " + faces[0]);
        return false;
    }

    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    GLenum internalFormat = (channels == 4) ? GL_RGBA8 : GL_RGB8;

    // Create cubemap with DSA (immutable storage)
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_cubemapTexture);
    glTextureStorage2D(m_cubemapTexture, 1, internalFormat, width, height);

    // Upload first face (face 0 = +X)
    glTextureSubImage3D(m_cubemapTexture, 0, 0, 0, 0, width, height, 1,
                        format, GL_UNSIGNED_BYTE, firstData);
    stbi_image_free(firstData);

    // Load and upload remaining 5 faces
    for (unsigned int i = 1; i < 6; i++)
    {
        int faceW = 0;
        int faceH = 0;
        int faceCh = 0;
        unsigned char* data = stbi_load(faces[i].c_str(), &faceW, &faceH, &faceCh, 0);

        if (data)
        {
            GLenum faceFormat = (faceCh == 4) ? GL_RGBA : GL_RGB;
            glTextureSubImage3D(m_cubemapTexture, 0, 0, 0, static_cast<GLint>(i),
                                faceW, faceH, 1, faceFormat, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else
        {
            Logger::error("Failed to load cubemap face: " + faces[i]);
            glDeleteTextures(1, &m_cubemapTexture);
            m_cubemapTexture = 0;
            return false;
        }
    }

    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    m_hasTexture = true;
    Logger::info("Cubemap loaded successfully");
    return true;
}

bool Skybox::loadEquirectangular(const std::string& path)
{
    // AUDIT M26: cap on-disk equirect size before handing off to stb_image.
    // A hostile 10 GB HDR header would otherwise drive stbi into multi-GB
    // allocations. 512 MB comfortably admits 16K²×3 floats (768 MB would
    // exceed the cap, forcing a downscaled asset — consistent with the
    // 1024² face-size ceiling below).
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const std::uintmax_t sz = fs::file_size(path, ec);
        constexpr std::uintmax_t MAX_EQUIRECT_BYTES =
            512ULL * 1024ULL * 1024ULL;
        if (!ec && sz > MAX_EQUIRECT_BYTES)
        {
            Logger::error("Skybox equirect exceeds "
                + std::to_string(MAX_EQUIRECT_BYTES) + "-byte cap: "
                + path + " (" + std::to_string(sz) + " bytes)");
            return false;
        }
    }

    // Load equirectangular image (HDR float or LDR byte)
    // No vertical flip: row 0 = top of image = north pole (sky).
    // The sampling math maps directions to UV with this convention.
    stbi_set_flip_vertically_on_load_thread(0);

    int width = 0;
    int height = 0;
    int channels = 0;
    bool isHdr = stbi_is_hdr(path.c_str());

    float* hdrData = nullptr;
    unsigned char* ldrData = nullptr;

    if (isHdr)
    {
        hdrData = stbi_loadf(path.c_str(), &width, &height, &channels, 3);
        channels = 3;  // Forced to 3 channels
    }
    else
    {
        ldrData = stbi_load(path.c_str(), &width, &height, &channels, 3);
        channels = 3;
    }

    if (!hdrData && !ldrData)
    {
        Logger::error("Failed to load equirectangular map: " + path);
        return false;
    }

    // Face size: standard convention is height/2 for 2:1 equirect aspect
    // Cap at 1024 to keep memory/time reasonable
    int faceSize = std::min(height / 2, 1024);

    Logger::info("Converting equirectangular " + std::to_string(width) + "x"
        + std::to_string(height) + (isHdr ? " HDR" : " LDR") + " to "
        + std::to_string(faceSize) + "x" + std::to_string(faceSize) + " cubemap");

    // Delete old cubemap if present
    if (m_cubemapTexture != 0)
    {
        glDeleteTextures(1, &m_cubemapTexture);
        m_cubemapTexture = 0;
    }

    // Create cubemap with HDR internal format (even for LDR input, for IBL quality)
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_cubemapTexture);
    glTextureStorage2D(m_cubemapTexture, 1, GL_RGB16F, faceSize, faceSize);

    // Convert each cubemap face
    std::vector<float> faceData(faceSize * faceSize * 3);
    constexpr float PI = 3.14159265358979323846f;

    for (int face = 0; face < 6; face++)
    {
        for (int y = 0; y < faceSize; y++)
        {
            for (int x = 0; x < faceSize; x++)
            {
                // Convert pixel to normalized [-1, 1] face coordinates
                float u = (2.0f * (static_cast<float>(x) + 0.5f)
                           / static_cast<float>(faceSize)) - 1.0f;
                float v = (2.0f * (static_cast<float>(y) + 0.5f)
                           / static_cast<float>(faceSize)) - 1.0f;

                // Map to 3D direction based on cubemap face.
                // OpenGL cubemap convention (sc/tc from spec table 8.19):
                //   +X: sc=-z, tc=-y    -X: sc=+z, tc=-y
                //   +Y: sc=+x, tc=+z    -Y: sc=+x, tc=-z
                //   +Z: sc=+x, tc=-y    -Z: sc=-x, tc=-y
                // u maps to sc direction, -v maps to tc (v increases bottom→top
                // in GL texture, but tc=-y means positive y is at the bottom).
                glm::vec3 dir;
                switch (face)
                {
                    case 0: dir = glm::vec3( 1.0f,   -v,   -u); break; // +X
                    case 1: dir = glm::vec3(-1.0f,   -v,    u); break; // -X
                    case 2: dir = glm::vec3(    u, 1.0f,    v); break; // +Y
                    case 3: dir = glm::vec3(    u, -1.0f,  -v); break; // -Y
                    case 4: dir = glm::vec3(    u,   -v, 1.0f); break; // +Z
                    case 5: dir = glm::vec3(   -u,   -v, -1.0f); break; // -Z
                }
                dir = glm::normalize(dir);

                // Convert direction to equirectangular UV
                float phi = std::atan2(dir.z, dir.x);      // [-PI, PI]
                float theta = std::asin(glm::clamp(dir.y, -1.0f, 1.0f)); // [-PI/2, PI/2]

                float eu = phi / (2.0f * PI) + 0.5f;       // [0, 1]
                // Invert: up (theta=+PI/2) → ev=0 (top of image = sky)
                //         down (theta=-PI/2) → ev=1 (bottom = ground)
                float ev = 0.5f - theta / PI;               // [0, 1]

                // Bilinear sample from equirect source
                float sx = eu * static_cast<float>(width) - 0.5f;
                float sy = ev * static_cast<float>(height) - 0.5f;

                int ix = static_cast<int>(std::floor(sx));
                int iy = static_cast<int>(std::floor(sy));
                float fx = sx - static_cast<float>(ix);
                float fy = sy - static_cast<float>(iy);

                // Horizontal wrap, vertical clamp
                ix = ((ix % width) + width) % width;
                iy = glm::clamp(iy, 0, height - 1);
                int ix1 = (ix + 1) % width;
                int iy1 = glm::clamp(iy + 1, 0, height - 1);

                glm::vec3 color;
                if (hdrData)
                {
                    auto sample = [&](int px, int py) -> glm::vec3
                    {
                        int idx = (py * width + px) * 3;
                        return glm::vec3(hdrData[idx], hdrData[idx + 1], hdrData[idx + 2]);
                    };
                    glm::vec3 c00 = sample(ix, iy);
                    glm::vec3 c10 = sample(ix1, iy);
                    glm::vec3 c01 = sample(ix, iy1);
                    glm::vec3 c11 = sample(ix1, iy1);
                    color = glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
                }
                else
                {
                    auto sample = [&](int px, int py) -> glm::vec3
                    {
                        int idx = (py * width + px) * 3;
                        return glm::vec3(ldrData[idx], ldrData[idx + 1], ldrData[idx + 2])
                               / 255.0f;
                    };
                    glm::vec3 c00 = sample(ix, iy);
                    glm::vec3 c10 = sample(ix1, iy);
                    glm::vec3 c01 = sample(ix, iy1);
                    glm::vec3 c11 = sample(ix1, iy1);
                    color = glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
                }

                // Clamp to prevent extreme HDR values / NaN from poisoning IBL
                color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(50.0f));

                int outIdx = (y * faceSize + x) * 3;
                faceData[outIdx + 0] = color.r;
                faceData[outIdx + 1] = color.g;
                faceData[outIdx + 2] = color.b;
            }
        }

        glTextureSubImage3D(m_cubemapTexture, 0, 0, 0, face,
                            faceSize, faceSize, 1, GL_RGB, GL_FLOAT, faceData.data());
    }

    if (hdrData) stbi_image_free(hdrData);
    if (ldrData) stbi_image_free(ldrData);

    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_cubemapTexture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    m_hasTexture = true;
    Logger::info("Equirectangular map loaded and converted to cubemap ("
        + std::to_string(faceSize) + "x" + std::to_string(faceSize) + ")");
    return true;
}

void Skybox::draw() const
{
    glBindVertexArray(m_vao);

    if (m_hasTexture)
    {
        glBindTextureUnit(0, m_cubemapTexture);
    }

    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

bool Skybox::hasTexture() const
{
    return m_hasTexture;
}

GLuint Skybox::getTextureId() const
{
    return m_cubemapTexture;
}

} // namespace Vestige
