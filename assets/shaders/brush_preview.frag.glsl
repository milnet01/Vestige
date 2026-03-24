/// @file brush_preview.frag.glsl
/// @brief Terrain brush preview fragment shader — renders a dashed semi-transparent overlay for accessibility.
#version 450 core

uniform vec3 u_color;

out vec4 fragColor;

void main()
{
    // Dashed pattern (for colorblind accessibility)
    float dashPattern = step(0.5, fract(gl_FragCoord.x * 0.05 + gl_FragCoord.y * 0.05));
    float alpha = mix(0.8, 0.3, dashPattern);
    fragColor = vec4(u_color, alpha);
}
