/// @file scene.frag.glsl
/// @brief Main scene PBR fragment shader — multi-light Blinn-Phong/PBR with CSM shadows, normal mapping, and IBL.
#version 450 core

// Maximum light counts — must match C++ constants
const int MAX_POINT_LIGHTS = 8;
const int MAX_SPOT_LIGHTS = 4;
const int MAX_POINT_SHADOW_LIGHTS = 2;

const float PI = 3.14159265359;

// Inputs from vertex shader
in vec3 v_fragPosition;
in vec3 v_normal;
in vec3 v_color;
in vec2 v_texCoord;
in float v_viewDepth;
in mat3 v_TBN;

out vec4 fragColor;

// Material (Blinn-Phong)
uniform vec3 u_materialDiffuse;
uniform vec3 u_materialSpecular;
uniform float u_materialShininess;
uniform vec3 u_materialEmissive;
uniform float u_materialEmissiveStrength;
uniform bool u_hasTexture;
uniform sampler2D u_diffuseTexture;   // Unit 0

// Normal mapping
uniform bool u_hasNormalMap;
uniform sampler2D u_normalMap;        // Unit 1

// Parallax occlusion mapping
uniform bool u_hasHeightMap;
uniform sampler2D u_heightMap;        // Unit 2
uniform float u_heightScale;

// Camera
uniform vec3 u_viewPosition;

// Directional light
uniform bool u_hasDirLight;
uniform vec3 u_dirLight_direction;
uniform vec3 u_dirLight_ambient;
uniform vec3 u_dirLight_diffuse;
uniform vec3 u_dirLight_specular;

// Cascaded shadow mapping
uniform bool u_hasShadows;
uniform sampler2DArray u_cascadeShadowMap;  // Unit 3
uniform int u_cascadeCount;
uniform float u_cascadeSplits[4];
uniform mat4 u_cascadeLightSpaceMatrices[4];
uniform bool u_cascadeDebug;

// Point lights
uniform int u_pointLightCount;
uniform vec3 u_pointLights_position[MAX_POINT_LIGHTS];
uniform vec3 u_pointLights_ambient[MAX_POINT_LIGHTS];
uniform vec3 u_pointLights_diffuse[MAX_POINT_LIGHTS];
uniform vec3 u_pointLights_specular[MAX_POINT_LIGHTS];
uniform float u_pointLights_constant[MAX_POINT_LIGHTS];
uniform float u_pointLights_linear[MAX_POINT_LIGHTS];
uniform float u_pointLights_quadratic[MAX_POINT_LIGHTS];

// Point light shadows
uniform int u_pointShadowCount;
uniform samplerCube u_pointShadowMaps[MAX_POINT_SHADOW_LIGHTS];  // Units 4-5
uniform int u_pointShadowIndices[MAX_POINT_SHADOW_LIGHTS];
uniform float u_pointShadowFarPlane[MAX_POINT_SHADOW_LIGHTS];

// Spot lights
uniform int u_spotLightCount;
uniform vec3 u_spotLights_position[MAX_SPOT_LIGHTS];
uniform vec3 u_spotLights_direction[MAX_SPOT_LIGHTS];
uniform vec3 u_spotLights_ambient[MAX_SPOT_LIGHTS];
uniform vec3 u_spotLights_diffuse[MAX_SPOT_LIGHTS];
uniform vec3 u_spotLights_specular[MAX_SPOT_LIGHTS];
uniform float u_spotLights_innerCutoff[MAX_SPOT_LIGHTS];
uniform float u_spotLights_outerCutoff[MAX_SPOT_LIGHTS];
uniform float u_spotLights_constant[MAX_SPOT_LIGHTS];
uniform float u_spotLights_linear[MAX_SPOT_LIGHTS];
uniform float u_spotLights_quadratic[MAX_SPOT_LIGHTS];

// PBR mode toggle
uniform bool u_usePBR;

// PBR material uniforms
uniform vec3 u_pbrAlbedo;
uniform float u_pbrMetallic;
uniform float u_pbrRoughness;
uniform float u_pbrAo;
uniform vec3 u_pbrEmissive;
uniform float u_pbrEmissiveStrength;

// Clearcoat PBR layer
uniform float u_clearcoat;           // 0.0 = none, 1.0 = full clearcoat
uniform float u_clearcoatRoughness;  // Roughness of the clearcoat layer

// PBR textures
uniform bool u_hasMetallicRoughnessMap;
uniform sampler2D u_metallicRoughnessMap;  // Unit 6

uniform bool u_hasEmissiveMap;
uniform sampler2D u_emissiveMap;           // Unit 7

uniform bool u_hasAoMap;
uniform sampler2D u_aoMap;                 // Unit 8

// UV tiling scale
uniform float u_uvScale;

// Wireframe mode
uniform bool u_wireframe;

// Transparency
uniform int u_alphaMode;          // 0=OPAQUE, 1=MASK, 2=BLEND
uniform float u_alphaCutoff;
uniform float u_baseColorAlpha;

// IBL (Image-Based Lighting)
uniform bool u_hasIBL;
uniform float u_iblMultiplier;          // Per-material IBL scale (0=no sky light, 1=full)
uniform samplerCube u_irradianceMap;    // Unit 14 — global (sky) IBL
uniform samplerCube u_prefilterMap;     // Unit 15 — global (sky) IBL
uniform sampler2D u_brdfLUT;            // Unit 16 — shared BRDF LUT
uniform float u_maxPrefilterLod;

