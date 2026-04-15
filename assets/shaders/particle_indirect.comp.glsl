// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_indirect.comp.glsl
/// @brief Updates the indirect draw command with the current alive particle count.
///
/// Single-thread dispatch that writes aliveCount into the instanceCount field
/// of a DrawArraysIndirectCommand. This avoids CPU readback of the particle count.
#version 450 core

layout(local_size_x = 1) in;

layout(std430, binding = 1) buffer Counters
{
    uint aliveCount;
    uint deadCount;
    uint emitCount;
    uint maxParticles;
};

// DrawArraysIndirectCommand structure
layout(std430, binding = 3) buffer IndirectDraw
{
    uint vertexCount;    // 6 for billboard quad (2 triangles)
    uint instanceCount;  // = aliveCount
    uint firstVertex;    // 0
    uint baseInstance;   // 0
};

void main()
{
    instanceCount = aliveCount;
    vertexCount = 6u;
    firstVertex = 0u;
    baseInstance = 0u;
}
