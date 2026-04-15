// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file smaa_blend.frag.glsl
/// @brief SMAA blend weight calculation — searches edge patterns and looks up area texture.
#version 450 core

uniform sampler2D u_edgeTexture;
uniform sampler2D u_areaTexture;
uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

in vec2 v_texCoord;
in vec2 v_pixCoord;
in vec4 v_offset[3];

out vec4 fragColor;

const int SMAA_MAX_SEARCH_STEPS = 16;
const float SMAA_AREATEX_MAX_DISTANCE = 16.0;
const vec2 SMAA_AREATEX_PIXEL_SIZE = vec2(1.0 / 160.0, 1.0 / 560.0);
const float SMAA_AREATEX_SUBTEX_SIZE = 1.0 / 7.0;

// ============================================================================
// Edge search functions
// ============================================================================

/// Search for the end of a horizontal edge to the left.
float searchXLeft(vec2 texcoord, float end)
{
    // Sample at half-pixel offsets for bilinear filtering disambiguation.
    // The edge texture encodes horizontal edges in the R channel.
    // We search leftward until we find a pixel with no left-edge or reach the limit.
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++)
    {
        e = textureLod(u_edgeTexture, texcoord, 0.0).rg;
        texcoord -= vec2(2.0, 0.0) * u_rtMetrics.xy;

        // Stop if no horizontal edge (G channel = top edge)
        if (e.g < 0.9 || e.r > 0.9 || texcoord.x < end)
            break;
    }
    // Refine: offset by the edge value for sub-pixel accuracy
    return texcoord.x + (3.25 - (255.0 / 127.0) * e.r) * u_rtMetrics.x;
}

/// Search for the end of a horizontal edge to the right.
float searchXRight(vec2 texcoord, float end)
{
    vec2 e = vec2(0.0, 1.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++)
    {
        e = textureLod(u_edgeTexture, texcoord, 0.0).rg;
        texcoord += vec2(2.0, 0.0) * u_rtMetrics.xy;

        if (e.g < 0.9 || e.r > 0.9 || texcoord.x > end)
            break;
    }
    return texcoord.x - (3.25 - (255.0 / 127.0) * e.r) * u_rtMetrics.x;
}

/// Search for the end of a vertical edge upward.
float searchYUp(vec2 texcoord, float end)
{
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++)
    {
        e = textureLod(u_edgeTexture, texcoord, 0.0).rg;
        texcoord -= vec2(0.0, 2.0) * u_rtMetrics.xy;

        if (e.r < 0.9 || e.g > 0.9 || texcoord.y < end)
            break;
    }
    return texcoord.y + (3.25 - (255.0 / 127.0) * e.g) * u_rtMetrics.y;
}

/// Search for the end of a vertical edge downward.
float searchYDown(vec2 texcoord, float end)
{
    vec2 e = vec2(1.0, 0.0);
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++)
    {
        e = textureLod(u_edgeTexture, texcoord, 0.0).rg;
        texcoord += vec2(0.0, 2.0) * u_rtMetrics.xy;

        if (e.r < 0.9 || e.g > 0.9 || texcoord.y > end)
            break;
    }
    return texcoord.y - (3.25 - (255.0 / 127.0) * e.g) * u_rtMetrics.y;
}

// ============================================================================
// Area texture lookup
// ============================================================================

/// Look up the precomputed area for a given edge pattern and distances.
vec2 area(vec2 dist, float e1, float e2)
{
    // Map to area texture coordinates
    vec2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * vec2(e1, e2)) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;

    // For SMAA 1x: subsample index = 0, no offset needed
    return textureLod(u_areaTexture, texcoord, 0.0).rg;
}

// ============================================================================
// Main blend weight calculation
// ============================================================================

void main()
{
    vec4 weights = vec4(0.0);
    vec2 e = texture(u_edgeTexture, v_texCoord).rg;

    // --- Horizontal edge processing (top edge detected) ---
    if (e.g > 0.0)
    {
        // Search for left and right ends of this horizontal edge
        vec2 d;

        // Left search
        vec2 coords;
        coords.x = searchXLeft(v_offset[0].xy, v_offset[2].x);
        coords.y = v_offset[1].y;  // v_offset[1].y = texcoord.y - 0.25 * rtMetrics.y
        d.x = coords.x;

        // Fetch the crossing edges at the left end
        float e1 = textureLod(u_edgeTexture, coords, 0.0).r;

        // Right search
        coords.x = searchXRight(v_offset[0].zw, v_offset[2].y);
        d.y = coords.x;

        // Convert to pixel distances
        d = abs(round(u_rtMetrics.zz * d - v_pixCoord.xx));

        // Fetch the crossing edge at the right end
        float e2 = textureLodOffset(u_edgeTexture, coords, 0.0, ivec2(1, 0)).r;

        // Look up blend weights from area texture
        weights.rg = area(sqrt(d), e1, e2);
    }

    // --- Vertical edge processing (left edge detected) ---
    if (e.r > 0.0)
    {
        // Search for top and bottom ends of this vertical edge
        vec2 d;

        // Top search
        vec2 coords;
        coords.y = searchYUp(v_offset[1].xy, v_offset[2].z);
        coords.x = v_offset[0].x;
        d.x = coords.y;

        // Fetch the crossing edge at the top end
        float e1 = textureLod(u_edgeTexture, coords, 0.0).g;

        // Bottom search
        coords.y = searchYDown(v_offset[1].zw, v_offset[2].w);
        d.y = coords.y;

        // Convert to pixel distances
        d = abs(round(u_rtMetrics.ww * d - v_pixCoord.yy));

        // Fetch the crossing edge at the bottom end
        float e2 = textureLodOffset(u_edgeTexture, coords, 0.0, ivec2(0, 1)).g;

        // Look up blend weights from area texture
        weights.ba = area(sqrt(d), e1, e2);
    }

    fragColor = weights;
}
