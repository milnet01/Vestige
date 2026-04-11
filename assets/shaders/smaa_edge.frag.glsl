/// @file smaa_edge.frag.glsl
/// @brief SMAA luma-based edge detection with local contrast adaptation.
#version 450 core

uniform sampler2D u_colorTexture;
uniform vec4 u_rtMetrics; // (1/width, 1/height, width, height)

in vec2 v_texCoord;
in vec4 v_offset[3];

out vec4 fragColor;

// SMAA HIGH preset threshold
const float SMAA_THRESHOLD = 0.1;
const float SMAA_LOCAL_CONTRAST_ADAPTATION = 2.0;

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    // Sample luma values at center and 4 neighbors
    float L      = luminance(texture(u_colorTexture, v_texCoord).rgb);
    float Lleft  = luminance(texture(u_colorTexture, v_offset[0].xy).rgb);
    float Ltop   = luminance(texture(u_colorTexture, v_offset[0].zw).rgb);

    // Compute deltas
    float deltaLeft = abs(L - Lleft);
    float deltaTop  = abs(L - Ltop);

    // Threshold to detect edges
    vec2 edges = step(vec2(SMAA_THRESHOLD), vec2(deltaLeft, deltaTop));

    // Early discard if no edges found
    if (dot(edges, vec2(1.0)) == 0.0)
        discard;

    // Local contrast adaptation: suppress weak edges near strong edges
    // to reduce false positives from noise/texture detail
    float Lright      = luminance(texture(u_colorTexture, v_offset[1].xy).rgb);
    float Lbottom     = luminance(texture(u_colorTexture, v_offset[1].zw).rgb);
    float LleftLeft   = luminance(texture(u_colorTexture, v_offset[2].xy).rgb);
    float LtopTop     = luminance(texture(u_colorTexture, v_offset[2].zw).rgb);

    // Maximum delta in the neighborhood for each direction
    float maxDeltaLeft = max(max(abs(Lleft - LleftLeft), abs(Lleft - L)),
                             max(abs(L - Lright), deltaLeft));
    float maxDeltaTop  = max(max(abs(Ltop - LtopTop), abs(Ltop - L)),
                             max(abs(L - Lbottom), deltaTop));

    // Local contrast adaptation: only keep edges that are strong relative to neighbors
    // Guard against division by zero in uniform regions
    edges *= step(vec2(1.0), SMAA_LOCAL_CONTRAST_ADAPTATION * vec2(deltaLeft, deltaTop)
                              / max(vec2(maxDeltaLeft, maxDeltaTop), vec2(0.0001)));

    fragColor = vec4(edges, 0.0, 1.0);
}
