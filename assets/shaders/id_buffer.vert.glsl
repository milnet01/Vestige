// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file id_buffer.vert.glsl
/// @brief Entity ID buffer vertex shader — transforms geometry for the color-encoded picking pass.
#version 450 core

layout(location = 0) in vec3 position;

// Bone vertex attributes (locations 10-11)
layout(location = 10) in ivec4 boneIds;
layout(location = 11) in vec4 boneWeights;

// Bone matrices for skeletal animation (binding 2)
layout(std430, binding = 2) buffer BoneMatrices
{
    mat4 u_boneMatrices[];
};

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform bool u_hasBones;

void main()
{
    vec3 skinnedPos;
    if (u_hasBones)
    {
        mat4 boneTransform = boneWeights.x * u_boneMatrices[boneIds.x]
                           + boneWeights.y * u_boneMatrices[boneIds.y]
                           + boneWeights.z * u_boneMatrices[boneIds.z]
                           + boneWeights.w * u_boneMatrices[boneIds.w];
        skinnedPos = vec3(boneTransform * vec4(position, 1.0));
    }
    else
    {
        skinnedPos = position;
    }

    gl_Position = u_viewProjection * u_model * vec4(skinnedPos, 1.0);
}
