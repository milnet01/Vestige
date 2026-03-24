/// @file skybox.cpp
/// @brief Skybox implementation.
#include "renderer/skybox.h"
#include "core/logger.h"

#include <stb_image.h>

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
