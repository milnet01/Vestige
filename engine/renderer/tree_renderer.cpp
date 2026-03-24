/// @file tree_renderer.cpp
/// @brief TreeRenderer implementation — placeholder trees with LOD and billboard crossfade.
#include "renderer/tree_renderer.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Vestige
{

TreeRenderer::~TreeRenderer()
{
    shutdown();
}

bool TreeRenderer::init(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    // Reuse the foliage shader for simple unlit tree mesh rendering
    if (!m_meshShader.loadFromFiles(assetPath + "/shaders/tree_mesh.vert.glsl",
                                     assetPath + "/shaders/tree_mesh.frag.glsl"))
    {
        Logger::error("Failed to load tree mesh shaders");
        return false;
    }

    if (!m_billboardShader.loadFromFiles(assetPath + "/shaders/tree_billboard.vert.glsl",
                                          assetPath + "/shaders/tree_billboard.frag.glsl"))
    {
        Logger::error("Failed to load tree billboard shaders");
        return false;
    }

    createPlaceholderTree();
    createBillboardQuad();
    generateBillboardTexture();

    m_initialized = true;
    Logger::info("Tree renderer initialized");
    return true;
}

void TreeRenderer::shutdown()
{
    if (m_treeVao != 0) { glDeleteVertexArrays(1, &m_treeVao); m_treeVao = 0; }
    if (m_treeVbo != 0) { glDeleteBuffers(1, &m_treeVbo); m_treeVbo = 0; }
    if (m_treeEbo != 0) { glDeleteBuffers(1, &m_treeEbo); m_treeEbo = 0; }
    if (m_treeInstanceVbo != 0) { glDeleteBuffers(1, &m_treeInstanceVbo); m_treeInstanceVbo = 0; }
    if (m_billboardVao != 0) { glDeleteVertexArrays(1, &m_billboardVao); m_billboardVao = 0; }
    if (m_billboardVbo != 0) { glDeleteBuffers(1, &m_billboardVbo); m_billboardVbo = 0; }
    if (m_billboardInstanceVbo != 0) { glDeleteBuffers(1, &m_billboardInstanceVbo); m_billboardInstanceVbo = 0; }
    if (m_billboardTexture != 0) { glDeleteTextures(1, &m_billboardTexture); m_billboardTexture = 0; }
    m_treeInstanceCapacity = 0;
    m_billboardInstanceCapacity = 0;
    m_meshShader.destroy();
    m_billboardShader.destroy();
    m_initialized = false;
}

void TreeRenderer::render(
    const std::vector<const FoliageChunk*>& chunks,
    const Camera& camera,
    const glm::mat4& viewProjection,
    float time)
{
    if (!m_initialized || chunks.empty())
    {
        return;
    }

    glm::vec3 camPos = camera.getPosition();

    // Sort trees into LOD buckets
    m_lod0Instances.clear();
    m_lod1Instances.clear();

    for (const FoliageChunk* chunk : chunks)
    {
        const auto& trees = chunk->getTrees();
        for (const auto& tree : trees)
        {
            float dist = glm::distance(camPos, tree.position);
            if (dist > maxDistance)
            {
                continue;
            }

            TreeDrawInstance inst;
            inst.position = tree.position;
            inst.rotation = tree.rotation;
            inst.scale = tree.scale;

            if (dist < lodDistance)
            {
                inst.alpha = 1.0f;
                m_lod0Instances.push_back(inst);
            }
            else if (dist < lodDistance + fadeRange)
            {
                // Crossfade zone: both LODs rendered with complementary alpha
                float t = (dist - lodDistance) / fadeRange;
                inst.alpha = 1.0f - t;
                m_lod0Instances.push_back(inst);
                inst.alpha = t;
                m_lod1Instances.push_back(inst);
            }
            else
            {
                inst.alpha = 1.0f;
                m_lod1Instances.push_back(inst);
            }
        }
    }

    // --- Render LOD0 (mesh) ---
    if (!m_lod0Instances.empty())
    {
        // Upload instances
        int count = static_cast<int>(m_lod0Instances.size());
        glBindBuffer(GL_ARRAY_BUFFER, m_treeInstanceVbo);
        if (count > m_treeInstanceCapacity)
        {
            m_treeInstanceCapacity = count + count / 4 + 64;
            glBufferData(GL_ARRAY_BUFFER,
                         m_treeInstanceCapacity * static_cast<GLsizeiptr>(sizeof(TreeDrawInstance)),
                         nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                         count * static_cast<GLsizeiptr>(sizeof(TreeDrawInstance)),
                         m_lod0Instances.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        m_meshShader.use();
        m_meshShader.setMat4("u_viewProjection", viewProjection);
        m_meshShader.setFloat("u_time", time);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glBindVertexArray(m_treeVao);
        glDrawElementsInstanced(GL_TRIANGLES, m_treeIndexCount, GL_UNSIGNED_INT,
                                nullptr, count);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    // --- Render LOD1 (billboard) ---
    if (!m_lod1Instances.empty())
    {
        int count = static_cast<int>(m_lod1Instances.size());
        glBindBuffer(GL_ARRAY_BUFFER, m_billboardInstanceVbo);
        if (count > m_billboardInstanceCapacity)
        {
            m_billboardInstanceCapacity = count + count / 4 + 64;
            glBufferData(GL_ARRAY_BUFFER,
                         m_billboardInstanceCapacity * static_cast<GLsizeiptr>(sizeof(TreeDrawInstance)),
                         nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                         count * static_cast<GLsizeiptr>(sizeof(TreeDrawInstance)),
                         m_lod1Instances.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        m_billboardShader.use();
        m_billboardShader.setMat4("u_viewProjection", viewProjection);
        // Extract camera right from view matrix (row 0)
        glm::mat4 view = camera.getViewMatrix();
        glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
        m_billboardShader.setVec3("u_cameraRight", cameraRight);
        m_billboardShader.setVec3("u_cameraUp", glm::vec3(0.0f, 1.0f, 0.0f));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_billboardTexture);
        m_billboardShader.setInt("u_texture", 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(m_billboardVao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
        glBindVertexArray(0);

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }
}

void TreeRenderer::createPlaceholderTree()
{
    // Simple tree: cylinder trunk + cone crown
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const int segments = 8;

    // Trunk (cylinder, height 0-2m, radius 0.15m)
    const float trunkRadius = 0.15f;
    const float trunkHeight = 2.0f;
    const glm::vec3 trunkColor(0.4f, 0.25f, 0.1f);

    for (int i = 0; i <= segments; ++i)
    {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        float x = std::cos(angle) * trunkRadius;
        float z = std::sin(angle) * trunkRadius;
        vertices.push_back({{x, 0.0f, z}, trunkColor});
        vertices.push_back({{x, trunkHeight, z}, trunkColor});
    }

    for (int i = 0; i < segments; ++i)
    {
        uint32_t base = static_cast<uint32_t>(i * 2);
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }

    // Crown (cone, height 2-5m, radius 1.5m)
    uint32_t crownBase = static_cast<uint32_t>(vertices.size());
    const float crownRadius = 1.5f;
    const float crownBottom = trunkHeight - 0.3f;
    const float crownTop = 5.0f;
    const glm::vec3 crownColor(0.15f, 0.45f, 0.1f);

    // Crown tip
    vertices.push_back({{0.0f, crownTop, 0.0f}, crownColor});

    // Crown ring
    for (int i = 0; i <= segments; ++i)
    {
        float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        float x = std::cos(angle) * crownRadius;
        float z = std::sin(angle) * crownRadius;
        vertices.push_back({{x, crownBottom, z}, crownColor * 0.8f});
    }

    // Crown triangles
    for (int i = 0; i < segments; ++i)
    {
        indices.push_back(crownBase);  // tip
        indices.push_back(crownBase + 1 + static_cast<uint32_t>(i));
        indices.push_back(crownBase + 2 + static_cast<uint32_t>(i));
    }

    m_treeIndexCount = static_cast<int>(indices.size());

    // Create VAO
    glGenVertexArrays(1, &m_treeVao);
    glGenBuffers(1, &m_treeVbo);
    glGenBuffers(1, &m_treeEbo);
    glGenBuffers(1, &m_treeInstanceVbo);

    glBindVertexArray(m_treeVao);

    // Mesh data
    glBindBuffer(GL_ARRAY_BUFFER, m_treeVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_treeEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    // Vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));

    // Instance attributes
    glBindBuffer(GL_ARRAY_BUFFER, m_treeInstanceVbo);

    // i_position (location 3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(0));
    glVertexAttribDivisor(3, 1);

    // i_rotation (location 4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(12));
    glVertexAttribDivisor(4, 1);

    // i_scale (location 5)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(16));
    glVertexAttribDivisor(5, 1);

    // i_alpha (location 6)
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(20));
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
}

void TreeRenderer::createBillboardQuad()
{
    // Billboard quad: 2 triangles in local XY space, centered at base
    struct BillboardVertex
    {
        glm::vec2 offset;   // Offset from center (-1..1)
        glm::vec2 texCoord;
    };

    BillboardVertex vertices[] = {
        {{-1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, 2.0f}, {1.0f, 1.0f}},
        {{-1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, 2.0f}, {1.0f, 1.0f}},
        {{-1.0f, 2.0f}, {0.0f, 1.0f}},
    };

    glGenVertexArrays(1, &m_billboardVao);
    glGenBuffers(1, &m_billboardVbo);
    glGenBuffers(1, &m_billboardInstanceVbo);

    glBindVertexArray(m_billboardVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_billboardVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Quad vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex),
                          reinterpret_cast<void*>(8));

    // Instance attributes (same layout as TreeDrawInstance)
    glBindBuffer(GL_ARRAY_BUFFER, m_billboardInstanceVbo);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(0));
    glVertexAttribDivisor(3, 1);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(12));
    glVertexAttribDivisor(4, 1);

    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(16));
    glVertexAttribDivisor(5, 1);

    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(TreeDrawInstance),
                          reinterpret_cast<void*>(20));
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
}

void TreeRenderer::generateBillboardTexture()
{
    // Generate a simple procedural tree billboard (64x128 RGBA)
    const int width = 64;
    const int height = 128;
    std::vector<uint8_t> pixels(width * height * 4, 0);

    for (int y = 0; y < height; ++y)
    {
        float v = static_cast<float>(y) / static_cast<float>(height - 1);  // 0=bottom, 1=top
        for (int x = 0; x < width; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(width - 1);
            float cx = u - 0.5f;

            int idx = (y * width + x) * 4;

            // Trunk (bottom 40%, narrow)
            if (v < 0.4f && std::abs(cx) < 0.08f)
            {
                pixels[idx + 0] = 100;  // R
                pixels[idx + 1] = 65;   // G
                pixels[idx + 2] = 25;   // B
                pixels[idx + 3] = 255;  // A
            }
            // Crown (top 70%, circular blob)
            else if (v >= 0.25f)
            {
                float crownCy = 0.65f;
                float dy = v - crownCy;
                float crownRadius = 0.35f - 0.15f * std::abs(dy) / 0.35f;
                if (std::abs(cx) < crownRadius && std::abs(dy) < 0.35f)
                {
                    float dist = std::sqrt(cx * cx + dy * dy);
                    float shade = 1.0f - dist * 1.5f;
                    shade = std::max(0.4f, std::min(1.0f, shade));
                    pixels[idx + 0] = static_cast<uint8_t>(40 * shade);
                    pixels[idx + 1] = static_cast<uint8_t>(120 * shade);
                    pixels[idx + 2] = static_cast<uint8_t>(30 * shade);
                    pixels[idx + 3] = 255;
                }
            }
        }
    }

    glGenTextures(1, &m_billboardTexture);
    glBindTexture(GL_TEXTURE_2D, m_billboardTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace Vestige
