#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_depthTexture;    // Unit 12 — resolved depth
uniform sampler2D u_noiseTexture;    // Unit 11 — 4x4 random rotation vectors

uniform vec3 u_samples[64];
uniform int u_kernelSize;
uniform float u_radius;
uniform float u_bias;
uniform vec2 u_noiseScale;
uniform mat4 u_projection;
uniform mat4 u_invProjection;

out vec4 fragColor;

/// Reconstruct view-space position from depth.
/// Reverse-Z with [0,1] depth range: depth is already in [0,1] NDC.
vec3 viewPosFromDepth(vec2 uv, float depth)
{
    // With glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE), NDC z = depth directly
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = u_invProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

/// Reconstruct only view-space Z from depth (cheaper than full mat4*vec4).
/// For reverse-Z infinite projection: depth = near / (-viewZ), so viewZ = -near / depth.
/// We extract the near plane from the projection matrix: proj[3][2] = near.
float viewZFromDepth(float depth)
{
    float nearPlane = u_projection[3][2];  // Our projection stores near in [3][2]
    return -nearPlane / max(depth, 0.0001);
}

void main()
{
    float depth = texture(u_depthTexture, v_texCoord).r;

    // Reverse-Z: sky is at depth 0.0 (far plane), geometry is near 1.0 (near plane)
    if (depth <= 0.0001)
    {
        fragColor = vec4(1.0);
        return;
    }

    // Reconstruct view-space position (full vec3 needed for TBN + normal)
    vec3 fragPos = viewPosFromDepth(v_texCoord, depth);

    // Reconstruct view-space normal from position derivatives
    vec3 normal = normalize(cross(dFdx(fragPos), dFdy(fragPos)));

    // Sample noise for random TBN rotation
    vec3 randomVec = texture(u_noiseTexture, v_texCoord * u_noiseScale).xyz;

    // Build TBN matrix (Gram-Schmidt)
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    // Sample hemisphere and accumulate occlusion
    float occlusion = 0.0;
    for (int i = 0; i < u_kernelSize; i++)
    {
        // Transform sample to view space
        vec3 samplePos = TBN * u_samples[i];
        samplePos = fragPos + samplePos * u_radius;

        // Project sample to screen space
        vec4 offset = u_projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        // XY: NDC [-1,1] → UV [0,1]. Z is already in [0,1] for reverse-Z.
        offset.xy = offset.xy * 0.5 + 0.5;

        // Only reconstruct Z (not full vec3) — saves a mat4*vec4 per sample
        float sampleDepth = texture(u_depthTexture, offset.xy).r;
        float sampleViewZ = viewZFromDepth(sampleDepth);

        // Range check — only occlude if within radius
        float rangeCheck = smoothstep(0.0, 1.0, u_radius / abs(fragPos.z - sampleViewZ));
        occlusion += (sampleViewZ >= samplePos.z + u_bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(u_kernelSize));
    fragColor = vec4(ao, ao, ao, 1.0);
}
