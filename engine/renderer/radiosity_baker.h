// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file radiosity_baker.h
/// @brief Multi-bounce indirect lighting baker using iterative SH probe grid captures.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

class Renderer;
class Camera;
struct SceneRenderData;

/// @brief Configuration for the radiosity bake.
struct RadiosityConfig
{
    int maxBounces = 4;               ///< Maximum number of light bounces to simulate.
    float convergenceThreshold = 0.02f; ///< Stop when relative energy change falls below this.
    float normalBias = 0.3f;           ///< Normal bias for SH grid sampling (anti-leak, in meters).
};

/// @brief Bakes multi-bounce diffuse indirect lighting into the SH probe grid.
///
/// Uses an iterative gathering approach: each iteration re-captures the SH probe
/// grid with the previous bounce's indirect lighting visible in the scene. After
/// N iterations, the grid contains N bounces of indirect light.
///
/// This replaces single-capture SH grids that only see direct lighting, providing
/// physically plausible light bounce in enclosed spaces (doorway light spreading
/// across walls, gold surfaces reflecting warm light onto nearby surfaces, etc.).
class RadiosityBaker
{
public:
    /// @brief Bakes multi-bounce indirect lighting into the renderer's SH probe grid.
    ///
    /// The SH grid must already be initialized and have had at least one capture
    /// (so direct lighting is established). This method performs additional capture
    /// passes to accumulate indirect bounces.
    ///
    /// @param renderer The renderer (owns the SH probe grid).
    /// @param renderData Scene geometry and lights.
    /// @param camera Main camera (for shadow reference).
    /// @param aspectRatio Camera aspect ratio.
    /// @param config Bake configuration.
    void bake(Renderer& renderer, const SceneRenderData& renderData,
              const Camera& camera, float aspectRatio,
              const RadiosityConfig& config = {});
};

} // namespace Vestige
