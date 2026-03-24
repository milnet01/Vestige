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

    // Create buffers with DSA (immutable storage for static geometry)
    glCreateBuffers(1, &m_vbo);
    glNamedBufferStorage(m_vbo,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(), 0);

    glCreateBuffers(1, &m_ebo);
    glNamedBufferStorage(m_ebo,
        static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
        indices.data(), 0);

    // Create VAO with DSA
    glCreateVertexArrays(1, &m_vao);

    // Bind VBO to binding point 0, EBO to VAO
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(m_vao, m_ebo);

    // Position attribute (location 0)
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_vao, 0, 0);

    // Normal attribute (location 1)
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(m_vao, 1, 0);

    // Color attribute (location 2)
    glEnableVertexArrayAttrib(m_vao, 2);
    glVertexArrayAttribFormat(m_vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
    glVertexArrayAttribBinding(m_vao, 2, 0);

    // Texture coordinate attribute (location 3)
    glEnableVertexArrayAttrib(m_vao, 3);
    glVertexArrayAttribFormat(m_vao, 3, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
    glVertexArrayAttribBinding(m_vao, 3, 0);

    // Tangent attribute (location 4)
    glEnableVertexArrayAttrib(m_vao, 4);
    glVertexArrayAttribFormat(m_vao, 4, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, tangent));
    glVertexArrayAttribBinding(m_vao, 4, 0);

    // Bitangent attribute (location 5)
    glEnableVertexArrayAttrib(m_vao, 5);
    glVertexArrayAttribFormat(m_vao, 5, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, bitangent));
    glVertexArrayAttribBinding(m_vao, 5, 0);

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

    // Bind instance VBO to binding point 1 (binding point 0 is mesh vertex data)
    glVertexArrayVertexBuffer(m_vao, 1, instanceVbo, 0, sizeof(glm::mat4));
    glVertexArrayBindingDivisor(m_vao, 1, 1);

    // mat4 = 4 x vec4, using locations 6-9
    for (int i = 0; i < 4; i++)
    {
        GLuint loc = static_cast<GLuint>(6 + i);
        glEnableVertexArrayAttrib(m_vao, loc);
        glVertexArrayAttribFormat(m_vao, loc, 4, GL_FLOAT, GL_FALSE,
            static_cast<GLuint>(i) * static_cast<GLuint>(sizeof(glm::vec4)));
        glVertexArrayAttribBinding(m_vao, loc, 1);
    }
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

