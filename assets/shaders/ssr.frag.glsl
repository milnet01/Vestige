/// @file ssr.frag.glsl
/// @brief Screen-space reflections — ray-marches reflection vectors against the depth buffer to approximate local reflections.
#version 450 core

/// Screen-space reflections — ray-march along the reflection vector in screen
/// space, sampling the depth buffer to find intersections. The reflected scene
/// color at the hit point is used as a local reflection.
///
/// Limitations (forward renderer without G-buffer):
/// - No per-pixel roughness: reflection sharpness is uniform
/// - Screen-space only: objects behind the camera or off-screen cannot reflect
/// - Driven by Fresnel: strongest at grazing angles (physically correct)

in vec2 v_texCoord;

uniform sampler2D u_depthTexture;     // Resolved depth buffer (reverse-Z)
uniform sampler2D u_sceneTexture;     // Resolved HDR scene color
uniform mat4 u_projection;           // Camera projection (reverse-Z)
uniform mat4 u_invProjection;        // Inverse projection
uniform vec2 u_texelSize;            // 1.0 / screen resolution
uniform float u_maxDistance;          // Maximum ray length in view space
uniform float u_thickness;           // Depth comparison thickness
uniform int u_maxSteps;              // Ray march step count

out vec4 fragColor;  // RGB = reflected color, A = confidence

/// Reconstruct view-space position from screen UV and depth (reverse-Z).
vec3 viewPosFromDepth(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = u_invProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

/// Linearize reverse-Z depth to view-space Z (negative, into screen).
float linearizeDepth(float depth)
{
    float nearPlane = u_projection[3][2];
    return -nearPlane / max(depth, 0.0001);
}

void main()
{
    float depth = texture(u_depthTexture, v_texCoord).r;

    // Skip sky (reverse-Z: depth 0.0)
    if (depth < 0.0001)
    {
        fragColor = vec4(0.0);
        return;
    }

    vec3 viewPos = viewPosFromDepth(v_texCoord, depth);

    // Approximate view-space normal from depth derivatives
    float depthR = texture(u_depthTexture, v_texCoord + vec2(u_texelSize.x, 0.0)).r;
    float depthU = texture(u_depthTexture, v_texCoord + vec2(0.0, u_texelSize.y)).r;

    // Use central differences where possible for better quality
    vec3 viewPosR = viewPosFromDepth(v_texCoord + vec2(u_texelSize.x, 0.0), depthR);
    vec3 viewPosU = viewPosFromDepth(v_texCoord + vec2(0.0, u_texelSize.y), depthU);
    vec3 ddx = viewPosR - viewPos;
    vec3 ddy = viewPosU - viewPos;
    vec3 normal = normalize(cross(ddx, ddy));

    // View direction (from camera origin to fragment)
    vec3 viewDir = normalize(viewPos);

    // Reflection direction
    vec3 reflectDir = reflect(viewDir, normal);

    // Skip reflections going toward the camera (nothing to sample)
    if (reflectDir.z > -0.01)
    {
        fragColor = vec4(0.0);
        return;
    }

    // Fresnel — drives SSR intensity. Without per-pixel roughness/metallic,
    // Fresnel is our best signal for "how reflective is this surface."
    // Schlick approximation with a moderate F0.
    float NdotV = max(dot(normal, -viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 4.0) * 0.8 + 0.02;

    // Skip if Fresnel contribution is negligible
    if (fresnel < 0.01)
    {
        fragColor = vec4(0.0);
        return;
    }

    // Ray march in view space, project each step to screen space
    vec3 rayStep = reflectDir * (u_maxDistance / float(u_maxSteps));

    // Start 2 steps ahead to avoid self-intersection
    vec3 currentPos = viewPos + rayStep * 2.0;

    vec3 hitColor = vec3(0.0);
    float confidence = 0.0;

    for (int i = 2; i <= u_maxSteps; i++)
    {
        // Project to screen space
        vec4 clipPos = u_projection * vec4(currentPos, 1.0);
        if (clipPos.w <= 0.0)
        {
            break;
        }

        vec2 screenUV = (clipPos.xy / clipPos.w) * 0.5 + 0.5;

        // Screen bounds with smooth edge fade
        vec2 edgeDist = min(screenUV, 1.0 - screenUV);
        float edgeFade = smoothstep(0.0, 0.1, min(edgeDist.x, edgeDist.y));
        if (edgeFade <= 0.0)
        {
            break;
        }

        // Sample depth buffer
        float sampleDepth = texture(u_depthTexture, screenUV).r;

        // Skip sky
        if (sampleDepth < 0.0001)
        {
            currentPos += rayStep;
            continue;
        }

        // Compare linear view-space depths
        float sampleZ = linearizeDepth(sampleDepth);
        float rayZ = currentPos.z;
        float depthDiff = sampleZ - rayZ;

        // Hit: depth buffer surface is in front of the ray and within thickness
        if (depthDiff > 0.0 && depthDiff < u_thickness)
        {
            hitColor = texture(u_sceneTexture, screenUV).rgb;

            // Fade by distance along the ray and screen edge
            float distanceFade = 1.0 - (float(i) / float(u_maxSteps));
            distanceFade *= distanceFade;  // Quadratic falloff

            confidence = fresnel * edgeFade * distanceFade;
            break;
        }

        currentPos += rayStep;
    }

    fragColor = vec4(hitColor * confidence, confidence);
}
