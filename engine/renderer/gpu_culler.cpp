/// @file gpu_culler.cpp
/// @brief GPU-driven frustum culling implementation.
#include "renderer/gpu_culler.h"
#include "core/logger.h"

namespace Vestige
{

GpuCuller::GpuCuller() = default;

GpuCuller::~GpuCuller()
{
    if (m_objectSsbo != 0)
    {
        glDeleteBuffers(1, &m_objectSsbo);
    }
}

bool GpuCuller::init(const std::string& shaderPath)
{
    if (!m_computeShader.loadComputeShader(shaderPath))
    {
        Logger::error("GpuCuller: failed to load compute shader: " + shaderPath);
        return false;
    }

    glCreateBuffers(1, &m_objectSsbo);

    Logger::info("GPU frustum culler initialized");
    return true;
}

void GpuCuller::cull(GLuint commandBuffer, int commandCount,
                      const std::vector<CullObjectData>& objects,
                      const std::array<glm::vec4, 6>& frustumPlanes)
{
    if (commandCount <= 0 || objects.empty())
    {
        return;
    }

    // Upload object AABB data to SSBO
    auto dataSize = static_cast<GLsizeiptr>(objects.size() * sizeof(CullObjectData));

    if (objects.size() > m_objectCapacity)
    {
        glDeleteBuffers(1, &m_objectSsbo);
        glCreateBuffers(1, &m_objectSsbo);
        m_objectCapacity = objects.size();
        glNamedBufferStorage(m_objectSsbo, dataSize, objects.data(),
                             GL_DYNAMIC_STORAGE_BIT);
    }
    else
    {
        glNamedBufferSubData(m_objectSsbo, 0, dataSize, objects.data());
    }

    // Bind SSBOs:
    // binding 1 = draw commands (read/write — we zero instanceCount for culled)
    // binding 2 = object AABBs (read-only)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, commandBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_objectSsbo);

    // Set frustum plane uniforms
    m_computeShader.use();
    for (size_t i = 0; i < 6; i++)
    {
        m_computeShader.setVec4("u_frustumPlanes[" + std::to_string(i) + "]",
                                frustumPlanes[i]);
    }
    m_computeShader.setInt("u_objectCount", commandCount);

    // Dispatch one thread per object
    int groups = (commandCount + 63) / 64;
    glDispatchCompute(static_cast<GLuint>(groups), 1, 1);

    // Memory barrier: ensure compute writes are visible to subsequent draw commands
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
}

} // namespace Vestige
