#version 450 core

in vec2 v_texCoord;
in vec3 v_colorTint;
in float v_alpha;

uniform sampler2D u_texture;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(u_texture, v_texCoord);

    // Alpha test — discard transparent pixels
    if (texel.a < 0.5)
        discard;

    // Apply tint and distance fade
    fragColor = vec4(texel.rgb * v_colorTint, texel.a * v_alpha);

    // Discard fully faded fragments
    if (fragColor.a < 0.01)
        discard;
}
