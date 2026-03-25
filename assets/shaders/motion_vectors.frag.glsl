/// @file motion_vectors.frag.glsl
/// @brief Computes per-pixel motion vectors from current and previous frame view-projection matrices.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_depthTexture;         // Current frame depth
uniform mat4 u_currentInvViewProjection;  // Inverse VP for this frame
uniform mat4 u_prevViewProjection;        // VP matrix from previous frame

out vec4 fragColor;

void main()
{
    // Read depth and reconstruct world position
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
