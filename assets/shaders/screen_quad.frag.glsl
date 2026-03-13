#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_screenTexture;

out vec4 fragColor;

void main()
{
    fragColor = texture(u_screenTexture, v_texCoord);
}
