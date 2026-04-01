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

// Bone vertex attributes (locations 10-11)
layout(location = 10) in ivec4 boneIds;
layout(location = 11) in vec4 boneWeights;

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

// Bone matrices for skeletal animation (binding 2)
layout(std430, binding = 2) buffer BoneMatrices
{
    mat4 u_boneMatrices[];
};

// Morph target deltas (binding 3)
// Layout: [pos deltas: target0_vert0..N, target1_vert0..N, ...]
//         [nor deltas: target0_vert0..N, target1_vert0..N, ...]
// Each element is vec4 (xyz = delta, w = 0).
layout(std430, binding = 3) buffer MorphDeltas
{
    vec4 u_morphDeltas[];
};

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform bool u_useInstancing;
uniform bool u_useMDI;
uniform bool u_hasBones;      // True for skinned meshes — enables bone transform
uniform mat3 u_normalMatrix;  // Precomputed on CPU for non-instanced path
uniform vec4 u_clipPlane;     // Water clip plane (0,0,0,0 = disabled)

// Morph target uniforms
uniform int u_morphTargetCount;   // 0 = no morph targets
uniform int u_morphVertexCount;   // Vertex count for SSBO indexing
uniform float u_morphWeights[8];  // Max 8 simultaneous morph targets

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

    // --- Morph target deformation (before bone skinning) ---
    vec3 morphedPos = position;
    vec3 morphedNor = normal;

    if (u_morphTargetCount > 0)
    {
        int vid = gl_VertexID;
        int vc = u_morphVertexCount;
        int tc = u_morphTargetCount;

        for (int i = 0; i < tc; i++)
        {
            float w = u_morphWeights[i];
            if (w != 0.0)
            {
                // Position delta: buffer[i * vertCount + vertexID]
                morphedPos += w * u_morphDeltas[i * vc + vid].xyz;
                // Normal delta: buffer[targetCount * vertCount + i * vertCount + vertexID]
                morphedNor += w * u_morphDeltas[tc * vc + i * vc + vid].xyz;
            }
        }
    }

    // --- Skeletal animation skinning ---
    vec3 skinnedPos;
    vec3 skinnedNormal;
    vec3 skinnedTangent;
    vec3 skinnedBitangent;

    if (u_hasBones)
    {
        mat4 boneTransform = boneWeights.x * u_boneMatrices[boneIds.x]
                           + boneWeights.y * u_boneMatrices[boneIds.y]
                           + boneWeights.z * u_boneMatrices[boneIds.z]
                           + boneWeights.w * u_boneMatrices[boneIds.w];

        skinnedPos       = vec3(boneTransform * vec4(morphedPos, 1.0));
        mat3 boneMat3    = mat3(boneTransform);
        skinnedNormal    = boneMat3 * morphedNor;
        skinnedTangent   = boneMat3 * tangent;
        skinnedBitangent = boneMat3 * bitangent;
    }
    else
    {
        skinnedPos       = morphedPos;
        skinnedNormal    = morphedNor;
        skinnedTangent   = tangent;
        skinnedBitangent = bitangent;
    }

    vec4 worldPosition = model * vec4(skinnedPos, 1.0);
    gl_Position = u_projection * u_view * worldPosition;

    // Water clip plane for reflection/refraction passes
    gl_ClipDistance[0] = dot(worldPosition, u_clipPlane);

    v_fragPosition = vec3(worldPosition);
    v_normal = normalMatrix * skinnedNormal;

    // Compute TBN matrix for normal mapping
    vec3 T = normalize(normalMatrix * skinnedTangent);
    vec3 B = normalize(normalMatrix * skinnedBitangent);
    vec3 N = normalize(normalMatrix * skinnedNormal);
    v_TBN = mat3(T, B, N);

    v_color = color;
    v_texCoord = texCoord;
    v_viewDepth = -(u_view * worldPosition).z;
}
