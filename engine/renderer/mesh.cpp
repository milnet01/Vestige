/// @file mesh.cpp
/// @brief Mesh implementation.
#include "renderer/mesh.h"
#include "core/logger.h"

#include <cmath>
#include <limits>

namespace Vestige
{

void calculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    // Zero out tangents/bitangents for accumulation
    for (auto& v : vertices)
    {
        v.tangent = glm::vec3(0.0f);
        v.bitangent = glm::vec3(0.0f);
    }

    // Accumulate per-triangle tangent/bitangent
    size_t vertCount = vertices.size();
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        // Validate index buffer — malformed meshes could have out-of-range indices
        if (indices[i] >= vertCount || indices[i + 1] >= vertCount || indices[i + 2] >= vertCount)
        {
            continue;
        }

        Vertex& v0 = vertices[indices[i]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;
        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;

        // Handle degenerate UVs — fall back to a default tangent
        if (std::abs(denom) < 1e-6f)
        {
            // Choose a tangent perpendicular to the normal
            glm::vec3 n = glm::normalize(v0.normal);
            glm::vec3 fallback = (std::abs(n.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::vec3 t = glm::normalize(glm::cross(n, fallback));
            glm::vec3 b = glm::cross(n, t);
            v0.tangent += t; v0.bitangent += b;
            v1.tangent += t; v1.bitangent += b;
            v2.tangent += t; v2.bitangent += b;
            continue;
        }

        float f = 1.0f / denom;

        glm::vec3 tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        glm::vec3 bitangent;
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        v0.tangent += tangent; v0.bitangent += bitangent;
        v1.tangent += tangent; v1.bitangent += bitangent;
        v2.tangent += tangent; v2.bitangent += bitangent;
    }

    // Gram-Schmidt orthogonalize and normalize
    for (auto& v : vertices)
    {
        glm::vec3 n = glm::normalize(v.normal);
        glm::vec3 t = v.tangent;

        // Orthogonalize: remove the component of tangent along normal
        t = glm::normalize(t - n * glm::dot(n, t));

        // Compute bitangent with correct handedness
        glm::vec3 b = glm::cross(n, t);
        if (glm::dot(b, v.bitangent) < 0.0f)
        {
            b = -b;
        }

        v.tangent = t;
        v.bitangent = b;
    }
}

Mesh::Mesh()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_indexCount(0)
{
}

Mesh::~Mesh()
{
    cleanup();
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(other.m_vao)
    , m_vbo(other.m_vbo)
    , m_ebo(other.m_ebo)
    , m_indexCount(other.m_indexCount)
    , m_localBounds(other.m_localBounds)
{
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ebo = 0;
    other.m_indexCount = 0;
    other.m_localBounds = {};
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        cleanup();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        m_localBounds = other.m_localBounds;
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_indexCount = 0;
        other.m_localBounds = {};
    }
    return *this;
}

void Mesh::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    cleanup();

    m_indexCount = static_cast<uint32_t>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(), GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
        indices.data(), GL_STATIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, position)));

    // Normal attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, normal)));

    // Color attribute (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, color)));

    // Texture coordinate attribute (location 3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, texCoord)));

    // Tangent attribute (location 4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, tangent)));

    // Bitangent attribute (location 5)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, bitangent)));

    glBindVertexArray(0);

    // Compute local-space AABB from vertex positions
    if (!vertices.empty())
    {
        glm::vec3 bmin(std::numeric_limits<float>::max());
        glm::vec3 bmax(std::numeric_limits<float>::lowest());
        for (const auto& v : vertices)
        {
            bmin = glm::min(bmin, v.position);
            bmax = glm::max(bmax, v.position);
        }
        m_localBounds = {bmin, bmax};
    }

    Logger::debug("Mesh uploaded: " + std::to_string(vertices.size()) + " vertices, "
        + std::to_string(indices.size()) + " indices");
}

void Mesh::bind() const
{
    glBindVertexArray(m_vao);
}

void Mesh::unbind() const
{
    glBindVertexArray(0);
}

uint32_t Mesh::getIndexCount() const
{
    return m_indexCount;
}

void Mesh::setupInstanceAttributes(GLuint instanceVbo) const
{
    if (m_vao == 0)
    {
        return;
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);

    // mat4 = 4 x vec4, using locations 6-9
    for (int i = 0; i < 4; i++)
    {
        GLuint loc = static_cast<GLuint>(6 + i);
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE,
            sizeof(glm::mat4),
            reinterpret_cast<void*>(static_cast<size_t>(i) * sizeof(glm::vec4)));
        glVertexAttribDivisor(loc, 1);
    }

    glBindVertexArray(0);
}

GLuint Mesh::getVao() const
{
    return m_vao;
}

const AABB& Mesh::getLocalBounds() const
{
    return m_localBounds;
}

Mesh Mesh::createCube()
{
    // Each face has its own vertices for correct normals and UVs
    glm::vec3 white(1.0f, 1.0f, 1.0f);
    std::vector<Vertex> vertices = {
        // Front face — normal: +Z
        {{-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Back face — normal: -Z
        {{ 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{-0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Top face — normal: +Y
        {{-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Bottom face — normal: -Y
        {{-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Right face — normal: +X
        {{ 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{ 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{ 0.5f,  0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Left face — normal: -X
        {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, white, {0.0f, 1.0f}, {}, {}},
    };

    std::vector<uint32_t> indices = {
        0,  1,  2,   2,  3,  0,   // Front
        4,  5,  6,   6,  7,  4,   // Back
        8,  9,  10,  10, 11, 8,   // Top
        12, 13, 14,  14, 15, 12,  // Bottom
        16, 17, 18,  18, 19, 16,  // Right
        20, 21, 22,  22, 23, 20,  // Left
    };

    calculateTangents(vertices, indices);

    Mesh cube;
    cube.upload(vertices, indices);
    return cube;
}

Mesh Mesh::createPlane(float size)
{
    glm::vec3 color = {1.0f, 1.0f, 1.0f};
    glm::vec3 normal = {0.0f, 1.0f, 0.0f};
    float uvScale = size / 5.0f;

    std::vector<Vertex> vertices = {
        {{-size, 0.0f, -size}, normal, color, {0.0f,    0.0f},    {}, {}},
        {{ size, 0.0f, -size}, normal, color, {uvScale, 0.0f},    {}, {}},
        {{ size, 0.0f,  size}, normal, color, {uvScale, uvScale}, {}, {}},
        {{-size, 0.0f,  size}, normal, color, {0.0f,    uvScale}, {}, {}},
    };

    std::vector<uint32_t> indices = {0, 2, 1, 0, 3, 2};

    calculateTangents(vertices, indices);

    Mesh plane;
    plane.upload(vertices, indices);
    return plane;
}

void Mesh::cleanup()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0)
    {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    m_indexCount = 0;
}

} // namespace Vestige
