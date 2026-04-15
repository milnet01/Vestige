// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file water.frag.glsl
/// @brief Water surface fragment shader — Fresnel blending, reflections, refractions, and Beer's law absorption.
/// Normal and distortion detail is fully procedural (gradient noise FBM) to eliminate texture tiling.
#version 450 core

in vec3 v_worldPos;
in vec2 v_texCoord;
in vec4 v_clipSpace;
in vec3 v_normal;

uniform samplerCube u_environmentMap;
uniform sampler2D u_normalMap;
uniform sampler2D u_dudvMap;

// Planar reflection/refraction textures
uniform sampler2D u_reflectionTex;
uniform sampler2D u_refractionTex;
uniform sampler2D u_refractionDepthTex;
uniform bool u_hasReflectionTex;
uniform bool u_hasRefractionTex;

uniform vec3 u_cameraPos;
uniform float u_time;
uniform float u_cameraNear;

// Water parameters
uniform vec4 u_shallowColor;
uniform vec4 u_deepColor;
uniform float u_dudvStrength;
uniform float u_normalStrength;
uniform float u_flowSpeed;
uniform float u_specularPower;

// Directional light
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;

uniform bool u_hasNormalMap;
uniform bool u_hasDudvMap;
uniform bool u_hasEnvironmentMap;

// Quality tier: 0=Full (3 FBM x 3 octaves), 1=Approximate (2 FBM x 2 octaves), 2=Simple (texture only)
uniform int u_waterQualityTier;

// Shore foam
uniform sampler2D u_foamTex;
uniform bool u_hasFoamTex;
uniform float u_foamDistance;      // How far from shore foam extends (metres)

// Soft edge
uniform float u_softEdgeDistance;  // Water-to-geometry fade distance (metres)

out vec4 fragColor;

// --- Procedural gradient noise (Inigo Quilez) ---
// Returns vec3(noise_value, dNoise/dx, dNoise/dy) — analytical derivatives for free normals.
// Uses "hash without sine" for cross-platform consistency (Dave Hoskins).

vec2 noiseHash(vec2 x)
{
    const vec2 k = vec2(0.3183099, 0.3678794);
    x = x * k + k.yx;
    return -1.0 + 2.0 * fract(16.0 * k * fract(x.x * x.y * (x.x + x.y)));
}

vec3 noised(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Quintic Hermite interpolation (C2 continuous — no visible grid lines)
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);

    vec2 ga = noiseHash(i + vec2(0.0, 0.0));
    vec2 gb = noiseHash(i + vec2(1.0, 0.0));
    vec2 gc = noiseHash(i + vec2(0.0, 1.0));
    vec2 gd = noiseHash(i + vec2(1.0, 1.0));

    float va = dot(ga, f - vec2(0.0, 0.0));
    float vb = dot(gb, f - vec2(1.0, 0.0));
    float vc = dot(gc, f - vec2(0.0, 1.0));
    float vd = dot(gd, f - vec2(1.0, 1.0));

    return vec3(
        va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd),
        ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd)
           + du * (u.yx * (va - vb - vc + vd) + vec2(vb, vc) - va)
    );
}

// FBM with analytical derivatives — fixed octave counts so the GPU can unroll.
// Rotating domain between octaves to eliminate directional grid bias.
// Returns vec3(value, dF/dx, dF/dy).
// Rotation matrix (~37.5 degrees) — irrational angle decorrelates octaves
const mat2 fbmRot = mat2(0.8, 0.6, -0.6, 0.8);

vec3 waterFbm2(vec2 p)
{
    float amplitude = 0.5;
    vec3 result = vec3(0.0);
    for (int i = 0; i < 2; i++)
    {
        vec3 n = noised(p);
        result += amplitude * n;
        p = fbmRot * p * 2.01;
        amplitude *= 0.5;
    }
    return result;
}

