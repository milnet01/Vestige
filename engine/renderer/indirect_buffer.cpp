// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file indirect_buffer.cpp
/// @brief Indirect draw command buffer and matrix SSBO implementation.
#include "renderer/indirect_buffer.h"

#include "renderer/instance_buffer.h"

namespace Vestige
{

IndirectBuffer::IndirectBuffer()
{
    glCreateBuffers(1, &m_commandBuffer);
    glCreateBuffers(1, &m_matrixSsbo);
    glCreateBuffers(1, &m_prevMatrixSsbo);
}

IndirectBuffer::~IndirectBuffer()
{
    if (m_commandBuffer != 0) glDeleteBuffers(1, &m_commandBuffer);
    if (m_matrixSsbo != 0) glDeleteBuffers(1, &m_matrixSsbo);
    if (m_prevMatrixSsbo != 0) glDeleteBuffers(1, &m_prevMatrixSsbo);
}

void IndirectBuffer::addCommand(const MeshPoolEntry& poolEntry,
                                 const std::vector<glm::mat4>& matrices,
                                 const std::vector<glm::mat4>& prevMatrices)
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

    // Prev matrices are in lock-step with model matrices (same baseInstance index).
    // Fall back to the current matrices if a caller passes a mismatched length, so
    // the SSBO stays the same size as the model SSBO (degrades to zero motion).
    if (prevMatrices.size() == matrices.size())
    {
        m_allPrevMatrices.insert(m_allPrevMatrices.end(), prevMatrices.begin(), prevMatrices.end());
    }
    else
    {
        m_allPrevMatrices.insert(m_allPrevMatrices.end(), matrices.begin(), matrices.end());
    }
}

void IndirectBuffer::clear()
{
    m_commands.clear();
    m_allMatrices.clear();
    m_allPrevMatrices.clear();
}

void IndirectBuffer::upload()
{
    if (m_commands.empty())
    {
        return;
    }

    uploadWithHeadroom(m_commandBuffer, m_commandCapacity, m_commands.data(),
                       m_commands.size(), sizeof(DrawElementsIndirectCommand));
    uploadWithHeadroom(m_matrixSsbo, m_matrixCapacity, m_allMatrices.data(),
                       m_allMatrices.size(), sizeof(glm::mat4));
    // Previous-frame matrices, in the parallel SSBO (motion vectors, binding 4).
    uploadWithHeadroom(m_prevMatrixSsbo, m_prevMatrixCapacity, m_allPrevMatrices.data(),
                       m_allPrevMatrices.size(), sizeof(glm::mat4));
}

void IndirectBuffer::uploadWithHeadroom(GLuint& buffer, size_t& capacity, const void* data,
                                        size_t count, size_t elementSize)
{
    if (count > capacity)
    {
        // Immutable storage cannot grow: delete and recreate, with room to
        // spare so one more command or instance does not recreate it again
        // (a recreation cost a slow frame on InstanceBuffer, 3D_E-0795).
        glDeleteBuffers(1, &buffer);
        glCreateBuffers(1, &buffer);
        capacity = InstanceBuffer::grownCapacity(count, capacity);
        glNamedBufferStorage(buffer, static_cast<GLsizeiptr>(capacity * elementSize),
                             nullptr, GL_DYNAMIC_STORAGE_BIT);
    }
    glNamedBufferSubData(buffer, 0, static_cast<GLsizeiptr>(count * elementSize), data);
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

    // Bind previous-frame matrix SSBO to binding point 4 (motion vectors, Slice R1)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_prevMatrixSsbo);

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

GLuint IndirectBuffer::getPrevMatrixSsbo() const
{
    return m_prevMatrixSsbo;
}

GLuint IndirectBuffer::getCommandBuffer() const
{
    return m_commandBuffer;
}

} // namespace Vestige
