// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file debug_line.frag.glsl
/// @brief Debug line fragment shader — outputs per-vertex color for debug and gizmo lines.
///
/// Lines are drawn into the editor's LDR output FBO, whose depth buffer is
/// the selection-outline renderbuffer and never holds scene depth. So the
/// occlusion test is done here against the resolved scene depth texture
/// instead (3D_E-0699: the ground grid showed through terrain and grass).
#version 450 core

in vec3 v_color;

uniform bool u_depthOcclude;
uniform sampler2D u_sceneDepth;
uniform vec4 u_viewportRect;  // x, y, width, height of the current viewport

out vec4 fragColor;

// Reverse-Z: a larger depth is nearer. A line whose depth falls below the
// scene's by more than this relative margin is behind a surface. The margin
// absorbs depth-resolve precision so a line resting on a surface still shows.
const float OCCLUSION_RELATIVE_MARGIN = 1.0e-4;

void main()
{
    if (u_depthOcclude)
    {
        vec2 uv = (gl_FragCoord.xy - u_viewportRect.xy) / u_viewportRect.zw;
        float sceneDepth = texture(u_sceneDepth, uv).r;
        if (gl_FragCoord.z < sceneDepth * (1.0 - OCCLUSION_RELATIVE_MARGIN))
        {
            discard;
        }
    }
    fragColor = vec4(v_color, 1.0);
}
