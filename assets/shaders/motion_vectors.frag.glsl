// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file motion_vectors.frag.glsl
/// @brief Motion-vector COMBINE pass (Slice R1).
///
/// Produces the final motion buffer TAA resolve samples: object motion where the
/// opaque scene pass wrote it (coverage flag set in the MRT attachment), (0,0) on
/// sky, and camera-reprojection motion everywhere else (cloth / terrain / water /
/// particles, and behind depth-write-off transparent geometry). This replaces the
/// old per-object overlay re-draw — the geometry pass now emits object motion via
/// MRT, and this pass just selects between that and the camera fallback.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_depthTexture;         // Current frame depth (resolved)
uniform sampler2D u_sceneMotion;          // Scene-pass motion MRT (.rg motion, .b coverage)
uniform mat4 u_currentInvViewProjection;  // Inverse VP for this frame
uniform mat4 u_prevViewProjection;        // VP matrix from previous frame

out vec4 fragColor;

void main()
{
    // Object motion from the opaque scene pass wins where its coverage flag is set.
    vec4 sceneMotion = texture(u_sceneMotion, v_texCoord);
    if (sceneMotion.b > 0.5)
    {
        fragColor = vec4(sceneMotion.rg, 0.0, 1.0);
        return;
    }

    // Read depth and reconstruct world position for the camera-motion fallback.
    float depth = texture(u_depthTexture, v_texCoord).r;

    // Reverse-Z: sky is at depth 0.0 (far plane)
    if (depth <= 0.0001)
    {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // NDC position (current frame). With glClipControl [0,1], depth IS the NDC z.
    vec2 ndc = v_texCoord * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth, 1.0);

    // Reconstruct world position
    vec4 worldPos = u_currentInvViewProjection * clipPos;
    worldPos /= worldPos.w;

    // Project to previous frame's clip space
    vec4 prevClip = u_prevViewProjection * worldPos;
    prevClip /= prevClip.w;

    // Previous UV
    vec2 prevUV = prevClip.xy * 0.5 + 0.5;

    // Motion vector = current UV - previous UV
    vec2 motion = v_texCoord - prevUV;

    fragColor = vec4(motion, 0.0, 1.0);
}
