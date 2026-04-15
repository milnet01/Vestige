// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_component.h
/// @brief Entity component that owns a ClothSimulator and DynamicMesh for rendering.
#pragma once

#include "physics/cloth_presets.h"
#include "physics/cloth_simulator.h"
#include "renderer/dynamic_mesh.h"
#include "renderer/material.h"
#include "scene/component.h"

#include <memory>
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

    /// @brief Access the underlying simulator for pinning, wind, colliders, etc.
    ClothSimulator& getSimulator();
    const ClothSimulator& getSimulator() const;

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
    ClothSimulator m_simulator;
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
