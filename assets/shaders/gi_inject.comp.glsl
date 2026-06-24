// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gi_inject.comp.glsl
/// @brief Slice R4 (Variant A) — dynamic-GI inject pass (the one new compute).
///
/// One thread per froxel. Caches one bounce of dynamic direct light in a GI
/// channel co-located with the fog froxel grid (design §11.2):
///   1. Reconstruct the froxel centre's world position (this frame's matrices).
///   2. Reproject it into the PREVIOUS frame's cache and read history there
///      (the C1 fix: a view-aligned froxel is a different world point each
///      frame, so history MUST be read at the reprojected coord, not at i,j,k).
///   3. Sample this frame's injection source (att3 = albedo·direct-diffuse) at
///      the froxel column's screen UV, gated by a depth-match test (the froxel
///      must sit on the visible surface within ±½ its exponential slice
///      thickness).
///   4. Confidence-weighted EMA blend, written to the ping-pong target.
///
/// All math mirrors engine/renderer/gi_math.h (CLAUDE.md Rule 7: CPU spec pins
/// GPU runtime). The slice helpers also match the fog passes'
/// sliceToViewDepth() / volumetric_fog.cpp, so the GI cache is addressed
/// identically to the fog volume it co-locates in. No #include in this shader
/// pipeline — the helpers are duplicated inline, pinned by the parity tests.
#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform writeonly image3D u_giImage; // m_giTex (write target)

uniform sampler3D u_giHistory;       // m_giHistoryTex — previous frame's cache (trilinear)
uniform sampler2D u_injectionSource; // att3: albedo · Σ(direct diffuse, shadowed)
uniform sampler2D u_sceneDepth;      // resolved scene depth (reversed-Z, [0,1])

uniform vec3  u_froxelRes;          // (resX, resY, resZ) as float; exact for our sizes
uniform vec2  u_froxelNearFar;      // x = near, y = far (view-space metres)
uniform mat4  u_invProjection;      // current clip -> view
uniform mat4  u_invView;            // current view -> world
uniform mat4  u_prevViewProjection; // previous world -> clip (reprojection)
uniform mat4  u_prevView;           // previous world -> view (prev view depth)
uniform float u_giAlpha;            // EMA blend weight (0 in reduce-motion)
uniform float u_giDecay;            // confidence bleed per frame (0 in reduce-motion)

// View-space linear depth at the centre of depth slice `slice` (exponential
// distribution). Mirrors froxelSliceToViewDepth() / the fog passes' sliceToViewDepth().
float sliceToViewDepth(float slice, float resZ, float nearD, float farD)
{
    float t = (slice + 0.5) / resZ;
    return nearD * pow(farD / nearD, t);
}

// View-space depth at depth-slice *boundary* `b` (integer edge). Mirrors
// froxelSliceBoundaryViewDepth(); boundary(k) is the near edge of slice k.
float boundaryViewDepth(float b, float resZ, float nearD, float farD)
{
    float t = b / resZ;
    return nearD * pow(farD / nearD, t);
}

// Normalised [0,1] depth-slice sample coord for a view depth. Mirrors
// giVolumetricSliceCoord() in gi_math.h. Degenerate ranges clamp to 0.
float giSliceCoord(float viewDepth, float nearD, float farD)
{
    if (!(nearD > 0.0) || !(farD > nearD) || !(viewDepth > 0.0))
    {
        return 0.0;
    }
    return clamp(log(viewDepth / nearD) / log(farD / nearD), 0.0, 1.0);
}

void main()
{
    ivec3 res = ivec3(u_froxelRes);
    ivec3 c   = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, res)))
    {
        return;
    }

    float nearD = u_froxelNearFar.x;
    float farD  = u_froxelNearFar.y;
    float resZ  = float(res.z);

    // 1. Froxel-centre world position (current frame). Mirrors giFroxelCenterWorld().
    vec2  uv  = (vec2(c.xy) + 0.5) / vec2(res.xy);
    vec2  ndc = uv * 2.0 - 1.0;
    vec4  vp  = u_invProjection * vec4(ndc, 1.0, 1.0);
    vec3  ray = vp.xyz / vp.w;
    float viewDepth = sliceToViewDepth(float(c.z), resZ, nearD, farD);
    vec3  viewPos   = ray * (viewDepth / max(-ray.z, 1e-4));
    vec3  worldPos  = (u_invView * vec4(viewPos, 1.0)).xyz;

    // 2. Reprojected history read (the C1 fix). Mirrors giReprojectToHistory() +
    //    a trilinear sample of m_giHistoryTex at the reprojected coord.
    vec4 history = vec4(0.0);
    bool warm    = false;
    vec4 clip    = u_prevViewProjection * vec4(worldPos, 1.0);
    if (clip.w > 0.0) // in front of the previous camera
    {
        vec2  puv    = (clip.xy / clip.w) * 0.5 + 0.5;
        float prevVd = -(u_prevView * vec4(worldPos, 1.0)).z;
        if (puv.x >= 0.0 && puv.x <= 1.0 && puv.y >= 0.0 && puv.y <= 1.0
            && prevVd >= nearD && prevVd <= farD)
        {
            history = texture(u_giHistory,
                              vec3(puv, giSliceCoord(prevVd, nearD, farD)));
            warm = true;
        }
    }

    // 3. Injection sample (current frame). The grid is view-aligned, so the
    //    froxel column's screen UV is exactly `uv`. valid iff the froxel sits on
    //    the visible surface (its slice depth ≈ the depth-buffer surface depth
    //    within ±½ the exponential slice thickness — the tolerance tracks the
    //    slice, not a fixed epsilon).
    bool valid    = false;
    vec3 injected = vec3(0.0);
    float depthSample = texture(u_sceneDepth, uv).r;
    if (depthSample > 0.0) // reversed-Z: 0 = far/cleared (no opaque surface)
    {
        vec4  svp       = u_invProjection * vec4(ndc, depthSample, 1.0);
        float surfDepth = -(svp.xyz / svp.w).z;
        float lo        = boundaryViewDepth(float(c.z),       resZ, nearD, farD);
        float hi        = boundaryViewDepth(float(c.z) + 1.0, resZ, nearD, farD);
        float halfThick = 0.5 * (hi - lo);
        if (abs(surfDepth - viewDepth) <= halfThick)
        {
            valid    = true;
            injected = texture(u_injectionSource, uv).rgb;
        }
    }

    // 4. Confidence-weighted EMA. Mirrors giConfidenceBlend(): cold froxel takes
    //    the injection outright (never blends against undefined history); warm
    //    froxel EMA-blends toward the injection when valid, else holds radiance
    //    and bleeds confidence by (1 - decay). With alpha == 0 AND decay == 0
    //    (reduce-motion) the output rgb·a is frozen.
    vec4 outGi;
    if (!warm)
    {
        outGi = valid ? vec4(injected, u_giAlpha) : vec4(0.0);
    }
    else
    {
        vec3  rgb = valid ? mix(history.rgb, injected, u_giAlpha) : history.rgb;
        float a   = valid ? min(history.a + u_giAlpha, 1.0)
                          : history.a * (1.0 - u_giDecay);
        outGi = vec4(rgb, a);
    }

    imageStore(u_giImage, c, outGi);
}
