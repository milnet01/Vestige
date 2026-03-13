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
    std::shared_ptr<Mesh> getMesh() const;

    /// @brief Gets the material.
    std::shared_ptr<Material> getMaterial() const;

    /// @brief Gets the local-space bounding box for collision.
    const AABB& getBounds() const;

    /// @brief Sets the local-space bounding box.
    void setBounds(const AABB& bounds);

private:
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Material> m_material;
    AABB m_bounds;
};

} // namespace Vestige
