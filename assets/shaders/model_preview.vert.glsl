/// @file model_preview.vert.glsl
/// @brief Model viewer vertex shader — matches the engine Vertex struct layout.
///        Paired with material_preview.frag.glsl for the same PBR/Blinn-Phong output.
#version 450 core

// Matches Mesh VAO layout (engine/renderer/mesh.cpp):
// 0 = position, 1 = normal, 2 = color, 3 = texCoord, 4 = tangent, 5 = bitangent
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
// location 2 = color (unused in preview)
layout(location = 3) in vec2 a_texCoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_texCoord;

/// Computes the cofactor matrix for correct non-uniform scale normal transform.
mat3 cofactorMatrix(mat3 m)
{
    return mat3(cross(m[1], m[2]),
                cross(m[2], m[0]),
                cross(m[0], m[1]));
}

void main()
{
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_normal = normalize(cofactorMatrix(mat3(u_model)) * a_normal);
    v_texCoord = a_texCoord;
    gl_Position = u_projection * u_view * worldPos;
}
