/// @file bloom_bright.frag.glsl
/// @brief Bloom brightness extraction — isolates pixels above a luminance threshold with soft knee.
#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_hdrTexture;
uniform float u_threshold;

out vec4 fragColor;

void main()
{
    vec3 color = texture(u_hdrTexture, v_texCoord).rgb;

    // BT.709 luminance
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft knee: smooth transition instead of hard cutoff (reduces firefly artifacts)
    float contribution = max(0.0, luminance - u_threshold);
    contribution = contribution / (contribution + 1.0);
    fragColor = vec4(color * contribution / (luminance + 0.0001), 1.0);
}