// Light probes — local IBL override for indoor/enclosed areas
uniform bool u_hasProbe;
uniform float u_probeWeight;            // 0=global only, 1=probe only (blends at boundary)
uniform samplerCube u_probeIrradianceMap;  // Unit 10
uniform samplerCube u_probePrefilterMap;   // Unit 11

// SH Probe Grid — smooth diffuse ambient from L2 spherical harmonics
uniform bool u_hasSHGrid;
uniform vec3 u_shGridWorldMin;
uniform vec3 u_shGridWorldMax;
uniform sampler3D u_shTex[7];           // Units 17-23

// Stochastic tiling
uniform bool u_stochasticTiling;

// Water caustics (applied to geometry below water surface, within water XZ bounds)
uniform bool u_causticsEnabled;
uniform sampler2D u_causticsTex;      // Unit 9
uniform float u_causticsScale;        // World-space tiling (default 0.1)
uniform float u_causticsIntensity;    // Additive strength (default 0.3)
uniform float u_causticsTime;
uniform float u_waterY;               // Water surface height
uniform vec2 u_waterCenter;           // Water surface XZ center
uniform vec2 u_waterHalfExtent;       // Water surface half-width/half-depth

// =============================================================================
// Stochastic tiling (hex-grid tri-cell blending with random UV offsets)
// =============================================================================

/// Integer-based pseudo-random hash: vec2 → vec2.
/// Uses ALU integer ops instead of sin() for bit-identical results across all GPUs.
vec2 stochasticHash(vec2 p)
{
    uvec2 q = uvec2(floatBitsToUint(p.x), floatBitsToUint(p.y));
    q = 1103515245u * ((q >> 1u) ^ q.yx);
    uint n = 1103515245u * (q.x ^ (q.y >> 3u));
    return vec2(n, n * 48271u) * (1.0 / float(0xFFFFFFFFu));
}

/// Sample a texture with stochastic tiling to break visible repetition.
/// Uses a hex grid with three overlapping cells, each with a random UV offset.
/// Weight sharpening (pow 7) keeps blending tight to avoid blur.
vec4 textureStochastic(sampler2D samp, vec2 uv)
{
    // Skew to hex grid
    vec2 skewedUV = vec2(uv.x + uv.y * 0.5, uv.y);
    vec2 cell = floor(skewedUV);
    vec2 f = fract(skewedUV);

    // Three closest hex cell vertices
    float w1 = f.x + f.y < 1.0 ? 1.0 : 0.0;
    vec2 v0 = cell;
    vec2 v1 = cell + vec2(1.0, 0.0);
    vec2 v2 = cell + vec2(0.0, 1.0);
    vec2 v3 = cell + vec2(1.0, 1.0);

    // Pick the correct triangle
    vec2 A, B, C;
    vec3 bary;
    if (w1 > 0.5)
    {
        A = v0; B = v1; C = v2;
        bary = vec3(1.0 - f.x - f.y, f.x, f.y);
    }
    else
    {
        A = v3; B = v2; C = v1;
        bary = vec3(f.x + f.y - 1.0, 1.0 - f.x, 1.0 - f.y);
    }

    // Random UV offsets per cell vertex
    vec2 offsetA = stochasticHash(A);
    vec2 offsetB = stochasticHash(B);
    vec2 offsetC = stochasticHash(C);

    // Sample texture at each offset
    vec4 sA = texture(samp, uv + offsetA);
    vec4 sB = texture(samp, uv + offsetB);
    vec4 sC = texture(samp, uv + offsetC);

    // Sharpen barycentric weights to reduce blur
    vec3 w = pow(bary, vec3(7.0));
    w /= (w.x + w.y + w.z);

    return sA * w.x + sB * w.y + sC * w.z;
}

/// Dispatches to stochastic or regular texture sampling based on uniform flag.
vec4 sampleMaterial(sampler2D samp, vec2 uv)
{
    if (u_stochasticTiling)
    {
        return textureStochastic(samp, uv);
    }
    return texture(samp, uv);
}

// =============================================================================
// Shadow functions (shared by both lighting models)
// =============================================================================

// PCSS light size (in shadow map texels) — controls penumbra width.
// Larger values = softer shadows further from the caster.
uniform float u_shadowLightSize;

