// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file god_rays_combine.frag.glsl
/// @brief Phase 10 slice 11.5 — additive upsample-combine for screen-space
///        god rays. Samples the half-resolution gather result (linear filter →
///        smooth upscale) and outputs it; the renderer draws this full-res with
///        additive blending into the pre-bloom HDR scene, so the shafts bloom
///        and feed auto-exposure. Alpha is 0 so the additive blend leaves the
///        scene's alpha channel untouched.
#version 450 core

in  vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D u_godRaysTexture; // half-res gather result (unit 0)

void main()
{
    FragColor = vec4(texture(u_godRaysTexture, v_texCoord).rgb, 0.0);
}
