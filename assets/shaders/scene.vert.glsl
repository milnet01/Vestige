/// @file scene.vert.glsl
/// @brief Main scene vertex shader — transforms geometry with instancing, MDI, TBN matrix, and TAA jitter support.
#version 460 core
#extension GL_ARB_shader_draw_parameters : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 texCoord;
layout(location = 4) in vec3 tangent;
layout(location = 5) in vec3 bitangent;

// Per-instance model matrix (locations 6-9, one vec4 column each) — legacy instancing
layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

// MDI per-instance model matrices (accessed via gl_BaseInstance + gl_InstanceID)
layout(std430, binding = 0) buffer ModelMatrices
{
    mat4 u_modelMatrices[];
};

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform bool u_useInstancing;
uniform bool u_useMDI;
uniform mat3 u_normalMatrix;  // Precomputed on CPU for non-instanced path
uniform vec4 u_clipPlane;     // Water clip plane (0,0,0,0 = disabled)

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

    if (u_useMDI)
    {
        // MDI path: model matrix from SSBO indexed by gl_BaseInstance + gl_InstanceID
        model = u_modelMatrices[gl_BaseInstance + gl_InstanceID];
        normalMatrix = cofactorMatrix(mat3(model));
    }
    else if (u_useInstancing)
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

    // Water clip plane for reflection/refraction passes
    gl_ClipDistance[0] = dot(worldPosition, u_clipPlane);

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
