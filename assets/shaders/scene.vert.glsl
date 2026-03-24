/// @file scene.vert.glsl
/// @brief Main scene vertex shader — transforms geometry with instancing, TBN matrix, and TAA jitter support.
#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 texCoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;

// Per-instance model matrix (locations 6-9, one vec4 column each)
layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform bool u_useInstancing;
uniform mat3 u_normalMatrix;  // Precomputed on CPU for non-instanced path

out vec3 v_fragPosition;
out vec3 v_normal;
out vec3 v_color;
out vec2 v_texCoord;
out float v_viewDepth;
out mat3 v_TBN;

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
    mat4 model;
    mat3 normalMatrix;

    if (u_useInstancing)
    {
        model = mat4(instanceModelCol0, instanceModelCol1,
                     instanceModelCol2, instanceModelCol3);
        normalMatrix = cofactorMatrix(mat3(model));
    }
    else
    {
        model = u_model;
        normalMatrix = u_normalMatrix;
    }

    vec4 worldPosition = model * vec4(position, 1.0);
    gl_Position = u_projection * u_view * worldPosition;

    v_fragPosition = vec3(worldPosition);
    v_normal = normalMatrix * normal;

    // Compute TBN matrix for normal mapping
    vec3 T = normalize(normalMatrix * tangent);
    vec3 B = normalize(normalMatrix * bitangent);
    vec3 N = normalize(normalMatrix * normal);
    v_TBN = mat3(T, B, N);

    v_color = color;
    v_texCoord = texCoord;
    v_viewDepth = -(u_view * worldPosition).z;
}
