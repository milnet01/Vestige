#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_ssaoInput;

out vec4 fragColor;

void main()
{
    // 4x4 box blur to remove noise from the SSAO result
    vec2 texelSize = 1.0 / textureSize(u_ssaoInput, 0);
    float result = 0.0;

    for (int x = -2; x < 2; x++)
    {
        for (int y = -2; y < 2; y++)
        {
            // Offset by +0.5 to center the 4x4 kernel at the texel
            vec2 offset = (vec2(float(x), float(y)) + 0.5) * texelSize;
            result += texture(u_ssaoInput, v_texCoord + offset).r;
        }
    }

    result /= 16.0;
    fragColor = vec4(result, result, result, 1.0);
}
