/// @file equirect_preview.vert.glsl
/// @brief Fullscreen quad vertex shader for equirectangular HDRI preview.
#version 450 core

out vec2 v_texCoord;

void main()
{
    // Generate fullscreen triangle from vertex ID
    v_texCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(v_texCoord * 2.0 - 1.0, 0.0, 1.0);
}
