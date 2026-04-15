// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_culler.h
/// @brief GPU-driven frustum culling via compute shader.
#pragma once

#include "renderer/shader.h"
#include "renderer/indirect_buffer.h"
#include "utils/aabb.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace Vestige
{

/// @brief Per-object data for GPU culling (uploaded to SSBO).
struct CullObjectData
{
    glm::vec4 center;    // xyz = AABB center, w = unused
    glm::vec4 extent;    // xyz = AABB half-extents, w = unused
};

/// @brief Performs GPU-driven frustum culling on indirect draw commands.
///
/// Before rendering, dispatches a compute shader that tests each object's
/// AABB against 6 frustum planes. Objects outside the frustum have their
/// instanceCount set to 0 in the indirect command buffer, effectively
/// culling them without CPU readback.
class GpuCuller
{
public:
    GpuCuller();
    ~GpuCuller();

    // Non-copyable
    GpuCuller(const GpuCuller&) = delete;
    GpuCuller& operator=(const GpuCuller&) = delete;

    /// @brief Loads the frustum culling compute shader.
    /// @param shaderPath Path to frustum_cull.comp.glsl.
    /// @return True if shader loaded successfully.
    bool init(const std::string& shaderPath);

    /// @brief Uploads object bounding data and dispatches the culling shader.
    /// @param commandBuffer The indirect command buffer to modify (instanceCount zeroed for culled).
    /// @param commandCount Number of draw commands.
    /// @param objects Per-object AABB data matching command order.
    /// @param frustumPlanes 6 frustum planes (left, right, bottom, top, near, far).
    void cull(GLuint commandBuffer, int commandCount,
              const std::vector<CullObjectData>& objects,
              const std::array<glm::vec4, 6>& frustumPlanes);

private:
    Shader m_computeShader;
    GLuint m_objectSsbo = 0;       // Per-object AABB data
    size_t m_objectCapacity = 0;   // Allocated object count
};

} // namespace Vestige
