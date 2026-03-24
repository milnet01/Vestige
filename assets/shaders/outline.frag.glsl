/// @file outline.frag.glsl
/// @brief Selection outline fragment shader — outputs a solid color for the selected entity outline.
#version 450 core

uniform vec3 u_outlineColor;

out vec4 fragColor;

void main()
{
    fragColor = vec4(u_outlineColor, 1.0);
}