// Poisson disk samples (16 points) for shadow map filtering.
// Pre-computed for even distribution with randomized look.
const vec2 POISSON_DISK[16] = vec2[16](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

/// Simple noise for rotating Poisson disk per-fragment (reduces banding).
float interleavedGradientNoise(vec2 screenPos)
{
    return fract(52.9829189 * fract(dot(screenPos, vec2(0.06711056, 0.00583715))));
}

/// Determines which cascade to use based on fragment's view-space depth.
int getCascadeIndex()
{
    float depth = abs(v_viewDepth);
    for (int i = 0; i < u_cascadeCount; i++)
    {
        if (depth < u_cascadeSplits[i])
        {
            return i;
        }
    }
    return u_cascadeCount - 1;
}

/// PCSS blocker search: find average depth of occluders in a search region.
/// Returns average blocker depth, or -1.0 if no blockers found.
float blockerSearch(vec3 projCoords, int cascade, float bias, vec2 texelSize, float searchRadius)
{
    float blockerSum = 0.0;
    int blockerCount = 0;

    // Rotate Poisson disk per-fragment to break up banding artifacts
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rotation = mat2(cosA, sinA, -sinA, cosA);

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = rotation * POISSON_DISK[i] * searchRadius * texelSize;
        float sampleDepth = texture(u_cascadeShadowMap,
            vec3(projCoords.xy + offset, float(cascade))).r;

        if (projCoords.z - bias > sampleDepth)
        {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
    {
        return -1.0;
    }
    return blockerSum / float(blockerCount);
}

/// PCSS shadow: variable penumbra width based on blocker distance.
float calcShadowForCascade(int cascade, vec3 normal, vec3 lightDir)
{
    // Transform fragment position to light space for this cascade
    vec4 lightSpacePos = u_cascadeLightSpaceMatrices[cascade] * vec4(v_fragPosition, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0)
    {
        return 0.0;
    }

    float currentDepth = projCoords.z;

    // Slope-scaled bias
    float bias = max(0.002 * (1.0 - dot(normal, lightDir)), 0.0003);

    vec2 texelSize = 1.0 / vec2(textureSize(u_cascadeShadowMap, 0).xy);

    // Step 1: Blocker search — find average occluder depth
    float searchRadius = u_shadowLightSize;
    float avgBlockerDepth = blockerSearch(projCoords, cascade, bias, texelSize, searchRadius);

    // No blockers found → fully lit
    if (avgBlockerDepth < 0.0)
    {
        return 0.0;
    }

    // Step 2: Penumbra estimation
    // penumbraWidth = lightSize * (receiverDepth - blockerDepth) / blockerDepth
    float penumbraWidth = u_shadowLightSize * (currentDepth - avgBlockerDepth)
                          / max(avgBlockerDepth, 0.001);
    penumbraWidth = clamp(penumbraWidth, 1.0, u_shadowLightSize * 4.0);

    // Step 3: PCF with variable-size kernel
    float shadow = 0.0;
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rotation = mat2(cosA, sinA, -sinA, cosA);

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = rotation * POISSON_DISK[i] * penumbraWidth * texelSize;
        float pcfDepth = texture(u_cascadeShadowMap,
            vec3(projCoords.xy + offset, float(cascade))).r;
        shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    }
    shadow /= 16.0;

    return shadow;
}

/// Calculates shadow factor from cascaded shadow maps with cascade blending.
/// Returns 0.0 (fully lit) to 1.0 (fully in shadow).
float calcShadow(vec3 normal, vec3 lightDir)
{
    float depth = abs(v_viewDepth);

    // Beyond shadow distance — smoothly fade to no shadow
    float maxShadowDist = u_cascadeSplits[u_cascadeCount - 1];
    float fadeStart = maxShadowDist * 0.8;
    if (depth > maxShadowDist)
    {
        return 0.0;
    }
    float shadowFade = 1.0 - smoothstep(fadeStart, maxShadowDist, depth);

    int cascade = getCascadeIndex();
    float shadow = calcShadowForCascade(cascade, normal, lightDir);

    // Blend with the next cascade near the boundary to eliminate visible seams
    if (cascade < u_cascadeCount - 1)
    {
        float splitDist = u_cascadeSplits[cascade];
        float prevSplit = (cascade == 0) ? 0.0 : u_cascadeSplits[cascade - 1];
        float cascadeRange = splitDist - prevSplit;
        float blendStart = splitDist - cascadeRange * 0.2;

        if (depth > blendStart)
        {
            float blendFactor = (depth - blendStart) / (splitDist - blendStart);
            float nextShadow = calcShadowForCascade(cascade + 1, normal, lightDir);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    return shadow * shadowFade;
}

/// Samples point shadow cubemap 0 and returns shadow factor.
float samplePointShadow0(vec3 fragToLight, float currentDepth, float farPlane, float bias)
{
    float closestDepth = texture(u_pointShadowMaps[0], fragToLight).r * farPlane;
    return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
}

/// Samples point shadow cubemap 1 and returns shadow factor.
float samplePointShadow1(vec3 fragToLight, float currentDepth, float farPlane, float bias)
{
    float closestDepth = texture(u_pointShadowMaps[1], fragToLight).r * farPlane;
    return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
}

/// Calculates the point light shadow factor using a hard-coded cubemap index.
/// Returns 0.0 (fully lit) to 1.0 (fully in shadow).
float calcPointShadow(int shadowIdx, vec3 fragPos, vec3 lightPos, vec3 normal)
{
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);
    float farPlane = u_pointShadowFarPlane[shadowIdx];

    // Slope-scaled bias (matches CSM approach)
    vec3 lightDir = normalize(-fragToLight);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);

    // Use compile-time constant index for sampler access
    if (shadowIdx == 0)
    {
        return samplePointShadow0(fragToLight, currentDepth, farPlane, bias);
    }
    else
    {
        return samplePointShadow1(fragToLight, currentDepth, farPlane, bias);
    }
}

/// Finds the shadow map index for a given point light index, or returns -1.
int findPointShadowIndex(int lightIdx)
{
    for (int s = 0; s < u_pointShadowCount && s < MAX_POINT_SHADOW_LIGHTS; s++)
    {
        if (u_pointShadowIndices[s] == lightIdx)
        {
            return s;
        }
    }
    return -1;
}

// =============================================================================
// PBR BRDF functions (Cook-Torrance)
// =============================================================================

/// GGX/Trowbridge-Reitz normal distribution function.
/// Describes the statistical distribution of microfacet normals.
float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;

    return a2 / denom;
}

