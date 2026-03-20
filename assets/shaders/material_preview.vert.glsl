#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(u_model))) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = u_projection * u_view * worldPos;
}
