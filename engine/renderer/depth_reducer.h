// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file depth_reducer.h
/// @brief GPU depth buffer min/max reduction for SDSM cascade optimization.
#pragma once

#include "renderer/shader.h"

#include <glad/gl.h>

namespace Vestige
{

/// @brief Dispatches a compute shader to find the min/max depth in the scene.
///
/// Uses double-buffered SSBOs with one-frame delay to avoid GPU stalls.
/// The reverse-Z infinite projection means z_linear = near / depth.
class DepthReducer
{
public:
    DepthReducer();
    ~DepthReducer();

    // Non-copyable
    DepthReducer(const DepthReducer&) = delete;
    DepthReducer& operator=(const DepthReducer&) = delete;

    /// @brief Loads the compute shader.
    /// @param shaderPath Path to depth_reduce.comp.glsl.
    /// @return True if shader loaded successfully.
    bool init(const std::string& shaderPath);

    /// @brief Dispatches the depth reduction compute shader.
    /// @param depthTexture The resolved depth texture to analyze.
    /// @param width Texture width in pixels.
    /// @param height Texture height in pixels.
    void dispatch(GLuint depthTexture, int width, int height);

    /// @brief Reads the depth bounds from the previous frame's SSBO.
    /// @param cameraNear Camera near plane distance (for depth linearization).
    /// @param outNear Nearest geometry distance (view-space).
    /// @param outFar Farthest geometry distance (view-space).
    /// @return True if valid bounds were found (false on first frame or empty scene).
    bool readBounds(float cameraNear, float& outNear, float& outFar);

private:
    Shader m_computeShader;
    GLuint m_ssbo[2] = {0, 0};   // Double-buffered SSBOs
    int m_writeIndex = 0;         // Which SSBO to write into this frame
    int m_frameCount = 0;         // Counts frames since init (need >=2 for valid read)
};

} // namespace Vestige