/// Schlick-GGX geometry function for a single direction.
/// Approximates microfacet self-shadowing.
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
/// Describes how reflectance changes at grazing angles.
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/// Evaluate L2 SH irradiance from 7 packed RGBA16F 3D textures.
/// Uses Ramamoorthi/Hanrahan optimized constants (basis × cosine lobe pre-baked).
vec3 evaluateSHGridIrradiance(vec3 worldPos, vec3 normal)
{
    // Convert world position to grid UV [0,1]
    vec3 gridUV = (worldPos - u_shGridWorldMin) / (u_shGridWorldMax - u_shGridWorldMin);
    gridUV = clamp(gridUV, vec3(0.001), vec3(0.999));

    // Sample 7 textures (hardware trilinear interpolation between 8 probes)
    vec4 t0 = texture(u_shTex[0], gridUV);
    vec4 t1 = texture(u_shTex[1], gridUV);
    vec4 t2 = texture(u_shTex[2], gridUV);
    vec4 t3 = texture(u_shTex[3], gridUV);
    vec4 t4 = texture(u_shTex[4], gridUV);
    vec4 t5 = texture(u_shTex[5], gridUV);
    vec4 t6 = texture(u_shTex[6], gridUV);

    // Unpack 9 vec3 coefficients from 7 vec4s
    // Layout: 27 channels (9 coeffs × RGB) packed sequentially into RGBA
    vec3 L[9];
    L[0] = vec3(t0.r, t0.g, t0.b);
    L[1] = vec3(t0.a, t1.r, t1.g);
    L[2] = vec3(t1.b, t1.a, t2.r);
    L[3] = vec3(t2.g, t2.b, t2.a);
    L[4] = vec3(t3.r, t3.g, t3.b);
    L[5] = vec3(t3.a, t4.r, t4.g);
    L[6] = vec3(t4.b, t4.a, t5.r);
    L[7] = vec3(t5.g, t5.b, t5.a);
    L[8] = vec3(t6.r, t6.g, t6.b);

    // Ramamoorthi/Hanrahan optimized evaluation (basis constants × cosine lobe)
    const float c1 = 0.429043;
    const float c2 = 0.511664;
    const float c3 = 0.743125;
    const float c4 = 0.886227;
    const float c5 = 0.247708;

    vec3 n = normal;
    return max(vec3(0.0),
        c4 * L[0] +
        2.0 * c2 * (L[1]*n.y + L[2]*n.z + L[3]*n.x) +
        2.0 * c1 * (L[4]*n.x*n.y + L[5]*n.y*n.z + L[7]*n.x*n.z) +
        c3 * L[8] * (n.x*n.x - n.y*n.y) +
        c5 * L[6] * (3.0*n.z*n.z - 1.0));
}

/// Fresnel-Schlick with roughness — prevents harsh edges on rough metals under IBL.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
             * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/// Computes the clearcoat specular contribution for a single light.
/// Uses a separate GGX lobe with fixed F0=0.04 (IOR ~1.5, like lacquer/varnish).
/// Returns the clearcoat specular term and the Fresnel factor (for base attenuation).
vec3 calcClearcoatLobe(float NdotH, float NdotV, float NdotL, float HdotV,
                        float clearcoatRoughness, out float Fc)
{
    float Dc = distributionGGX(NdotH, max(clearcoatRoughness, 0.04));
    float Gc = geometrySmith(NdotV, NdotL, clearcoatRoughness);
    Fc = pow(1.0 - HdotV, 5.0) * 0.96 + 0.04;  // Schlick with F0=0.04

    return vec3(Dc * Gc * Fc) / (4.0 * NdotV * NdotL + 0.0001);
}

// =============================================================================
// Blinn-Phong lighting functions
// =============================================================================

/// Calculates the contribution from a directional light (Blinn-Phong).
vec3 calcDirectionalLight(vec3 norm, vec3 viewDir, vec3 baseColor)
{
    vec3 lightDir = normalize(-u_dirLight_direction);

    // Diffuse — how directly the surface faces the light
    float diff = max(dot(norm, lightDir), 0.0);

    // Specular (Blinn-Phong) — halfway vector between light and view
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), u_materialShininess);

    // Shadow factor
    float shadow = 0.0;
    if (u_hasShadows)
    {
        shadow = calcShadow(norm, lightDir);
    }

    vec3 ambient  = u_dirLight_ambient * baseColor;
    vec3 diffuse  = u_dirLight_diffuse * diff * baseColor;
    vec3 specular = u_dirLight_specular * spec * u_materialSpecular;

    // Ambient is never shadowed; diffuse and specular are
    return ambient + (1.0 - shadow) * (diffuse + specular);
}

