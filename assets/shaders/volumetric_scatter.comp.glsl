// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_scatter.comp.glsl
/// @brief Phase 10 slice 11.6 pass 2/3 — single-scatter inscattering.
///
/// Reads the injected medium (sigma_s, sigma_t), evaluates the sun's
/// single-scatter contribution per froxel, and writes the lit inscattering
/// back in place (rgb = inscatter radiance, a = sigma_t preserved). Each
/// invocation reads and writes only its own texel, so in-place READ_WRITE is
/// race-free. One thread per froxel.
///
/// Slice 11.6 evaluates the *unshadowed* sun lobe; CSM shadow sampling is
/// added when the pass is wired into the renderer (slice 11.6 part B, which
/// can supply the shadow map + light-space matrices). The Henyey-Greenstein
/// phase is the literal closed form here; slice 11.7 swaps in the
/// Workbench-fit Schlick approximation.
///
/// `henyeyGreenstein` is a standalone function so the parity test
/// (tests/test_volumetric_fog_gpu.cpp) can extract it and pin it against the
/// CPU reference `henyeyGreensteinPhase()` in volumetric_fog.cpp (Rule 7).
#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform image3D u_volumeImage; // read + write in place

uniform vec3  u_froxelRes;        // (resX, resY, resZ) as float
uniform vec2  u_froxelNearFar;    // x = near, y = far  (view-space metres)
uniform mat4  u_invProjection;    // clip -> view, to place the froxel centre
uniform vec3  u_sunDirViewSpace;  // normalised, view space, points TOWARD the sun
uniform vec3  u_sunRadiance;      // linear HDR
uniform vec3  u_ambient;          // ambient inscatter floor
uniform float u_anisotropy;       // Henyey-Greenstein g, in (-1, 1)

const float PI = 3.14159265358979323846;

// Henyey-Greenstein phase function p(cosTheta; g). Normalised so the
// integral over the sphere is 1. Matches henyeyGreensteinPhase() in
// engine/renderer/volumetric_fog.cpp.
float henyeyGreenstein(float cosTheta, float g)
{
    float gg    = g * g;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (1.0 - gg) / (4.0 * PI * pow(max(denom, 1e-4), 1.5));
}

// View-space linear depth at the centre of depth slice `slice` (exponential
// distribution). Mirrors froxelSliceToViewDepth() in volumetric_fog.cpp.
float sliceToViewDepth(int slice, int resZ, float nearD, float farD)
{
    float t = (float(slice) + 0.5) / float(resZ);
    return nearD * pow(farD / nearD, t);
}

void main()
{
    ivec3 res = ivec3(u_froxelRes);
    ivec3 c   = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, res)))
    {
        return;
    }

    vec4 medium = imageLoad(u_volumeImage, c);   // rgb = sigma_s, a = sigma_t
    vec3 sigmaS = medium.rgb;

    // Reconstruct the froxel centre in view space: a ray through the tile
    // centre, scaled so its view-depth equals this slice's depth.
    vec2 uv  = (vec2(c.xy) + 0.5) / vec2(res.xy);
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 vp  = u_invProjection * vec4(ndc, 1.0, 1.0);
    vec3 ray = vp.xyz / vp.w;                     // view-space point on the ray
    float viewDepth = sliceToViewDepth(c.z, res.z,
                                       u_froxelNearFar.x, u_froxelNearFar.y);
    vec3 viewPos = ray * (viewDepth / max(-ray.z, 1e-4));

    float cosTheta = dot(normalize(viewPos), u_sunDirViewSpace);
    float phase    = henyeyGreenstein(cosTheta, u_anisotropy);

    // Unshadowed sun lobe + ambient floor (CSM shadow factor lands in 11.6B).
    vec3 inscatter = sigmaS * (phase * u_sunRadiance + u_ambient);

    imageStore(u_volumeImage, c, vec4(inscatter, medium.a));
}
