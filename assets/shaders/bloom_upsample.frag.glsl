#version 450 core

in vec2 v_texCoord;

uniform sampler2D u_sourceTexture;  // Lower-resolution mip being upsampled
uniform vec2 u_srcTexelSize;        // 1.0 / source (lower-res) resolution
uniform float u_filterRadius;       // Controls bloom spread (typically 1.0)

out vec4 fragColor;

void main()
{
    // 9-tap tent filter (3x3) for smooth upsampling.
    // Produces a wide, natural-looking bloom when applied progressively
    // up the mip chain. Weights sum to 1.0 (energy preserving).

    float x = u_srcTexelSize.x * u_filterRadius;
    float y = u_srcTexelSize.y * u_filterRadius;

    // 3x3 tent filter weights:
    //   1  2  1
    //   2  4  2   / 16
    //   1  2  1
    vec3 a = texture(u_sourceTexture, v_texCoord + vec2(-x,  y)).rgb;
    vec3 b = texture(u_sourceTexture, v_texCoord + vec2( 0,  y)).rgb;
    vec3 c = texture(u_sourceTexture, v_texCoord + vec2( x,  y)).rgb;

    vec3 d = texture(u_sourceTexture, v_texCoord + vec2(-x,  0)).rgb;
    vec3 e = texture(u_sourceTexture, v_texCoord + vec2( 0,  0)).rgb;
    vec3 f = texture(u_sourceTexture, v_texCoord + vec2( x,  0)).rgb;

    vec3 g = texture(u_sourceTexture, v_texCoord + vec2(-x, -y)).rgb;
    vec3 h = texture(u_sourceTexture, v_texCoord + vec2( 0, -y)).rgb;
    vec3 i = texture(u_sourceTexture, v_texCoord + vec2( x, -y)).rgb;

    vec3 result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i);
    result *= (1.0 / 16.0);

    fragColor = vec4(result, 1.0);
}