/// Calculates the contribution from a point light (Blinn-Phong).
vec3 calcPointLight(int i, vec3 norm, vec3 viewDir, vec3 baseColor)
{
    vec3 toLight = u_pointLights_position[i] - v_fragPosition;
    float dist = length(toLight);
    vec3 lightDir = toLight / dist;  // normalize reusing dist

    // Diffuse
    float diff = max(dot(norm, lightDir), 0.0);

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), u_materialShininess);

    // Attenuation — light fades with distance
    float attenuation = 1.0 / (u_pointLights_constant[i]
        + u_pointLights_linear[i] * dist
        + u_pointLights_quadratic[i] * dist * dist);

    // Point light shadow
    float shadow = 0.0;
    int shadowIdx = findPointShadowIndex(i);
    if (shadowIdx >= 0)
    {
        shadow = calcPointShadow(shadowIdx, v_fragPosition, u_pointLights_position[i], norm);
    }

    vec3 ambient  = u_pointLights_ambient[i] * baseColor;
    vec3 diffuse  = u_pointLights_diffuse[i] * diff * baseColor;
    vec3 specular = u_pointLights_specular[i] * spec * u_materialSpecular;

    return (ambient + (1.0 - shadow) * (diffuse + specular)) * attenuation;
}

/// Calculates the contribution from a spot light (Blinn-Phong).
vec3 calcSpotLight(int i, vec3 norm, vec3 viewDir, vec3 baseColor)
{
    vec3 toLight = u_spotLights_position[i] - v_fragPosition;
    float dist = length(toLight);
    vec3 lightDir = toLight / dist;  // normalize reusing dist

    // Diffuse
    float diff = max(dot(norm, lightDir), 0.0);

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), u_materialShininess);

    // Attenuation
    float attenuation = 1.0 / (u_spotLights_constant[i]
        + u_spotLights_linear[i] * dist
        + u_spotLights_quadratic[i] * dist * dist);

    // Spotlight cone — smooth edge falloff
    float theta = dot(lightDir, normalize(-u_spotLights_direction[i]));
    float epsilon = u_spotLights_innerCutoff[i] - u_spotLights_outerCutoff[i];
    float intensity = clamp((theta - u_spotLights_outerCutoff[i]) / epsilon, 0.0, 1.0);

    vec3 ambient  = u_spotLights_ambient[i] * baseColor;
    vec3 diffuse  = u_spotLights_diffuse[i] * diff * baseColor;
    vec3 specular = u_spotLights_specular[i] * spec * u_materialSpecular;

    return (ambient + (diffuse + specular) * intensity) * attenuation;
}

// =============================================================================
// PBR lighting functions (Cook-Torrance)
// =============================================================================

