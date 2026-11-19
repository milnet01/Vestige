// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file text.frag.glsl
/// @brief Text rendering fragment shader — samples glyph atlas alpha and
///        applies the per-vertex RGBA color (Phase 10.9 Pe1 batched path).
#version 450 core

in vec2 v_texCoord;
in vec4 v_color;

uniform sampler2D u_glyphAtlas;

out vec4 fragColor;

void main()
{
    float alpha = texture(u_glyphAtlas, v_texCoord).r;
    if (alpha < 0.01)
    {
        discard;
    }
    fragColor = vec4(v_color.rgb, alpha * v_color.a);
}
