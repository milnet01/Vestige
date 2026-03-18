#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_glyphAtlas;
uniform vec3 u_textColor;

out vec4 fragColor;

void main()
{
    float alpha = texture(u_glyphAtlas, v_texCoord).r;
    if (alpha < 0.01)
    {
        discard;
    }
    fragColor = vec4(u_textColor, alpha);
}
