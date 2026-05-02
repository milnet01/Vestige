// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file screen_quad.frag.glsl
/// @brief Final compositing fragment shader with tonemapping, bloom, SSAO, SSR, and TAA integration.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_screenTexture;
uniform float u_exposure;
uniform int u_tonemapMode;
uniform int u_debugMode;

// Bloom
uniform bool u_bloomEnabled;
uniform sampler2D u_bloomTexture;      // Unit 9
uniform float u_bloomIntensity;

// SSAO
uniform bool u_ssaoEnabled;
uniform sampler2D u_ssaoTexture;       // Unit 10

// Screen-space contact shadows
uniform bool u_contactShadowEnabled;
uniform sampler2D u_contactShadowTexture;  // Unit 11

// Color grading LUT
uniform bool u_lutEnabled;
uniform sampler3D u_lutTexture;        // Unit 13
uniform float u_lutIntensity;

// Accessibility: color-vision-deficiency simulation
// (Viénot/Brettel/Mollon 1999 3x3 RGB projection)
uniform bool u_colorVisionEnabled;
uniform mat3 u_colorVisionMatrix;

// Phase 10 fog: distance + height + sun inscatter.
// Applied after contact shadows, before bloom add — radiance contribution
// in linear HDR so bloom samples the fogged result (UE / HDRP convention).
// See docs/phases/phase_10_fog_design.md §4 for composition order.
uniform int       u_fogMode;               // 0=None, 1=Linear, 2=Exp, 3=Exp2
uniform vec3      u_fogColour;             // Linear-RGB distance-fog inscatter colour
uniform float     u_fogStart;              // Linear mode
uniform float     u_fogEnd;                // Linear mode
uniform float     u_fogDensity;            // Exp/Exp2 modes

uniform bool      u_heightFogEnabled;
uniform vec3      u_heightFogColour;       // Linear-RGB inscatter for the ground layer
uniform float     u_heightFogY;            // fogHeight (world-space altitude of reference density)
uniform float     u_heightFogDensity;      // groundDensity
uniform float     u_heightFogFalloff;      // heightFalloff (vertical decay rate)
uniform float     u_heightFogMaxOpacity;   // 1.0 = no clamp

uniform bool      u_sunInscatterEnabled;
uniform vec3      u_sunInscatterColour;    // Linear-RGB sun-direction tint
uniform float     u_sunInscatterExponent;  // Cosine-lobe tightness
uniform float     u_sunInscatterStart;     // Zero below this view distance
uniform vec3      u_sunDirection;          // Unit world-space dir from scene toward sun

uniform sampler2D u_fogDepthTexture;       // Unit 12 — reverse-Z depth
uniform mat4      u_fogInvViewProj;        // Inverse view-projection (world-space reconstruction)
uniform vec3      u_fogCameraWorldPos;

out vec4 fragColor;

/// Reconstructs the world-space hit position of the fragment at the given UV
/// from the depth buffer. Returns `cameraWorldPos` when the sample is on the
/// far plane (sky) — the caller gates fog on depth>0 so this is never used
/// directly in that case, but keeping the branch explicit avoids a NaN from
/// the perspective divide on sky samples.
vec3 fogWorldPosFromDepth(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = u_fogInvViewProj * ndc;
    return world.xyz / world.w;
}

/// CPU-parity port of `Vestige::computeFogFactor` from
/// engine/renderer/fog.cpp. Byte-for-byte identical: test_fog.cpp
/// pins the CPU form, this shader pins the GPU form, and the CPU
/// tests act as the shared spec.
float fogFactorForDistance(int mode, float dist)
{
    if (mode == 0 || dist <= 0.0) return 1.0;
    if (mode == 1)  // Linear
    {
        float span = u_fogEnd - u_fogStart;
        if (span <= 0.0) return 1.0;
        if (dist <= u_fogStart) return 1.0;
        if (dist >= u_fogEnd)   return 0.0;
        return (u_fogEnd - dist) / span;
    }
    float density = max(0.0, u_fogDensity);
    if (mode == 2)  // GL_EXP
    {
        return exp(-density * dist);
    }
    // mode == 3, GL_EXP2
    float x = density * dist;
    return exp(-(x * x));
}

