#version 450 core

/// Entity ID encoded as an RGB color (passed from CPU).
uniform vec3 u_entityColor;

out vec4 fragColor;

void main()
{
    fragColor = vec4(u_entityColor, 1.0);
}
