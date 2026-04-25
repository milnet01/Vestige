// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file breakable_component.cpp
/// @brief BreakableComponent implementation.

#include "experimental/physics/breakable_component.h"
#include "core/logger.h"

#include <functional>

namespace Vestige
{

void BreakableComponent::update(float deltaTime)
{
    // Fragment lifetime tracking is handled externally by the scene/game code
    (void)deltaTime;
}

std::unique_ptr<Component> BreakableComponent::clone() const
{
    auto copy = std::make_unique<BreakableComponent>();
    copy->breakImpulse = breakImpulse;
    copy->fragmentCount = fragmentCount;
    copy->fragmentLifetime = fragmentLifetime;
    copy->fragmentMassDensity = fragmentMassDensity;
    copy->interiorMaterial = interiorMaterial;
    return copy;
}

void BreakableComponent::fracture(const glm::vec3& impactPoint, const glm::vec3& impulse)
{
    if (m_fractured)
        return;

    (void)impulse;
    m_fractured = true;

    Logger::info("Entity fractured at (" +
        std::to_string(impactPoint.x) + ", " +
        std::to_string(impactPoint.y) + ", " +
        std::to_string(impactPoint.z) + ")");
}

void BreakableComponent::precomputeFragments(const std::vector<glm::vec3>& vertices,
                                              const std::vector<uint32_t>& indices)
{
    // Use a hash of vertex data as seed for reproducible fracture
    size_t seed = 0;
    for (const auto& v : vertices)
    {
        seed ^= std::hash<float>{}(v.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<float>{}(v.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    m_fractureSeed = static_cast<uint32_t>(seed);

    // Use center of vertices as default impact point for pre-computation
    glm::vec3 center(0.0f);
    for (const auto& v : vertices)
    {
        center += v;
    }
    if (!vertices.empty())
    {
        center /= static_cast<float>(vertices.size());
    }

    auto result = Fracture::fractureConvex(
        vertices, indices, fragmentCount, center, m_fractureSeed);

    if (result.success)
    {
        m_fragments = std::move(result.fragments);
        Logger::info("Pre-computed " + std::to_string(m_fragments.size()) + " fracture fragments");
    }
    else
    {
        Logger::warning("Failed to pre-compute fracture fragments");
    }
}

} // namespace Vestige
