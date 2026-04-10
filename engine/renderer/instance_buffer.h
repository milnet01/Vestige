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
