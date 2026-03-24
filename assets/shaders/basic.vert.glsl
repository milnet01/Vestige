/// @file basic.vert.glsl
/// @brief Basic vertex shader with model-view-projection transform and normal matrix via cofactor.
#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_color;
out vec3 v_normal;
out vec3 v_fragPosition;

/// Compute cofactor matrix (equivalent to transpose(inverse(m)) * det(m)).
/// Since we normalize the result, the determinant scaling cancels out.
/// 3 cross products — much cheaper than inverse() on the GPU.
mat3 cofactorMatrix(mat3 m)
{
    return mat3(cross(m[1], m[2]),
                cross(m[2], m[0]),
                cross(m[0], m[1]));
}

void main()
{
    vec4 worldPosition = u_model * vec4(position, 1.0);
    gl_Position = u_projection * u_view * worldPosition;

    v_color = color;
    v_normal = normalize(cofactorMatrix(mat3(u_model)) * normal);
    v_fragPosition = vec3(worldPosition);
}
