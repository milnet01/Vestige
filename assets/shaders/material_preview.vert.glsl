/// @file material_preview.vert.glsl
/// @brief Material preview vertex shader — transforms preview sphere geometry with cofactor normal matrix.
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

/// Computes the cofactor matrix for correct non-uniform scale normal transform.
mat3 cofactorMatrix(mat3 m)
{
    return mat3(cross(m[1], m[2]),
                cross(m[2], m[0]),
                cross(m[0], m[1]));
}

void main()
{
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(cofactorMatrix(mat3(u_model)) * aNormal);
    vTexCoord = aTexCoord;
    gl_Position = u_projection * u_view * worldPos;
}
