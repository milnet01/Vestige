/// @file foliage_renderer.cpp
/// @brief FoliageRenderer implementation — instanced star-mesh grass with wind.
#include "renderer/foliage_renderer.h"
#include "renderer/cascaded_shadow_map.h"
#include "core/logger.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>

namespace Vestige
{

FoliageRenderer::~FoliageRenderer()
{
    shutdown();
}

bool FoliageRenderer::init(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    // Load foliage shaders
    if (!m_shader.loadFromFiles(assetPath + "/shaders/foliage.vert.glsl",
                                assetPath + "/shaders/foliage.frag.glsl"))
    {
        Logger::error("Failed to load foliage shaders");
        return false;
    }

    // Load foliage shadow shaders
    if (!m_shadowShader.loadFromFiles(assetPath + "/shaders/foliage_shadow.vert.glsl",
                                      assetPath + "/shaders/foliage_shadow.frag.glsl"))
    {
        Logger::warning("Failed to load foliage shadow shaders — grass won't cast shadows");
    }

    createStarMesh();
    generateDefaultTexture();

    m_initialized = true;
    Logger::info("Foliage renderer initialized");
    return true;
}

void FoliageRenderer::shutdown()
{
    if (m_starVao != 0)
    {
        glDeleteVertexArrays(1, &m_starVao);
        m_starVao = 0;
    }
    if (m_starVbo != 0)
    {
        glDeleteBuffers(1, &m_starVbo);
        m_starVbo = 0;
    }
    if (m_instanceVbo != 0)
    {
        glDeleteBuffers(1, &m_instanceVbo);
        m_instanceVbo = 0;
    }
    if (m_defaultTexture != 0)
    {
        glDeleteTextures(1, &m_defaultTexture);
        m_defaultTexture = 0;
    }
    m_instanceCapacity = 0;
    m_shader.destroy();
    m_shadowShader.destroy();
    m_initialized = false;
}

void FoliageRenderer::render(
    const std::vector<const FoliageChunk*>& chunks,
    const Camera& camera,
    const glm::mat4& viewProjection,
    float time,
    float maxDistance,
    CascadedShadowMap* csm,
    const DirectionalLight* dirLight)
{
    if (!m_initialized || chunks.empty())
    {
        return;
    }

    // Collect all visible foliage instances from all visible chunks
    m_visibleInstances.clear();

    glm::vec3 camPos = camera.getPosition();
    float maxDistSq = maxDistance * maxDistance;

    for (const FoliageChunk* chunk : chunks)
    {
        // Check chunk-level distance (rough cull)
        AABB bounds = chunk->getBounds();
        glm::vec3 chunkCenter = bounds.getCenter();
        glm::vec3 diff = chunkCenter - camPos;
        // Use XZ distance only (foliage is mostly ground-level)
        float chunkDistSq = diff.x * diff.x + diff.z * diff.z;
        float chunkRadius = FoliageChunk::CHUNK_SIZE * 0.707f;  // Diagonal half
        if (chunkDistSq > (maxDistance + chunkRadius) * (maxDistance + chunkRadius))
        {
            continue;
        }

        for (uint32_t typeId : chunk->getFoliageTypeIds())
        {
            const auto& instances = chunk->getFoliage(typeId);
            for (const auto& inst : instances)
            {
                glm::vec3 d = inst.position - camPos;
                float distSq = d.x * d.x + d.z * d.z;
                if (distSq <= maxDistSq)
                {
                    m_visibleInstances.push_back(inst);
                }
            }
        }
    }

    if (m_visibleInstances.empty())
    {
        return;
    }

    // Upload instances to GPU
    uploadInstances(m_visibleInstances);

    // Render
    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);
    m_shader.setMat4("u_view", camera.getViewMatrix());
    m_shader.setFloat("u_time", time);
    m_shader.setVec3("u_windDirection", glm::normalize(windDirection));
    m_shader.setFloat("u_windAmplitude", windAmplitude);
    m_shader.setFloat("u_windFrequency", windFrequency);
    m_shader.setFloat("u_maxDistance", maxDistance);
    m_shader.setVec3("u_cameraPos", camPos);

    // Lighting uniforms
    bool hasShadows = (csm != nullptr && dirLight != nullptr);
    m_shader.setBool("u_hasShadows", hasShadows);

