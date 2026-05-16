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

#include <cstdint>

namespace Vestige::Testing
{

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

}  // namespace Vestige::Testing
