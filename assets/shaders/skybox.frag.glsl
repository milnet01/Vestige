// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file skybox.frag.glsl
/// @brief Skybox fragment shader — samples cubemap or renders a procedural gradient sky.
#version 450 core

in vec3 v_texCoord;

out vec4 fragColor;

uniform bool u_hasCubemap;
uniform samplerCube u_skyboxTexture;

void main()
{
    if (u_hasCubemap)
    {
        fragColor = texture(u_skyboxTexture, v_texCoord);
    }
    else
    {
        // Procedural gradient sky
        vec3 dir = normalize(v_texCoord);
        float y = dir.y;

        // Blue sky at top, light blue at horizon, warm tone below
        vec3 topColor = vec3(0.25, 0.45, 0.8);     // Deep blue
        vec3 horizonColor = vec3(0.7, 0.8, 0.95);   // Light blue/white
        vec3 groundColor = vec3(0.5, 0.45, 0.35);   // Warm brown

        vec3 color;
        if (y > 0.0)
        {
            // Above horizon: blend from horizon to sky blue
            float t = pow(y, 0.5);
            color = mix(horizonColor, topColor, t);
        }
        else
        {
            // Below horizon: blend from horizon to warm ground
            float t = pow(-y, 0.7);
            color = mix(horizonColor, groundColor, t);
        }

        fragColor = vec4(color, 1.0);
    }
}
