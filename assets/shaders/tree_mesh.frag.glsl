// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_mesh.frag.glsl
/// @brief Tree mesh LOD fragment shader — albedo texture (or flat colour),
///        half-Lambert directional light, CSM shadow receive, alpha-cutout
///        leaves, and crossfade alpha. (design §4.4/§4.6, 3D_E-0033)
#version 450 core

in vec3 v_normal;
in vec3 v_tangent;
in vec3 v_bitangent;
in vec3 v_worldPos;
in vec2 v_texCoord;
in float v_alpha;
in float v_viewDepth;

// Material
uniform sampler2D u_texture;
uniform bool u_hasTexture;
uniform vec3 u_albedo;        // fallback flat colour when no texture
uniform bool u_useAlphaTest;  // leaf cutout
uniform float u_alphaCutoff;
uniform vec3 u_cameraPos;

// Normal map (unit 1) — per-pixel surface relief on the flat leaf/bark cards
// so light plays across the canopy instead of sliding over a flat sheet (T8).
uniform sampler2D u_normalMap;
uniform bool u_hasNormalMap;

// Directional light
uniform bool u_hasDirectionalLight;
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;

// Cascaded shadow mapping (unit 3)
uniform bool u_hasShadows;
uniform sampler2DArray u_cascadeShadowMap;
uniform int u_cascadeCount;
uniform float u_cascadeSplits[4];
uniform mat4 u_cascadeLightSpaceMatrices[4];

out vec4 fragColor;

float interleavedGradientNoise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

int getCascadeIndex()
{
    float depth = abs(v_viewDepth);
    for (int i = 0; i < u_cascadeCount; i++)
    {
        if (depth < u_cascadeSplits[i])
            return i;
    }
    return u_cascadeCount - 1;
}

// 4-sample rotated-Poisson PCF, matching the foliage pass (§4.6).
float calcTreeShadow()
{
    int cascade = getCascadeIndex();
    vec4 lightSpacePos = u_cascadeLightSpaceMatrices[cascade] * vec4(v_worldPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0)
        return 0.0;

    float bias = 0.003;
    vec2 texelSize = 1.0 / vec2(textureSize(u_cascadeShadowMap, 0).xy);

    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rot = mat2(cosA, sinA, -sinA, cosA);

    const vec2 samples[4] = vec2[4](
        vec2(-0.94201624, -0.39906216),
        vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870),
        vec2( 0.34495938,  0.29387760));

    float shadow = 0.0;
    for (int i = 0; i < 4; i++)
    {
        vec2 offset = rot * samples[i] * 1.5 * texelSize;
        float d = texture(u_cascadeShadowMap, vec3(proj.xy + offset, float(cascade))).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }

    float maxShadowDist = u_cascadeSplits[u_cascadeCount - 1];
    float fadeStart = maxShadowDist * 0.8;
    float shadowFade = 1.0 - smoothstep(fadeStart, maxShadowDist, abs(v_viewDepth));
    return shadow * 0.25 * shadowFade;
}

void main()
{
    vec3 base;
    float texAlpha = 1.0;
    if (u_hasTexture)
    {
        vec4 texel = texture(u_texture, v_texCoord);
        base = texel.rgb;
        texAlpha = texel.a;
        if (u_useAlphaTest && texAlpha < u_alphaCutoff)
            discard;
    }
    else
    {
        base = u_albedo;
    }

    vec3 finalColor;
    if (u_hasDirectionalLight)
    {
        vec3 Ng = normalize(v_normal);
        // Per-pixel normal from the tangent-space map (T8). No viewer flip: the
        // leaf term below is abs()-based (two-sided) so a per-pixel N inherits the
        // "backlit underside never black" guarantee by construction. Leaf cards
        // fetch one mip coarser (+1 bias) to curb high-frequency shimmer; bark 0.
        vec3 N;
        if (u_hasNormalMap)
        {
            mat3 TBN = mat3(normalize(v_tangent), normalize(v_bitangent), Ng);
            vec3 nts = texture(u_normalMap, v_texCoord, u_useAlphaTest ? 1.0 : 0.0).xyz * 2.0 - 1.0;
            N = normalize(TBN * nts);
        }
        else
        {
            N = Ng;
        }
        vec3 L = normalize(-u_lightDirection);

        // Leaf cards (u_useAlphaTest) are two-sided stand-ins for a rounded leaf
        // cluster: light whichever side faces the sun so a backlit underside
        // never collapses to black (a signed N·L drops to ~0 there, leaving only
        // ambient). Opaque bark is one-sided — keep the signed half-Lambert so
        // its shaded side stays dark.
        float NdotL = u_useAlphaTest
            ? abs(dot(N, L)) * 0.5 + 0.5
            : dot(N, L) * 0.5 + 0.5;

        // Backlit translucency: thin leaves glow when the sun sits behind them
        // (mirrors foliage.frag). Bark is opaque — no transmission.
        vec3 V = normalize(u_cameraPos - v_worldPos);
        float backlit = pow(max(dot(V, -L), 0.0), 3.0);
        float trans = u_useAlphaTest ? backlit * 0.4 : 0.0;

        float shadow = u_hasShadows ? calcTreeShadow() : 0.0;

        vec3 ambient = base * u_ambientColor;
        vec3 direct = base * NdotL * u_lightColor;
        vec3 transmission = base * trans * u_lightColor;
        finalColor = ambient + (direct + transmission) * (1.0 - shadow * 0.65);
    }
    else
    {
        finalColor = base;
    }

    fragColor = vec4(finalColor, v_alpha);
    if (fragColor.a < 0.01)
        discard;
}
