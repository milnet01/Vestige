// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain.frag.glsl
/// @brief CDLOD terrain fragment shader — normal-mapped PBR lighting with CSM shadows.
#version 450 core

in vec2 v_terrainUV;
in vec3 v_worldPos;
in float v_viewDepth;

// Terrain textures
uniform sampler2D u_normalMap;
uniform sampler2D u_splatmap;

// Lighting
uniform vec3 u_viewPos;
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;

// Cascaded shadow mapping
uniform bool u_hasShadows;
uniform sampler2DArray u_cascadeShadowMap;
uniform int u_cascadeCount;
uniform float u_cascadeSplits[4];
uniform mat4 u_cascadeLightSpaceMatrices[4];

// Triplanar mapping
uniform bool u_triplanarEnabled;
uniform float u_triplanarSharpness; // Blending sharpness (4-8 typical)
uniform float u_triplanarStart;     // Steepness where triplanar begins (0.4)
uniform float u_triplanarEnd;       // Steepness where fully triplanar (0.7)
uniform float u_textureTiling;      // World-space tiling for layer textures

// PBR ground material arrays (Phase 10 / 3D_E-0031). When u_useGroundTextures is
// false the flat-colour path below runs unchanged (fallback / Tabernacle).
uniform bool u_useGroundTextures;
uniform sampler2DArray u_albedoArray;    // unit 6, sRGB
uniform sampler2DArray u_normalArray;    // unit 7, tangent-space (used from slice A3)
uniform sampler2DArray u_materialArray;  // unit 8, R=AO G=Roughness B=Height
uniform float u_layerTiling[4];          // world→UV scale per layer (grass/rock/dirt/sand)

// Water caustics (applied to terrain below water surface, within water XZ bounds)
uniform bool u_causticsEnabled;
uniform sampler2D u_causticsTex;      // Unit 5
uniform float u_causticsScale;
uniform float u_causticsIntensity;
uniform float u_causticsTime;
uniform float u_waterY;
uniform vec2 u_waterCenter;
uniform vec2 u_waterHalfExtent;
uniform int u_causticsQuality;        // 0=Full, 1=Approximate, 2=Simple

out vec4 fragColor;

// ---------------------------------------------------------------------------
// Per-fragment noise for rotating Poisson samples
// ---------------------------------------------------------------------------
float interleavedGradientNoise(vec2 screenPos)
{
    return fract(52.9829189 * fract(dot(screenPos, vec2(0.06711056, 0.00583715))));
}

// ---------------------------------------------------------------------------
// Select cascade based on view-space depth
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// PCF shadow sampling (8-sample Poisson for terrain — better than grass, cheaper than scene)
// ---------------------------------------------------------------------------
float calcTerrainShadow(vec3 normal, vec3 lightDir)
{
    int cascade = getCascadeIndex();

    vec4 lightSpacePos = u_cascadeLightSpaceMatrices[cascade] * vec4(v_worldPos, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0)
        return 0.0;

    // Slope-scaled bias
    float bias = max(0.002 * (1.0 - dot(normal, lightDir)), 0.0003);

    vec2 texelSize = 1.0 / vec2(textureSize(u_cascadeShadowMap, 0).xy);

    // Rotate 8 Poisson samples per-fragment
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rot = mat2(cosA, sinA, -sinA, cosA);

    const vec2 samples[8] = vec2[8](
        vec2(-0.94201624, -0.39906216),
        vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870),
        vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432),
        vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845),
        vec2( 0.97484398,  0.75648379)
    );

    float shadow = 0.0;
    for (int i = 0; i < 8; i++)
    {
        vec2 offset = rot * samples[i] * 2.0 * texelSize;
        float d = texture(u_cascadeShadowMap,
            vec3(proj.xy + offset, float(cascade))).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }

    // Fade shadow at cascade boundary
    float maxShadowDist = u_cascadeSplits[u_cascadeCount - 1];
    float fadeStart = maxShadowDist * 0.8;
    float depth = abs(v_viewDepth);
    float shadowFade = 1.0 - smoothstep(fadeStart, maxShadowDist, depth);

    return shadow * 0.125 * shadowFade;  // 1/8 samples
}

