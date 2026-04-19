// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_vectors_object.frag.glsl
/// @brief Per-object motion vector fragment shader (AUDIT.md §H15 / FIXPLAN G1).
///
/// Emits the screen-space vector `currentUV - previousUV` by perspective-
/// dividing the clip-space positions interpolated from the vertex shader.
/// The output format matches the full-screen motion pass so downstream
/// TAA resolve doesn't care which pass wrote the pixel.
#version 450 core

in vec4 v_currentClip;
in vec4 v_prevClip;

out vec4 fragColor;

// Guard against the vertex being on / behind the camera plane (w ~= 0).
// Without this a divide-by-zero produces NaN/Inf in the motion vector,
// which TAA resolve then clamps away — but the NaN can leak into
// neighbouring pixels via bilinear sampling.
// TODO: revisit clip-divide epsilon via Formula Workbench once reference
// data is available (currently using the conventional 1e-6 guard).
vec2 safeClipDivide(vec4 clip)
{
    return (abs(clip.w) > 1e-6) ? (clip.xy / clip.w) : vec2(0.0);
}

void main()
{
    vec2 currNDC = safeClipDivide(v_currentClip);
    vec2 prevNDC = safeClipDivide(v_prevClip);

    vec2 currUV = currNDC * 0.5 + 0.5;
    vec2 prevUV = prevNDC * 0.5 + 0.5;

    fragColor = vec4(currUV - prevUV, 0.0, 1.0);
}
