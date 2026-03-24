/// @file outline.vert.glsl
/// @brief Selection outline vertex shader — transforms the scaled-up mesh for stencil-based outlining.
#version 450 core

layout(location = 0) in vec3 position;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

void main()
{
    gl_Position = u_viewProjection * u_model * vec4(position, 1.0);
}
