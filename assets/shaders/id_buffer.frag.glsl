// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file id_buffer.frag.glsl
/// @brief Entity ID buffer fragment shader — outputs the entity ID as an RGB color for mouse picking.
#version 450 core

/// Entity ID encoded as an RGB color (passed from CPU).
uniform vec3 u_entityColor;

out vec4 fragColor;

void main()
{
    fragColor = vec4(u_entityColor, 1.0);
}
