// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

// Per-instance PREVIOUS-frame model matrix (locations 12-15) — legacy instancing,
// for motion vectors (Slice R1). Parallel stream to locations 6-9.
layout(location = 12) in vec4 instancePrevModelCol0;
layout(location = 13) in vec4 instancePrevModelCol1;
layout(location = 14) in vec4 instancePrevModelCol2;
layout(location = 15) in vec4 instancePrevModelCol3;

// MDI per-instance model matrices (accessed via gl_BaseInstance + gl_InstanceID)
layout(std430, binding = 0) buffer ModelMatrices
{
    mat4 u_modelMatrices[];
};

// MDI per-instance PREVIOUS-frame model matrices (binding 4), in lock-step with
// binding 0 — for motion vectors (Slice R1). Indexed identically to the model SSBO.
layout(std430, binding = 4) buffer PrevModelMatrices
{
    mat4 u_prevModelMatrices[];
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
uniform mat4 u_prevModel;          // Previous-frame model (per-entity path) — motion vectors (R1)
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_prevViewProjection; // Previous-frame view-projection — motion vectors (R1)
uniform bool u_useInstancing;
uniform bool u_useMDI;
uniform bool u_hasBones;      // True for skinned meshes — enables bone transform
uniform mat3 u_normalMatrix;  // Precomputed on CPU for non-instanced path
uniform vec4 u_clipPlane;     // Water clip plane (0,0,0,0 = disabled)

// Morph target uniforms
// Cap on simultaneous morph targets; must match the u_morphWeights[] size
// below and the C++-side MAX_MORPH_TARGETS constant.
const int MAX_MORPH_TARGETS = 8;

uniform int u_morphTargetCount;                   // 0 = no morph targets
uniform int u_morphVertexCount;                   // Vertex count for SSBO indexing
uniform float u_morphWeights[MAX_MORPH_TARGETS];  // Per-target blend weight

// Normalize `v`; fall back to `fallback` when length is too small for the
// division to be numerically meaningful. Prevents NaN/Inf propagation into
// the TBN basis when a mesh has degenerate tangents or a skinning / morph
// combination collapses a vector to zero.
// TODO: revisit safeNormalize epsilon via Formula Workbench once reference
// data is available (currently using the conventional 1e-6 guard).
vec3 safeNormalize(vec3 v, vec3 fallback)
{
    float lenSq = dot(v, v);
    return (lenSq > 1e-12) ? (v * inversesqrt(lenSq)) : fallback;
}

out vec3 v_fragPosition;
out vec3 v_normal;
out vec3 v_color;
out vec2 v_texCoord;
out float v_viewDepth;
out mat3 v_TBN;

// Motion-vector clip positions (Slice R1). Computed from the RAW base object-space
// position (pre-skin / pre-morph), matching the deleted per-object overlay — so
// skinned/morph meshes get byte-identical rigid-body motion (R2 adds animated-pose
// motion). The fragment shader perspective-divides these to currUV - prevUV.
out vec4 v_currentClip_motion;
out vec4 v_prevClip_motion;

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
    mat4 prevModel;   // Previous-frame model, selected from the SAME source as `model`
    mat3 normalMatrix;

    if (u_useMDI)
    {
        // MDI path: model matrix from SSBO indexed by gl_BaseInstance + gl_InstanceID
        model = u_modelMatrices[gl_BaseInstance + gl_InstanceID];
        prevModel = u_prevModelMatrices[gl_BaseInstance + gl_InstanceID];
        normalMatrix = cofactorMatrix(mat3(model));
    }
    else if (u_useInstancing)
    {
        model = mat4(instanceModelCol0, instanceModelCol1,
                     instanceModelCol2, instanceModelCol3);
        prevModel = mat4(instancePrevModelCol0, instancePrevModelCol1,
                         instancePrevModelCol2, instancePrevModelCol3);
        normalMatrix = cofactorMatrix(mat3(model));
    }
    else
    {
        model = u_model;
        prevModel = u_prevModel;
        normalMatrix = u_normalMatrix;
    }

    // --- Morph target deformation (before bone skinning) ---
    vec3 morphedPos = position;
    vec3 morphedNor = normal;

    if (u_morphTargetCount > 0)
    {
        int vid = gl_VertexID;
        int vc = u_morphVertexCount;
        // `tc` addresses the normal-delta region of the buffer and must
        // match the engine-side upload count exactly, so it stays
        // unclamped. `loopCount` bounds the iteration so a corrupt /
        // out-of-range uniform can never drive the loop past the
        // u_morphWeights[] array size.
        int tc = u_morphTargetCount;
        int loopCount = min(tc, MAX_MORPH_TARGETS);

        for (int i = 0; i < loopCount; i++)
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

    // Compute TBN matrix for normal mapping. Fall back to world-axis basis
    // vectors when any of the skinned/morphed inputs collapse to zero, so a
    // zero-length tangent never produces NaN lighting.
    vec3 T = safeNormalize(normalMatrix * skinnedTangent,   vec3(1.0, 0.0, 0.0));
    vec3 B = safeNormalize(normalMatrix * skinnedBitangent, vec3(0.0, 1.0, 0.0));
    vec3 N = safeNormalize(normalMatrix * skinnedNormal,    vec3(0.0, 0.0, 1.0));
    v_TBN = mat3(T, B, N);

    v_color = color;
    v_texCoord = texCoord;
    v_viewDepth = -(u_view * worldPosition).z;

    // --- Motion vectors (Slice R1) ---
    // Use the RAW base position (pre-morph, pre-skin) for BOTH terms, exactly as
    // the deleted overlay did — so skinned/morph meshes get rigid-body motion and
    // the animated pose (which drove gl_Position above) never enters the motion
    // output. For non-skinned meshes base == shaded, so the current term equals
    // gl_Position. u_projection * u_view equals the overlay's jittered VP.
    vec4 motionBasePos = vec4(position, 1.0);
    v_currentClip_motion = u_projection * u_view * (model * motionBasePos);
    v_prevClip_motion = u_prevViewProjection * (prevModel * motionBasePos);
}
