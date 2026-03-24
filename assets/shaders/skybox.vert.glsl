/// @file skybox.vert.glsl
/// @brief Skybox vertex shader — strips view translation so the sky is always centered on the camera.
#version 450 core

layout(location = 0) in vec3 position;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_texCoord;

void main()
{
    v_texCoord = position;

    // Strip translation from view matrix — sky stays centered on camera
    mat4 viewNoTranslation = mat4(mat3(u_view));
    vec4 pos = u_projection * viewNoTranslation * vec4(position, 1.0);

    // Reverse-Z: far plane is depth 0.0. Set z = 0 so skybox renders behind everything.
    gl_Position = pos.xyww;
    gl_Position.z = 0.0;
}
