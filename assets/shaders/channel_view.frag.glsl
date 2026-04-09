/// @file channel_view.frag.glsl
/// @brief Channel isolation, mip selection, tiling, and exposure for texture viewers.
#version 450 core

in vec2 v_texCoord;

out vec4 fragColor;

uniform sampler2D u_texture;
uniform int u_channelMode;   // 0=RGB, 1=R, 2=G, 3=B, 4=A
uniform float u_mipLevel;    // -1.0 = auto, 0+ = explicit mip level
uniform int u_tileCount;     // 1 = no tiling, 2 = 2x2, 3 = 3x3
uniform float u_exposure;    // EV stops (0.0 = no adjustment)
uniform bool u_isHdr;        // true = apply tonemapping + gamma

void main()
{
    // Apply tiling
    vec2 uv = v_texCoord * float(u_tileCount);

    // Sample texture at selected mip level
    vec4 texel;
    if (u_mipLevel < 0.0)
    {
        texel = texture(u_texture, uv);
    }
    else
    {
        texel = textureLod(u_texture, uv, u_mipLevel);
    }

    // Apply exposure
    texel.rgb *= pow(2.0, u_exposure);

    // Channel isolation
    vec3 color;
    float alpha = 1.0;

    switch (u_channelMode)
    {
        case 0: // RGB
            color = texel.rgb;
            break;
        case 1: // R only
            color = vec3(texel.r);
            break;
        case 2: // G only
            color = vec3(texel.g);
            break;
        case 3: // B only
            color = vec3(texel.b);
            break;
        case 4: // A only
            color = vec3(texel.a);
            break;
        default:
            color = texel.rgb;
            break;
    }

    // HDR tonemapping (Reinhard) + gamma
    if (u_isHdr)
    {
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
    }

    fragColor = vec4(color, alpha);
}
