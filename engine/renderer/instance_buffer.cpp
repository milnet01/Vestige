// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file instance_buffer.cpp
/// @brief GPU buffer for per-instance model matrices.
#include "renderer/instance_buffer.h"

#include <algorithm>

namespace Vestige
{

InstanceBuffer::InstanceBuffer()
{
    glCreateBuffers(1, &m_vbo);
}

InstanceBuffer::~InstanceBuffer()
{
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
    }
}

InstanceBuffer::InstanceBuffer(InstanceBuffer&& other) noexcept
    : m_vbo(other.m_vbo)
    , m_instanceCount(other.m_instanceCount)
    , m_capacity(other.m_capacity)
{
    other.m_vbo = 0;
    other.m_instanceCount = 0;
    other.m_capacity = 0;
}

InstanceBuffer& InstanceBuffer::operator=(InstanceBuffer&& other) noexcept
{
    if (this != &other)
    {
        if (m_vbo != 0)
        {
            glDeleteBuffers(1, &m_vbo);
        }

        m_vbo = other.m_vbo;
        m_instanceCount = other.m_instanceCount;
        m_capacity = other.m_capacity;

        other.m_vbo = 0;
        other.m_instanceCount = 0;
        other.m_capacity = 0;
    }
    return *this;
}

void InstanceBuffer::upload(const std::vector<glm::mat4>& matrices)
{
    m_instanceCount = matrices.size();
    if (m_instanceCount == 0)
    {
        return;
    }

    auto dataSize = static_cast<GLsizeiptr>(m_instanceCount * sizeof(glm::mat4));

    if (m_instanceCount > m_capacity)
    {
        // Reallocate — delete old immutable buffer and create larger one,
        // with room to spare so the next few growths need no new buffer.
        glDeleteBuffers(1, &m_vbo);
        m_capacity = grownCapacity(m_instanceCount, m_capacity);
        glCreateBuffers(1, &m_vbo);
        glNamedBufferStorage(m_vbo, static_cast<GLsizeiptr>(m_capacity * sizeof(glm::mat4)),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);
    }
    glNamedBufferSubData(m_vbo, 0, dataSize, matrices.data());
}

size_t InstanceBuffer::grownCapacity(size_t needed, size_t current)
{
    return std::max({needed, current * 2, MIN_CAPACITY});
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
