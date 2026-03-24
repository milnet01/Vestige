/// @file smaa_neighborhood.frag.glsl
/// @brief SMAA neighborhood blending — final pass, blends pixels using computed weights.
#version 450 core

uniform sampler2D u_colorTexture;
uniform sampler2D u_blendTexture;
uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

in vec2 v_texCoord;
in vec4 v_offset;

out vec4 fragColor;

void main()
{
    // Fetch blend weights for this pixel and its neighbors
    vec4 a;
    a.x = texture(u_blendTexture, v_offset.xy).a;  // Right neighbor's left weight
    a.y = texture(u_blendTexture, v_offset.zw).g;  // Bottom neighbor's top weight
    a.z = texture(u_blendTexture, v_texCoord).r;    // Our top weight (was stored in R by horizontal edge)
    a.w = texture(u_blendTexture, v_texCoord).a;    // Our left weight (was stored in A by vertical edge)

    // If no blend weights, output the original color
    if (dot(a, vec4(1.0)) < 1e-5)
    {
        fragColor = texture(u_colorTexture, v_texCoord);
        return;
    }

    // Determine whether to blend horizontally or vertically
    // (blend in the direction with stronger weights)
    bool isHorizontal = max(a.x, a.z) > max(a.y, a.w);

    // Compute blend offsets and weights
    vec4 blendingOffset = vec4(0.0);
    vec2 blendingWeight = vec2(0.0);

    if (isHorizontal)
    {
        blendingOffset = vec4(u_rtMetrics.x, 0.0, -u_rtMetrics.x, 0.0);
        blendingWeight = vec2(a.x, a.z);
    }
    else
    {
        blendingOffset = vec4(0.0, u_rtMetrics.y, 0.0, -u_rtMetrics.y);
        blendingWeight = vec2(a.y, a.w);
    }

    // Normalize weights
    float totalWeight = dot(blendingWeight, vec2(1.0));
    if (totalWeight < 1e-5)
    {
        fragColor = texture(u_colorTexture, v_texCoord);
        return;
    }
    blendingWeight /= totalWeight;

    // Blend with neighbors
    vec4 colorCenter = texture(u_colorTexture, v_texCoord);
    vec4 color1 = texture(u_colorTexture, v_texCoord + blendingOffset.xy);
    vec4 color2 = texture(u_colorTexture, v_texCoord + blendingOffset.zw);

    // Mix: the center gets (1 - total blend), neighbors get their share
    float keepCenter = 1.0 - min(totalWeight, 1.0);
    fragColor = colorCenter * keepCenter + color1 * blendingWeight.x + color2 * blendingWeight.y;
}