/// Calculates Cook-Torrance specular + Lambertian diffuse for a directional light.
vec3 calcDirectionalLightPBR(vec3 N, vec3 V, vec3 albedo, float metallic,
                              float roughness, vec3 F0)
{
    vec3 L = normalize(-u_dirLight_direction);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Cook-Torrance BRDF
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(HdotV, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 radiance = u_dirLight_diffuse;

    // Shadow
    float shadow = 0.0;
    if (u_hasShadows)
    {
        shadow = calcShadow(N, L);
    }

    vec3 baseLighting = (kD * albedo / PI + specular) * radiance * NdotL;

    // Clearcoat: second specular lobe (smooth lacquer layer)
    if (u_clearcoat > 0.0)
    {
        float Fc;
        vec3 ccSpec = calcClearcoatLobe(NdotH, NdotV, NdotL, HdotV,
                                         u_clearcoatRoughness, Fc);
        // Attenuate base by clearcoat Fresnel (energy conservation)
        baseLighting *= (1.0 - u_clearcoat * Fc);
        baseLighting += ccSpec * radiance * NdotL * u_clearcoat;
    }

    return baseLighting * (1.0 - shadow);
}

/// Calculates Cook-Torrance specular + Lambertian diffuse for a point light.
vec3 calcPointLightPBR(int i, vec3 N, vec3 V, vec3 fragPos, vec3 albedo,
                        float metallic, float roughness, vec3 F0)
{
    vec3 toLight = u_pointLights_position[i] - fragPos;
    float dist = length(toLight);
    vec3 L = toLight / dist;  // normalize reusing dist
    vec3 H = normalize(V + L);

    float attenuation = 1.0 / (u_pointLights_constant[i]
        + u_pointLights_linear[i] * dist
        + u_pointLights_quadratic[i] * dist * dist);
    vec3 radiance = u_pointLights_diffuse[i] * attenuation;

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(HdotV, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    // Shadow
    float shadow = 0.0;
    int shadowIdx = findPointShadowIndex(i);
    if (shadowIdx >= 0)
    {
        shadow = calcPointShadow(shadowIdx, fragPos, u_pointLights_position[i], N);
    }

    vec3 baseLighting = (kD * albedo / PI + specular) * radiance * NdotL;

    // Clearcoat
    if (u_clearcoat > 0.0)
    {
        float Fc;
        vec3 ccSpec = calcClearcoatLobe(NdotH, NdotV, NdotL, HdotV,
                                         u_clearcoatRoughness, Fc);
        baseLighting *= (1.0 - u_clearcoat * Fc);
        baseLighting += ccSpec * radiance * NdotL * u_clearcoat;
    }

    return baseLighting * (1.0 - shadow);
}

/// Calculates Cook-Torrance specular + Lambertian diffuse for a spot light.
vec3 calcSpotLightPBR(int i, vec3 N, vec3 V, vec3 fragPos, vec3 albedo,
                       float metallic, float roughness, vec3 F0)
{
    vec3 toLight = u_spotLights_position[i] - fragPos;
    float dist = length(toLight);
    vec3 L = toLight / dist;  // normalize reusing dist
    vec3 H = normalize(V + L);

    float attenuation = 1.0 / (u_spotLights_constant[i]
        + u_spotLights_linear[i] * dist
        + u_spotLights_quadratic[i] * dist * dist);

    // Spotlight cone
    float theta = dot(L, normalize(-u_spotLights_direction[i]));
    float epsilon = u_spotLights_innerCutoff[i] - u_spotLights_outerCutoff[i];
    float intensity = clamp((theta - u_spotLights_outerCutoff[i]) / epsilon, 0.0, 1.0);

    vec3 radiance = u_spotLights_diffuse[i] * attenuation * intensity;

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3  F = fresnelSchlick(HdotV, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 baseLighting = (kD * albedo / PI + specular) * radiance * NdotL;

    // Clearcoat
    if (u_clearcoat > 0.0)
    {
        float Fc;
        vec3 ccSpec = calcClearcoatLobe(NdotH, NdotV, NdotL, HdotV,
                                         u_clearcoatRoughness, Fc);
        baseLighting *= (1.0 - u_clearcoat * Fc);
        baseLighting += ccSpec * radiance * NdotL * u_clearcoat;
    }

    return baseLighting;
}

// =============================================================================
// Parallax occlusion mapping
// =============================================================================

/// Computes offset UV coordinates via parallax occlusion mapping.
/// Ray-marches through a height field in tangent space for sub-pixel depth.
vec2 parallaxOcclusionMap(vec2 texCoords, vec3 viewDirTS)
{
    // Adaptive layer count: more layers at grazing angles for accuracy
    float numLayers = mix(32.0, 8.0, abs(viewDirTS.z));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    // Direction to shift UVs per layer (scaled by height)
    vec2 p = viewDirTS.xy * u_heightScale;
    vec2 deltaTexCoords = p / numLayers;

    vec2 currentTexCoords = texCoords;
    float currentDepthMapValue = texture(u_heightMap, currentTexCoords).r;
    float previousDepthMapValue = currentDepthMapValue;

    // March through layers until ray depth exceeds sampled height
    for (int i = 0; i < 32; i++)
    {
        if (currentLayerDepth >= currentDepthMapValue)
        {
            break;
        }
        previousDepthMapValue = currentDepthMapValue;
        currentTexCoords -= deltaTexCoords;
        currentDepthMapValue = texture(u_heightMap, currentTexCoords).r;
        currentLayerDepth += layerDepth;
    }

    // Linear interpolation between last two samples for sub-step precision
    vec2 prevTexCoords = currentTexCoords + deltaTexCoords;
    float afterDepth = currentDepthMapValue - currentLayerDepth;
    float beforeDepth = previousDepthMapValue - (currentLayerDepth - layerDepth);
    float denom = afterDepth - beforeDepth;
    float weight = (abs(denom) < 0.0001) ? 0.5 : afterDepth / denom;
    vec2 finalTexCoords = prevTexCoords * weight + currentTexCoords * (1.0 - weight);

    return finalTexCoords;
}

/// Computes a soft self-shadow factor for POM surfaces by ray-marching toward the
/// light through the height field. Returns 1.0 (lit) or 0.0..1.0 (shadowed).
float pomSelfShadow(vec2 texCoords, vec3 lightDirTS, float currentHeight)
{
    // March from the POM intersection point toward the light in tangent space.
    // If the ray hits the height field, this point is in the surface's own shadow.
    float numLayers = 16.0;
    float layerStep = 1.0 / numLayers;

    vec2 deltaUV = lightDirTS.xy * u_heightScale / numLayers;
    float deltaHeight = lightDirTS.z / numLayers;

    vec2 sampleUV = texCoords + deltaUV;
    float sampleLayerH = currentHeight + deltaHeight;
    float shadow = 0.0;

    for (int i = 0; i < 16; i++)
    {
        if (sampleLayerH > 1.0)
        {
            break;  // Exited the surface — reached the light
        }

        float mapHeight = texture(u_heightMap, sampleUV).r;

        // If the height field is above our ray, we're in shadow
        if (mapHeight > sampleLayerH)
        {
            // Soft shadow: weight by how much the height field exceeds the ray
            shadow = max(shadow, (mapHeight - sampleLayerH) * 8.0);
        }

        sampleUV += deltaUV;
        sampleLayerH += deltaHeight;
    }

    return 1.0 - clamp(shadow, 0.0, 1.0);
}

// =============================================================================
// Main
// =============================================================================

void main()
{

    // Wireframe mode — solid color, no lighting
    if (u_wireframe)
    {
        fragColor = vec4(0.0, 1.0, 0.0, 1.0);
        return;
    }

    // Compute texture coordinates — apply UV scale, then POM offset if height map is present
    vec2 texCoords = v_texCoord * u_uvScale;
    float pomShadowFactor = 1.0;
    if (u_hasHeightMap)
    {
        // View direction in tangent space for parallax calculation
        vec3 viewDirTS = normalize(transpose(v_TBN) * (u_viewPosition - v_fragPosition));
        texCoords = parallaxOcclusionMap(texCoords, viewDirTS);

        // POM self-shadowing: march toward the directional light to check if
        // the height field occludes this point. Only for the primary light.
        if (u_hasDirLight)
        {
            vec3 lightDirTS = normalize(transpose(v_TBN) * (-u_dirLight_direction));
            float currentHeight = texture(u_heightMap, texCoords).r;
            pomShadowFactor = pomSelfShadow(texCoords, lightDirTS, currentHeight);
        }
    }

    // Get surface normal — from normal map or vertex data
    vec3 norm;
    if (u_hasNormalMap)
    {
        // Sample normal map and transform from [0,1] to [-1,1]
        norm = sampleMaterial(u_normalMap, texCoords).rgb * 2.0 - 1.0;
        norm = normalize(v_TBN * norm);
    }
    else
    {
        norm = normalize(v_normal);
    }

    vec3 viewDir = normalize(u_viewPosition - v_fragPosition);

    if (u_usePBR)
    {
        // =====================================================================
        // PBR path (Cook-Torrance BRDF)
        // =====================================================================

        // Albedo — base color modulated by texture and vertex color
        vec3 albedo = u_pbrAlbedo;
        float alpha = u_baseColorAlpha;
        if (u_hasTexture)
        {
            vec4 texSample = sampleMaterial(u_diffuseTexture, texCoords);
            albedo *= texSample.rgb;
            alpha *= texSample.a;
        }
        albedo *= v_color;

        // Metallic and roughness — sample from packed texture if available
        float metallic = u_pbrMetallic;
        float roughness = u_pbrRoughness;
        if (u_hasMetallicRoughnessMap)
        {
            vec3 mrSample = sampleMaterial(u_metallicRoughnessMap, texCoords).rgb;
            // glTF packing: G = roughness, B = metallic
            roughness *= mrSample.g;
            metallic *= mrSample.b;
        }

        // Ambient occlusion
        float ao = u_pbrAo;
        if (u_hasAoMap)
        {
            ao *= sampleMaterial(u_aoMap, texCoords).r;
        }

        // Emissive
        vec3 emissive = u_pbrEmissive;
        if (u_hasEmissiveMap)
        {
            emissive *= sampleMaterial(u_emissiveMap, texCoords).rgb;
        }
        emissive *= u_pbrEmissiveStrength;

        // F0 — base reflectivity: 0.04 for dielectrics, albedo for metals
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        // Accumulate direct lighting
        vec3 Lo = vec3(0.0);

        if (u_hasDirLight)
        {
            Lo += calcDirectionalLightPBR(norm, viewDir, albedo, metallic, roughness, F0) * pomShadowFactor;
        }

        for (int i = 0; i < u_pointLightCount && i < MAX_POINT_LIGHTS; i++)
        {
            Lo += calcPointLightPBR(i, norm, viewDir, v_fragPosition,
                                     albedo, metallic, roughness, F0);
        }

        for (int i = 0; i < u_spotLightCount && i < MAX_SPOT_LIGHTS; i++)
        {
            Lo += calcSpotLightPBR(i, norm, viewDir, v_fragPosition,
                                    albedo, metallic, roughness, F0);
        }

        // Ambient lighting — IBL or fallback constant
        vec3 ambient;
        if (u_hasIBL)
        {
            // Fresnel and diffuse/specular split
            vec3 F = fresnelSchlickRoughness(max(dot(norm, viewDir), 0.0), F0, roughness);
            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
            vec3 R = reflect(-viewDir, norm);
            float lod = roughness * u_maxPrefilterLod;

            // Diffuse irradiance: SH grid (preferred) > cubemap probe > global sky
            vec3 irradiance;
            if (u_hasSHGrid)
            {
                // SH probe grid: smooth trilinear-interpolated local irradiance
                irradiance = evaluateSHGridIrradiance(v_fragPosition, norm);
            }
            else if (u_hasProbe && u_probeWeight > 0.0)
            {
                // Cubemap probe: blend between global and probe irradiance
                vec3 globalIrr = texture(u_irradianceMap, norm).rgb;
                vec3 probeIrr = texture(u_probeIrradianceMap, norm).rgb;
                irradiance = mix(globalIrr, probeIrr, u_probeWeight);
            }
            else
            {
                // Global sky IBL only
                irradiance = texture(u_irradianceMap, norm).rgb;
            }

            // Specular: always use global prefilter cubemap (SH is diffuse-only)
            // Cubemap probe can override specular in its influence zone
            vec3 prefilteredColor = textureLod(u_prefilterMap, R, lod).rgb;
            if (u_hasProbe && u_probeWeight > 0.0)
            {
                vec3 probePrefilt = textureLod(u_probePrefilterMap, R, lod).rgb;
                prefilteredColor = mix(prefilteredColor, probePrefilt, u_probeWeight);
            }

            vec3 diffuseIBL = kD * irradiance * albedo;
            vec2 brdf = texture(u_brdfLUT, vec2(max(dot(norm, viewDir), 0.0), roughness)).rg;
            vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

            ambient = (diffuseIBL + specularIBL) * ao * u_iblMultiplier;

            // Clearcoat IBL reflection (uses blended prefilter)
            if (u_clearcoat > 0.0)
            {
                float ccLod = u_clearcoatRoughness * u_maxPrefilterLod;
                vec3 ccPrefilt = textureLod(u_prefilterMap, R, ccLod).rgb;
                if (u_hasProbe && u_probeWeight > 0.0)
                {
                    vec3 ccProbe = textureLod(u_probePrefilterMap, R, ccLod).rgb;
                    ccPrefilt = mix(ccPrefilt, ccProbe, u_probeWeight);
                }
                float ccFresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 5.0) * 0.96 + 0.04;
                vec3 ccIBL = ccPrefilt * ccFresnel;
                ambient = ambient * (1.0 - u_clearcoat * ccFresnel) + ccIBL * u_clearcoat * ao * u_iblMultiplier;
            }
        }
        else
        {
            vec3 ambientColor = vec3(0.03);
            if (u_hasDirLight)
            {
                ambientColor = u_dirLight_ambient;
            }
            ambient = ambientColor * albedo * ao;
        }

        vec3 color = ambient + Lo + emissive;

        // Alpha masking: discard below cutoff
        if (u_alphaMode == 1 && alpha < u_alphaCutoff)
        {
            discard;
        }

        // Output alpha: only BLEND mode outputs actual alpha
        fragColor = vec4(color, u_alphaMode == 2 ? alpha : 1.0);
    }
    else
    {
        // =====================================================================
        // Blinn-Phong path (unchanged from original)
        // =====================================================================

        // Determine base color — texture or material color, modulated by vertex color
        vec3 baseColor;
        float alpha = u_baseColorAlpha;
        if (u_hasTexture)
        {
            vec4 texSample = sampleMaterial(u_diffuseTexture, texCoords);
            baseColor = texSample.rgb * v_color;
            alpha *= texSample.a;
        }
        else
        {
            baseColor = u_materialDiffuse * v_color;
        }

        // Accumulate light contributions
        vec3 result = vec3(0.0);

        // Directional light (with POM self-shadow if applicable)
        if (u_hasDirLight)
        {
            result += calcDirectionalLight(norm, viewDir, baseColor) * pomShadowFactor;
        }

        // Point lights
        for (int i = 0; i < u_pointLightCount && i < MAX_POINT_LIGHTS; i++)
        {
            result += calcPointLight(i, norm, viewDir, baseColor);
        }

        // Spot lights
        for (int i = 0; i < u_spotLightCount && i < MAX_SPOT_LIGHTS; i++)
        {
            result += calcSpotLight(i, norm, viewDir, baseColor);
        }

        // Emissive contribution (HDR — feeds into bloom)
        result += u_materialEmissive * u_materialEmissiveStrength;

        // Alpha masking: discard below cutoff
        if (u_alphaMode == 1 && alpha < u_alphaCutoff)
        {
            discard;
        }

        // Output alpha: only BLEND mode outputs actual alpha
        fragColor = vec4(result, u_alphaMode == 2 ? alpha : 1.0);
    }

    // Water caustics — additive light pattern on geometry below the water surface
    // Only within the water's XZ footprint (not the entire scene)
    if (u_causticsEnabled && v_fragPosition.y < u_waterY
        && abs(v_fragPosition.x - u_waterCenter.x) < u_waterHalfExtent.x
        && abs(v_fragPosition.z - u_waterCenter.y) < u_waterHalfExtent.y)
    {
        // Dual scrolling samples with min-blending for organic caustic pattern
        vec2 causticUV1 = v_fragPosition.xz * u_causticsScale
                        + u_causticsTime * vec2(0.03, 0.02);
        vec2 causticUV2 = v_fragPosition.xz * u_causticsScale * 1.4
                        + u_causticsTime * vec2(-0.02, 0.03);

        // Chromatic aberration — offset R and B channels for rainbow fringing
        float r1 = texture(u_causticsTex, causticUV1 + vec2(0.001, 0.0)).r;
        float g1 = texture(u_causticsTex, causticUV1).r;
        float b1 = texture(u_causticsTex, causticUV1 - vec2(0.001, 0.0)).r;
        vec3 caustic1 = vec3(r1, g1, b1);

        float r2 = texture(u_causticsTex, causticUV2 + vec2(0.0, 0.001)).r;
        float g2 = texture(u_causticsTex, causticUV2).r;
        float b2 = texture(u_causticsTex, causticUV2 - vec2(0.0, 0.001)).r;
        vec3 caustic2 = vec3(r2, g2, b2);

        vec3 caustics = min(caustic1, caustic2) * u_causticsIntensity;

        // Tint caustics with a subtle blue-green (refracted sunlight through water)
        caustics *= vec3(0.7, 0.9, 1.0);

        // Fade with depth below water — caustics weaken deeper down
        float depthBelowWater = u_waterY - v_fragPosition.y;
        float depthFade = 1.0 - smoothstep(0.0, 5.0, depthBelowWater);
        caustics *= depthFade;

        // Scale by directional light intensity (caustics are refracted sunlight)
        float lightScale = u_hasDirLight ? length(u_dirLight_diffuse) * 0.5 : 0.25;
        fragColor.rgb += caustics * lightScale;
    }

    // Cascade debug visualization — tints fragments by cascade index
    if (u_cascadeDebug && u_hasShadows)
    {
        int cascade = getCascadeIndex();
        vec3 debugColors[4] = vec3[4](
            vec3(1.0, 0.2, 0.2),  // Red — nearest
            vec3(0.2, 1.0, 0.2),  // Green
            vec3(0.2, 0.2, 1.0),  // Blue
            vec3(1.0, 1.0, 0.2)   // Yellow — farthest
        );
        vec3 tint = debugColors[clamp(cascade, 0, 3)];
        fragColor.rgb = mix(fragColor.rgb, tint, 0.3);
    }
}
