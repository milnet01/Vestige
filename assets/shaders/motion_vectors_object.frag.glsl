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

void main()
{
    vec2 currNDC = v_currentClip.xy / v_currentClip.w;
    vec2 prevNDC = v_prevClip.xy  / v_prevClip.w;

    vec2 currUV = currNDC * 0.5 + 0.5;
    vec2 prevUV = prevNDC * 0.5 + 0.5;

    fragColor = vec4(currUV - prevUV, 0.0, 1.0);
}
