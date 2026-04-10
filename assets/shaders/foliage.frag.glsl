/// @file foliage.frag.glsl
/// @brief Foliage fragment shader — alpha-tested textured grass/plants with directional lighting and CSM shadows.
#version 450 core

in vec2 v_texCoord;
in vec3 v_colorTint;
in float v_alpha;
in vec3 v_fragPosition;
in float v_viewDepth;
in float v_heightAO;

uniform sampler2D u_texture;
uniform vec3 u_cameraPos;

// Lighting
uniform bool u_hasDirectionalLight;
uniform vec3 u_lightDirection;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;

// Cascaded shadow mapping
uniform bool u_hasShadows;
uniform sampler2DArray u_cascadeShadowMap;
uniform int u_cascadeCount;
uniform float u_cascadeSplits[4];
uniform mat4 u_cascadeLightSpaceMatrices[4];

out vec4 fragColor;

// ---------------------------------------------------------------------------
// Per-fragment noise for rotating Poisson samples (breaks banding artifacts)
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
// Simplified 4-sample Poisson PCF shadow for grass
// (cheaper than the 32-sample PCSS used for scene objects)
// ---------------------------------------------------------------------------
float calcGrassShadow()
{
    int cascade = getCascadeIndex();

    vec4 lightSpacePos = u_cascadeLightSpaceMatrices[cascade] * vec4(v_fragPosition, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;

    // Fragment beyond shadow map range
    if (proj.z > 1.0)
        return 0.0;

    // Constant bias (grass normals are unreliable for slope-scaled bias)
    float bias = 0.003;

    vec2 texelSize = 1.0 / vec2(textureSize(u_cascadeShadowMap, 0).xy);

    // Rotate 4 Poisson samples per-fragment to break banding
    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.28318;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rot = mat2(cosA, sinA, -sinA, cosA);

    const vec2 samples[4] = vec2[4](
        vec2(-0.94201624, -0.39906216),
        vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870),
        vec2( 0.34495938,  0.29387760)
    );

    float shadow = 0.0;
    for (int i = 0; i < 4; i++)
    {
        vec2 offset = rot * samples[i] * 1.5 * texelSize;
        float d = texture(u_cascadeShadowMap,
            vec3(proj.xy + offset, float(cascade))).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }

    // Fade shadow at cascade far boundary
    float maxShadowDist = u_cascadeSplits[u_cascadeCount - 1];
    float fadeStart = maxShadowDist * 0.8;
    float depth = abs(v_viewDepth);
    float shadowFade = 1.0 - smoothstep(fadeStart, maxShadowDist, depth);

    return shadow * 0.25 * shadowFade;
}

void main()
{
    vec4 texel = texture(u_texture, v_texCoord);

    // Alpha test — discard transparent pixels
    if (texel.a < 0.5)
        discard;

    // Base color with height-based ambient occlusion
    // Darkens grass at base (inter-blade occlusion), bright at tips
    float aoFactor = mix(0.35, 1.0, v_heightAO);
    vec3 baseColor = texel.rgb * v_colorTint * aoFactor;

    vec3 finalColor;

    if (u_hasDirectionalLight)
    {
        vec3 lightDir = normalize(-u_lightDirection);

        // Half-Lambert wrap diffuse (softer than standard Lambertian)
        // Grass normals point sideways (perpendicular to quad face), so we use
        // a fixed upward normal for more natural ground-level lighting
        vec3 grassNormal = vec3(0.0, 1.0, 0.0);
        float NdotL = dot(grassNormal, lightDir) * 0.5 + 0.5;

        // Translucency: backlit glow when sun is behind grass
        vec3 viewDir = normalize(u_cameraPos - v_fragPosition);
        float viewLightAlign = pow(max(dot(viewDir, -lightDir), 0.0), 3.0);
        float translucency = max(dot(vec3(0.0, -1.0, 0.0), lightDir), 0.0) * viewLightAlign * 0.4;

        // Shadow
        float shadow = 0.0;
        if (u_hasShadows)
        {
            shadow = calcGrassShadow();
        }

        // Combine lighting
        vec3 ambient = baseColor * u_ambientColor;
        vec3 direct = baseColor * NdotL * u_lightColor;
        vec3 trans = baseColor * translucency * u_lightColor;

        // Shadow darkens direct light and partially attenuates translucency
        float shadowStrength = 0.65;
        finalColor = ambient + (direct + trans) * (1.0 - shadow * shadowStrength);
    }
    else
    {
        // Unlit fallback
        finalColor = baseColor;
    }

    fragColor = vec4(finalColor, texel.a * v_alpha);
}
