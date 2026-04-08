#version 450 core

layout (location = 0) in vec2 a_position;
layout (location = 1) in vec2 a_texCoord;
layout (location = 2) in vec4 a_color;

uniform mat4 u_projection;

out vec2 v_texCoord;
out vec4 v_color;

void main()
{
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
    v_color = a_color;
}
