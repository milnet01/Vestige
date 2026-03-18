#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_currentTexture;       // Current frame HDR
uniform sampler2D u_historyTexture;       // Previous frame TAA output
uniform sampler2D u_motionVectorTexture;  // Per-pixel motion in UV space
uniform float u_feedbackFactor;           // History blend weight (e.g. 0.9)
uniform vec2 u_texelSize;                 // 1.0 / resolution

out vec4 fragColor;

void main()
{
    // Sample current frame
    vec3 currentColor = texture(u_currentTexture, v_texCoord).rgb;

    // Read motion vector and reproject
    vec2 motion = texture(u_motionVectorTexture, v_texCoord).rg;
    vec2 historyUV = v_texCoord - motion;

    // Neighborhood color clamping (5-tap cross pattern)
    // Prevents ghosting from stale history
    vec3 n0 = texture(u_currentTexture, v_texCoord + vec2(-u_texelSize.x, 0.0)).rgb;
    vec3 n1 = texture(u_currentTexture, v_texCoord + vec2( u_texelSize.x, 0.0)).rgb;
    vec3 n2 = texture(u_currentTexture, v_texCoord + vec2(0.0, -u_texelSize.y)).rgb;
    vec3 n3 = texture(u_currentTexture, v_texCoord + vec2(0.0,  u_texelSize.y)).rgb;

    vec3 neighborMin = min(currentColor, min(min(n0, n1), min(n2, n3)));
    vec3 neighborMax = max(currentColor, max(max(n0, n1), max(n2, n3)));

    // Sample history and clamp to neighborhood AABB
    vec3 historyColor = texture(u_historyTexture, historyUV).rgb;
    historyColor = clamp(historyColor, neighborMin, neighborMax);

    // Reduce feedback when history UV is outside [0,1] (disoccluded)
    float feedback = u_feedbackFactor;
    if (historyUV.x < 0.0 || historyUV.x > 1.0 || historyUV.y < 0.0 || historyUV.y > 1.0)
    {
        feedback = 0.0;
    }

    // Blend
    vec3 result = mix(currentColor, historyColor, feedback);
    fragColor = vec4(result, 1.0);
}
