/// @file cloth_component.h
/// @brief Entity component that owns a ClothSimulator and DynamicMesh for rendering.
#pragma once

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

    /// @brief Returns true if initialize() has been called successfully.
    bool isReady() const;

private:
    ClothSimulator m_simulator;
    DynamicMesh m_mesh;
    std::shared_ptr<Material> m_material;
    std::vector<Vertex> m_vertexBuffer;  ///< CPU-side vertex data for mesh updates
    bool m_ready = false;
};

} // namespace Vestige
