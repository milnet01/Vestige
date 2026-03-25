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

// Water caustics (applied to terrain below water surface, within water XZ bounds)
uniform bool u_causticsEnabled;
uniform sampler2D u_causticsTex;      // Unit 5
uniform float u_causticsScale;
uniform float u_causticsIntensity;
uniform float u_causticsTime;
uniform float u_waterY;
uniform vec2 u_waterCenter;
uniform vec2 u_waterHalfExtent;

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

void main()
{
    // Read terrain normal from pre-computed normal map (encoded as n * 0.5 + 0.5)
    vec3 normal = texture(u_normalMap, v_terrainUV).rgb * 2.0 - 1.0;
    normal = normalize(normal);

    // Read splatmap weights for base color
    vec4 splat = texture(u_splatmap, v_terrainUV);

    // Simple base colors for each layer (will be replaced with texture arrays in 5I-2)
    vec3 grassColor = vec3(0.28, 0.52, 0.18);
    vec3 rockColor  = vec3(0.45, 0.42, 0.38);
    vec3 dirtColor  = vec3(0.55, 0.40, 0.25);
    vec3 sandColor  = vec3(0.76, 0.70, 0.50);

    vec3 albedo = grassColor * splat.r
                + rockColor  * splat.g
                + dirtColor  * splat.b
                + sandColor  * splat.a;

    // Directional lighting (Blinn-Phong)
    vec3 lightDir = normalize(-u_lightDirection);
    float NdotL = max(dot(normal, lightDir), 0.0);

    vec3 viewDir = normalize(u_viewPos - v_worldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float specular = pow(NdotH, 64.0) * 0.15;

    // Shadow
    float shadow = 0.0;
    if (u_hasShadows)
    {
        shadow = calcTerrainShadow(normal, lightDir);
    }

    vec3 ambient = albedo * u_ambientColor;
    vec3 diffuse = albedo * NdotL * u_lightColor;
    vec3 spec = u_lightColor * specular * NdotL;

    vec3 color = ambient + (diffuse + spec) * (1.0 - shadow);

    // Water caustics — additive light pattern on terrain below the water surface
    if (u_causticsEnabled && v_worldPos.y < u_waterY
        && abs(v_worldPos.x - u_waterCenter.x) < u_waterHalfExtent.x
        && abs(v_worldPos.z - u_waterCenter.y) < u_waterHalfExtent.y)
    {
        vec2 causticUV1 = v_worldPos.xz * u_causticsScale
                        + u_causticsTime * vec2(0.03, 0.02);
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

        vec3 caustics = min(caustic1, caustic2) * u_causticsIntensity;

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
