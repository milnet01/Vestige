/// @file particle_renderer.cpp
/// @brief Instanced billboard renderer for particle systems.
#include "renderer/particle_renderer.h"
#include "scene/gpu_particle_emitter.h"
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
    m_assetPath = assetPath;

    std::string vertPath = assetPath + "/shaders/particle.vert.glsl";
    std::string fragPath = assetPath + "/shaders/particle.frag.glsl";

    if (!m_shader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("Failed to load particle shaders");
        return false;
    }

    // Load GPU particle shader (uses SSBO instead of VBOs)
    std::string gpuVertPath = assetPath + "/shaders/particle_gpu.vert.glsl";
    if (!m_gpuShader.loadFromFiles(gpuVertPath, fragPath))
    {
        Logger::warning("Failed to load GPU particle shaders — GPU rendering unavailable");
        // Non-fatal: GPU rendering is optional
    }

    // Create empty VAO for GPU particle rendering (vertices generated in shader)
    glCreateVertexArrays(1, &m_gpuVao);

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
    if (m_instanceAgeVbo)
    {
        glDeleteBuffers(1, &m_instanceAgeVbo);
        m_instanceAgeVbo = 0;
    }
    if (m_gpuVao)
    {
        glDeleteVertexArrays(1, &m_gpuVao);
        m_gpuVao = 0;
    }
    m_shader.destroy();
    m_gpuShader.destroy();
    m_instanceBufferCapacity = 0;
    m_textureCache.clear();
}

void ParticleRenderer::createQuadVao()
{
    // Static billboard quad: 6 vertices (2 triangles), each vertex is vec2 (local offset)
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

    glCreateVertexArrays(1, &m_quadVao);
    glCreateBuffers(1, &m_quadVbo);

    glNamedBufferStorage(m_quadVbo, sizeof(quadVertices), quadVertices, 0);

    // Bind VBO to VAO binding point 0
    glVertexArrayVertexBuffer(m_quadVao, 0, m_quadVbo, 0, 2 * sizeof(float));

    // Quad vertex positions (location 0, binding 0)
    glEnableVertexArrayAttrib(m_quadVao, 0);
    glVertexArrayAttribFormat(m_quadVao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_quadVao, 0, 0);

    // Create instance data buffers (initially empty)
    glCreateBuffers(1, &m_instancePositionVbo);
    glCreateBuffers(1, &m_instanceColorVbo);
    glCreateBuffers(1, &m_instanceSizeVbo);
    glCreateBuffers(1, &m_instanceAgeVbo);

    // Instance position (location 1, binding 1) — vec3
    glEnableVertexArrayAttrib(m_quadVao, 1);
    glVertexArrayAttribFormat(m_quadVao, 1, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_quadVao, 1, 1);
    glVertexArrayBindingDivisor(m_quadVao, 1, 1);

    // Instance color (location 2, binding 2) — vec4
    glEnableVertexArrayAttrib(m_quadVao, 2);
    glVertexArrayAttribFormat(m_quadVao, 2, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_quadVao, 2, 2);
    glVertexArrayBindingDivisor(m_quadVao, 2, 1);

    // Instance size (location 3, binding 3) — float
    glEnableVertexArrayAttrib(m_quadVao, 3);
    glVertexArrayAttribFormat(m_quadVao, 3, 1, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_quadVao, 3, 3);
    glVertexArrayBindingDivisor(m_quadVao, 3, 1);

    // Instance normalizedAge (location 4, binding 4) — float
    glEnableVertexArrayAttrib(m_quadVao, 4);
    glVertexArrayAttribFormat(m_quadVao, 4, 1, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_quadVao, 4, 4);
    glVertexArrayBindingDivisor(m_quadVao, 4, 1);
}

