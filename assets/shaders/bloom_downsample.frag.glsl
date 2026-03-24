/// @file bloom_downsample.frag.glsl
/// @brief Bloom mip-chain downsampling with Karis average to suppress firefly artifacts.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_sourceTexture;
uniform vec2 u_srcTexelSize;    // 1.0 / source resolution
uniform bool u_useKarisAverage; // true only on first downsample (prevents fireflies)
uniform float u_threshold;      // Brightness threshold (only on first downsample)

out vec4 fragColor;

/// Karis average weight — suppresses firefly artifacts on the first downsample
/// by weighting each quadrant inversely proportional to its luminance.
float karisWeight(vec3 color)
{
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

void main()
{
    // 13-tap downsample filter from Call of Duty: Advanced Warfare (Jorge Jimenez).
    // Uses bilinear hardware filtering to cover a wide area with only 13 taps.
    // Pattern: 4 corner quads + 4 edge quads + 1 center quad, weighted for a
    // smooth, alias-free downsample that preserves energy.

    float x = u_srcTexelSize.x;
    float y = u_srcTexelSize.y;

    // 4 corner samples (2 texels away, bilinear gives 2x2 average each)
    vec3 a = texture(u_sourceTexture, v_texCoord + vec2(-2.0*x,  2.0*y)).rgb;
    vec3 b = texture(u_sourceTexture, v_texCoord + vec2( 0.0,    2.0*y)).rgb;
    vec3 c = texture(u_sourceTexture, v_texCoord + vec2( 2.0*x,  2.0*y)).rgb;

    vec3 d = texture(u_sourceTexture, v_texCoord + vec2(-2.0*x,  0.0)).rgb;
    vec3 e = texture(u_sourceTexture, v_texCoord + vec2( 0.0,    0.0)).rgb;
    vec3 f = texture(u_sourceTexture, v_texCoord + vec2( 2.0*x,  0.0)).rgb;

    vec3 g = texture(u_sourceTexture, v_texCoord + vec2(-2.0*x, -2.0*y)).rgb;
    vec3 h = texture(u_sourceTexture, v_texCoord + vec2( 0.0,   -2.0*y)).rgb;
    vec3 i = texture(u_sourceTexture, v_texCoord + vec2( 2.0*x, -2.0*y)).rgb;

    // 4 inner samples (1 texel away)
    vec3 j = texture(u_sourceTexture, v_texCoord + vec2(-x,  y)).rgb;
    vec3 k = texture(u_sourceTexture, v_texCoord + vec2( x,  y)).rgb;
    vec3 l = texture(u_sourceTexture, v_texCoord + vec2(-x, -y)).rgb;
    vec3 m = texture(u_sourceTexture, v_texCoord + vec2( x, -y)).rgb;

    // On the first downsample, apply luminance-based soft threshold to extract
    // only bright areas. Uses BT.709 luminance to preserve color ratios
    // (per-component threshold would shift colors, e.g. orange → red).
    if (u_useKarisAverage && u_threshold > 0.0)
    {
        #define SOFT_THRESHOLD(s) { \
            float luma = dot(s, vec3(0.2126, 0.7152, 0.0722)); \
            float contrib = max(0.0, luma - u_threshold); \
            contrib = contrib / (contrib + 1.0); \
            s *= contrib / (luma + 0.0001); \
        }
        SOFT_THRESHOLD(a) SOFT_THRESHOLD(b) SOFT_THRESHOLD(c)
        SOFT_THRESHOLD(d) SOFT_THRESHOLD(e) SOFT_THRESHOLD(f)
        SOFT_THRESHOLD(g) SOFT_THRESHOLD(h) SOFT_THRESHOLD(i)
        SOFT_THRESHOLD(j) SOFT_THRESHOLD(k) SOFT_THRESHOLD(l) SOFT_THRESHOLD(m)
        #undef SOFT_THRESHOLD
    }

    vec3 result;

    if (u_useKarisAverage)
    {
        // First downsample: apply Karis average to suppress fireflies.
        // Weight each of the 5 sample groups by inverse luminance.
        vec3 g0 = (a + b + d + e) * 0.25;
        vec3 g1 = (b + c + e + f) * 0.25;
        vec3 g2 = (d + e + g + h) * 0.25;
        vec3 g3 = (e + f + h + i) * 0.25;
        vec3 g4 = (j + k + l + m) * 0.25;

        float w0 = karisWeight(g0);
        float w1 = karisWeight(g1);
        float w2 = karisWeight(g2);
        float w3 = karisWeight(g3);
        float w4 = karisWeight(g4);

        float wSum = w0 + w1 + w2 + w3 + w4;
        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / wSum;
    }
    else
    {
        // Standard weighted downsample (energy preserving)
        //   0.5  for inner quad (j,k,l,m)
        //   0.125 for each of the 4 corner quads
        result  = e * 0.125;
        result += (a + c + g + i) * 0.03125;       // 0.125 / 4
        result += (b + d + f + h) * 0.0625;        // 0.25 / 4
        result += (j + k + l + m) * 0.125;         // 0.5 / 4
    }

    fragColor = vec4(result, 1.0);
}
