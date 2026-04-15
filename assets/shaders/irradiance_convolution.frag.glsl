// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file irradiance_convolution.frag.glsl
/// @brief IBL diffuse irradiance convolution — integrates the environment map over the hemisphere per texel.
#version 450 core

in vec3 v_texCoord;
out vec4 fragColor;

uniform samplerCube u_environmentMap;

const float PI = 3.14159265359;

void main()
{
    // The sample direction equals the hemisphere's normal
    vec3 N = normalize(v_texCoord);

    // Build a tangent-space frame around N
    vec3 up = vec3(0.0, 1.0, 0.0);
    if (abs(dot(N, up)) > 0.999)
    {
        up = vec3(1.0, 0.0, 0.0);
    }
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    // Integrate incoming radiance over the hemisphere weighted by cos(theta)
    vec3 irradiance = vec3(0.0);
    const float sampleDelta = 0.025;
    int nrSamples = 0;
    int phiSteps = int(2.0 * PI / sampleDelta);
    int thetaSteps = int(0.5 * PI / sampleDelta);

    for (int iPhi = 0; iPhi < phiSteps; iPhi++)
    {
        float phi = float(iPhi) * sampleDelta;
        for (int iTheta = 0; iTheta < thetaSteps; iTheta++)
        {
            float theta = float(iTheta) * sampleDelta;

            // Spherical to tangent-space direction
            vec3 tangentSample = vec3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta));

            // Tangent space to world space
            vec3 sampleVec = tangentSample.x * right
                           + tangentSample.y * up
                           + tangentSample.z * N;

            irradiance += texture(u_environmentMap, sampleVec).rgb
                        * cos(theta) * sin(theta);
            nrSamples++;
        }
    }

    irradiance = PI * irradiance / float(nrSamples);
    fragColor = vec4(irradiance, 1.0);
}
