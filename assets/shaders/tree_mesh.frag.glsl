// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_mesh.frag.glsl
/// @brief Tree mesh LOD fragment shader — outputs instanced vertex color with crossfade alpha discard.
#version 450 core

in vec3 v_color;
in float v_alpha;

out vec4 fragColor;

void main()
{
    fragColor = vec4(v_color, v_alpha);
    if (fragColor.a < 0.01)
        discard;
}
