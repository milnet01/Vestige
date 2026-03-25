/// @file material_preview.frag.glsl
/// @brief Material preview fragment shader — renders a sphere with Blinn-Phong or PBR lighting for the editor panel.
#version 450 core

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_texCoord;

out vec4 fragColor;

// Material properties
uniform bool u_usePBR;

// Blinn-Phong
uniform vec3 u_diffuseColor;
uniform vec3 u_specularColor;
uniform float u_shininess;

// PBR
uniform vec3 u_albedo;
uniform float u_metallic;
uniform float u_roughness;
uniform float u_ao;

// Textures
uniform bool u_hasAlbedoTex;
uniform sampler2D u_albedoTex;

// Fixed preview light
uniform vec3 u_lightDir;
uniform vec3 u_viewPos;

const float PI = 3.14159265359;

// ---- Simplified PBR (no IBL, single directional light) ----

float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_viewPos - v_worldPos);
    vec3 L = normalize(-u_lightDir);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);

    // Sample albedo texture if available
    vec3 baseColor;
    if (u_usePBR)
    {
        baseColor = u_albedo;
    }
    else
    {
        baseColor = u_diffuseColor;
    }

    if (u_hasAlbedoTex)
    {
        baseColor *= texture(u_albedoTex, v_texCoord).rgb;
    }

    vec3 result;

    if (u_usePBR)
    {
        // PBR lighting
        vec3 F0 = mix(vec3(0.04), baseColor, u_metallic);

        float D = distributionGGX(N, H, u_roughness);
        float G = geometrySchlickGGX(max(dot(N, V), 0.0), u_roughness)
                * geometrySchlickGGX(NdotL, u_roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (D * G * F) /
            (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);

        vec3 kD = (vec3(1.0) - F) * (1.0 - u_metallic);
        result = (kD * baseColor / PI + specular) * vec3(1.0) * NdotL;

        // Ambient approximation
        result += baseColor * 0.03 * u_ao;
    }
    else
    {
        // Blinn-Phong
        vec3 ambient = baseColor * 0.1;
        vec3 diffuse = baseColor * NdotL;
        float spec = pow(max(dot(N, H), 0.0), u_shininess);
        vec3 specular = u_specularColor * spec;

        result = ambient + diffuse + specular * NdotL;
    }

    // Simple tone mapping for preview
    result = result / (result + vec3(1.0));
    result = pow(result, vec3(1.0 / 2.2));

    fragColor = vec4(result, 1.0);
}
