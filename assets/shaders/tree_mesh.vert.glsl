/// @file tree_mesh.vert.glsl
/// @brief Tree mesh LOD vertex shader — instanced rendering with per-instance rotation, scale, and crossfade alpha.
#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;

// Per-instance
layout(location = 3) in vec3 i_position;
layout(location = 4) in float i_rotation;
layout(location = 5) in float i_scale;
layout(location = 6) in float i_alpha;

uniform mat4 u_viewProjection;
uniform float u_time;
uniform vec4 u_clipPlane;     // Water clip plane (0,0,0,0 = disabled)

out vec3 v_color;
out float v_alpha;

void main()
{
    // Y-axis rotation
    float s = sin(i_rotation);
    float c = cos(i_rotation);
    vec3 rotated = vec3(
        a_position.x * c - a_position.z * s,
        a_position.y,
        a_position.x * s + a_position.z * c
    );

    // Scale and position
    vec3 worldPos = rotated * i_scale + i_position;

    // Gentle wind sway for crown (upper parts only)
    float swayAmount = max(0.0, a_position.y - 2.0) * 0.02;
    float windPhase = u_time * 1.5 + i_position.x * 0.3 + i_position.z * 0.2;
    worldPos.x += sin(windPhase) * swayAmount * i_scale;

    vec4 worldPosition = vec4(worldPos, 1.0);
    gl_Position = u_viewProjection * worldPosition;
    gl_ClipDistance[0] = dot(worldPosition, u_clipPlane);
    v_color = a_color;
    v_alpha = i_alpha;
}
