// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cubemap_render.vert.glsl
/// @brief Cubemap face rendering vertex shader — used for IBL convolution and prefiltering passes.
#version 450 core

layout(location = 0) in vec3 position;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_texCoord;

void main()
{
    v_texCoord = position;
    gl_Position = u_projection * u_view * vec4(position, 1.0);
}
