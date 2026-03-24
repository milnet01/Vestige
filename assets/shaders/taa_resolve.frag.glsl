/// @file taa_resolve.frag.glsl
/// @brief Temporal anti-aliasing resolve — blends current frame with clamped history using YCoCg variance clipping.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_currentTexture;       // Current frame HDR
uniform sampler2D u_historyTexture;       // Previous frame TAA output
uniform sampler2D u_motionVectorTexture;  // Per-pixel motion in UV space
uniform float u_feedbackFactor;           // History blend weight (e.g. 0.9)
uniform vec2 u_texelSize;                 // 1.0 / resolution

out vec4 fragColor;

// --- Color space conversions (YCoCg gives tighter variance bounds than RGB) ---

vec3 rgbToYCoCg(vec3 rgb)
{
    return vec3(
         0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
         0.5  * rgb.r                - 0.5  * rgb.b,
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b
    );
}

vec3 yCoCgToRgb(vec3 ycocg)
{
    float y  = ycocg.x;
    float co = ycocg.y;
    float cg = ycocg.z;
    return vec3(y + co - cg, y + cg, y - co - cg);
}

// --- Catmull-Rom bicubic sampling (5 bilinear taps, sharper than bilinear) ---
// Based on Matt Pettineo's optimized 5-tap Catmull-Rom using hardware bilinear.

vec3 sampleCatmullRom(sampler2D tex, vec2 uv, vec2 texSize)
{
    vec2 samplePos = uv * texSize;
    vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
    vec2 f = samplePos - texPos1;

    // Catmull-Rom weights
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    // Combine pairs of taps using bilinear filtering
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;

    vec2 texPos0 = (texPos1 - 1.0) / texSize;
    vec2 texPos3 = (texPos1 + 2.0) / texSize;
    vec2 texPos12 = (texPos1 + offset12) / texSize;

    // 5-tap pattern (center + 4 cardinal)
    vec3 result = vec3(0.0);
    result += texture(tex, vec2(texPos12.x, texPos0.y)).rgb  * w12.x * w0.y;
    result += texture(tex, vec2(texPos0.x,  texPos12.y)).rgb * w0.x  * w12.y;
    result += texture(tex, vec2(texPos12.x, texPos12.y)).rgb * w12.x * w12.y;
    result += texture(tex, vec2(texPos3.x,  texPos12.y)).rgb * w3.x  * w12.y;
    result += texture(tex, vec2(texPos12.x, texPos3.y)).rgb  * w12.x * w3.y;

    // Clamp to prevent negative values from the cubic filter
    return max(result, vec3(0.0));
}

void main()
{
    // Sample current frame
    vec3 currentColor = texture(u_currentTexture, v_texCoord).rgb;

    // --- 3x3 neighborhood sampling in YCoCg for variance clipping ---
    vec3 samples[9];
    int idx = 0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * u_texelSize;
            samples[idx] = rgbToYCoCg(texture(u_currentTexture, v_texCoord + offset).rgb);
            idx++;
        }
    }

    // Compute mean and variance of the 3x3 neighborhood
    vec3 mean = vec3(0.0);
    vec3 sqMean = vec3(0.0);
    for (int i = 0; i < 9; i++)
    {
        mean += samples[i];
        sqMean += samples[i] * samples[i];
    }
    mean /= 9.0;
    sqMean /= 9.0;
    vec3 stddev = sqrt(max(sqMean - mean * mean, vec3(0.0)));

    // Variance clipping bounds (gamma=1.0 is tight, 1.25 is loose)
    float gamma = 1.0;
    vec3 clipMin = mean - gamma * stddev;
    vec3 clipMax = mean + gamma * stddev;

    // --- Reproject and sample history ---
    vec2 motion = texture(u_motionVectorTexture, v_texCoord).rg;
    vec2 historyUV = v_texCoord - motion;

    // Catmull-Rom bicubic sampling of history (sharper than bilinear)
    vec2 texSize = 1.0 / u_texelSize;
    vec3 historyColor = sampleCatmullRom(u_historyTexture, historyUV, texSize);

    // Clip history to neighborhood bounds in YCoCg
    vec3 historyYCoCg = rgbToYCoCg(historyColor);
    vec3 clippedHistory = clamp(historyYCoCg, clipMin, clipMax);
    historyColor = yCoCgToRgb(clippedHistory);

    // --- Feedback weight with confidence adjustments ---
    float feedback = u_feedbackFactor;

    // Reduce feedback when reprojected UV is outside the screen
    if (historyUV.x < 0.0 || historyUV.x > 1.0 || historyUV.y < 0.0 || historyUV.y > 1.0)
    {
        feedback = 0.0;
    }

    // Reduce feedback on fast motion (less reliable history)
    float motionLength = length(motion * texSize);  // in pixels
    feedback *= 1.0 / (1.0 + motionLength * 0.5);

    // Reduce feedback when clipping moved the history color significantly
    float clipDist = length(historyYCoCg - clippedHistory);
    feedback *= 1.0 / (1.0 + clipDist * 4.0);

    // Blend
    vec3 result = mix(currentColor, historyColor, feedback);
    fragColor = vec4(result, 1.0);
}
