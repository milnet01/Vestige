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

    // Set z = w so after perspective divide, depth = 1.0 (far plane)
    gl_Position = pos.xyww;
}
