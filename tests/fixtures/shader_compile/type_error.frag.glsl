// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

// Fixture for ShaderCompileCatchesError (3D_E-0638). It passes the #version
// line-scan, so the ONLY thing that can fail it is the glslangValidator
// compile: a float assigned to a vec3 is a type error no regex sees.
#version 450 core

out vec4 fragColor;

void main()
{
    vec3 colour = 1.0;
    fragColor = vec4(colour, 1.0);
}
