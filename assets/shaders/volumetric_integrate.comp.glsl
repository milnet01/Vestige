// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_integrate.comp.glsl
/// @brief Phase 10 slice 11.6 pass 3/3 — front-to-back ray-march accumulation.
///
/// One thread per screen tile (i, j). Marches the depth slices front to back
/// accumulating in-scattered radiance and transmittance (Beer-Lambert), and
/// writes (rgb = accumulated inscatter, a = transmittance-so-far) into each
/// froxel of the column. The composite samples this texture per pixel.
///
/// Per-slab integration is the energy-conserving analytic form (Hillaire,
/// Frostbite SIGGRAPH 2015): for constant scattering S and extinction sigma_t
/// across a slab of thickness d, the integral of S * T(x) dx over the slab is
/// S * (1 - exp(-sigma_t * d)) / sigma_t, with the limit S * d as sigma_t -> 0.
///
/// Slice boundaries use the exponential distribution evaluated at *integer*
/// indices (no +0.5 centre offset); mirrors froxelSliceBoundaryViewDepth() in
/// engine/renderer/volumetric_fog.cpp (CLAUDE.md Rule 7).
#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, binding = 0) uniform readonly  image3D u_scatterImage;     // rgb=inscatter, a=sigma_t
layout(rgba16f, binding = 1) uniform writeonly image3D u_integratedImage;  // rgb=accum, a=transmittance

uniform vec3 u_froxelRes;       // (resX, resY, resZ) as float
uniform vec2 u_froxelNearFar;   // x = near, y = far

// View-space linear depth at depth-slice *boundary* index `b` (b in [0, resZ]).
float sliceBoundaryViewDepth(int b, int resZ, float nearD, float farD)
{
    float t = float(b) / float(resZ);
    return nearD * pow(farD / nearD, t);
}

void main()
{
    ivec3 res  = ivec3(u_froxelRes);
    ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(tile, res.xy)))
    {
        return;
    }

    vec3  accumScatter       = vec3(0.0);
    float accumTransmittance = 1.0;
    float prevBoundary       = u_froxelNearFar.x;   // boundary(0) == near

    for (int k = 0; k < res.z; ++k)
    {
        vec4  s        = imageLoad(u_scatterImage, ivec3(tile, k));
        vec3  inscatter = s.rgb;
        float sigmaT    = max(s.a, 0.0);

        float nextBoundary = sliceBoundaryViewDepth(k + 1, res.z,
                                                    u_froxelNearFar.x,
                                                    u_froxelNearFar.y);
        float d = max(nextBoundary - prevBoundary, 0.0);
        prevBoundary = nextBoundary;

        float sliceT = exp(-sigmaT * d);
        vec3  sInt   = (sigmaT > 1e-5)
                     ? inscatter * (1.0 - sliceT) / sigmaT
                     : inscatter * d;

        accumScatter       += accumTransmittance * sInt;
        accumTransmittance *= sliceT;

        imageStore(u_integratedImage, ivec3(tile, k),
                   vec4(accumScatter, accumTransmittance));
    }
}