    if (dirLight)
    {
        m_shader.setVec3("u_lightDirection", dirLight->direction);
        m_shader.setVec3("u_lightColor", dirLight->diffuse);
        m_shader.setVec3("u_ambientColor", dirLight->ambient);
        m_shader.setBool("u_hasDirectionalLight", true);
    }
    else
    {
        m_shader.setBool("u_hasDirectionalLight", false);
    }

    // Shadow map uniforms
    if (hasShadows)
    {
        csm->bindShadowTexture(3);
        m_shader.setInt("u_cascadeShadowMap", 3);

        int cascadeCount = csm->getCascadeCount();
        m_shader.setInt("u_cascadeCount", cascadeCount);
        for (int i = 0; i < cascadeCount; ++i)
        {
            std::string idx = std::to_string(i);
            m_shader.setFloat("u_cascadeSplits[" + idx + "]",
                              csm->getCascadeSplit(i));
            m_shader.setMat4("u_cascadeLightSpaceMatrices[" + idx + "]",
                             csm->getLightSpaceMatrix(i));
        }
    }

    // Bind grass texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
    m_shader.setInt("u_texture", 0);

    // Draw instanced
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);  // Star mesh visible from all angles

    glBindVertexArray(m_starVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 18,
                          static_cast<GLsizei>(m_visibleInstances.size()));
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void FoliageRenderer::renderShadow(
    const std::vector<const FoliageChunk*>& chunks,
    const Camera& camera,
    const glm::mat4& lightSpaceMatrix,
    float time)
{
    if (!m_initialized || !m_shadowShader.getId() || chunks.empty())
    {
        return;
    }

    // Collect nearby grass instances only (shadow casting is expensive)
    m_visibleInstances.clear();

    glm::vec3 camPos = camera.getPosition();
    float maxDistSq = shadowMaxDistance * shadowMaxDistance;

    for (const FoliageChunk* chunk : chunks)
    {
        AABB bounds = chunk->getBounds();
        glm::vec3 chunkCenter = bounds.getCenter();
        glm::vec3 diff = chunkCenter - camPos;
        float chunkDistSq = diff.x * diff.x + diff.z * diff.z;
        float chunkRadius = FoliageChunk::CHUNK_SIZE * 0.707f;
        if (chunkDistSq > (shadowMaxDistance + chunkRadius) * (shadowMaxDistance + chunkRadius))
        {
            continue;
        }

        for (uint32_t typeId : chunk->getFoliageTypeIds())
        {
            const auto& instances = chunk->getFoliage(typeId);
            for (const auto& inst : instances)
            {
                glm::vec3 d = inst.position - camPos;
                float distSq = d.x * d.x + d.z * d.z;
                if (distSq <= maxDistSq)
                {
                    m_visibleInstances.push_back(inst);
                }
            }
        }
    }

    if (m_visibleInstances.empty())
    {
        return;
    }

    uploadInstances(m_visibleInstances);

    m_shadowShader.use();
    m_shadowShader.setMat4("u_lightSpaceMatrix", lightSpaceMatrix);
    m_shadowShader.setFloat("u_time", time);
    m_shadowShader.setVec3("u_windDirection", glm::normalize(windDirection));
    m_shadowShader.setFloat("u_windAmplitude", windAmplitude);
    m_shadowShader.setFloat("u_windFrequency", windFrequency);

    // Bind grass texture for alpha testing
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
    m_shadowShader.setInt("u_texture", 0);

    // Two-sided rendering for grass shadow casting
    glDisable(GL_CULL_FACE);

    glBindVertexArray(m_starVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 18,
                          static_cast<GLsizei>(m_visibleInstances.size()));
    glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
}

