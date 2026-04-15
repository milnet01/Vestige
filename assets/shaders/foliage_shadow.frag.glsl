// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file foliage_shadow.frag.glsl
/// @brief Foliage shadow depth fragment shader — alpha test only.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_texture;

void main()
{
    float alpha = texture(u_texture, v_texCoord).a;

    // Alpha test — discard transparent pixels so only grass blade shapes cast shadows
    if (alpha < 0.5)
        discard;

    // Depth is written automatically by the fixed-function pipeline
}
