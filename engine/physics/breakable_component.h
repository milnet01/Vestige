/// @file breakable_component.h
/// @brief Component marking entities as destructible with Voronoi fracture.
#pragma once

#include "scene/component.h"
#include "physics/fracture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class PhysicsWorld;

/// @brief Component that marks an entity as destructible.
///
/// When the entity receives sufficient impact force, it fractures
/// into multiple rigid body fragments using Voronoi decomposition.
class BreakableComponent : public Component
{
public:
    BreakableComponent() = default;
    ~BreakableComponent() override = default;

    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

    /// @brief Minimum impact impulse required to fracture (N*s).
    float breakImpulse = 50.0f;

    /// @brief Number of Voronoi fragments to generate.
    int fragmentCount = 8;

    /// @brief Seconds before fragment cleanup.
    float fragmentLifetime = 5.0f;

    /// @brief Mass density for fragments (kg/m^3).
    float fragmentMassDensity = 500.0f;

    /// @brief Material name for interior (cut) faces.
    std::string interiorMaterial;

    /// @brief Returns true if this entity has been fractured.
    bool isFractured() const { return m_fractured; }

    /// @brief Triggers fracture at the given impact point.
    /// @param impactPoint World-space point of impact.
    /// @param impulse World-space impulse vector.
    void fracture(const glm::vec3& impactPoint, const glm::vec3& impulse);

    /// @brief Pre-computes fragments (call at setup for instant activation later).
    /// @param vertices Source mesh vertices (positions only).
    /// @param indices Source mesh triangle indices.
    void precomputeFragments(const std::vector<glm::vec3>& vertices,
                             const std::vector<uint32_t>& indices);

    /// @brief Returns true if fragments have been pre-computed.
    bool hasPrecomputedFragments() const { return !m_fragments.empty(); }

    /// @brief Access pre-computed fragments.
    const std::vector<FractureFragment>& getFragments() const { return m_fragments; }

private:
    bool m_fractured = false;
    std::vector<FractureFragment> m_fragments;
    glm::vec3 m_lastImpactPoint = glm::vec3(0.0f);
    glm::vec3 m_lastImpulse = glm::vec3(0.0f);
    uint32_t m_fractureSeed = 0;
};

} // namespace Vestige
