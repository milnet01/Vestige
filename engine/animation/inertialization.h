/// @file inertialization.h
/// @brief Inertialization blending for smooth animation transitions.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Vestige
{

/// @brief Per-bone inertialization state.
struct BoneInertializationState
{
    glm::vec3 positionOffset = glm::vec3(0.0f);
    glm::vec3 positionVelocity = glm::vec3(0.0f);
    glm::vec3 rotationOffset = glm::vec3(0.0f);  ///< Axis-angle offset
    glm::vec3 rotationVelocity = glm::vec3(0.0f);
};

/// @brief Offset-based transition blending using critically damped spring decay.
///
/// At transition time, records the difference between source and destination
/// poses. Each frame, decays the offset using a critically damped spring and
/// adds it to the destination pose. Only the destination animation is evaluated
/// during the transition — the source is never touched again.
///
/// Based on David Bollo's inertialization (GDC 2018) and Daniel Holden's
/// dead blending refinement.
class Inertialization
{
public:
    /// @brief Records offset between source and destination poses.
    /// @param srcPositions Source pose bone positions (model-space).
    /// @param srcRotations Source pose bone rotations (model-space).
    /// @param srcVelocities Source pose bone velocities.
    /// @param dstPositions Destination pose bone positions.
    /// @param dstRotations Destination pose bone rotations.
    /// @param dstVelocities Destination pose bone velocities.
    /// @param halflife Decay halflife in seconds (default 0.1).
    void start(const std::vector<glm::vec3>& srcPositions,
               const std::vector<glm::quat>& srcRotations,
               const std::vector<glm::vec3>& srcVelocities,
               const std::vector<glm::vec3>& dstPositions,
               const std::vector<glm::quat>& dstRotations,
               const std::vector<glm::vec3>& dstVelocities,
               float halflife = 0.1f);

    /// @brief Advances decay by dt seconds.
    void update(float dt);

    /// @brief Applies the current offset to the destination pose.
    /// @param positions In/out bone positions to modify.
    /// @param rotations In/out bone rotations to modify.
    void apply(std::vector<glm::vec3>& positions,
               std::vector<glm::quat>& rotations) const;

    /// @brief Whether inertialization is currently active (offsets non-negligible).
    bool isActive() const;

    /// @brief Gets the current elapsed time since the transition started.
    float getElapsedTime() const;

    /// @brief Gets the halflife.
    float getHalflife() const;

    /// @brief Resets to inactive state.
    void reset();

private:
    /// @brief Critically damped spring decay.
    static glm::vec3 springDecay(const glm::vec3& offset,
                                 const glm::vec3& velocity,
                                 float halflife, float t);

    /// @brief Converts halflife to damping coefficient.
    static float halflifeToDamping(float halflife);

    /// @brief Fast negative exponential approximation.
    static float fastNegexp(float x);

    std::vector<BoneInertializationState> m_states;
    float m_halflife = 0.1f;
    float m_elapsed = 0.0f;
    bool m_active = false;
};

} // namespace Vestige
