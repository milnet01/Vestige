/// @file water.frag.glsl
/// @brief Water surface fragment shader — Fresnel blending, reflections, refractions, and Beer's law absorption.
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

// Shore foam
uniform sampler2D u_foamTex;
uniform bool u_hasFoamTex;
uniform float u_foamDistance;      // How far from shore foam extends (metres)

// Soft edge
uniform float u_softEdgeDistance;  // Water-to-geometry fade distance (metres)

out vec4 fragColor;

void main()
{
    // Animated texture coordinates for normal/DuDv scrolling.
    // Use different scales and diagonal directions to break up tiling regularity.
    float flowOffset = u_time * u_flowSpeed;
    vec2 scrolledCoords1 = v_texCoord * 3.7 + vec2(flowOffset * 0.9, flowOffset * 0.3);
    vec2 scrolledCoords2 = v_texCoord * 5.3 + vec2(-flowOffset * 0.4, flowOffset * 0.7);

    // Surface normal (from wave geometry + optional normal map detail)
    vec3 normal = normalize(v_normal);
    if (u_hasNormalMap)
    {
        vec3 n1 = texture(u_normalMap, scrolledCoords1).rgb * 2.0 - 1.0;
        vec3 n2 = texture(u_normalMap, scrolledCoords2).rgb * 2.0 - 1.0;
        vec3 mapNormal = normalize(n1 + n2);
        normal = normalize(vec3(
            normal.x + mapNormal.x * u_normalStrength,
            normal.y,
            normal.z + mapNormal.z * u_normalStrength
        ));
    }

    // DuDv-based distortion
    vec2 totalDistortion = vec2(0.0);
    if (u_hasDudvMap)
    {
        vec2 d1 = texture(u_dudvMap, scrolledCoords1).rg * 2.0 - 1.0;
        vec2 d2 = texture(u_dudvMap, scrolledCoords2).rg * 2.0 - 1.0;
        totalDistortion = (d1 + d2) * u_dudvStrength;
        normal = normalize(vec3(
            normal.x + totalDistortion.x,
            normal.y,
            normal.z + totalDistortion.y
        ));
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
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

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