Mesh Mesh::createSphere(uint32_t sectors, uint32_t stacks)
{
    const float radius = 0.5f;
    const float PI = 3.14159265358979323846f;
    const glm::vec3 white(1.0f);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float sectorStep = 2.0f * PI / static_cast<float>(sectors);
    float stackStep = PI / static_cast<float>(stacks);

    // Vertices: (stacks+1) rings, each with (sectors+1) vertices (duplicate at UV seam)
    for (uint32_t i = 0; i <= stacks; ++i)
    {
        float stackAngle = PI / 2.0f - static_cast<float>(i) * stackStep;
        float xz = radius * std::cos(stackAngle);
        float y = radius * std::sin(stackAngle);

        for (uint32_t j = 0; j <= sectors; ++j)
        {
            float sectorAngle = static_cast<float>(j) * sectorStep;

            float x = xz * std::cos(sectorAngle);
            float z = xz * std::sin(sectorAngle);

            Vertex v;
            v.position = {x, y, z};
            v.normal = glm::normalize(glm::vec3(x, y, z));
            v.color = white;
            v.texCoord = {
                static_cast<float>(j) / static_cast<float>(sectors),
                static_cast<float>(i) / static_cast<float>(stacks)
            };
            v.tangent = {};
            v.bitangent = {};
            vertices.push_back(v);
        }
    }

    // Indices: quad strips between adjacent rings
    for (uint32_t i = 0; i < stacks; ++i)
    {
        uint32_t k1 = i * (sectors + 1);
        uint32_t k2 = k1 + (sectors + 1);

        for (uint32_t j = 0; j < sectors; ++j, ++k1, ++k2)
        {
            // Skip degenerate triangle at north pole
            if (i != 0)
            {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            // Skip degenerate triangle at south pole
            if (i != stacks - 1)
            {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    calculateTangents(vertices, indices);

    Mesh sphere;
    sphere.upload(vertices, indices);
    return sphere;
}

Mesh Mesh::createCylinder(uint32_t sectors)
{
    const float radius = 0.5f;
    const float halfHeight = 0.5f;
    const float PI = 3.14159265358979323846f;
    const glm::vec3 white(1.0f);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float sectorStep = 2.0f * PI / static_cast<float>(sectors);

    // --- Side wall ---
    uint32_t sideBaseIdx = 0;
    for (uint32_t j = 0; j <= sectors; ++j)
    {
        float angle = static_cast<float>(j) * sectorStep;
        float cs = std::cos(angle);
        float sn = std::sin(angle);
        float x = radius * cs;
        float z = radius * sn;
        float u = static_cast<float>(j) / static_cast<float>(sectors);
        glm::vec3 normal = glm::normalize(glm::vec3(cs, 0.0f, sn));

        // Bottom ring vertex
        vertices.push_back({{x, -halfHeight, z}, normal, white, {u, 0.0f}, {}, {}});
        // Top ring vertex
        vertices.push_back({{x, halfHeight, z}, normal, white, {u, 1.0f}, {}, {}});
    }

    for (uint32_t j = 0; j < sectors; ++j)
    {
        uint32_t bl = sideBaseIdx + j * 2;
        uint32_t tl = bl + 1;
        uint32_t br = bl + 2;
        uint32_t tr = bl + 3;

        indices.push_back(bl);
        indices.push_back(tl);
        indices.push_back(br);

        indices.push_back(tl);
        indices.push_back(tr);
        indices.push_back(br);
    }

    // --- Top cap (normal +Y) ---
    uint32_t topCenterIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, halfHeight, 0.0f}, {0.0f, 1.0f, 0.0f}, white, {0.5f, 0.5f}, {}, {}});

    uint32_t topRingIdx = static_cast<uint32_t>(vertices.size());
    for (uint32_t j = 0; j < sectors; ++j)
    {
        float angle = static_cast<float>(j) * sectorStep;
        float cs = std::cos(angle);
        float sn = std::sin(angle);
        float x = radius * cs;
        float z = radius * sn;

        vertices.push_back({{x, halfHeight, z}, {0.0f, 1.0f, 0.0f}, white,
            {cs * 0.5f + 0.5f, sn * 0.5f + 0.5f}, {}, {}});
    }

    for (uint32_t j = 0; j < sectors; ++j)
    {
        uint32_t next = (j + 1) % sectors;
        indices.push_back(topCenterIdx);
        indices.push_back(topRingIdx + next);
        indices.push_back(topRingIdx + j);
    }

    // --- Bottom cap (normal -Y) ---
    uint32_t botCenterIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.0f, -1.0f, 0.0f}, white, {0.5f, 0.5f}, {}, {}});

    uint32_t botRingIdx = static_cast<uint32_t>(vertices.size());
    for (uint32_t j = 0; j < sectors; ++j)
    {
        float angle = static_cast<float>(j) * sectorStep;
        float cs = std::cos(angle);
        float sn = std::sin(angle);
        float x = radius * cs;
        float z = radius * sn;

        vertices.push_back({{x, -halfHeight, z}, {0.0f, -1.0f, 0.0f}, white,
            {cs * 0.5f + 0.5f, 0.5f - sn * 0.5f}, {}, {}});
    }

    for (uint32_t j = 0; j < sectors; ++j)
    {
        uint32_t next = (j + 1) % sectors;
        indices.push_back(botCenterIdx);
        indices.push_back(botRingIdx + j);
        indices.push_back(botRingIdx + next);
    }

    calculateTangents(vertices, indices);

    Mesh cylinder;
    cylinder.upload(vertices, indices);
    return cylinder;
}

