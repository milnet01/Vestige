/// @file screen_quad.vert.glsl
/// @brief Full-screen quad vertex shader for post-processing passes.
#version 450 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

out vec2 v_texCoord;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    v_texCoord = texCoord;
}
