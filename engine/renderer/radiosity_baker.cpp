/// @file radiosity_baker.cpp
/// @brief Multi-bounce indirect lighting baker implementation.
#include "renderer/radiosity_baker.h"
#include "renderer/renderer.h"
#include "renderer/sh_probe_grid.h"
#include "renderer/camera.h"
#include "scene/scene.h"
#include "core/logger.h"

#include <chrono>
#include <cmath>

namespace Vestige
{

/// @brief Computes the average absolute SH energy across all probes (band 0 = DC term).
/// Used to measure convergence between bounces.
static float computeGridEnergy(const SHProbeGrid& grid)
{
    glm::ivec3 res = grid.getResolution();
    float totalEnergy = 0.0f;
    int probeCount = 0;

    for (int z = 0; z < res.z; z++)
    {
        for (int y = 0; y < res.y; y++)
        {
            for (int x = 0; x < res.x; x++)
            {
                glm::vec3 coeffs[9];
                grid.getProbeIrradiance(x, y, z, coeffs);

                // Band 0 (DC term) represents average irradiance — best convergence metric
                totalEnergy += std::abs(coeffs[0].r) + std::abs(coeffs[0].g) + std::abs(coeffs[0].b);
                probeCount++;
            }
        }
    }

    return (probeCount > 0) ? totalEnergy / static_cast<float>(probeCount) : 0.0f;
}

void RadiosityBaker::bake(Renderer& renderer, const SceneRenderData& renderData,
                           const Camera& camera, float aspectRatio,
                           const RadiosityConfig& config)
{
    SHProbeGrid* grid = renderer.getSHProbeGrid();
    if (!grid || !grid->isInitialized())
    {
        Logger::error("RadiosityBaker: SH probe grid not initialized");
        return;
    }

    auto totalStart = std::chrono::steady_clock::now();

    Logger::info("Radiosity bake starting: up to " + std::to_string(config.maxBounces)
                 + " bounces, convergence threshold " + std::to_string(config.convergenceThreshold));

    // The initial captureSHGrid (already done by engine) gives us direct + 1 bounce.
    // Each additional capture adds one more bounce of indirect light.
    float prevEnergy = computeGridEnergy(*grid);

    for (int bounce = 1; bounce <= config.maxBounces; bounce++)
    {
        auto bounceStart = std::chrono::steady_clock::now();

        // Re-capture the SH grid — surfaces now reflect the previous bounce's indirect light.
        // Use 16×16 face size for bounce captures (L2 SH only needs low-frequency data,
        // so 16×16 is sufficient and ~16× faster than the initial 64×64 capture).
        renderer.captureSHGrid(renderData, camera, aspectRatio, 16);

        auto bounceEnd = std::chrono::steady_clock::now();
        float bounceMs = std::chrono::duration<float, std::milli>(bounceEnd - bounceStart).count();

        // Check convergence
        float currentEnergy = computeGridEnergy(*grid);
        float relativeChange = (prevEnergy > 0.001f)
            ? std::abs(currentEnergy - prevEnergy) / prevEnergy
            : 0.0f;

        Logger::info("  Bounce " + std::to_string(bounce) + "/" + std::to_string(config.maxBounces)
                     + ": " + std::to_string(static_cast<int>(bounceMs)) + " ms"
                     + ", energy " + std::to_string(currentEnergy)
                     + ", change " + std::to_string(relativeChange * 100.0f) + "%");

        if (bounce >= 2 && relativeChange < config.convergenceThreshold)
        {
            Logger::info("  Converged at bounce " + std::to_string(bounce)
                         + " (change " + std::to_string(relativeChange * 100.0f) + "% < "
                         + std::to_string(config.convergenceThreshold * 100.0f) + "%)");
            break;
        }

        prevEnergy = currentEnergy;
    }

    auto totalEnd = std::chrono::steady_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(totalEnd - totalStart).count();

    Logger::info("Radiosity bake complete: " + std::to_string(static_cast<int>(totalMs)) + " ms total");
}

} // namespace Vestige
