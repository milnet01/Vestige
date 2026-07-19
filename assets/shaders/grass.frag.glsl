#version 450 core

// Meadow GPU grass — G1 flat-lit fragment stage. Real shading (Lambert + backlit
// translucency + AO + CSM receive) lands in G4; for bring-up this just emits the
// vertex-shader's root→tip green gradient so the blade geometry is visible.
// Design: docs/phases/phase_10_meadow_gpu_grass_design.md §5.1/§5.4.

in vec3 v_color;

out vec4 FragColor;

void main()
{
    FragColor = vec4(v_color, 1.0);
}
