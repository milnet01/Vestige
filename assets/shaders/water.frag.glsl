/// @file water.frag.glsl
/// @brief Water surface fragment shader — Fresnel blending, normal-mapped distortion, and environment reflections.
#version 450 core

in vec3 v_worldPos;
in vec2 v_texCoord;
in vec4 v_clipSpace;
in vec3 v_normal;

uniform samplerCube u_environmentMap;
uniform sampler2D u_normalMap;
uniform sampler2D u_dudvMap;

uniform vec3 u_cameraPos;
uniform float u_time;

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

out vec4 fragColor;

void main()
{
    // Animated texture coordinates for normal/DuDv scrolling
    float flowOffset = u_time * u_flowSpeed;
    vec2 scrolledCoords1 = v_texCoord * 4.0 + vec2(flowOffset, 0.0);
    vec2 scrolledCoords2 = v_texCoord * 4.0 + vec2(0.0, flowOffset * 0.8);

    // Surface normal (from wave geometry + optional normal map detail)
    vec3 normal = normalize(v_normal);
    if (u_hasNormalMap)
    {
        vec3 n1 = texture(u_normalMap, scrolledCoords1).rgb * 2.0 - 1.0;
        vec3 n2 = texture(u_normalMap, scrolledCoords2).rgb * 2.0 - 1.0;
        vec3 mapNormal = normalize(n1 + n2);
        // Blend normal map XZ with wave normal, preserving Y-up
        normal = normalize(vec3(
            normal.x + mapNormal.x * u_normalStrength,
            normal.y,
            normal.z + mapNormal.z * u_normalStrength
        ));
    }

    // DuDv-based normal perturbation (alternative detail when no normal map)
    if (u_hasDudvMap)
    {
        vec2 d1 = texture(u_dudvMap, scrolledCoords1).rg * 2.0 - 1.0;
        vec2 d2 = texture(u_dudvMap, scrolledCoords2).rg * 2.0 - 1.0;
        vec2 distortion = (d1 + d2) * u_dudvStrength;
        normal = normalize(vec3(
            normal.x + distortion.x,
            normal.y,
            normal.z + distortion.y
        ));
    }

    // View direction and reflected direction
    vec3 viewDir = normalize(u_cameraPos - v_worldPos);
    vec3 reflectedDir = reflect(-viewDir, normal);

    // Reflection from environment cubemap
    vec4 reflectionColor = vec4(0.6, 0.7, 0.8, 1.0);  // Default sky color
    if (u_hasEnvironmentMap)
    {
        reflectionColor = texture(u_environmentMap, reflectedDir);
    }

    // Fresnel (Schlick approximation, F0 = 0.02 for water)
    float cosTheta = max(dot(normal, viewDir), 0.0);
    float F0 = 0.02;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

    // Water base color: use shallow color (could add depth-based blend later with refraction FBO)
    vec4 waterColor = u_shallowColor;

    // Viewing angle blend: more of deep color when looking straight down
    float depthFactor = 1.0 - cosTheta;
    waterColor = mix(u_shallowColor, u_deepColor, depthFactor * depthFactor);

    // Specular highlight (Blinn-Phong from directional light)
    vec3 lightDir = normalize(-u_lightDirection);
    vec3 halfDir = normalize(viewDir + lightDir);
    float specAngle = max(dot(normal, halfDir), 0.0);
    float specular = pow(specAngle, u_specularPower);
    vec3 specularColor = u_lightColor * specular * 0.6;

    // Final: blend water color and reflection using Fresnel, add specular
    vec3 finalColor = mix(waterColor.rgb, reflectionColor.rgb, fresnel);
    finalColor += specularColor;

    // Alpha: mostly opaque, slight transparency at steep viewing angles
    float alpha = mix(0.85, 1.0, fresnel);

    fragColor = vec4(finalColor, alpha);
}
