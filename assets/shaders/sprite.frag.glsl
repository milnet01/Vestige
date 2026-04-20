// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#version 450 core

in vec2 v_uv;
in vec4 v_tint;

uniform sampler2D u_atlas;

out vec4 FragColor;

void main()
{
    vec4 texel = texture(u_atlas, v_uv);
    // Premultiplied tint keeps alpha behaviour stable under blend.
    FragColor = texel * v_tint;
    // Alpha test for fully-transparent pixels — cheap, avoids piling
    // zero-alpha fragments onto the blend pipeline for sprite sheets with
    // large transparent margins.
    if (FragColor.a <= 0.001) {
        discard;
    }
}
