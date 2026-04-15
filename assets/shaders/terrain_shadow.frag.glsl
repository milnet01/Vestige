// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_shadow.frag.glsl
/// @brief Terrain shadow depth fragment shader — writes depth only.
#version 450 core

void main()
{
    // Depth is written automatically by the fixed-function pipeline.
    // No color output needed for shadow map.
}