vec3 waterFbm3(vec2 p)
{
    float amplitude = 0.5;
    vec3 result = vec3(0.0);
    for (int i = 0; i < 3; i++)
    {
        vec3 n = noised(p);
        result += amplitude * n;
        p = fbmRot * p * 2.01;
        amplitude *= 0.5;
    }
    return result;
}

void main()
{
    float flow = u_time * u_flowSpeed;

    // --- Procedural surface normals from gradient noise FBM ---
    // Quality tiers control octave count and number of FBM evaluations:
    //   FULL (0):        3 FBM calls x 3 octaves = 9 noise evals
    //   APPROXIMATE (1): 2 FBM calls x 2 octaves = 4 noise evals
    //   SIMPLE (2):      texture-based normals only (0 noise evals)
    vec3 normal = normalize(v_normal);
    vec2 totalDistortion = vec2(0.0);

    if (u_waterQualityTier <= 1 && u_hasNormalMap)
    {
        vec2 totalGrad;
        if (u_waterQualityTier == 0)
        {
            // FULL: 3 FBM calls x 3 octaves — two normal layers for richer detail
            vec3 n1 = waterFbm3(v_worldPos.xz * 1.5 + flow * vec2(1.0, 0.3));
            vec3 n2 = waterFbm3(v_worldPos.xz * 2.8 + flow * vec2(-0.4, 0.8));
            totalGrad = (n1.yz + n2.yz * 0.7) * 0.15;
        }
        else
        {
            // APPROXIMATE: 1 FBM call x 2 octaves — single normal layer
            vec3 n1 = waterFbm2(v_worldPos.xz * 1.5 + flow * vec2(1.0, 0.3));
            totalGrad = n1.yz * 0.15;
        }

        normal = normalize(vec3(
            normal.x - totalGrad.x * u_normalStrength,
            normal.y,
            normal.z - totalGrad.y * u_normalStrength
        ));

        // --- Procedural distortion from noise ---
        if (u_hasDudvMap)
        {
            vec3 distNoise = (u_waterQualityTier == 0)
                ? waterFbm3(v_worldPos.xz * 2.0 + flow * vec2(0.7, -0.3) + vec2(5.2, 1.3))
                : waterFbm2(v_worldPos.xz * 2.0 + flow * vec2(0.7, -0.3) + vec2(5.2, 1.3));
            totalDistortion = distNoise.yz * u_dudvStrength;
            normal = normalize(vec3(
                normal.x + totalDistortion.x,
                normal.y,
                normal.z + totalDistortion.y
            ));
        }
    }
    else if (u_hasNormalMap)
    {
        // SIMPLE: use precomputed normal map texture (no procedural noise)
        vec2 scrollUV = v_texCoord + flow * vec2(0.1, 0.05);
        vec3 texNormal = texture(u_normalMap, scrollUV).rgb * 2.0 - 1.0;
        normal = normalize(vec3(
            normal.x + texNormal.x * u_normalStrength * 0.3,
            normal.y,
            normal.z + texNormal.z * u_normalStrength * 0.3
        ));

        if (u_hasDudvMap)
        {
            vec2 dudvUV = v_texCoord + flow * vec2(-0.05, 0.1);
            vec2 dudv = texture(u_dudvMap, dudvUV).rg * 2.0 - 1.0;
            totalDistortion = dudv * u_dudvStrength;
        }
    }

    // View direction and reflected direction
    vec3 viewDir = normalize(u_cameraPos - v_worldPos);

    // Projective texture coordinates for reflection/refraction
    vec2 screenUV = (v_clipSpace.xy / v_clipSpace.w) * 0.5 + 0.5;

    // Reflection color
    vec3 reflectionColor;
    if (u_hasReflectionTex)
    {
        // Planar reflection (flip Y, apply distortion)
        vec2 reflectUV = vec2(screenUV.x, 1.0 - screenUV.y) + totalDistortion;
        reflectUV = clamp(reflectUV, 0.001, 0.999);
        reflectionColor = texture(u_reflectionTex, reflectUV).rgb;
    }
    else if (u_hasEnvironmentMap)
    {
        vec3 reflectedDir = reflect(-viewDir, normal);
        reflectionColor = texture(u_environmentMap, reflectedDir).rgb;
    }
    else
    {
        reflectionColor = vec3(0.6, 0.7, 0.8); // Default sky
    }

    // Refraction color with Beer's law absorption
    vec3 refractionColor;
    float waterThickness = 1.0; // Default for soft edge calculation
    if (u_hasRefractionTex)
    {
        vec2 refractUV = clamp(screenUV + totalDistortion, 0.001, 0.999);
        refractionColor = texture(u_refractionTex, refractUV).rgb;

        // Beer's law: depth-based absorption
        float refractionDepth = texture(u_refractionDepthTex, refractUV).r;
        // Reverse-Z linearization: linearZ = cameraNear / depth
        float linearRefract = u_cameraNear / max(refractionDepth, 0.00001);
        float linearWater = u_cameraNear / max(gl_FragCoord.z, 0.00001);
        waterThickness = max(linearRefract - linearWater, 0.0);

        // Per-channel absorption (red absorbed fastest)
        vec3 absorptionCoeffs = vec3(0.4, 0.2, 0.1);
        vec3 absorption = exp(-absorptionCoeffs * waterThickness);
        refractionColor *= absorption;
        refractionColor = mix(refractionColor, u_deepColor.rgb, 1.0 - absorption.b);
    }
    else
    {
        // No refraction FBO: use angle-based shallow/deep blend
        float cosTheta = max(dot(normal, viewDir), 0.0);
        float depthFactor = 1.0 - cosTheta;
        refractionColor = mix(u_shallowColor.rgb, u_deepColor.rgb, depthFactor * depthFactor);
    }

    // Fresnel (Schlick approximation, F0 = 0.02 for water)
    float cosTheta = max(dot(normal, viewDir), 0.0);
    float F0 = 0.02;
    float x = 1.0 - cosTheta;
    float x2 = x * x;
    float fresnel = F0 + (1.0 - F0) * (x2 * x2 * x);

    // Specular highlight (Blinn-Phong from directional light)
    vec3 lightDir = normalize(-u_lightDirection);
    vec3 halfDir = normalize(viewDir + lightDir);
    float specAngle = max(dot(normal, halfDir), 0.0);
    float specular = pow(specAngle, u_specularPower);
    vec3 specularColor = u_lightColor * specular * 0.6;

    // Final: blend refraction and reflection using Fresnel, add specular
    vec3 finalColor = mix(refractionColor, reflectionColor, fresnel);
    finalColor += specularColor;

    // Shore foam — white froth where water meets geometry
    if (u_hasFoamTex && u_hasRefractionTex && u_foamDistance > 0.0)
    {
        float foamFactor = 1.0 - smoothstep(0.0, u_foamDistance, waterThickness);
        if (foamFactor > 0.01)
        {
            // Dual scrolling for animated foam pattern
            vec2 foamUV1 = v_worldPos.xz * 2.0 + u_time * vec2(0.01, 0.02);
            vec2 foamUV2 = v_worldPos.xz * 1.5 + u_time * vec2(-0.02, 0.01);
            float foam = texture(u_foamTex, foamUV1).r * texture(u_foamTex, foamUV2).r;
            finalColor = mix(finalColor, vec3(1.0), foam * foamFactor * 0.5);
        }
    }

    // Alpha: mostly opaque, soft edge fade at shore/object intersections
    float alpha = mix(0.85, 1.0, fresnel);
    if (u_hasRefractionTex)
    {
        float edgeFade = smoothstep(0.0, u_softEdgeDistance, waterThickness);
        alpha *= edgeFade;
    }

    fragColor = vec4(finalColor, alpha);
}
