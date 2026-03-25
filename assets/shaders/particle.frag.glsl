/// @file particle.frag.glsl
/// @brief Particle fragment shader with texture support and soft particles.
#version 450 core

in vec2 v_texCoord;
in vec4 v_color;
in float v_normalizedAge;

out vec4 fragColor;

uniform sampler2D u_texture;
uniform bool u_hasTexture;

// Soft particles (depth-based fade at geometry intersections)
uniform bool u_softParticles;
uniform sampler2D u_depthTexture;
uniform vec2 u_screenSize;
uniform float u_cameraNear;
uniform float u_softDistance;

void main()
{
    vec4 color = v_color;

    if (u_hasTexture)
    {
        color *= texture(u_texture, v_texCoord);
    }
    else
    {
        // Default circular soft particle (no texture)
        float dist = length(v_texCoord - vec2(0.5));
        float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
        color.a *= alpha;
    }

    // Soft particles: fade when close to scene geometry
    if (u_softParticles)
    {
        vec2 screenUV = gl_FragCoord.xy / u_screenSize;
        float sceneDepth = texture(u_depthTexture, screenUV).r;
        float particleDepth = gl_FragCoord.z;

        // Reverse-Z: linearize depth as cameraNear / depth
        // Particle is in front when particleDepth > sceneDepth (reverse-Z)
        float linearScene = u_cameraNear / max(sceneDepth, 0.00001);
        float linearParticle = u_cameraNear / max(particleDepth, 0.00001);
        float depthDiff = linearScene - linearParticle;

        float softFactor = smoothstep(0.0, u_softDistance, depthDiff);
        color.a *= softFactor;
    }

    // Discard fully transparent fragments
    if (color.a < 0.001)
    {
        discard;
    }

    fragColor = color;
}
