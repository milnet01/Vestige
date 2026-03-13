#version 450 core

// Maximum light counts — must match C++ constants
const int MAX_POINT_LIGHTS = 8;
const int MAX_SPOT_LIGHTS = 4;

// Inputs from vertex shader
in vec3 v_fragPosition;
in vec3 v_normal;
in vec3 v_color;
in vec2 v_texCoord;
in vec4 v_fragPosLightSpace;

out vec4 fragColor;

// Material
uniform vec3 u_materialDiffuse;
uniform vec3 u_materialSpecular;
uniform float u_materialShininess;
uniform bool u_hasTexture;
uniform sampler2D u_diffuseTexture;

// Camera
uniform vec3 u_viewPosition;

// Directional light
uniform bool u_hasDirLight;
uniform vec3 u_dirLight_direction;
uniform vec3 u_dirLight_ambient;
uniform vec3 u_dirLight_diffuse;
uniform vec3 u_dirLight_specular;

// Shadow mapping
uniform bool u_hasShadows;
uniform sampler2D u_shadowMap;

// Point lights
uniform int u_pointLightCount;
uniform vec3 u_pointLights_position[MAX_POINT_LIGHTS];
uniform vec3 u_pointLights_ambient[MAX_POINT_LIGHTS];
uniform vec3 u_pointLights_diffuse[MAX_POINT_LIGHTS];
uniform vec3 u_pointLights_specular[MAX_POINT_LIGHTS];
uniform float u_pointLights_constant[MAX_POINT_LIGHTS];
uniform float u_pointLights_linear[MAX_POINT_LIGHTS];
uniform float u_pointLights_quadratic[MAX_POINT_LIGHTS];

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

// Wireframe mode
uniform bool u_wireframe;

/// Calculates the shadow factor using PCF (Percentage Closer Filtering).
/// Returns 0.0 (fully lit) to 1.0 (fully in shadow).
float calcShadow(vec3 normal, vec3 lightDir)
{
    // Perspective divide (identity for ortho, but correct for both)
    vec3 projCoords = v_fragPosLightSpace.xyz / v_fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;  // Transform from [-1,1] to [0,1]

    // Fragments outside the shadow map are not in shadow
    if (projCoords.z > 1.0)
    {
        return 0.0;
    }

    // Angle-dependent bias: surfaces facing the light need less bias
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // PCF 3x3: sample a grid around the fragment and average the results
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_shadowMap, 0);
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float closestDepth = texture(u_shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (projCoords.z - bias > closestDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

/// Calculates the contribution from a directional light.
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

/// Calculates the contribution from a point light.
vec3 calcPointLight(int i, vec3 norm, vec3 viewDir, vec3 baseColor)
{
    vec3 lightDir = normalize(u_pointLights_position[i] - v_fragPosition);

    // Diffuse
    float diff = max(dot(norm, lightDir), 0.0);

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), u_materialShininess);

    // Attenuation — light fades with distance
    float dist = length(u_pointLights_position[i] - v_fragPosition);
    float attenuation = 1.0 / (u_pointLights_constant[i]
        + u_pointLights_linear[i] * dist
        + u_pointLights_quadratic[i] * dist * dist);

    vec3 ambient  = u_pointLights_ambient[i] * baseColor;
    vec3 diffuse  = u_pointLights_diffuse[i] * diff * baseColor;
    vec3 specular = u_pointLights_specular[i] * spec * u_materialSpecular;

    return (ambient + diffuse + specular) * attenuation;
}

/// Calculates the contribution from a spot light.
vec3 calcSpotLight(int i, vec3 norm, vec3 viewDir, vec3 baseColor)
{
    vec3 lightDir = normalize(u_spotLights_position[i] - v_fragPosition);

    // Diffuse
    float diff = max(dot(norm, lightDir), 0.0);

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), u_materialShininess);

    // Attenuation
    float dist = length(u_spotLights_position[i] - v_fragPosition);
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

void main()
{
    // Wireframe mode — solid color, no lighting
    if (u_wireframe)
    {
        fragColor = vec4(0.0, 1.0, 0.0, 1.0);
        return;
    }

    vec3 norm = normalize(v_normal);
    vec3 viewDir = normalize(u_viewPosition - v_fragPosition);

    // Determine base color — texture or material color, modulated by vertex color
    vec3 baseColor;
    if (u_hasTexture)
    {
        baseColor = texture(u_diffuseTexture, v_texCoord).rgb * v_color;
    }
    else
    {
        baseColor = u_materialDiffuse * v_color;
    }

    // Accumulate light contributions
    vec3 result = vec3(0.0);

    // Directional light
    if (u_hasDirLight)
    {
        result += calcDirectionalLight(norm, viewDir, baseColor);
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

    fragColor = vec4(result, 1.0);
}
