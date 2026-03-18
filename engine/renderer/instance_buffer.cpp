/// @file instance_buffer.cpp
/// @brief GPU buffer for per-instance model matrices.
#include "renderer/instance_buffer.h"

namespace Vestige
{

InstanceBuffer::InstanceBuffer()
{
    glGenBuffers(1, &m_vbo);
}

InstanceBuffer::~InstanceBuffer()
{
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
    }
}

void InstanceBuffer::upload(const std::vector<glm::mat4>& matrices)
{
    m_instanceCount = matrices.size();
    if (m_instanceCount == 0)
    {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    auto dataSize = static_cast<GLsizeiptr>(m_instanceCount * sizeof(glm::mat4));

    if (m_instanceCount > m_capacity)
    {
        // Reallocate — grow to fit
        m_capacity = m_instanceCount;
        glBufferData(GL_ARRAY_BUFFER, dataSize, matrices.data(), GL_DYNAMIC_DRAW);
    }
    else
    {
        // Reuse existing allocation
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, matrices.data());
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

GLuint InstanceBuffer::getHandle() const
{
    return m_vbo;
}

size_t InstanceBuffer::getInstanceCount() const
{
    return m_instanceCount;
}

} // namespace Vestige
