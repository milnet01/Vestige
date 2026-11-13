// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file instance_buffer.h
/// @brief GPU buffer for per-instance model matrices used in instanced rendering.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief Manages a dynamic VBO for per-instance mat4 model matrices.
class InstanceBuffer
{
public:
    InstanceBuffer();
    ~InstanceBuffer();

    InstanceBuffer(const InstanceBuffer&) = delete;
    InstanceBuffer& operator=(const InstanceBuffer&) = delete;

    InstanceBuffer(InstanceBuffer&& other) noexcept;
    InstanceBuffer& operator=(InstanceBuffer&& other) noexcept;

    /// @brief Uploads model matrices to the GPU buffer.
    /// Reuses existing buffer capacity when possible.
    void upload(const std::vector<glm::mat4>& matrices);

    /// @brief Fewest instances a buffer is ever allocated for.
    static constexpr size_t MIN_CAPACITY = 256;

    /// @brief How many instances to allocate for when @p needed no longer
    ///        fit in @p current: at least double, and at least MIN_CAPACITY.
    ///        Recreating the buffer costs a slow frame (measured on the
    ///        meadow flythrough, 3D_E-0795), and an exact fit recreated it
    ///        each time one more object of a kind came into view.
    static size_t grownCapacity(size_t needed, size_t current);

    /// @brief Gets the GL buffer handle.
    GLuint getHandle() const;

    /// @brief Gets the number of instances from the last upload.
    size_t getInstanceCount() const;

private:
    GLuint m_vbo = 0;
    size_t m_instanceCount = 0;
    size_t m_capacity = 0;
};

} // namespace Vestige
