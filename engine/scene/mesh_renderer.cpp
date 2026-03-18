/// @file mesh_renderer.cpp
/// @brief MeshRenderer component implementation.
#include "scene/mesh_renderer.h"

namespace Vestige
{

MeshRenderer::MeshRenderer(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material)
    : m_mesh(std::move(mesh))
    , m_material(std::move(material))
{
}

void MeshRenderer::setMesh(std::shared_ptr<Mesh> mesh)
{
    m_mesh = std::move(mesh);
}

void MeshRenderer::setMaterial(std::shared_ptr<Material> material)
{
    m_material = std::move(material);
}

const std::shared_ptr<Mesh>& MeshRenderer::getMesh() const
{
    return m_mesh;
}

const std::shared_ptr<Material>& MeshRenderer::getMaterial() const
{
    return m_material;
}

const AABB& MeshRenderer::getBounds() const
{
    return m_bounds;
}

void MeshRenderer::setBounds(const AABB& bounds)
{
    m_bounds = bounds;
}

const AABB& MeshRenderer::getCullingBounds() const
{
    return m_hasCullingBounds ? m_cullingBounds : m_bounds;
}

void MeshRenderer::setCullingBounds(const AABB& bounds)
{
    m_cullingBounds = bounds;
    m_hasCullingBounds = true;
}

} // namespace Vestige
