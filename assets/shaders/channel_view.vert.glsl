// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file channel_view.vert.glsl
/// @brief Fullscreen quad vertex shader for texture/HDRI channel viewer.
#version 450 core

// Fullscreen triangle trick — no VBO needed, use glDrawArrays(GL_TRIANGLES, 0, 3)
out vec2 v_texCoord;

void main()
{
    // Generate fullscreen triangle from vertex ID
    v_texCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(v_texCoord * 2.0 - 1.0, 0.0, 1.0);
}
