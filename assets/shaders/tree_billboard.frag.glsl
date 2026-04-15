// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_billboard.frag.glsl
/// @brief Tree billboard LOD fragment shader — alpha-tested texture sampling with crossfade discard.
#version 450 core

in vec2 v_texCoord;
in float v_alpha;

uniform sampler2D u_texture;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(u_texture, v_texCoord);

    if (texel.a < 0.1)
        discard;

    fragColor = vec4(texel.rgb, texel.a * v_alpha);

    if (fragColor.a < 0.01)
        discard;
}