void ParticleRenderer::ensureInstanceBufferCapacity(int count)
{
    if (count <= m_instanceBufferCapacity)
    {
        return;
    }

    // Round up to next power of 2
    int capacity = 1;
    while (capacity < count)
    {
        capacity *= 2;
    }

    m_instanceBufferCapacity = capacity;
    auto byteCount = static_cast<GLsizeiptr>(capacity);

    // Recreate all instance buffers with new capacity
    glDeleteBuffers(1, &m_instancePositionVbo);
    glCreateBuffers(1, &m_instancePositionVbo);
    glNamedBufferStorage(m_instancePositionVbo,
                         byteCount * static_cast<GLsizeiptr>(sizeof(glm::vec3)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glVertexArrayVertexBuffer(m_quadVao, 1, m_instancePositionVbo, 0, sizeof(glm::vec3));

    glDeleteBuffers(1, &m_instanceColorVbo);
    glCreateBuffers(1, &m_instanceColorVbo);
    glNamedBufferStorage(m_instanceColorVbo,
                         byteCount * static_cast<GLsizeiptr>(sizeof(glm::vec4)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glVertexArrayVertexBuffer(m_quadVao, 2, m_instanceColorVbo, 0, sizeof(glm::vec4));

    glDeleteBuffers(1, &m_instanceSizeVbo);
    glCreateBuffers(1, &m_instanceSizeVbo);
    glNamedBufferStorage(m_instanceSizeVbo,
                         byteCount * static_cast<GLsizeiptr>(sizeof(float)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glVertexArrayVertexBuffer(m_quadVao, 3, m_instanceSizeVbo, 0, sizeof(float));

    glDeleteBuffers(1, &m_instanceAgeVbo);
    glCreateBuffers(1, &m_instanceAgeVbo);
    glNamedBufferStorage(m_instanceAgeVbo,
                         byteCount * static_cast<GLsizeiptr>(sizeof(float)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glVertexArrayVertexBuffer(m_quadVao, 4, m_instanceAgeVbo, 0, sizeof(float));
}

GLuint ParticleRenderer::getOrLoadTexture(const std::string& path)
{
    auto it = m_textureCache.find(path);
    if (it != m_textureCache.end())
    {
        return it->second->getId();
    }

    auto tex = std::make_unique<Texture>();
    if (!tex->loadFromFile(path, true))  // Linear — particle textures are not sRGB
    {
        Logger::warning("Failed to load particle texture: " + path);
        return 0;
    }

    GLuint id = tex->getId();
    m_textureCache[path] = std::move(tex);
    return id;
}

void ParticleRenderer::render(
    const std::vector<std::pair<const ParticleEmitterComponent*, glm::mat4>>& emitters,
    const Camera& camera,
    const glm::mat4& viewProjection,
    GLuint depthTexture,
    int screenWidth,
    int screenHeight,
    float cameraNear)
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

    // Extract camera orientation for billboarding
    glm::mat4 view = camera.getViewMatrix();
    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    m_shader.use();
    m_shader.setMat4("u_viewProjection", viewProjection);
    m_shader.setVec3("u_cameraRight", cameraRight);
    m_shader.setVec3("u_cameraUp", cameraUp);

    // Soft particles: bind depth texture
    bool softParticles = (depthTexture != 0 && screenWidth > 0 && screenHeight > 0);
    m_shader.setBool("u_softParticles", softParticles);
    if (softParticles)
    {
        glBindTextureUnit(1, depthTexture);
        m_shader.setInt("u_depthTexture", 1);
        m_shader.setVec2("u_screenSize", glm::vec2(
            static_cast<float>(screenWidth), static_cast<float>(screenHeight)));
        m_shader.setFloat("u_cameraNear", cameraNear);
        m_shader.setFloat("u_softDistance", 0.5f);
    }

    glBindVertexArray(m_quadVao);

    // Render each emitter
    for (const auto& [emitter, worldMatrix] : emitters)
    {
        const ParticleData& data = emitter->getData();
        if (data.count == 0)
        {
            continue;
        }

        const ParticleEmitterConfig& config = emitter->getConfig();

        // Set blend mode
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

        // Bind particle texture if available
        bool hasTexture = false;
        if (!config.texturePath.empty())
        {
            GLuint texId = getOrLoadTexture(config.texturePath);
            if (texId != 0)
            {
                glBindTextureUnit(0, texId);
                hasTexture = true;
            }
        }
        m_shader.setBool("u_hasTexture", hasTexture);
        if (hasTexture)
        {
            m_shader.setInt("u_texture", 0);
        }

        int count = data.count;
        ensureInstanceBufferCapacity(count);

        auto countBytes = static_cast<GLsizeiptr>(count);

        // Upload instance data
        glNamedBufferSubData(m_instancePositionVbo, 0,
                             countBytes * static_cast<GLsizeiptr>(sizeof(glm::vec3)),
                             data.positions.data());

        glNamedBufferSubData(m_instanceColorVbo, 0,
                             countBytes * static_cast<GLsizeiptr>(sizeof(glm::vec4)),
                             data.colors.data());

        glNamedBufferSubData(m_instanceSizeVbo, 0,
                             countBytes * static_cast<GLsizeiptr>(sizeof(float)),
                             data.sizes.data());

        glNamedBufferSubData(m_instanceAgeVbo, 0,
                             countBytes * static_cast<GLsizeiptr>(sizeof(float)),
                             data.normalizedAges.data());

        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    }

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void ParticleRenderer::renderGPU(
    const std::vector<const GPUParticleEmitter*>& emitters,
    const Camera& camera,
    const glm::mat4& viewProjection,
    GLuint depthTexture,
    int screenWidth,
    int screenHeight,
    float cameraNear)
{
    if (emitters.empty() || m_gpuShader.getId() == 0)
        return;

    // Extract camera orientation for billboarding
    glm::mat4 view = camera.getViewMatrix();
    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    // Soft particles setup
    bool softParticles = (depthTexture != 0 && screenWidth > 0 && screenHeight > 0);

    glBindVertexArray(m_gpuVao);

    for (const auto* emitter : emitters)
    {
        if (!emitter || !emitter->isGPUPath())
            continue;

        const auto* gpuSys = emitter->getGPUSystem();
        if (!gpuSys || !gpuSys->isInitialized())
            continue;

        // Sort alpha-blend particles
        if (emitter->needsSorting())
        {
            // Sort pass was already dispatched during update — use sorted indices
        }

        const ParticleEmitterConfig& config = emitter->getConfig();

        // Set blend mode
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);

        if (config.blendMode == ParticleEmitterConfig::BlendMode::ADDITIVE)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        else
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Bind particle texture
        bool hasTexture = false;
        if (!config.texturePath.empty())
        {
            GLuint texId = getOrLoadTexture(config.texturePath);
            if (texId != 0)
            {
                glBindTextureUnit(0, texId);
                hasTexture = true;
            }
        }

        m_gpuShader.use();
        m_gpuShader.setMat4("u_viewProjection", viewProjection);
        m_gpuShader.setVec3("u_cameraRight", cameraRight);
        m_gpuShader.setVec3("u_cameraUp", cameraUp);
        m_gpuShader.setBool("u_useSortIndices", emitter->needsSorting());

        // Use the shared fragment shader uniforms
        m_gpuShader.setBool("u_hasTexture", hasTexture);
        if (hasTexture)
            m_gpuShader.setInt("u_texture", 0);

        m_gpuShader.setBool("u_softParticles", softParticles);
        if (softParticles)
        {
            glBindTextureUnit(1, depthTexture);
            m_gpuShader.setInt("u_depthTexture", 1);
            m_gpuShader.setVec2("u_screenSize", glm::vec2(
                static_cast<float>(screenWidth), static_cast<float>(screenHeight)));
            m_gpuShader.setFloat("u_cameraNear", cameraNear);
            m_gpuShader.setFloat("u_softDistance", 0.5f);
        }

        // Bind particle SSBO and draw with indirect command
        gpuSys->bindForRendering();
        gpuSys->drawIndirect();
    }

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

} // namespace Vestige
