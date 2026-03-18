#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_image;
uniform bool u_horizontal;

out vec4 fragColor;

void main()
{
    // 9-tap separable Gaussian blur weights
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    vec2 texelSize = 1.0 / textureSize(u_image, 0);
    vec3 result = texture(u_image, v_texCoord).rgb * weights[0];

    if (u_horizontal)
    {
        for (int i = 1; i < 5; i++)
        {
            result += texture(u_image, v_texCoord + vec2(texelSize.x * i, 0.0)).rgb * weights[i];
            result += texture(u_image, v_texCoord - vec2(texelSize.x * i, 0.0)).rgb * weights[i];
        }
    }
    else
    {
        for (int i = 1; i < 5; i++)
        {
            result += texture(u_image, v_texCoord + vec2(0.0, texelSize.y * i)).rgb * weights[i];
            result += texture(u_image, v_texCoord - vec2(0.0, texelSize.y * i)).rgb * weights[i];
        }
    }

    fragColor = vec4(result, 1.0);
}
