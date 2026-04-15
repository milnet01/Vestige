// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_gpu.vert.glsl
/// @brief GPU particle billboard vertex shader — reads particle data from SSBO.
///
/// Unlike the CPU particle path (particle.vert.glsl) which uses per-instance
/// VBOs, this shader reads directly from the particle SSBO. Each instance
/// is one particle; 6 vertices per instance form a camera-facing billboard quad.
#version 450 core

// Per-vertex: quad corner (0-5 mapping to two triangles)
// Vertices are generated procedurally from gl_VertexID

struct GPUParticle
{
    vec4 position;       // xyz = world pos, w = size
    vec4 velocity;       // xyz = velocity, w = rotation
    vec4 color;          // rgba
    float age;
    float lifetime;
    float startSize;
    uint flags;
};

layout(std430, binding = 0) buffer Particles
{
    GPUParticle particles[];
};

// Optional sort index buffer (for alpha-blended particles)
layout(std430, binding = 5) buffer SortKeys
{
    uvec2 sortKeys[];    // .y = particle index
};

uniform mat4 u_viewProjection;
uniform vec3 u_cameraRight;
uniform vec3 u_cameraUp;
uniform bool u_useSortIndices;

out vec2 v_texCoord;
out vec4 v_color;
out float v_normalizedAge;

// Quad corners for 2 triangles (CCW winding)
const vec2 QUAD_OFFSETS[6] = vec2[6](
    vec2(-0.5, -0.5),  // Triangle 1
    vec2( 0.5, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5, -0.5),  // Triangle 2
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

void main()
{
    uint instanceIdx = uint(gl_InstanceID);
    uint vertexIdx = uint(gl_VertexID);

    // Look up actual particle index (sorted or direct)
    uint particleIdx;
    if (u_useSortIndices)
        particleIdx = sortKeys[instanceIdx].y;
    else
        particleIdx = instanceIdx;

    GPUParticle p = particles[particleIdx];

    // Skip dead particles (shouldn't happen with correct indirect count, but safety)
    if ((p.flags & 1u) == 0u)
    {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0); // Off-screen
        return;
    }

    vec2 quadOffset = QUAD_OFFSETS[vertexIdx];
    float size = p.position.w; // Size stored in w component

    // Expand billboard in world space
    vec3 worldPos = p.position.xyz
        + u_cameraRight * quadOffset.x * size
        + u_cameraUp * quadOffset.y * size;

    gl_Position = u_viewProjection * vec4(worldPos, 1.0);

    // Outputs
    v_texCoord = quadOffset + 0.5; // Map [-0.5, 0.5] → [0, 1]
    v_color = p.color;
    v_normalizedAge = (p.lifetime > 0.0) ? p.age / p.lifetime : 1.0;
}
