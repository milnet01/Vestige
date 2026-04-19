// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file indirect_buffer.cpp
/// @brief Indirect draw command buffer and matrix SSBO implementation.
#include "renderer/indirect_buffer.h"

namespace Vestige
{

IndirectBuffer::IndirectBuffer()
{
    glCreateBuffers(1, &m_commandBuffer);
    glCreateBuffers(1, &m_matrixSsbo);
}

IndirectBuffer::~IndirectBuffer()
{
    if (m_commandBuffer != 0) glDeleteBuffers(1, &m_commandBuffer);
    if (m_matrixSsbo != 0) glDeleteBuffers(1, &m_matrixSsbo);
}

void IndirectBuffer::addCommand(const MeshPoolEntry& poolEntry,
                                 const std::vector<glm::mat4>& matrices)
{
    if (matrices.empty())
    {
        return;
    }

    DrawElementsIndirectCommand cmd;
    cmd.count = poolEntry.indexCount;
    cmd.instanceCount = static_cast<GLuint>(matrices.size());
    cmd.firstIndex = poolEntry.firstIndex;
    cmd.baseVertex = poolEntry.baseVertex;
    cmd.baseInstance = static_cast<GLuint>(m_allMatrices.size());

    m_commands.push_back(cmd);
    m_allMatrices.insert(m_allMatrices.end(), matrices.begin(), matrices.end());
}

void IndirectBuffer::clear()
{
    m_commands.clear();
    m_allMatrices.clear();
}

void IndirectBuffer::upload()
{
    if (m_commands.empty())
    {
        return;
    }

    // Upload commands to indirect buffer
    auto cmdSize = static_cast<GLsizeiptr>(
        m_commands.size() * sizeof(DrawElementsIndirectCommand));

    if (m_commands.size() > m_commandCapacity)
    {
        // Reallocate (immutable storage — must delete and recreate)
        glDeleteBuffers(1, &m_commandBuffer);
        glCreateBuffers(1, &m_commandBuffer);
        m_commandCapacity = m_commands.size();
        glNamedBufferStorage(m_commandBuffer, cmdSize, m_commands.data(),
                             GL_DYNAMIC_STORAGE_BIT);
    }
    else
    {
        glNamedBufferSubData(m_commandBuffer, 0, cmdSize, m_commands.data());
    }

    // Upload matrices to SSBO
    auto matSize = static_cast<GLsizeiptr>(
        m_allMatrices.size() * sizeof(glm::mat4));

    if (m_allMatrices.size() > m_matrixCapacity)
    {
        glDeleteBuffers(1, &m_matrixSsbo);
        glCreateBuffers(1, &m_matrixSsbo);
        m_matrixCapacity = m_allMatrices.size();
        glNamedBufferStorage(m_matrixSsbo, matSize, m_allMatrices.data(),
                             GL_DYNAMIC_STORAGE_BIT);
    }
    else
    {
        glNamedBufferSubData(m_matrixSsbo, 0, matSize, m_allMatrices.data());
    }
}

void IndirectBuffer::draw() const
{
    if (m_commands.empty())
    {
        return;
    }

    // Bind command buffer as indirect draw source
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_commandBuffer);

    // Bind matrix SSBO to binding point 0
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_matrixSsbo);

    // Issue all draw commands in one call
    glMultiDrawElementsIndirect(
        GL_TRIANGLES,
        GL_UNSIGNED_INT,
        nullptr,
        static_cast<GLsizei>(m_commands.size()),
        0  // stride = 0 means tightly packed
    );

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

int IndirectBuffer::getCommandCount() const
{
    return static_cast<int>(m_commands.size());
}

int IndirectBuffer::getTotalInstances() const
{
    return static_cast<int>(m_allMatrices.size());
}

GLuint IndirectBuffer::getMatrixSsbo() const
{
    return m_matrixSsbo;
}

GLuint IndirectBuffer::getCommandBuffer() const
{
    return m_commandBuffer;
}

} // namespace Vestige
