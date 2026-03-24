/// @file brush_preview.vert.glsl
/// @brief Terrain brush preview vertex shader — transforms the brush indicator mesh.
#version 450 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_mvp;

void main()
{
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
