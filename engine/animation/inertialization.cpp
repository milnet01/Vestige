/// @file inertialization.cpp
/// @brief Inertialization blending implementation.

#include "animation/inertialization.h"

#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Spring math
// ---------------------------------------------------------------------------

float Inertialization::halflifeToDamping(float halflife)
{
    return (4.0f * 0.69314718056f) / (halflife + 1e-8f);
}

float Inertialization::fastNegexp(float x)
{
    return 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
}

glm::vec3 Inertialization::springDecay(const glm::vec3& offset,
                                       const glm::vec3& velocity,
                                       float halflife, float t)
{
    float y = halflifeToDamping(halflife) / 2.0f;
    float eydt = fastNegexp(y * t);

    // Critically damped spring: x(t) = e^(-yt) * (j0 + j1*t)
    // where j0 = initial offset, j1 = initial velocity + j0 * y
    glm::vec3 j0 = offset;
    glm::vec3 j1 = velocity + offset * y;

    return eydt * (j0 + j1 * t);
}

// ---------------------------------------------------------------------------
// Inertialization
// ---------------------------------------------------------------------------

void Inertialization::start(const std::vector<glm::vec3>& srcPositions,
                            const std::vector<glm::quat>& srcRotations,
                            const std::vector<glm::vec3>& srcVelocities,
                            const std::vector<glm::vec3>& dstPositions,
                            const std::vector<glm::quat>& dstRotations,
                            const std::vector<glm::vec3>& dstVelocities,
                            float halflife)
{
    size_t boneCount = srcPositions.size();
    m_states.resize(boneCount);
    m_halflife = halflife;
    m_elapsed = 0.0f;
    m_active = true;

    for (size_t i = 0; i < boneCount; ++i)
    {
        auto& state = m_states[i];

        // Position offset: difference between source and destination
        state.positionOffset = srcPositions[i] - dstPositions[i];

        // Velocity offset
        if (i < srcVelocities.size() && i < dstVelocities.size())
            state.positionVelocity = srcVelocities[i] - dstVelocities[i];
        else
            state.positionVelocity = glm::vec3(0.0f);

        // Rotation offset: axis-angle representation of rotation difference
        if (i < srcRotations.size() && i < dstRotations.size())
        {
            glm::quat diff = srcRotations[i] * glm::inverse(dstRotations[i]);
            // Ensure shortest path
            if (diff.w < 0.0f)
                diff = -diff;

            // Convert to axis-angle
            float angle = 2.0f * std::acos(glm::clamp(diff.w, -1.0f, 1.0f));
            if (angle > 1e-6f)
            {
                float sinHalf = std::sin(angle / 2.0f);
                glm::vec3 axis(diff.x / sinHalf, diff.y / sinHalf, diff.z / sinHalf);
                state.rotationOffset = axis * angle;
            }
            else
            {
                state.rotationOffset = glm::vec3(0.0f);
            }
        }
        else
        {
            state.rotationOffset = glm::vec3(0.0f);
        }

        state.rotationVelocity = glm::vec3(0.0f);
    }
}

void Inertialization::update(float dt)
{
    if (!m_active)
        return;

    m_elapsed += dt;

    // Check if offsets have decayed to negligible
    // After ~5 halflives, offsets are < 3% of initial value
    if (m_elapsed > m_halflife * 5.0f)
    {
        m_active = false;
    }
}

void Inertialization::apply(std::vector<glm::vec3>& positions,
                            std::vector<glm::quat>& rotations) const
{
    if (!m_active)
        return;

    size_t count = std::min(m_states.size(), positions.size());
    for (size_t i = 0; i < count; ++i)
    {
        const auto& state = m_states[i];

        // Decay position offset
        glm::vec3 posOffset = springDecay(state.positionOffset,
                                          state.positionVelocity,
                                          m_halflife, m_elapsed);
        positions[i] += posOffset;

        // Decay rotation offset
        if (i < rotations.size())
        {
            glm::vec3 rotOffset = springDecay(state.rotationOffset,
                                              state.rotationVelocity,
                                              m_halflife, m_elapsed);
            float angle = glm::length(rotOffset);
            if (angle > 1e-6f)
            {
                glm::vec3 axis = rotOffset / angle;
                glm::quat correction = glm::angleAxis(angle, axis);
                rotations[i] = correction * rotations[i];
            }
        }
    }
}

bool Inertialization::isActive() const
{
    return m_active;
}

float Inertialization::getElapsedTime() const
{
    return m_elapsed;
}

float Inertialization::getHalflife() const
{
    return m_halflife;
}

void Inertialization::reset()
{
    m_states.clear();
    m_elapsed = 0.0f;
    m_active = false;
}

} // namespace Vestige
