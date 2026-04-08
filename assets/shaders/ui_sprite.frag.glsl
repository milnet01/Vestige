#version 450 core

in vec2 v_texCoord;
in vec4 v_color;

uniform sampler2D u_texture;
uniform int u_hasTexture;

out vec4 fragColor;

void main()
{
    if (u_hasTexture == 1)
    {
        fragColor = texture(u_texture, v_texCoord) * v_color;
    }
    else
    {
        fragColor = v_color;
    }
}