// ---------------------------------------------------------------------------
// Triplanar blend weights from world-space normal
// ---------------------------------------------------------------------------
vec3 triplanarWeights(vec3 worldNormal, float sharpness)
{
    vec3 w = pow(abs(worldNormal), vec3(sharpness));
    return w / (w.x + w.y + w.z);
}

// ---------------------------------------------------------------------------
// Procedural detail modulation (0.88-1.12) to break up the flat base colors.
// ---------------------------------------------------------------------------
// Dave Hoskins' hash — free of the strong diagonal banding that a
// fract(sin(dot(...))) hash produces on a regular terrain grid.
float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float tilingDetail(vec2 uv)
{
    // Smooth value noise instead of hard per-cell hashing: the old version
    // floor()'d into ~2.7 m cells and its sin-hash banded diagonally, which
    // read as corrugation across gentle terrain. Interpolating removes both.
    vec2 p = uv * 3.7;
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    float n = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    return mix(0.88, 1.12, n);
}

// ---------------------------------------------------------------------------
// Depth-aware height blend (Mishkinis). Mirrored on the CPU in
// engine/environment/terrain_material_blend.h — DEPTH here MUST equal the
// TERRAIN_HEIGHT_BLEND_DEPTH constant there (the parity contract, design §4.3).
// ---------------------------------------------------------------------------
const float TERRAIN_BLEND_DEPTH = 0.2;
vec4 heightBlendWeights(vec4 heights, vec4 weights)
{
    vec4 hw = heights + weights;
    float ma = max(max(hw.x, hw.y), max(hw.z, hw.w)) - TERRAIN_BLEND_DEPTH;
    vec4 b = max(hw - ma, 0.0);
    return b / (b.x + b.y + b.z + b.w);
}

// Cook-Torrance GGX helpers (3D_E-0031 A2). Lifted verbatim from scene.frag.glsl
// so the textured ground lights with the engine's canonical BRDF, not a Blinn-Phong
// approximation — pinned by the GGX-consistency test (test_terrain_ggx_parity).
const float PI = 3.14159265359;

/// GGX/Trowbridge-Reitz normal distribution function.
float distributionGGX(float NdotH, float roughness)
{
    roughness = max(roughness, 0.04);  // prevent NaN at roughness=0
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;

    return a2 / denom;
}

/// Schlick-GGX geometry function for a single direction.
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

/// Smith's geometry function — combines view and light direction masking.
float geometrySmith(float NdotV, float NdotL, float roughness)
{
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

/// Fresnel-Schlick approximation.
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    float x = clamp(1.0 - cosTheta, 0.0, 1.0);
    float x2 = x * x;
    return F0 + (1.0 - F0) * (x2 * x2 * x);
}

// ---------------------------------------------------------------------------
// PBR ground layers (3D_E-0031 A3). Sample + height-blend the four layers for
// one projection; returns blended albedo, AO, roughness and the blended
// tangent-space detail normal (+Z out of surface). Shared by the top-down and
// the per-axis triplanar projections (design §4.3 steps 2-3).
// ---------------------------------------------------------------------------
struct GroundSample
{
    vec3  albedo;
    float ao;
    float roughness;
    vec3  tnormal;   // tangent-space, +Z out of surface
};