void FoliageRenderer::createStarMesh()
{
    // A 3-quad star mesh: 3 intersecting vertical quads at 60-degree intervals.
    // Each quad: 2 triangles, 6 vertices. Total: 18 vertices.
    // Blade dimensions: ~0.15m wide, ~0.4m tall.
    const float halfWidth = 0.075f;
    const float height = 0.4f;

    struct Vertex
    {
        glm::vec3 position;
        glm::vec2 texCoord;
    };

    std::vector<Vertex> vertices;
    vertices.reserve(18);

    // 3 quads at 0, 60, 120 degrees around Y axis
    for (int i = 0; i < 3; ++i)
    {
        float angle = static_cast<float>(i) * glm::radians(60.0f);
        float s = std::sin(angle);
        float c = std::cos(angle);

        glm::vec3 right(c * halfWidth, 0.0f, s * halfWidth);

        // Quad corners
        glm::vec3 bl = -right;                        // bottom-left
        glm::vec3 br = right;                         // bottom-right
        glm::vec3 tl = -right + glm::vec3(0, height, 0);  // top-left
        glm::vec3 tr = right + glm::vec3(0, height, 0);   // top-right

        // Triangle 1: bl, br, tr
        vertices.push_back({bl, {0.0f, 0.0f}});
        vertices.push_back({br, {1.0f, 0.0f}});
        vertices.push_back({tr, {1.0f, 1.0f}});

        // Triangle 2: bl, tr, tl
        vertices.push_back({bl, {0.0f, 0.0f}});
        vertices.push_back({tr, {1.0f, 1.0f}});
        vertices.push_back({tl, {0.0f, 1.0f}});
    }

    // Create VAO and VBO for the star mesh
    glGenVertexArrays(1, &m_starVao);
    glGenBuffers(1, &m_starVbo);

    glBindVertexArray(m_starVao);

    // Upload star mesh geometry
    glBindBuffer(GL_ARRAY_BUFFER, m_starVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                 vertices.data(), GL_STATIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));

    // TexCoord attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, texCoord)));

    // Create instance VBO (initially empty)
    glGenBuffers(1, &m_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);

    // Per-instance attributes layout:
    // location 3: vec3 i_position  (offset 0)
    // location 4: float i_rotation (offset 12)
    // location 5: float i_scale    (offset 16)
    // location 6: vec3 i_colorTint (offset 20)
    // Total stride: 32 bytes (matches FoliageInstance layout)

    // i_position (location 3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(FoliageInstance),
                          reinterpret_cast<void*>(0));
    glVertexAttribDivisor(3, 1);

    // i_rotation (location 4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(FoliageInstance),
                          reinterpret_cast<void*>(12));
    glVertexAttribDivisor(4, 1);

    // i_scale (location 5)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(FoliageInstance),
                          reinterpret_cast<void*>(16));
    glVertexAttribDivisor(5, 1);

    // i_colorTint (location 6)
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(FoliageInstance),
                          reinterpret_cast<void*>(20));
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
}

void FoliageRenderer::generateDefaultTexture()
{
    // Generate a simple procedural grass blade texture (32x64 RGBA)
    const int width = 32;
    const int height = 64;
    std::vector<uint8_t> pixels(width * height * 4);

    for (int y = 0; y < height; ++y)
    {
        float t = static_cast<float>(y) / static_cast<float>(height - 1);  // 0=bottom, 1=top
        for (int x = 0; x < width; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(width - 1);

            // Blade shape: narrow at top, wider at bottom
            float bladeWidth = 0.3f + 0.4f * (1.0f - t);
            float distFromCenter = std::abs(u - 0.5f) * 2.0f;
            float inBlade = (distFromCenter < bladeWidth) ? 1.0f : 0.0f;

            // Smooth edge
            if (distFromCenter > bladeWidth * 0.7f && distFromCenter < bladeWidth)
            {
                float edgeT = (distFromCenter - bladeWidth * 0.7f) / (bladeWidth * 0.3f);
                inBlade = 1.0f - edgeT;
            }

            // Pointed tip
            if (t > 0.8f)
            {
                float tipT = (t - 0.8f) / 0.2f;
                inBlade *= (1.0f - tipT);
            }

            // Color: green with variation
            float green = 0.45f + 0.25f * t;  // Lighter toward top
            float red = 0.15f + 0.1f * (1.0f - t);

            int idx = (y * width + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(red * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(green * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(0.08f * 255.0f);
            pixels[idx + 3] = static_cast<uint8_t>(inBlade * 255.0f);
        }
    }

    glGenTextures(1, &m_defaultTexture);
    glBindTexture(GL_TEXTURE_2D, m_defaultTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FoliageRenderer::uploadInstances(const std::vector<FoliageInstance>& instances)
{
    int count = static_cast<int>(instances.size());

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);

    if (count > m_instanceCapacity)
    {
        // Grow with headroom to avoid frequent reallocation
        m_instanceCapacity = count + count / 4 + 256;
        glBufferData(GL_ARRAY_BUFFER,
                     m_instanceCapacity * static_cast<GLsizeiptr>(sizeof(FoliageInstance)),
                     nullptr, GL_DYNAMIC_DRAW);
    }

    // Upload instance data
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    count * static_cast<GLsizeiptr>(sizeof(FoliageInstance)),
                    instances.data());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace Vestige
