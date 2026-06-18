// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_inject.comp.glsl
/// @brief Phase 10 slice 11.6 pass 1/3 — inject the participating medium.
///
/// Writes per-froxel (scattering_rgb = sigma_s, extinction_a = sigma_t) into
/// the froxel volume. Slice 11.6 injects a uniform medium; density noise
/// (11.8) and mist/ground-fog volumes (11.11) layer additional contributions
/// here later. One thread per froxel.
///
/// Froxel coordinate math mirrors engine/renderer/volumetric_fog.cpp
/// (CLAUDE.md Rule 7: CPU spec pins GPU runtime).
#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform writeonly image3D u_volumeImage;

uniform vec3  u_froxelRes;    // (resX, resY, resZ) as float; exact for our sizes
uniform vec3  u_scattering;   // sigma_s per channel, 1/m
uniform float u_extinction;   // sigma_t, 1/m

void main()
{
    ivec3 res = ivec3(u_froxelRes);
    ivec3 c   = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, res)))
    {
        return;
    }

    imageStore(u_volumeImage, c, vec4(u_scattering, u_extinction));
}