GroundSample sampleGround(vec2 uv, vec4 splatWeights)
{
    vec3 a0 = texture(u_albedoArray,   vec3(uv * u_layerTiling[0], 0.0)).rgb;
    vec3 a1 = texture(u_albedoArray,   vec3(uv * u_layerTiling[1], 1.0)).rgb;
    vec3 a2 = texture(u_albedoArray,   vec3(uv * u_layerTiling[2], 2.0)).rgb;
    vec3 a3 = texture(u_albedoArray,   vec3(uv * u_layerTiling[3], 3.0)).rgb;

    vec3 m0 = texture(u_materialArray, vec3(uv * u_layerTiling[0], 0.0)).rgb;
    vec3 m1 = texture(u_materialArray, vec3(uv * u_layerTiling[1], 1.0)).rgb;
    vec3 m2 = texture(u_materialArray, vec3(uv * u_layerTiling[2], 2.0)).rgb;
    vec3 m3 = texture(u_materialArray, vec3(uv * u_layerTiling[3], 3.0)).rgb;

    vec3 n0 = texture(u_normalArray,   vec3(uv * u_layerTiling[0], 0.0)).rgb * 2.0 - 1.0;
    vec3 n1 = texture(u_normalArray,   vec3(uv * u_layerTiling[1], 1.0)).rgb * 2.0 - 1.0;
    vec3 n2 = texture(u_normalArray,   vec3(uv * u_layerTiling[2], 2.0)).rgb * 2.0 - 1.0;
    vec3 n3 = texture(u_normalArray,   vec3(uv * u_layerTiling[3], 3.0)).rgb * 2.0 - 1.0;

    // Height (material B) drives the same depth-aware blend used for albedo.
    vec4 heights = vec4(m0.b, m1.b, m2.b, m3.b);
    vec4 w = heightBlendWeights(heights, splatWeights);

    GroundSample g;
    g.albedo    = a0 * w.x + a1 * w.y + a2 * w.z + a3 * w.w;
    g.ao        = m0.r * w.x + m1.r * w.y + m2.r * w.z + m3.r * w.w;
    g.roughness = m0.g * w.x + m1.g * w.y + m2.g * w.z + m3.g * w.w;
    g.tnormal   = normalize(n0 * w.x + n1 * w.y + n2 * w.z + n3 * w.w);
    return g;
}

// ---------------------------------------------------------------------------
// Apply a tangent-space detail normal onto the world macro normal via Ben Golus's
// Whiteout blend (design §4.3 step 5). The top-down projection's tangent frame is
// world-XZ (T = +X, B = +Z), so we reorient the macro normal from world (+Y up)
// into the detail's tangent basis (+Z up), Whiteout-blend, and reorient back.
// Using the whiteout form (not a raw TBN transform) keeps the blend robust where
// the macro normal tilts on gentle, sub-triplanar slopes. Mirrored on the CPU in
// engine/environment/terrain_material_blend.h (whiteoutBlendNormal) for the
// A3 directional parity test.
// ---------------------------------------------------------------------------
vec3 whiteoutBlend(vec3 macroN, vec3 detailN)
{
    vec3 n1 = vec3(macroN.x, macroN.z, macroN.y);   // world +Y up -> tangent +Z up
    vec3 r  = normalize(vec3(n1.xy + detailN.xy, n1.z * detailN.z));
    return normalize(vec3(r.x, r.z, r.y));          // tangent +Z up -> world +Y up
}

// ---------------------------------------------------------------------------
// Ben Golus Whiteout triplanar normal (design §4.3 steps 4-5, slope path):
// reorient each axis' tangent-space detail normal onto the geometric world
// normal, then blend the three by the triplanar weights. tnX/tnY/tnZ are the
// per-projection tangent normals (X: worldPos.yz, Y: worldPos.xz, Z: worldPos.xy).
// ---------------------------------------------------------------------------
vec3 triplanarWorldNormal(vec3 tnX, vec3 tnY, vec3 tnZ, vec3 geomN, vec3 w)
{
    vec3 bnX = vec3(tnX.xy + geomN.zy, abs(tnX.z) * geomN.x);
    vec3 bnY = vec3(tnY.xy + geomN.xz, abs(tnY.z) * geomN.y);
    vec3 bnZ = vec3(tnZ.xy + geomN.xy, abs(tnZ.z) * geomN.z);
    return normalize(bnX.zyx * w.x + bnY.xzy * w.y + bnZ.xyz * w.z);
}

