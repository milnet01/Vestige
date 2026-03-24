/// @file foliage.vert.glsl
/// @brief Foliage vertex shader — instanced grass/plants with wind animation, distance fade, and LOD crossfading.
#version 450 core

// Star mesh vertex attributes
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texCoord;

// Per-instance attributes
layout(location = 3) in vec3 i_position;
layout(location = 4) in float i_rotation;
layout(location = 5) in float i_scale;
layout(location = 6) in vec3 i_colorTint;

uniform mat4 u_viewProjection;
uniform mat4 u_view;
uniform float u_time;
uniform vec3 u_windDirection;
uniform float u_windAmplitude;
uniform float u_windFrequency;
uniform float u_maxDistance;
uniform vec3 u_cameraPos;

out vec2 v_texCoord;
out vec3 v_colorTint;
out float v_alpha;
out vec3 v_fragPosition;
out float v_viewDepth;
out float v_heightAO;

void main()
{
    // Apply per-instance Y-axis rotation
    float s = sin(i_rotation);
    float c = cos(i_rotation);
    vec3 rotated = vec3(
        a_position.x * c - a_position.z * s,
        a_position.y,
        a_position.x * s + a_position.z * c
    );

    // Apply scale and world position
    vec3 worldPos = rotated * i_scale + i_position;

    // Wind animation — only affects upper portion (modulated by local Y)
    float heightFactor = a_position.y * 2.5; // 0 at base, ~1 at tip
    float windPhase = u_time * u_windFrequency
                    + i_position.x * 0.5
                    + i_position.z * 0.3;
    float windOffset = sin(windPhase) * u_windAmplitude * heightFactor;
    worldPos.xz += u_windDirection.xz * windOffset;

    // Distance-based alpha fade
    float dist = distance(u_cameraPos, i_position);
    v_alpha = 1.0 - smoothstep(u_maxDistance * 0.8, u_maxDistance, dist);

    gl_Position = u_viewProjection * vec4(worldPos, 1.0);

    v_texCoord = a_texCoord;
    v_colorTint = i_colorTint;

    // Shadow + lighting data
    v_fragPosition = worldPos;
    v_viewDepth = -(u_view * vec4(worldPos, 1.0)).z;

    // Height-based ambient occlusion: dark at base, bright at tip
    v_heightAO = pow(clamp(heightFactor, 0.0, 1.0), 1.5);
}
