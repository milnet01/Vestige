#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_screenTexture;
uniform float u_exposure;
uniform int u_tonemapMode;
uniform int u_debugMode;

// Bloom
uniform bool u_bloomEnabled;
uniform sampler2D u_bloomTexture;      // Unit 9
uniform float u_bloomIntensity;

// SSAO
uniform bool u_ssaoEnabled;
uniform sampler2D u_ssaoTexture;       // Unit 10

// Screen-space contact shadows
uniform bool u_contactShadowEnabled;
uniform sampler2D u_contactShadowTexture;  // Unit 11

// Color grading LUT
uniform bool u_lutEnabled;
uniform sampler3D u_lutTexture;        // Unit 13
uniform float u_lutIntensity;

out vec4 fragColor;

/// Reinhard tone mapping: simple, preserves color ratios.
vec3 tonemapReinhard(vec3 color)
{
    return color / (color + vec3(1.0));
}

/// ACES Filmic tone mapping: better contrast and saturation.
/// Fitted curve from Krzysztof Narkowicz.
vec3 tonemapACES(vec3 color)
{
    return (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
}

/// False-color luminance visualization for HDR debugging.
vec3 falseColorLuminance(float luminance)
{
    if (luminance < 0.1)
        return vec3(0.0, 0.0, 1.0);       // Blue: very dark
    else if (luminance < 0.25)
        return vec3(0.0, 1.0, 1.0);       // Cyan: dark
    else if (luminance < 0.5)
        return vec3(0.0, 1.0, 0.0);       // Green: mid-tone
    else if (luminance < 1.0)
        return vec3(1.0, 1.0, 0.0);       // Yellow: bright
    else if (luminance < 2.0)
        return vec3(1.0, 0.5, 0.0);       // Orange: HDR bright
    else if (luminance < 5.0)
        return vec3(1.0, 0.0, 0.0);       // Red: very bright HDR
    else
        return vec3(1.0, 0.0, 1.0);       // Magenta: extreme HDR
}

void main()
{
    // 1. Sample HDR color
    vec3 color = texture(u_screenTexture, v_texCoord).rgb;

    // 2. Apply SSAO (before exposure, multiplicative darkening)
    if (u_ssaoEnabled)
    {
        float ao = texture(u_ssaoTexture, v_texCoord).r;
        color *= ao;
    }

    // 2b. Apply screen-space contact shadows (directional light)
    if (u_contactShadowEnabled)
    {
        float contactShadow = texture(u_contactShadowTexture, v_texCoord).r;
        color *= contactShadow;
    }

    // 3. Add bloom (before exposure)
    if (u_bloomEnabled)
    {
        vec3 bloom = texture(u_bloomTexture, v_texCoord).rgb;
        color += bloom * u_bloomIntensity;
    }

    // 4. Apply exposure
    color *= u_exposure;

    // 5. Debug mode: false-color luminance overlay
    if (u_debugMode == 1)
    {
        float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(falseColorLuminance(luminance), 1.0);
        return;
    }

    // 6. Apply selected tonemapper
    if (u_tonemapMode == 0)
    {
        color = tonemapReinhard(color);
    }
    else if (u_tonemapMode == 1)
    {
        color = tonemapACES(color);
    }
    // Mode 2 = None (linear clamp) — no tonemapping applied

    // Clamp after tonemapping — ACES can produce values slightly > 1.0
    color = clamp(color, vec3(0.0), vec3(1.0));

    // 7. Color grading LUT
    if (u_lutEnabled)
    {
        float lutSize = float(textureSize(u_lutTexture, 0).x);
        vec3 lutCoord = clamp(color, 0.0, 1.0) * ((lutSize - 1.0) / lutSize) + vec3(0.5 / lutSize);
        vec3 graded = texture(u_lutTexture, lutCoord).rgb;
        color = mix(color, graded, u_lutIntensity);
    }

    // 8. Gamma correction: linear → sRGB
    color = pow(color, vec3(1.0 / 2.2));

    // 9. Output
    fragColor = vec4(color, 1.0);
}
