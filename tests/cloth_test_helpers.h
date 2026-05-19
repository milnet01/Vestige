// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_test_helpers.h
/// @brief Shared cloth-test fixtures.
///
/// Phase 10.9 Slice 18 Ts3 extraction: the same `smallConfig` /
/// `smallClothConfig` helper was duplicated byte-identical across
/// three cloth test files (test_cloth_simulator.cpp,
/// test_cloth_solver_improvements.cpp, test_cloth_collision.cpp).
/// Each file now forwards to this single canonical definition.
#pragma once

#include "physics/cloth_simulator.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige::Testing
{

/// @brief Bare config for the live-parameter / reset tests in
///        test_cloth_presets.cpp — just w, h, and a `spacing` of 0.5f.
///        Callers set whatever single field they care about on top
///        (particleMass, stretchCompliance, damping, …).
inline ClothConfig clothLiveParamConfig(uint32_t w, uint32_t h)
{
    ClothConfig cfg;
    cfg.width = w;
    cfg.height = h;
    cfg.spacing = 0.5f;
    return cfg;
}

/// @brief Small (default 4×4) cloth config with sensible compliance
///        defaults — used as the baseline fixture across cloth unit
///        tests.
inline ClothConfig clothSmallConfig(uint32_t w = 4, uint32_t h = 4)
{
    ClothConfig cfg;
    cfg.width = w;
    cfg.height = h;
    cfg.spacing = 1.0f;
    cfg.particleMass = 1.0f;
    cfg.substeps = 5;
    cfg.stretchCompliance = 0.0f;
    cfg.shearCompliance = 0.0001f;
    cfg.bendCompliance = 0.01f;
    cfg.damping = 0.01f;
    return cfg;
}

/// A single triangle in the XZ plane at Y=0: (0,0,0), (1,0,0), (0,0,1).
inline void makeTriangle(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices)
{
    verts = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    indices = {0, 1, 2};
}

/// A flat quad (two triangles) in the XZ plane at Y=0: (0,0,0) to (1,0,1).
inline void makeQuad(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices)
{
    verts = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    indices = {0, 1, 2, 0, 2, 3};
}

/// Axis-aligned cube mesh centred at origin, half-extent 0.5.
inline void makeCube(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices)
{
    verts = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    };
    indices = {
        // Front
        0, 1, 2,  0, 2, 3,
        // Back
        5, 4, 7,  5, 7, 6,
        // Left
        4, 0, 3,  4, 3, 7,
        // Right
        1, 5, 6,  1, 6, 2,
        // Top
        3, 2, 6,  3, 6, 7,
        // Bottom
        4, 5, 1,  4, 1, 0,
    };
}

}  // namespace Vestige::Testing
