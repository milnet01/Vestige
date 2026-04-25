// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file stasis_system.h
/// @brief Per-body time freeze / slow-motion system.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>

namespace JPH { class BodyID; }

namespace Vestige
{

class PhysicsWorld;

/// @brief Stores saved motion state for a body under stasis.
struct StasisState
{
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    float timeScale = 0.0f;         ///< 0 = frozen, 0.1 = slow-mo, 1.0 = normal.
    float duration = 0.0f;          ///< Remaining stasis time (0 = indefinite).
    float elapsed = 0.0f;           ///< Time elapsed since stasis started.
};

/// @brief Manages per-body time freeze/slow-motion effects.
///
/// When a body enters stasis:
/// - Its velocity is stored and zeroed (or scaled for slow-motion)
/// - For full freeze (timeScale < 0.001), the body is deactivated in place
/// - A visual tint can be applied (caller's responsibility)
///
/// When stasis ends:
/// - Original velocity is restored
/// - The body is reactivated
class StasisSystem
{
public:
    StasisSystem() = default;

    /// @brief Set the physics world reference.
    void setPhysicsWorld(PhysicsWorld* world) { m_physicsWorld = world; }

    /// @brief Put a body into stasis (freeze/slow-motion).
    /// @param bodyId The Jolt body ID (as uint32_t index+sequence).
    /// @param duration Duration in seconds (0 = indefinite until releaseStasis called).
    /// @param timeScale 0.0 = complete freeze, 0.01 = extreme slow-mo.
    void applyStasis(uint32_t bodyId, float duration = 0.0f, float timeScale = 0.0f);

    /// @brief Release a body from stasis, restoring its original motion.
    void releaseStasis(uint32_t bodyId);

    /// @brief Check if a body is currently in stasis.
    bool isInStasis(uint32_t bodyId) const;

    /// @brief Get remaining stasis duration for a body (0 if indefinite or not in stasis).
    float getRemainingDuration(uint32_t bodyId) const;

    /// @brief Update all stasis effects (call each frame).
    void update(float deltaTime);

    /// @brief Release all active stasis effects.
    void releaseAll();

    /// @brief Get number of bodies currently in stasis.
    size_t getActiveCount() const { return m_stasisMap.size(); }

private:
    PhysicsWorld* m_physicsWorld = nullptr;
    std::unordered_map<uint32_t, StasisState> m_stasisMap;
};

} // namespace Vestige