void main()
{
    // Read terrain macro normal from the pre-computed normal map (encoded n*0.5+0.5).
    vec3 macroNormal = normalize(texture(u_normalMap, v_terrainUV).rgb * 2.0 - 1.0);

    // Read splatmap weights for base color
    vec4 splat = texture(u_splatmap, v_terrainUV);

    // Simple base colors for each layer (will be replaced with texture arrays later)
    vec3 grassColor = vec3(0.28, 0.52, 0.18);
    vec3 rockColor  = vec3(0.45, 0.42, 0.38);
    vec3 dirtColor  = vec3(0.55, 0.40, 0.25);
    vec3 sandColor  = vec3(0.76, 0.70, 0.50);

    // Ground PBR properties — defaults for the flat-colour path (AO off, mid roughness).
    float groundAO = 1.0;
    float groundRoughness = 0.6;

    // Shading normal — the macro normal for the flat-colour paths, detail-perturbed
    // in the textured path (slice A3).
    vec3 N = macroNormal;

    vec3 albedo;
    if (u_useGroundTextures)
    {
        // Steepness → triplanar blend (reuse the existing terrain steepness logic).
        float steepness = 1.0 - abs(macroNormal.y);
        float triBlend = smoothstep(u_triplanarStart, u_triplanarEnd, steepness);

        // Top-down (world-XZ / Y-axis) projection — the meadow-dominant case.
        GroundSample gY = sampleGround(v_worldPos.xz, splat);
        vec3  worldN = whiteoutBlend(macroNormal, gY.tnormal);
        vec3  outAlbedo = gY.albedo;
        float outAO     = gY.ao;
        float outRough  = gY.roughness;

        // Slopes: blend in the X/Z world-plane projections (Ben Golus triplanar).
        if (triBlend > 0.001)
        {
            vec3 tw = triplanarWeights(macroNormal, u_triplanarSharpness);

            vec2 uvX = v_worldPos.yz;
            vec2 uvZ = v_worldPos.xy;
            if (macroNormal.x < 0.0) uvX.x = -uvX.x;   // fix mirroring on -facing sides
            if (macroNormal.z < 0.0) uvZ.x = -uvZ.x;

            GroundSample gX = sampleGround(uvX, splat);
            GroundSample gZ = sampleGround(uvZ, splat);

            vec3  triAlbedo = gX.albedo    * tw.x + gY.albedo    * tw.y + gZ.albedo    * tw.z;
            float triAO     = gX.ao        * tw.x + gY.ao        * tw.y + gZ.ao        * tw.z;
            float triRough  = gX.roughness * tw.x + gY.roughness * tw.y + gZ.roughness * tw.z;
            vec3  triN      = triplanarWorldNormal(gX.tnormal, gY.tnormal, gZ.tnormal,
                                                   macroNormal, tw);

            outAlbedo = mix(outAlbedo, triAlbedo, triBlend);
            outAO     = mix(outAO,     triAO,     triBlend);
            outRough  = mix(outRough,  triRough,  triBlend);
            worldN    = normalize(mix(worldN, triN, triBlend));
        }

        // Subtle large-scale brightness variation to break exact tiling repeats.
        albedo          = outAlbedo * tilingDetail(v_worldPos.xz * 0.15);
        groundAO        = outAO;
        groundRoughness = outRough;
        N               = worldN;
    }
    else if (u_triplanarEnabled && u_textureTiling > 0.0)
    {
        float steepness = 1.0 - abs(macroNormal.y);
        float triBlend = smoothstep(u_triplanarStart, u_triplanarEnd, steepness);

        // Standard top-down (Y-axis) sampling using worldPos.xz
        vec2 uvY = v_worldPos.xz * u_textureTiling;
        float detailY = tilingDetail(uvY);

        vec3 flatAlbedo = (grassColor * splat.r + rockColor * splat.g
                         + dirtColor * splat.b + sandColor * splat.a) * detailY;

        if (triBlend > 0.001)
        {
            // X-axis and Z-axis projections for steep slopes
            vec3 tw = triplanarWeights(macroNormal, u_triplanarSharpness);

            vec2 uvX = v_worldPos.yz * u_textureTiling;
            vec2 uvZ = v_worldPos.xy * u_textureTiling;

            // Fix mirroring on negative-facing sides
            if (macroNormal.x < 0.0) uvX.x = -uvX.x;
            if (macroNormal.z < 0.0) uvZ.x = -uvZ.x;

            float detailX = tilingDetail(uvX);
            float detailZ = tilingDetail(uvZ);

            vec3 baseColor = grassColor * splat.r + rockColor * splat.g
                           + dirtColor * splat.b + sandColor * splat.a;

            vec3 triAlbedo = baseColor * detailX * tw.x
                           + baseColor * detailY * tw.y
                           + baseColor * detailZ * tw.z;

            albedo = mix(flatAlbedo, triAlbedo, triBlend);
        }
        else
        {
            albedo = flatAlbedo;
        }
    }
    else
    {
        albedo = grassColor * splat.r
               + rockColor  * splat.g
               + dirtColor  * splat.b
               + sandColor  * splat.a;
    }

    // Directional lighting (Blinn-Phong)
    vec3 lightDir = normalize(-u_lightDirection);
    float NdotL = max(dot(N, lightDir), 0.0);

    vec3 viewDir = normalize(u_viewPos - v_worldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(N, halfDir), 0.0);

    // Specular. The textured ground shades with the engine's canonical
    // Cook-Torrance GGX (3D_E-0031 A2, design §4.4 item 1) — roughness-driven,
    // dielectric F0=0.04 — so it matches every other surface exactly. The
    // flat-colour fallback keeps its fixed Blinn-Phong term byte-identical.
    vec3 spec;
    if (u_useGroundTextures)
    {
        float NdotV = max(dot(N, viewDir), 0.0);
        float HdotV = max(dot(halfDir, viewDir), 0.0);
        vec3  F0 = vec3(0.04);                       // dielectric ground (metallic = 0)
        float D  = distributionGGX(NdotH, groundRoughness);
        float G  = geometrySmith(NdotV, NdotL, groundRoughness);
        vec3  F  = fresnelSchlick(HdotV, F0);
        vec3 specTerm = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
        spec = u_lightColor * specTerm * NdotL;
    }
    else
    {
        float specular = pow(NdotH, 64.0) * 0.15;
        spec = u_lightColor * specular * NdotL;
    }

    // Shadow
    float shadow = 0.0;
    if (u_hasShadows)
    {
        shadow = calcTerrainShadow(N, lightDir);
    }

    vec3 ambient = albedo * u_ambientColor * groundAO;
    vec3 diffuse = albedo * NdotL * u_lightColor;

    vec3 color = ambient + (diffuse + spec) * (1.0 - shadow);

    // Water caustics — additive light pattern on terrain below the water surface
    if (u_causticsEnabled && v_worldPos.y < u_waterY
        && abs(v_worldPos.x - u_waterCenter.x) < u_waterHalfExtent.x
        && abs(v_worldPos.z - u_waterCenter.y) < u_waterHalfExtent.y)
    {
        vec3 caustics;
        vec2 causticUV1 = v_worldPos.xz * u_causticsScale
                        + u_causticsTime * vec2(0.03, 0.02);

        if (u_causticsQuality == 0)
        {
            // FULL: dual scroll + min-blend + chromatic aberration (6 reads)
            vec2 causticUV2 = v_worldPos.xz * u_causticsScale * 1.4
                            + u_causticsTime * vec2(-0.02, 0.03);

            float r1 = texture(u_causticsTex, causticUV1 + vec2(0.001, 0.0)).r;
            float g1 = texture(u_causticsTex, causticUV1).r;
            float b1 = texture(u_causticsTex, causticUV1 - vec2(0.001, 0.0)).r;
            vec3 caustic1 = vec3(r1, g1, b1);

            float r2 = texture(u_causticsTex, causticUV2 + vec2(0.0, 0.001)).r;
            float g2 = texture(u_causticsTex, causticUV2).r;
            float b2 = texture(u_causticsTex, causticUV2 - vec2(0.0, 0.001)).r;
            vec3 caustic2 = vec3(r2, g2, b2);

            caustics = min(caustic1, caustic2) * u_causticsIntensity;
        }
        else if (u_causticsQuality == 1)
        {
            // APPROXIMATE: dual scroll + min-blend, no chromatic aberration (2 reads)
            vec2 causticUV2 = v_worldPos.xz * u_causticsScale * 1.4
                            + u_causticsTime * vec2(-0.02, 0.03);
            float c1 = texture(u_causticsTex, causticUV1).r;
            float c2 = texture(u_causticsTex, causticUV2).r;
            caustics = vec3(min(c1, c2)) * u_causticsIntensity;
        }
        else
        {
            // SIMPLE: single scroll (1 read)
            float c = texture(u_causticsTex, causticUV1).r;
            caustics = vec3(c) * u_causticsIntensity * 0.7;
        }

        // Tint caustics with a subtle blue-green (refracted sunlight through water)
        caustics *= vec3(0.7, 0.9, 1.0);

        float depthBelowWater = u_waterY - v_worldPos.y;
        float depthFade = 1.0 - smoothstep(0.0, 5.0, depthBelowWater);
        caustics *= depthFade;

        float lightScale = length(u_lightColor) * 0.25;
        color += caustics * lightScale;
    }

    fragColor = vec4(color, 1.0);
}
