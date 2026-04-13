/// @file contact_shadows.frag.glsl
/// @brief Screen-space contact shadows — ray-marches toward the light to catch fine shadow detail at object contacts.
#version 450 core

/// Screen-space contact shadows — ray-march toward the light in screen space
/// to catch fine shadow detail at object contact points that CSM cannot resolve.
///
/// Key challenge: without a normal buffer (forward renderer), we must avoid
/// self-shadowing by using screen-space depth derivatives to estimate the
/// surface orientation and reject samples on the same surface.

in vec2 v_texCoord;

uniform sampler2D u_depthTexture;     // Resolved depth buffer
uniform mat4 u_projection;           // Camera projection matrix (reverse-Z)
uniform mat4 u_invProjection;        // Inverse projection for reconstruction
uniform mat4 u_view;                 // Camera view matrix
uniform vec3 u_lightDirection;       // World-space directional light direction
uniform vec2 u_texelSize;            // 1.0 / screen resolution
uniform float u_rayLength;           // Maximum ray length in view space
uniform int u_numSteps;              // Number of ray march steps

out float fragColor;

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
    // For reverse-Z: near maps to 1.0, far maps to 0.0.
    // Extract near plane from projection matrix [3][2].
    float nearPlane = u_projection[3][2];
    return -nearPlane / max(depth, 0.0001);
}

void main()
{
    float depth = texture(u_depthTexture, v_texCoord).r;

    // Skip sky pixels (reverse-Z: sky at depth 0.0)
    if (depth < 0.0001)
    {
        fragColor = 1.0;
        return;
    }

    // Screen-space depth gradient → approximate view-space normal.
    // AUDIT.md §M18 / FIXPLAN F2: same central-difference upgrade as SSR
    // (§M16). Four-tap sampling with edge rejection replaces the prior
    // forward-only difference that produced garbage normals at depth
    // discontinuities. Garbage normals here caused spurious contact
    // shadows along silhouettes.
    vec3 viewPos = viewPosFromDepth(v_texCoord, depth);
    vec2 tx = vec2(u_texelSize.x, 0.0);
    vec2 ty = vec2(0.0, u_texelSize.y);
    vec3 viewPosL = viewPosFromDepth(v_texCoord - tx, texture(u_depthTexture, v_texCoord - tx).r);
    vec3 viewPosR = viewPosFromDepth(v_texCoord + tx, texture(u_depthTexture, v_texCoord + tx).r);
    vec3 viewPosD = viewPosFromDepth(v_texCoord - ty, texture(u_depthTexture, v_texCoord - ty).r);
    vec3 viewPosU = viewPosFromDepth(v_texCoord + ty, texture(u_depthTexture, v_texCoord + ty).r);

    // Reject edges where left/right or down/up diverge sharply.
    float edgeThreshold = max(abs(viewPos.z) * 0.05, 0.05);
    bool edgeX = abs((viewPosR.z - viewPos.z) - (viewPos.z - viewPosL.z)) > edgeThreshold;
    bool edgeY = abs((viewPosU.z - viewPos.z) - (viewPos.z - viewPosD.z)) > edgeThreshold;
    if (edgeX || edgeY)
    {
        // Cannot trust the normal at a silhouette; skip contact shadow.
        fragColor = 1.0;
        return;
    }

    vec3 ddx = 0.5 * (viewPosR - viewPosL);
    vec3 ddy = 0.5 * (viewPosU - viewPosD);
    vec3 approxNormal = normalize(cross(ddx, ddy));

    // Light direction in view space (pointing toward the light)
    vec3 lightDirVS = normalize(mat3(u_view) * (-u_lightDirection));

    // If the surface normal faces the light strongly, skip — no contact shadow needed.
    // This prevents the "glass sheet" artifact on cube tops facing the sun.
    float NdotL = dot(approxNormal, lightDirVS);
    if (NdotL > 0.5)
    {
        fragColor = 1.0;
        return;
    }
    // Fade the effect based on how much the surface faces the light
    float normalFade = 1.0 - smoothstep(0.0, 0.5, NdotL);

    // Scale ray length with distance from camera (farther = longer ray)
    float viewDist = -viewPos.z;
    float scaledRayLength = u_rayLength * max(viewDist * 0.1, 1.0);
    scaledRayLength = min(scaledRayLength, u_rayLength * 3.0);

    // Ray step in view space toward the light
    vec3 rayStep = lightDirVS * (scaledRayLength / float(u_numSteps));

    // Thickness threshold — reject occluders that are too thick (behind the surface)
    float thicknessThreshold = scaledRayLength * 0.1;

    // Start a few steps ahead to avoid self-intersection
    vec3 currentPos = viewPos + rayStep * 2.0;

    float shadow = 0.0;

    for (int i = 2; i <= u_numSteps; i++)
    {
        currentPos += rayStep;

        // Project current ray position to screen space
        vec4 clipPos = u_projection * vec4(currentPos, 1.0);

        // Behind the camera — stop
        if (clipPos.w <= 0.0)
        {
            break;
        }

        vec3 ndc = clipPos.xyz / clipPos.w;
        vec2 screenUV = ndc.xy * 0.5 + 0.5;

        // Fade out near screen edges instead of hard cutoff
        float edgeFade = 1.0;
        vec2 edgeDist = min(screenUV, 1.0 - screenUV);
        edgeFade = smoothstep(0.0, 0.05, min(edgeDist.x, edgeDist.y));
        if (edgeFade <= 0.0)
        {
            break;
        }

        // Sample depth buffer at this screen position
        float sampleDepth = texture(u_depthTexture, screenUV).r;

        // Skip sky pixels
        if (sampleDepth < 0.0001)
        {
            continue;
        }

        // Linear view-space depth comparison (cheaper than full reconstruction)
        float sampleLinearZ = linearizeDepth(sampleDepth);
        float rayLinearZ = currentPos.z;  // already in view space (negative)

        // Depth difference: positive means the depth buffer surface is in front of the ray
        float depthDiff = sampleLinearZ - rayLinearZ;

        // Occluder check: must be in front of the ray AND not too thick
        if (depthDiff > 0.01 && depthDiff < thicknessThreshold)
        {
            // Fade shadow: stronger near the starting point, weaker further along
            float t = float(i) / float(u_numSteps);
            float hitShadow = (1.0 - t * t) * edgeFade * normalFade;
            shadow = max(shadow, hitShadow);
            break;
        }
    }

    // Apply a soft cap — contact shadows supplement CSM, not replace it
    fragColor = 1.0 - shadow * 0.6;
}
