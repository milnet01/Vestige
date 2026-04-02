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
    generateTypeTextures();

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
    for (auto& [typeId, tex] : m_typeTextures)
    {
        if (tex != 0)
        {
            glDeleteTextures(1, &tex);
        }
    }
    m_typeTextures.clear();
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
    const DirectionalLight* dirLight,
    const glm::vec4& clipPlane)
{
    if (!m_initialized || chunks.empty())
    {
        return;
    }

    // Collect visible foliage instances grouped by type
    for (auto& [typeId, vec] : m_visibleByType)
    {
        vec.clear();
    }

    glm::vec3 camPos = camera.getPosition();
    float maxDistSq = maxDistance * maxDistance;

    for (const FoliageChunk* chunk : chunks)
    {
        AABB bounds = chunk->getBounds();
        glm::vec3 chunkCenter = bounds.getCenter();
        glm::vec3 diff = chunkCenter - camPos;
        float chunkDistSq = diff.x * diff.x + diff.z * diff.z;
        float chunkRadius = FoliageChunk::CHUNK_SIZE * 0.707f;
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
                    m_visibleByType[typeId].push_back(inst);
                }
            }
        }
    }

    // Check if anything is visible
    bool anyVisible = false;
    for (const auto& [typeId, vec] : m_visibleByType)
    {
        if (!vec.empty())
        {
            anyVisible = true;
            break;
        }
    }
    if (!anyVisible)
    {
        return;
    }

    // Set up shared shader state
    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);
    m_shader.setMat4("u_view", camera.getViewMatrix());
    m_shader.setFloat("u_time", time);
    m_shader.setVec3("u_windDirection", glm::normalize(windDirection));
    m_shader.setFloat("u_windAmplitude", windAmplitude);
    m_shader.setFloat("u_windFrequency", windFrequency);
    m_shader.setFloat("u_maxDistance", maxDistance);
    m_shader.setVec3("u_cameraPos", camPos);
    m_shader.setVec4("u_clipPlane", clipPlane);

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

    if (hasShadows)
    {
        csm->bindShadowTexture(3);
        m_shader.setInt("u_cascadeShadowMap", 3);

        int cascadeCount = csm->getCascadeCount();
        m_shader.setInt("u_cascadeCount", cascadeCount);
        static const char* cascadeSplitNames[4] = {
            "u_cascadeSplits[0]", "u_cascadeSplits[1]",
            "u_cascadeSplits[2]", "u_cascadeSplits[3]"
        };
        static const char* cascadeMatNames[4] = {
            "u_cascadeLightSpaceMatrices[0]", "u_cascadeLightSpaceMatrices[1]",
            "u_cascadeLightSpaceMatrices[2]", "u_cascadeLightSpaceMatrices[3]"
        };
        for (int i = 0; i < cascadeCount && i < 4; ++i)
        {
            m_shader.setFloat(cascadeSplitNames[i],
                              csm->getCascadeSplit(i));
            m_shader.setMat4(cascadeMatNames[i],
                             csm->getLightSpaceMatrix(i));
        }
    }

    m_shader.setInt("u_texture", 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_starVao);

    // Draw each foliage type with its own texture
    for (const auto& [typeId, instances] : m_visibleByType)
    {
        if (instances.empty())
        {
            continue;
        }

        // Bind type-specific texture (fall back to type 0 if unknown)
        auto texIt = m_typeTextures.find(typeId);
        if (texIt == m_typeTextures.end())
        {
            texIt = m_typeTextures.find(0);
        }
        if (texIt != m_typeTextures.end())
        {
            glBindTextureUnit(0, texIt->second);
        }

        uploadInstances(instances);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 18,
                              static_cast<GLsizei>(instances.size()));
    }

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

    // Collect nearby foliage instances grouped by type (shadow casting is expensive)
    for (auto& [typeId, vec] : m_visibleByType)
    {
        vec.clear();
    }

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
                    m_visibleByType[typeId].push_back(inst);
                }
            }
        }
    }

    m_shadowShader.use();
    m_shadowShader.setMat4("u_lightSpaceMatrix", lightSpaceMatrix);
    m_shadowShader.setFloat("u_time", time);
    m_shadowShader.setVec3("u_windDirection", glm::normalize(windDirection));
    m_shadowShader.setFloat("u_windAmplitude", windAmplitude);
    m_shadowShader.setFloat("u_windFrequency", windFrequency);
    m_shadowShader.setInt("u_texture", 0);

    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_starVao);

    for (const auto& [typeId, instances] : m_visibleByType)
    {
        if (instances.empty())
        {
            continue;
        }

        auto texIt = m_typeTextures.find(typeId);
        if (texIt == m_typeTextures.end())
        {
            texIt = m_typeTextures.find(0);
        }
        if (texIt != m_typeTextures.end())
        {
            glBindTextureUnit(0, texIt->second);
        }

        uploadInstances(instances);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 18,
                              static_cast<GLsizei>(instances.size()));
    }

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

    // Create VAO and VBO for the star mesh (DSA)
    glCreateVertexArrays(1, &m_starVao);
    glCreateBuffers(1, &m_starVbo);

    // Upload star mesh geometry (immutable, static)
    glNamedBufferStorage(m_starVbo, vertices.size() * sizeof(Vertex),
                         vertices.data(), 0);

    // Bind VBO to VAO binding point 0
    glVertexArrayVertexBuffer(m_starVao, 0, m_starVbo, 0, sizeof(Vertex));

    // Position attribute (location 0, binding 0)
    glEnableVertexArrayAttrib(m_starVao, 0);
    glVertexArrayAttribFormat(m_starVao, 0, 3, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, position));
    glVertexArrayAttribBinding(m_starVao, 0, 0);

    // TexCoord attribute (location 1, binding 0)
    glEnableVertexArrayAttrib(m_starVao, 1);
    glVertexArrayAttribFormat(m_starVao, 1, 2, GL_FLOAT, GL_FALSE,
                              offsetof(Vertex, texCoord));
    glVertexArrayAttribBinding(m_starVao, 1, 0);

    // Create instance VBO (initially empty, will be created on first upload)
    glCreateBuffers(1, &m_instanceVbo);

    // Per-instance attributes layout (binding 1):
    // location 3: vec3 i_position  (offset 0)
    // location 4: float i_rotation (offset 12)
    // location 5: float i_scale    (offset 16)
    // location 6: vec3 i_colorTint (offset 20)
    // Total stride: 32 bytes (matches FoliageInstance layout)

    // i_position (location 3, binding 1)
    glEnableVertexArrayAttrib(m_starVao, 3);
    glVertexArrayAttribFormat(m_starVao, 3, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_starVao, 3, 1);
    glVertexArrayBindingDivisor(m_starVao, 1, 1);

    // i_rotation (location 4, binding 1)
    glEnableVertexArrayAttrib(m_starVao, 4);
    glVertexArrayAttribFormat(m_starVao, 4, 1, GL_FLOAT, GL_FALSE, 12);
    glVertexArrayAttribBinding(m_starVao, 4, 1);

    // i_scale (location 5, binding 1)
    glEnableVertexArrayAttrib(m_starVao, 5);
    glVertexArrayAttribFormat(m_starVao, 5, 1, GL_FLOAT, GL_FALSE, 16);
    glVertexArrayAttribBinding(m_starVao, 5, 1);

    // i_colorTint (location 6, binding 1)
    glEnableVertexArrayAttrib(m_starVao, 6);
    glVertexArrayAttribFormat(m_starVao, 6, 3, GL_FLOAT, GL_FALSE, 20);
    glVertexArrayAttribBinding(m_starVao, 6, 1);
}

