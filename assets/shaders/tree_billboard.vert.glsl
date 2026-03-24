/// @file tree_billboard.vert.glsl
/// @brief Tree billboard LOD vertex shader — camera-facing quads with per-instance transform and crossfade alpha.
#version 450 core

layout(location = 0) in vec2 a_offset;
layout(location = 1) in vec2 a_texCoord;

// Per-instance
layout(location = 3) in vec3 i_position;
layout(location = 4) in float i_rotation;
layout(location = 5) in float i_scale;
layout(location = 6) in float i_alpha;

uniform mat4 u_viewProjection;
uniform vec3 u_cameraRight;
uniform vec3 u_cameraUp;

out vec2 v_texCoord;
out float v_alpha;

void main()
{
    // Billboard facing camera: expand quad in camera-aligned directions
    float halfWidth = 2.5 * i_scale;
    float height = 5.0 * i_scale;

    vec3 worldPos = i_position
        + u_cameraRight * a_offset.x * halfWidth
        + u_cameraUp * a_offset.y * height * 0.5;

    gl_Position = u_viewProjection * vec4(worldPos, 1.0);
    v_texCoord = a_texCoord;
    v_alpha = i_alpha;
}