Mesh Mesh::createCone(uint32_t sectors, uint32_t stacks)
{
    const float baseRadius = 0.5f;
    const float height = 1.0f;
    const float halfHeight = 0.5f;
    const float PI = 3.14159265358979323846f;
    const glm::vec3 white(1.0f);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float sectorStep = 2.0f * PI / static_cast<float>(sectors);

    // Normal tilt for cone side: the outward direction makes an angle with the Y axis
    float slantLen = std::sqrt(baseRadius * baseRadius + height * height);
    float ny = baseRadius / slantLen;
    float nxz = height / slantLen;

    // --- Side surface (stacked rings from base to apex) ---
    uint32_t sideBaseIdx = 0;
    for (uint32_t i = 0; i <= stacks; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(stacks);
        float ringRadius = baseRadius * (1.0f - t);
        float y = -halfHeight + t * height;

        for (uint32_t j = 0; j <= sectors; ++j)
        {
            float angle = static_cast<float>(j) * sectorStep;
            float cs = std::cos(angle);
            float sn = std::sin(angle);

            float x = ringRadius * cs;
            float z = ringRadius * sn;
            glm::vec3 normal = glm::normalize(glm::vec3(nxz * cs, ny, nxz * sn));

            float u = static_cast<float>(j) / static_cast<float>(sectors);
            float v = t;

            vertices.push_back({{x, y, z}, normal, white, {u, v}, {}, {}});
        }
    }

    // Side indices: quad strips between adjacent rings
    for (uint32_t i = 0; i < stacks; ++i)
    {
        uint32_t k1 = sideBaseIdx + i * (sectors + 1);
        uint32_t k2 = k1 + (sectors + 1);

        for (uint32_t j = 0; j < sectors; ++j, ++k1, ++k2)
        {
            // Lower triangle (always valid)
            indices.push_back(k1);
            indices.push_back(k2);
            indices.push_back(k1 + 1);

            // Upper triangle (skip at apex ring where positions collapse)
            if (i != stacks - 1)
            {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    // --- Base cap (normal -Y) ---
    uint32_t baseCenterIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.0f, -1.0f, 0.0f}, white, {0.5f, 0.5f}, {}, {}});

    uint32_t baseRingIdx = static_cast<uint32_t>(vertices.size());
    for (uint32_t j = 0; j < sectors; ++j)
    {
        float angle = static_cast<float>(j) * sectorStep;
        float cs = std::cos(angle);
        float sn = std::sin(angle);
        float x = baseRadius * cs;
        float z = baseRadius * sn;

        vertices.push_back({{x, -halfHeight, z}, {0.0f, -1.0f, 0.0f}, white,
            {cs * 0.5f + 0.5f, 0.5f - sn * 0.5f}, {}, {}});
    }

    for (uint32_t j = 0; j < sectors; ++j)
    {
        uint32_t next = (j + 1) % sectors;
        indices.push_back(baseCenterIdx);
        indices.push_back(baseRingIdx + j);
        indices.push_back(baseRingIdx + next);
    }

    calculateTangents(vertices, indices);

    Mesh cone;
    cone.upload(vertices, indices);
    return cone;
}

Mesh Mesh::createWedge()
{
    // Triangular prism (ramp) — 5 faces, 1×1×1 bounding box centered at origin.
    // Slope runs from top-back edge (Y=0.5, Z=-0.5) down to bottom-front edge (Y=-0.5, Z=0.5).
    glm::vec3 white(1.0f);

    // Slope normal: cross product of two edges on the slope face
    glm::vec3 slopeNormal = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));

    std::vector<Vertex> vertices = {
        // Bottom face — normal: -Y (4 vertices)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Back face — normal: -Z (4 vertices)
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, white, {1.0f, 0.0f}, {}, {}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, white, {0.0f, 1.0f}, {}, {}},

        // Slope face — normal: (0, 0.707, 0.707) (4 vertices)
        {{-0.5f, -0.5f,  0.5f}, slopeNormal, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f, -0.5f,  0.5f}, slopeNormal, white, {1.0f, 0.0f}, {}, {}},
        {{ 0.5f,  0.5f, -0.5f}, slopeNormal, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f,  0.5f, -0.5f}, slopeNormal, white, {0.0f, 1.0f}, {}, {}},

        // Left triangle — normal: -X (3 vertices)
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, white, {1.0f, 1.0f}, {}, {}},
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, white, {1.0f, 0.0f}, {}, {}},

        // Right triangle — normal: +X (3 vertices)
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, white, {0.0f, 0.0f}, {}, {}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, white, {0.0f, 1.0f}, {}, {}},
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, white, {1.0f, 0.0f}, {}, {}},
    };

    std::vector<uint32_t> indices = {
        0,  1,  2,   2,  3,  0,   // Bottom
        4,  5,  6,   6,  7,  4,   // Back
        8,  9,  10,  10, 11, 8,   // Slope
        12, 13, 14,                // Left triangle
        15, 16, 17,                // Right triangle
    };

    calculateTangents(vertices, indices);

    Mesh wedge;
    wedge.upload(vertices, indices);
    return wedge;
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
