// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file shadow_depth.vert.glsl
/// @brief Directional shadow map vertex shader — transforms geometry into light space with instancing and skeletal animation support.
#version 450 core

layout(location = 0) in vec3 position;
// Phase 13 G1: normal + UV feed the RSM flux term (albedo·radiance·N·L).
layout(location = 1) in vec3 normal;
layout(location = 3) in vec2 texCoord;

// Bone vertex attributes (locations 10-11)
layout(location = 10) in ivec4 boneIds;
layout(location = 11) in vec4 boneWeights;

// Per-instance model matrix (locations 6-9)
layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

// Bone matrices for skeletal animation (binding 2)
layout(std430, binding = 2) buffer BoneMatrices
{
    mat4 u_boneMatrices[];
};

uniform mat4 u_model;
uniform mat4 u_lightSpaceMatrix;
uniform bool u_useInstancing;
uniform bool u_hasBones;

// Phase 13 G1: world-space normal + UV for the RSM flux fragment term.
out vec3 v_worldNormal;
out vec2 v_texCoord;

void main()
{
    // Skeletal animation skinning (position + normal share the bone transform)
    vec3 skinnedPos;
    vec3 skinnedNormal;
    if (u_hasBones)
    {
        mat4 boneTransform = boneWeights.x * u_boneMatrices[boneIds.x]
                           + boneWeights.y * u_boneMatrices[boneIds.y]
                           + boneWeights.z * u_boneMatrices[boneIds.z]
                           + boneWeights.w * u_boneMatrices[boneIds.w];
        skinnedPos = vec3(boneTransform * vec4(position, 1.0));
        skinnedNormal = mat3(boneTransform) * normal;
    }
    else
    {
        skinnedPos = position;
        skinnedNormal = normal;
    }

    mat4 model;
    if (u_useInstancing)
    {
        model = mat4(instanceModelCol0, instanceModelCol1,
                     instanceModelCol2, instanceModelCol3);
    }
    else
    {
        model = u_model;
    }

    // mat3(model) (not the inverse-transpose) is an acceptable normal transform
    // for indirect-light flux: the cosine term tolerates non-uniform scale far
    // better than specular would, and it avoids a per-vertex matrix inverse on
    // the shadow hot path. The fragment renormalises.
    v_worldNormal = mat3(model) * skinnedNormal;
    v_texCoord = texCoord;

    gl_Position = u_lightSpaceMatrix * model * vec4(skinnedPos, 1.0);
}
