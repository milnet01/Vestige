// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_component.h
/// @brief Entity component that owns a ClothSimulator and DynamicMesh for rendering.
#pragma once

#include "physics/cloth_backend_factory.h"
#include "physics/cloth_presets.h"
#include "physics/cloth_simulator.h"
#include "physics/cloth_solver_backend.h"
#include "renderer/dynamic_mesh.h"
#include "renderer/material.h"
#include "scene/component.h"

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Component that integrates cloth simulation with the rendering pipeline.
///
/// Owns a ClothSimulator (pure CPU physics) and a DynamicMesh (GPU-streamable).
/// Each frame, update() runs the simulation, copies particle positions and normals
/// into the vertex buffer, and uploads to the GPU.
class ClothComponent : public Component
{
public:
    /// @brief Initializes the cloth with the given config and material.
    /// @param seed Unique seed for wind randomness (0 = default).
    void initialize(const ClothConfig& config, std::shared_ptr<Material> material,
                    uint32_t seed = 0);

    /// @brief Per-frame update: simulate physics, update mesh vertices.
    void update(float deltaTime) override;

    /// @brief Access the underlying solver backend (CPU XPBD or GPU compute).
    ///
    /// Returns the `IClothSolverBackend` interface so callers see the same
    /// surface regardless of which backend was auto-selected at `initialize()`.
    /// The concrete backend is chosen by `ClothBackendFactory` — the GPU path
    /// engages above `GPU_AUTO_SELECT_THRESHOLD` particles when compute is
    /// available. The overloads keep the old call-site `getSimulator()` name
    /// so existing code (`inspector_panel`, `engine.cpp`) needs no rename.
    IClothSolverBackend& getSimulator();
    const IClothSolverBackend& getSimulator() const;

    /// @brief Selects the backend policy used at the **next** `initialize()`.
    ///
    /// Defaults to `AUTO`. Game code can pin to `FORCE_CPU` for
    /// deterministic-regression tests, or `FORCE_GPU` to exercise the GPU
    /// path on a small cloth (threshold check is bypassed).
    void setBackendPolicy(ClothBackendPolicy policy) { m_backendPolicy = policy; }

    /// @brief Returns the policy currently in effect for the next initialize().
    ClothBackendPolicy getBackendPolicy() const { return m_backendPolicy; }

    /// @brief Optional shader path passed to the GPU backend (needed by the
    /// factory when the GPU policy is chosen). Default: empty string; if the
    /// factory needs a shader path and none is set, it falls back to CPU with
    /// a one-line warning.
    void setShaderPath(const std::string& path) { m_shaderPath = path; }

    /// @brief Access the dynamic mesh for rendering.
    DynamicMesh& getMesh();
    const DynamicMesh& getMesh() const;

    /// @brief Access the material.
    const std::shared_ptr<Material>& getMaterial() const;

    /// @brief Forces the mesh to sync with current particle positions.
    /// Call after externally modifying particle positions (e.g., pinning setup).
    void syncMesh();

    /// @brief Resets the cloth to its initial hanging position.
    void reset();

    /// @brief Returns true if initialize() has been called successfully.
    bool isReady() const;

    /// @brief Gets the current preset type (CUSTOM if manually modified).
    ClothPresetType getPresetType() const { return m_presetType; }

    /// @brief Sets the preset type tracker (does not apply parameters).
    void setPresetType(ClothPresetType type) { m_presetType = type; }

    /// @brief Applies a preset's solver and wind parameters without reinitializing the grid.
    void applyPreset(ClothPresetType type);

private:
    std::unique_ptr<IClothSolverBackend> m_simulator;  ///< CPU XPBD or GPU compute.
    ClothBackendPolicy m_backendPolicy = ClothBackendPolicy::AUTO;
    std::string m_shaderPath;            ///< Passed to GPU backend via the factory.
    DynamicMesh m_mesh;
    std::shared_ptr<Material> m_material;
    std::vector<Vertex> m_vertexBuffer;  ///< CPU-side vertex data for mesh updates
    ClothPresetType m_presetType = ClothPresetType::CUSTOM;
    float m_timeAccumulator = 0.0f;      ///< Fixed timestep accumulator
    bool m_ready = false;

    static constexpr float FIXED_DT = 1.0f / 60.0f;  ///< Simulate at 60 Hz
    static constexpr int MAX_STEPS_PER_FRAME = 4;     ///< Safety cap
};

} // namespace Vestige
