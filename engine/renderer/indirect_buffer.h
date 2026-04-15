// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file indirect_buffer.h
/// @brief Draw command buffer and per-instance SSBO for Multi-Draw Indirect.
#pragma once

#include "renderer/mesh_pool.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief OpenGL indirect draw command for glMultiDrawElementsIndirect.
struct DrawElementsIndirectCommand
{
    GLuint count;          // Number of indices
    GLuint instanceCount;  // Number of instances
    GLuint firstIndex;     // Offset into IBO (in indices)
    GLint  baseVertex;     // Added to each index value
    GLuint baseInstance;   // Offset into per-instance data (for gl_BaseInstance)
};

/// @brief Manages indirect draw commands and per-instance model matrix SSBO.
///
/// Builds draw commands from batched render data, packs model matrices
/// into an SSBO accessible via gl_BaseInstance + gl_InstanceID, and
/// dispatches via glMultiDrawElementsIndirect.
class IndirectBuffer
{
public:
    IndirectBuffer();
    ~IndirectBuffer();

    // Non-copyable
    IndirectBuffer(const IndirectBuffer&) = delete;
    IndirectBuffer& operator=(const IndirectBuffer&) = delete;

    /// @brief Adds a draw command for a mesh with instance matrices.
    /// @param poolEntry The mesh's location in the shared MeshPool.
    /// @param matrices Per-instance model matrices.
    void addCommand(const MeshPoolEntry& poolEntry,
                    const std::vector<glm::mat4>& matrices);

    /// @brief Clears all commands and resets for a new frame.
    void clear();

    /// @brief Uploads commands and matrices to GPU buffers.
    void upload();

    /// @brief Issues glMultiDrawElementsIndirect with all uploaded commands.
    void draw() const;

    /// @brief Returns the number of draw commands.
    int getCommandCount() const;

    /// @brief Returns the total number of instances across all commands.
    int getTotalInstances() const;

    /// @brief Gets the matrix SSBO handle (for shader binding).
    GLuint getMatrixSsbo() const;

    /// @brief Gets the command buffer handle (for GPU culling modification).
    GLuint getCommandBuffer() const;

private:
    // CPU-side staging buffers (rebuilt per frame)
    std::vector<DrawElementsIndirectCommand> m_commands;
    std::vector<glm::mat4> m_allMatrices;

    // GPU buffers
    GLuint m_commandBuffer = 0;     // GL_DRAW_INDIRECT_BUFFER
    GLuint m_matrixSsbo = 0;        // GL_SHADER_STORAGE_BUFFER (per-instance mat4)
    size_t m_commandCapacity = 0;   // Allocated command count
    size_t m_matrixCapacity = 0;    // Allocated matrix count
};

} // namespace Vestige
