/// @file particle.frag.glsl
/// @brief Particle fragment shader — simple textured billboard with per-instance color.
#version 450 core

in vec2 v_texCoord;
in vec4 v_color;

out vec4 fragColor;

uniform sampler2D u_texture;
uniform bool u_hasTexture;

void main()
{
    vec4 color = v_color;

    if (u_hasTexture)
    {
        color *= texture(u_texture, v_texCoord);
    }
    else
    {
        // Default circular soft particle (no texture)
        float dist = length(v_texCoord - vec2(0.5));
        float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
        color.a *= alpha;
    }

    // Discard fully transparent fragments
    if (color.a < 0.001)
    {
        discard;
    }

    fragColor = color;
}
