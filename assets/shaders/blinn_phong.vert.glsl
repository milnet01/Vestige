#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 texCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_lightSpaceMatrix;

out vec3 v_fragPosition;
out vec3 v_normal;
out vec3 v_color;
out vec2 v_texCoord;
out vec4 v_fragPosLightSpace;

void main()
{
    vec4 worldPosition = u_model * vec4(position, 1.0);
    gl_Position = u_projection * u_view * worldPosition;

    v_fragPosition = vec3(worldPosition);
    v_normal = mat3(transpose(inverse(u_model))) * normal;
    v_color = color;
    v_texCoord = texCoord;
    v_fragPosLightSpace = u_lightSpaceMatrix * worldPosition;
}