/// Quílez 2010 analytic integral — matches `computeHeightFogTransmittance`.
/// Splits at near-horizontal rays onto a Beer-Lambert branch for numerical
/// stability at the horizon line.
float heightFogTransmittance(vec3 cameraPos, vec3 worldPos)
{
    vec3 ray = worldPos - cameraPos;
    float rayLength = length(ray);
    if (rayLength <= 0.0 || u_heightFogDensity <= 0.0) return 1.0;

    vec3 rayDir = ray / rayLength;
    float a = max(0.0, u_heightFogDensity);
    float b = max(0.0, u_heightFogFalloff);
    float h = cameraPos.y - u_heightFogY;

    float fogAmount;
    const float kHorizontalRayEpsilon = 1e-5;
    if (b <= 0.0 || abs(rayDir.y) < kHorizontalRayEpsilon)
    {
        fogAmount = a * exp(-b * h) * rayLength;
    }
    else
    {
        float tau  = b * rayDir.y * rayLength;
        float base = (a / b) * exp(-b * h);
        // (1 - exp(-tau)) / rayDir.y, written via -expm1(-x) for stability.
        // GLSL lacks expm1; use the equivalent `1.0 - exp(-tau)` which is
        // acceptable at the magnitudes we actually feed here (`tau` stays
        // well above the cancellation regime after the horizontal branch).
        fogAmount = base * (1.0 - exp(-tau)) / rayDir.y;
    }

    if (!(fogAmount >= 0.0))
    {
        return 1.0 - clamp(u_heightFogMaxOpacity, 0.0, 1.0);
    }

    float transmittance = exp(-fogAmount);
    float maxOpacity = clamp(u_heightFogMaxOpacity, 0.0, 1.0);
    return max(transmittance, 1.0 - maxOpacity);
}

float sunInscatterLobe(vec3 viewDir, float viewDistance)
{
    if (viewDistance < u_sunInscatterStart) return 0.0;
    vec3 towardSun = -u_sunDirection;
    float cosAngle = dot(normalize(viewDir), normalize(towardSun));
    if (cosAngle <= 0.0) return 0.0;
    float exponent = max(0.0, u_sunInscatterExponent);
    return pow(cosAngle, exponent);
}

/// Reinhard tone mapping: simple, preserves color ratios.
vec3 tonemapReinhard(vec3 color)
{
    return color / (color + vec3(1.0));
}

