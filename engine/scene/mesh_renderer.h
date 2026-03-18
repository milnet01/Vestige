/// @file mesh_renderer.h
/// @brief Component that makes an entity visible by attaching a mesh and material.
#pragma once

#include "scene/component.h"
#include "renderer/mesh.h"
#include "renderer/material.h"
#include "utils/aabb.h"

#include <memory>

namespace Vestige
{

/// @brief Renders a mesh with a material at the entity's transform.
class MeshRenderer : public Component
{
public:
    MeshRenderer() = default;
    MeshRenderer(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material);

    /// @brief Sets the mesh to render.
    void setMesh(std::shared_ptr<Mesh> mesh);

    /// @brief Sets the material to use.
    void setMaterial(std::shared_ptr<Material> material);

    /// @brief Gets the mesh.
    const std::shared_ptr<Mesh>& getMesh() const;

    /// @brief Gets the material.
    const std::shared_ptr<Material>& getMaterial() const;

    /// @brief Gets the local-space bounding box for collision.
    const AABB& getBounds() const;

    /// @brief Sets the local-space bounding box (used for both collision and frustum culling).
    void setBounds(const AABB& bounds);

    /// @brief Gets the frustum-culling bounds (falls back to collision bounds if not set).
    const AABB& getCullingBounds() const;

    /// @brief Sets separate frustum-culling bounds (independent of collision bounds).
    void setCullingBounds(const AABB& bounds);

    /// @brief Sets whether this mesh casts shadows into shadow maps.
    /// Disable for surfaces that only receive shadows (e.g. ground planes).
    void setCastsShadow(bool casts);

    /// @brief Checks if this mesh casts shadows.
    bool castsShadow() const;

private:
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Material> m_material;
    AABB m_bounds;
    AABB m_cullingBounds;
    bool m_hasCullingBounds = false;
    bool m_castsShadow = true;
};

} // namespace Vestige
