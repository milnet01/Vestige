/// @file smaa_edge.vert.glsl
/// @brief SMAA edge detection vertex shader — precomputes neighbor offsets.
#version 450 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

out vec2 v_texCoord;
out vec4 v_offset[3]; // Precomputed offsets for neighbor sampling

void main()
{
    v_texCoord = a_texCoord;

    // Precompute offsets for left, top, right, bottom, left-left, top-top
    v_offset[0] = u_rtMetrics.xyxy * vec4(-1.0, 0.0, 0.0, -1.0) + a_texCoord.xyxy;
    v_offset[1] = u_rtMetrics.xyxy * vec4( 1.0, 0.0, 0.0,  1.0) + a_texCoord.xyxy;
    v_offset[2] = u_rtMetrics.xyxy * vec4(-2.0, 0.0, 0.0, -2.0) + a_texCoord.xyxy;

    gl_Position = vec4(a_position, 0.0, 1.0);
}