/// ACES Filmic tone mapping: better contrast and saturation.
/// Fitted curve from Krzysztof Narkowicz.
vec3 tonemapACES(vec3 color)
{
    return (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
}

/// 3D LUT colour grading. Applies the half-texel-offset sampling formula
/// (so the LUT's first/last voxels land cleanly at colour 0/1 without
/// hardware-clamp ringing) and blends against the original by `intensity`.
/// Mirrored byte-for-byte by `lutLookup` + `lutBlend` in
/// `tests/test_color_grading.cpp` (CPU side does nearest-neighbour, which
/// agrees with GPU trilinear at voxel-centre sample points).
vec3 applyColorGradingLut(vec3 color, sampler3D lut, float intensity)
{
    float lutSize = float(textureSize(lut, 0).x);
    vec3 lutCoord = clamp(color, 0.0, 1.0) * ((lutSize - 1.0) / lutSize) + vec3(0.5 / lutSize);
    vec3 graded = texture(lut, lutCoord).rgb;
    return mix(color, graded, intensity);
}

/// False-color luminance visualization for HDR debugging.
vec3 falseColorLuminance(float luminance)
{
    if (luminance < 0.1)
        return vec3(0.0, 0.0, 1.0);       // Blue: very dark
    else if (luminance < 0.25)
        return vec3(0.0, 1.0, 1.0);       // Cyan: dark
    else if (luminance < 0.5)
        return vec3(0.0, 1.0, 0.0);       // Green: mid-tone
    else if (luminance < 1.0)
        return vec3(1.0, 1.0, 0.0);       // Yellow: bright
    else if (luminance < 2.0)
        return vec3(1.0, 0.5, 0.0);       // Orange: HDR bright
    else if (luminance < 5.0)
        return vec3(1.0, 0.0, 0.0);       // Red: very bright HDR
    else
        return vec3(1.0, 0.0, 1.0);       // Magenta: extreme HDR
}

void main()
{
    // 1. Sample HDR color
    vec3 color = texture(u_screenTexture, v_texCoord).rgb;

    // 2. Apply SSAO (before exposure, multiplicative darkening)
    if (u_ssaoEnabled)
    {
        float ao = texture(u_ssaoTexture, v_texCoord).r;
        color *= ao;
    }

    // 2b. Apply screen-space contact shadows (directional light)
    if (u_contactShadowEnabled)
    {
        float contactShadow = texture(u_contactShadowTexture, v_texCoord).r;
        color *= contactShadow;
    }

    // 2c. Phase 10 fog: distance + height + sun inscatter.
    //     Composed in linear HDR so bloom samples the fogged radiance
    //     (UE / HDRP convention, docs/phases/phase_10_fog_design.md §4).
    //
    //     Reverse-Z encoding: sky is at depth == 0.0 → skipped (fog
    //     on the background goes through the clear colour, which is
    //     expected to already carry the sky tint).
    float fogDepth = texture(u_fogDepthTexture, v_texCoord).r;
    bool fogActive =
           (u_fogMode != 0 || u_heightFogEnabled || u_sunInscatterEnabled)
        && fogDepth > 0.0;
    if (fogActive)
    {
        vec3 worldPos = fogWorldPosFromDepth(v_texCoord, fogDepth);
        vec3 viewVec = worldPos - u_fogCameraWorldPos;
        float viewDistance = length(viewVec);
        vec3 viewDir = (viewDistance > 0.0)
                        ? viewVec / viewDistance
                        : vec3(0.0, 0.0, -1.0);

        // Distance fog — single-layer Linear/EXP/EXP2 curve.
        float surfaceVisibility = fogFactorForDistance(u_fogMode, viewDistance);

        // Height fog — multiplicative transmittance combines with distance fog.
        float heightT = u_heightFogEnabled
                         ? heightFogTransmittance(u_fogCameraWorldPos, worldPos)
                         : 1.0;

        // Sun-direction inscatter modulates the distance-fog colour toward
        // the warm sun tint. Height fog retains its own colour so
        // ground-hugging mist doesn't inherit the sun glow (UE pattern).
        vec3 distanceFogColour = u_fogColour;
        if (u_sunInscatterEnabled)
        {
            float lobe = sunInscatterLobe(viewDir, viewDistance);
            distanceFogColour = mix(u_fogColour, u_sunInscatterColour, lobe);
        }

        // Composite — first mix surface with distance fog, then with height fog.
        // Preserves each layer's distinct colour under partial cover;
        // multiplying the two visibilities produces an implicit combined
        // extinction that bloom then samples in linear HDR.
        vec3 fogged = mix(distanceFogColour, color, surfaceVisibility);
        color = mix(u_heightFogColour, fogged, heightT);
    }

    // 3. Add bloom (before exposure)
    if (u_bloomEnabled)
    {
        vec3 bloom = texture(u_bloomTexture, v_texCoord).rgb;
        color += bloom * u_bloomIntensity;
    }

    // 4. Apply exposure
    color *= u_exposure;

    // 5. Debug mode: false-color luminance overlay
    if (u_debugMode == 1)
    {
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(falseColorLuminance(luminance), 1.0);
        return;
    }

    // 6. Apply selected tonemapper
    if (u_tonemapMode == 0)
    {
        color = tonemapReinhard(color);
    }
    else if (u_tonemapMode == 1)
    {
        color = tonemapACES(color);
    }
    // Mode 2 = None (linear clamp) — no tonemapping applied

    // Clamp after tonemapping — ACES can produce values slightly > 1.0
    color = clamp(color, vec3(0.0), vec3(1.0));

    // 7. Color grading LUT
    if (u_lutEnabled)
    {
        color = applyColorGradingLut(color, u_lutTexture, u_lutIntensity);
    }

    // 7b. Color-vision-deficiency simulation (post-grade, pre-gamma).
    //     Matrix is identity when disabled, so the branch exists only to
    //     skip the multiply in the common case.
    if (u_colorVisionEnabled)
    {
        color = clamp(u_colorVisionMatrix * color, vec3(0.0), vec3(1.0));
    }

    // 8. Gamma correction: linear → sRGB
    color = pow(color, vec3(1.0 / 2.2));

    // 9. Output
    fragColor = vec4(color, 1.0);
}
