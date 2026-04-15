// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skybox.vert.glsl
/// @brief Skybox vertex shader — strips view translation so the sky is always centered on the camera.
#version 450 core

layout(location = 0) in vec3 position;

uniform mat4 u_view;
uniform mat4 u_projection;

// AUDIT.md §H18 / radiosity-bake fix.
// Z convention for the skybox far-plane depth:
//   0.0      → reverse-Z (main render path, glClipControl GL_ZERO_TO_ONE,
//              cleared depth 0, GL_GEQUAL — skybox at z_ndc=0 passes)
//   0.99999  → forward-Z (capture paths captureLightProbe/captureSHGrid,
//              glClipControl GL_NEGATIVE_ONE_TO_ONE, cleared depth 1,
//              GL_LESS — skybox at z_ndc≈1 passes, geometry at z<1 wins)
//
// Setting z = u_skyboxFarDepth * pos.w makes z/w = u_skyboxFarDepth after
// the perspective divide, regardless of how the projection matrix was set
// up. Defaults to 0 so existing call sites keep their old reverse-Z
// behaviour without any changes.
uniform float u_skyboxFarDepth = 0.0;

out vec3 v_texCoord;

void main()
{
    v_texCoord = position;

    // Strip translation from view matrix — sky stays centered on camera
    mat4 viewNoTranslation = mat4(mat3(u_view));
    vec4 pos = u_projection * viewNoTranslation * vec4(position, 1.0);

    // After perspective divide, z/w = u_skyboxFarDepth. See the comment on
    // the uniform above for which value matches which Z convention.
    gl_Position = pos.xyww;
    gl_Position.z = u_skyboxFarDepth * pos.w;
}