void FoliageRenderer::generateTypeTextures()
{
    // Generate procedural textures for each built-in foliage type
    // 0 = Short Grass, 1 = Tall Grass, 2 = Flowers, 3 = Ferns
    for (uint32_t typeId = 0; typeId < 4; typeId++)
    {
        m_typeTextures[typeId] = generateProceduralTexture(typeId);
    }
    Logger::info("Foliage textures generated: 4 types (grass, tall grass, flowers, ferns)");
}

GLuint FoliageRenderer::generateProceduralTexture(uint32_t typeId)
{
    const int width = 32;
    const int height = 64;
    std::vector<uint8_t> pixels(width * height * 4);

    for (int y = 0; y < height; ++y)
    {
        float t = static_cast<float>(y) / static_cast<float>(height - 1);
        for (int x = 0; x < width; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(width - 1);
            float distFromCenter = std::abs(u - 0.5f) * 2.0f;

            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;

            if (typeId == 0)
            {
                // Short Grass — narrow green blade
                float bladeWidth = 0.3f + 0.4f * (1.0f - t);
                a = (distFromCenter < bladeWidth) ? 1.0f : 0.0f;
                if (distFromCenter > bladeWidth * 0.7f && distFromCenter < bladeWidth)
                {
                    a = 1.0f - (distFromCenter - bladeWidth * 0.7f) / (bladeWidth * 0.3f);
                }
                if (t > 0.8f)
                {
                    a *= 1.0f - (t - 0.8f) / 0.2f;
                }
                r = 0.15f + 0.1f * (1.0f - t);
                g = 0.45f + 0.25f * t;
                b = 0.08f;
            }
            else if (typeId == 1)
            {
                // Tall Grass — wider, darker blade with drooping tip
                float bladeWidth = 0.35f + 0.35f * (1.0f - t);
                a = (distFromCenter < bladeWidth) ? 1.0f : 0.0f;
                if (distFromCenter > bladeWidth * 0.7f && distFromCenter < bladeWidth)
                {
                    a = 1.0f - (distFromCenter - bladeWidth * 0.7f) / (bladeWidth * 0.3f);
                }
                if (t > 0.85f)
                {
                    a *= 1.0f - (t - 0.85f) / 0.15f;
                }
                r = 0.12f + 0.06f * (1.0f - t);
                g = 0.35f + 0.2f * t;
                b = 0.06f;
            }
            else if (typeId == 2)
            {
                // Flowers — green stem with colored petal cluster at top
                bool isStem = (distFromCenter < 0.15f) && (t < 0.65f);
                bool isPetal = (t > 0.5f) && (distFromCenter < (0.6f - 0.3f * (t - 0.5f) * 2.0f));

                if (isPetal)
                {
                    // Colorful petal — warm red/yellow/pink
                    r = 0.85f + 0.15f * std::sin(u * 6.28f);
                    g = 0.35f + 0.2f * t;
                    b = 0.15f + 0.3f * std::cos(u * 3.14f);
                    a = 1.0f;
                    // Soft petal edge
                    float petalEdge = 0.6f - 0.3f * (t - 0.5f) * 2.0f;
                    if (distFromCenter > petalEdge * 0.7f)
                    {
                        a = 1.0f - (distFromCenter - petalEdge * 0.7f) / (petalEdge * 0.3f);
                    }
                    if (t > 0.9f)
                    {
                        a *= 1.0f - (t - 0.9f) / 0.1f;
                    }
                }
                else if (isStem)
                {
                    r = 0.12f;
                    g = 0.4f;
                    b = 0.06f;
                    a = 1.0f;
                }
            }
            else if (typeId == 3)
            {
                // Fern — wide frond shape with jagged edges
                float frondWidth = 0.5f * (1.0f - 0.6f * t);
                float jaggedOffset = 0.08f * std::sin(t * 20.0f);
                float effectiveWidth = frondWidth + jaggedOffset;

                a = (distFromCenter < effectiveWidth) ? 1.0f : 0.0f;
                if (distFromCenter > effectiveWidth * 0.6f && distFromCenter < effectiveWidth)
                {
                    a = 1.0f - (distFromCenter - effectiveWidth * 0.6f) / (effectiveWidth * 0.4f);
                }
                if (t > 0.9f)
                {
                    a *= 1.0f - (t - 0.9f) / 0.1f;
                }
                // Deep green fern color
                r = 0.08f + 0.05f * (1.0f - t);
                g = 0.3f + 0.25f * t;
                b = 0.1f + 0.05f * t;
            }

            a = std::max(0.0f, std::min(1.0f, a));

            int idx = (y * width + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(r * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(g * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(b * 255.0f);
            pixels[idx + 3] = static_cast<uint8_t>(a * 255.0f);
        }
    }

    GLuint texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    GLsizei mipLevels = 1 + static_cast<GLsizei>(
        std::floor(std::log2(std::max(width, height))));
    glTextureStorage2D(texture, mipLevels, GL_RGBA8, width, height);
    glTextureSubImage2D(texture, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateTextureMipmap(texture);

    return texture;
}

void FoliageRenderer::uploadInstances(const std::vector<FoliageInstance>& instances)
{
    int count = static_cast<int>(instances.size());

    if (count > m_instanceCapacity)
    {
        // Grow with headroom to avoid frequent reallocation
        m_instanceCapacity = count + count / 4 + 256;

        // Delete old buffer and create new immutable one
        glDeleteBuffers(1, &m_instanceVbo);
        glCreateBuffers(1, &m_instanceVbo);
        glNamedBufferStorage(m_instanceVbo,
                             m_instanceCapacity * static_cast<GLsizeiptr>(sizeof(FoliageInstance)),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);

        // Re-bind to VAO binding point 1
        glVertexArrayVertexBuffer(m_starVao, 1, m_instanceVbo, 0, sizeof(FoliageInstance));
    }

    // Upload instance data
    glNamedBufferSubData(m_instanceVbo, 0,
                         count * static_cast<GLsizeiptr>(sizeof(FoliageInstance)),
                         instances.data());
}

} // namespace Vestige
