// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_backend_factory.h
/// @brief Auto-selects CPU vs GPU cloth solver backend based on cloth size + GL support.
///
/// Phase 9B Step 10. Lives separately from `ClothComponent` so the selection
/// heuristic is testable without an entity context, and so a future
/// `ClothComponent::initialize()` call site can adopt the factory in a one-line
/// change.
///
/// **Heuristic.** Below `GPU_AUTO_SELECT_THRESHOLD` particles the CPU XPBD
/// solver wins on dispatch overhead alone. Above that threshold the GPU
/// solver's parallelism dominates. The threshold (1024 particles ≈ 32×32 grid)
/// is the design-doc starting point and may shift as Workbench-fit `cost(N)`
/// curves measure the actual crossover on dev hardware.
#pragma once

#include "physics/cloth_solver_backend.h"

#include <memory>
#include <string>

namespace Vestige
{

struct ClothConfig;

/// @brief Particle-count threshold above which the GPU backend becomes eligible.
///
/// 1024 particles ≈ 32×32 grid. Below this the CPU solver's lower per-frame
/// dispatch overhead beats the GPU's per-substep dispatch chain. Above it the
/// GPU's parallel constraint solver pulls ahead.
constexpr uint32_t GPU_AUTO_SELECT_THRESHOLD = 1024;

/// @brief Backend-selection policy.
enum class ClothBackendPolicy
{
    AUTO,        ///< Auto-select based on particle count + GL support.
    FORCE_CPU,   ///< Always use CPU XPBD solver.
    FORCE_GPU,   ///< Always use GPU compute solver (caller must verify isSupported()).
};

/// @brief Decision returned by the auto-select probe (for tests / telemetry).
enum class ClothBackendChoice
{
    CPU,
    GPU,
};

/// @brief Returns the backend that auto-select would choose for @a config.
///
/// Pure CPU function — does not allocate GL state. Useful for telemetry,
/// editor display, and unit testing the heuristic without a GL context.
ClothBackendChoice chooseClothBackend(const ClothConfig& config,
                                       ClothBackendPolicy policy,
                                       bool gpuSupported);

/// @brief Constructs an `IClothSolverBackend` of the chosen type.
///
/// For GPU backends, calls `setShaderPath()` on the constructed instance with
/// @a shaderPath so `initialize()` can load the cloth_*.comp.glsl shaders
/// from the engine's asset directory.
///
/// @param config       Cloth configuration (used to size the auto-select decision).
/// @param policy       Selection policy.
/// @param shaderPath   Asset path for the cloth_*.comp.glsl shaders. May be
///                     empty if the policy is `FORCE_CPU`.
/// @return The constructed backend; never nullptr (falls back to CPU on any
///         GPU instantiation failure).
std::unique_ptr<IClothSolverBackend> createClothSolverBackend(
    const ClothConfig& config,
    ClothBackendPolicy policy,
    const std::string& shaderPath);

} // namespace Vestige
