// Fixture for ShaderLintCatchesViolation — deliberately off-standard.
// Two seeded violations: a 4.60 version directive and a suffixless built-in.
#version 460 core
#extension GL_ARB_shader_draw_parameters : enable
layout(std430, binding = 0) buffer M { mat4 m[]; };
void main() { gl_Position = m[gl_BaseInstance] * vec4(0.0); }
