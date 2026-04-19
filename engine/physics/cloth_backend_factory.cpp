// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_backend_factory.cpp
/// @brief Implementation of the CPU↔GPU cloth-backend auto-select factory.

#include "physics/cloth_backend_factory.h"

#include "core/logger.h"
#include "physics/cloth_simulator.h"
#include "physics/gpu_cloth_simulator.h"

namespace Vestige
{

ClothBackendChoice chooseClothBackend(const ClothConfig& config,
                                       ClothBackendPolicy policy,
                                       bool gpuSupported)
{
    switch (policy)
    {
        case ClothBackendPolicy::FORCE_CPU:
            return ClothBackendChoice::CPU;

        case ClothBackendPolicy::FORCE_GPU:
            // Caller has asserted GPU is OK — trust them. Diagnostics can
            // separately log a warning if gpuSupported is false.
            return ClothBackendChoice::GPU;

        case ClothBackendPolicy::AUTO:
        {
            const uint32_t particles = config.width * config.height;
            if (!gpuSupported)               return ClothBackendChoice::CPU;
            if (particles < GPU_AUTO_SELECT_THRESHOLD)
                                              return ClothBackendChoice::CPU;
            return ClothBackendChoice::GPU;
        }
    }
    return ClothBackendChoice::CPU;  // Defensive default — unreachable.
}

std::unique_ptr<IClothSolverBackend> createClothSolverBackend(
    const ClothConfig& config,
    ClothBackendPolicy policy,
    const std::string& shaderPath)
{
    const bool gpuSupported = GpuClothSimulator::isSupported();
    const ClothBackendChoice choice = chooseClothBackend(config, policy, gpuSupported);

    if (choice == ClothBackendChoice::GPU)
    {
        if (shaderPath.empty())
        {
            Logger::warning(
                "[ClothBackendFactory] GPU backend chosen but shaderPath is "
                "empty — falling back to CPU to avoid silent no-op simulate()");
            return std::make_unique<ClothSimulator>();
        }
        auto gpu = std::make_unique<GpuClothSimulator>();
        gpu->setShaderPath(shaderPath);
        Logger::info("[ClothBackendFactory] Selected GPU backend ("
                     + std::to_string(config.width) + "x"
                     + std::to_string(config.height) + " particles)");
        return gpu;
    }

    if (policy == ClothBackendPolicy::FORCE_GPU && !gpuSupported)
    {
        Logger::warning("[ClothBackendFactory] FORCE_GPU but GL context lacks "
                        "compute support — falling back to CPU");
    }
    return std::make_unique<ClothSimulator>();
}

} // namespace Vestige
