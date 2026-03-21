#version 450 core

in vec3 v_color;
in float v_alpha;

out vec4 fragColor;

void main()
{
    fragColor = vec4(v_color, v_alpha);
    if (fragColor.a < 0.01)
        discard;
}
