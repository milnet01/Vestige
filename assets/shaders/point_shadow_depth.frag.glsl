#version 450 core

in vec3 v_fragPosition;

uniform vec3 u_lightPos;
uniform float u_farPlane;

void main()
{
    // Write linear distance from light as depth (normalized to [0,1]).
    // Note: gl_FragDepth disables early-Z/Hi-Z on RDNA2, but the alternative
    // (hardware depth + linearization in sampling shader) adds per-fragment cost
    // to the main scene pass and complicates cubemap edge comparison. For the
    // current max of 2 point shadow lights, this approach is acceptable.
    float lightDistance = length(v_fragPosition - u_lightPos);
    gl_FragDepth = lightDistance / u_farPlane;
}
