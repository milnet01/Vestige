// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

    // Auto-populate bounds from the mesh's local AABB if no explicit bounds were set
    if (m_mesh && m_bounds.getSize() == glm::vec3(0.0f))
    {
        m_bounds = m_mesh->getLocalBounds();
    }
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

void MeshRenderer::setCastsShadow(bool casts)
{
    m_castsShadow = casts;
}

bool MeshRenderer::castsShadow() const
{
    return m_castsShadow;
}

std::unique_ptr<Component> MeshRenderer::clone() const
{
    auto copy = std::make_unique<MeshRenderer>(m_mesh, m_material);
    copy->m_bounds = m_bounds;
    copy->m_cullingBounds = m_cullingBounds;
    copy->m_hasCullingBounds = m_hasCullingBounds;
    copy->m_castsShadow = m_castsShadow;
    copy->setEnabled(isEnabled());
    return copy;
}

} // namespace Vestige
