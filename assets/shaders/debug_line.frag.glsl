/// @file debug_line.frag.glsl
/// @brief Debug line fragment shader — outputs per-vertex color for debug and gizmo lines.
#version 450 core

in vec3 v_color;

out vec4 fragColor;

void main()
{
    fragColor = vec4(v_color, 1.0);
}
