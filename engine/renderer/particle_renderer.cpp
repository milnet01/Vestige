/// @file particle_renderer.cpp
/// @brief Instanced billboard renderer for particle systems.
#include "renderer/particle_renderer.h"
#include "core/logger.h"

#include <glm/gtc/type_ptr.hpp>

namespace Vestige
{

ParticleRenderer::ParticleRenderer() = default;

ParticleRenderer::~ParticleRenderer()
{
    shutdown();
}

bool ParticleRenderer::init(const std::string& assetPath)
{
    std::string vertPath = assetPath + "/shaders/particle.vert.glsl";
    std::string fragPath = assetPath + "/shaders/particle.frag.glsl";

    if (!m_shader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("Failed to load particle shaders");
        return false;
    }

    createQuadVao();
    Logger::info("Particle renderer initialized");
    return true;
}

void ParticleRenderer::shutdown()
{
    if (m_quadVao)
    {
        glDeleteVertexArrays(1, &m_quadVao);
        m_quadVao = 0;
    }
    if (m_quadVbo)
    {
        glDeleteBuffers(1, &m_quadVbo);
        m_quadVbo = 0;
    }
    if (m_instancePositionVbo)
    {
        glDeleteBuffers(1, &m_instancePositionVbo);
        m_instancePositionVbo = 0;
    }
    if (m_instanceColorVbo)
    {
        glDeleteBuffers(1, &m_instanceColorVbo);
        m_instanceColorVbo = 0;
    }
    if (m_instanceSizeVbo)
    {
        glDeleteBuffers(1, &m_instanceSizeVbo);
        m_instanceSizeVbo = 0;
    }
    m_shader.destroy();
    m_instanceBufferCapacity = 0;
}

void ParticleRenderer::createQuadVao()
{
    // Static billboard quad: 6 vertices (2 triangles), each vertex is vec2 (local offset)
    // Range: (-0.5, -0.5) to (0.5, 0.5)
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    float quadVertices[] = {
        // Triangle 1
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        // Triangle 2
        -0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f,
    };
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers(1, &m_quadVbo);

    glBindVertexArray(m_quadVao);

    // Quad vertex positions (location 0)
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    // Create instance data buffers (initially empty)
    glGenBuffers(1, &m_instancePositionVbo);
    glGenBuffers(1, &m_instanceColorVbo);
    glGenBuffers(1, &m_instanceSizeVbo);

    // Instance position (location 1) — vec3
    glBindBuffer(GL_ARRAY_BUFFER, m_instancePositionVbo);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glVertexAttribDivisor(1, 1);

    // Instance color (location 2) — vec4
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceColorVbo);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    glVertexAttribDivisor(2, 1);

    // Instance size (location 3) — float
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceSizeVbo);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
}

void ParticleRenderer::ensureInstanceBufferCapacity(int count)
{
    if (count <= m_instanceBufferCapacity)
    {
        return;
    }

    // Round up to next power of 2 for amortized growth
    int capacity = 1;
    while (capacity < count)
    {
        capacity *= 2;
    }

    m_instanceBufferCapacity = capacity;
    auto byteCount = static_cast<GLsizeiptr>(capacity);

    // Orphan and reallocate all instance buffers
    glBindBuffer(GL_ARRAY_BUFFER, m_instancePositionVbo);
    glBufferData(GL_ARRAY_BUFFER, byteCount * static_cast<GLsizeiptr>(sizeof(glm::vec3)),
                 nullptr, GL_STREAM_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceColorVbo);
    glBufferData(GL_ARRAY_BUFFER, byteCount * static_cast<GLsizeiptr>(sizeof(glm::vec4)),
                 nullptr, GL_STREAM_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceSizeVbo);
    glBufferData(GL_ARRAY_BUFFER, byteCount * static_cast<GLsizeiptr>(sizeof(float)),
                 nullptr, GL_STREAM_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ParticleRenderer::render(
    const std::vector<std::pair<const ParticleEmitterComponent*, glm::mat4>>& emitters,
    const Camera& camera,
    const glm::mat4& viewProjection)
{
    if (emitters.empty())
    {
        return;
    }

    // Count total particles across all emitters
    int totalParticles = 0;
    for (const auto& [emitter, worldMatrix] : emitters)
    {
        totalParticles += emitter->getData().count;
    }

    if (totalParticles == 0)
    {
        return;
    }

    // Extract camera orientation vectors from view matrix for billboarding
    glm::mat4 view = camera.getViewMatrix();
    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);
    m_shader.setVec3("u_cameraRight", cameraRight);
    m_shader.setVec3("u_cameraUp", cameraUp);
    m_shader.setBool("u_hasTexture", false);  // No texture support yet (5E-2)

    glBindVertexArray(m_quadVao);

    // Render each emitter separately (different blend modes possible)
    for (const auto& [emitter, worldMatrix] : emitters)
    {
        const ParticleData& data = emitter->getData();
        if (data.count == 0)
        {
            continue;
        }

        // Set blend mode
        const ParticleEmitterConfig& config = emitter->getConfig();
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);

        if (config.blendMode == ParticleEmitterConfig::BlendMode::ADDITIVE)
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        }
        else
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        int count = data.count;
        ensureInstanceBufferCapacity(count);

        auto countBytes = static_cast<GLsizeiptr>(count);

        // Upload position data (buffer orphaning pattern)
        glBindBuffer(GL_ARRAY_BUFFER, m_instancePositionVbo);
        glBufferData(GL_ARRAY_BUFFER, countBytes * static_cast<GLsizeiptr>(sizeof(glm::vec3)),
                     nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        countBytes * static_cast<GLsizeiptr>(sizeof(glm::vec3)),
                        data.positions.data());

        // Upload color data
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceColorVbo);
        glBufferData(GL_ARRAY_BUFFER, countBytes * static_cast<GLsizeiptr>(sizeof(glm::vec4)),
                     nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        countBytes * static_cast<GLsizeiptr>(sizeof(glm::vec4)),
                        data.colors.data());

        // Upload size data
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceSizeVbo);
        glBufferData(GL_ARRAY_BUFFER, countBytes * static_cast<GLsizeiptr>(sizeof(float)),
                     nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        countBytes * static_cast<GLsizeiptr>(sizeof(float)),
                        data.sizes.data());

        // Draw all particles in one instanced call
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    }

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

} // namespace Vestige
