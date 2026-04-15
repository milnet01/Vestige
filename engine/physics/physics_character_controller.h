// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_character_controller.h
/// @brief Physics-based character controller wrapping Jolt's CharacterVirtual.
#pragma once

#include "physics/physics_world.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Configuration for the physics character controller.
struct PhysicsControllerConfig
{
    float capsuleRadius = 0.3f;
    float capsuleHalfHeight = 0.55f;   ///< Total height = 2 * (halfHeight + radius) = 1.7m
    float eyeHeight = 1.7f;            ///< Eye height above feet
    float maxSlopeAngle = 50.0f;       ///< Max walkable slope in degrees
    float mass = 70.0f;                ///< Character mass (kg)
    float maxStrength = 100.0f;        ///< Max force to push dynamic bodies (N)
    float characterPadding = 0.02f;    ///< Padding around character shape
    float penetrationRecoverySpeed = 1.0f;
    float predictiveContactDistance = 0.1f;
    float stairStepUp = 0.35f;         ///< Max step height for stair climbing
    float stickToFloorDistance = 0.5f;  ///< Max snap-down distance for floor sticking
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    float inputSmoothing = 0.75f;      ///< XZ velocity smoothing (0 = none, 1 = full)
};

/// @brief Physics-based first-person character controller using Jolt CharacterVirtual.
///
/// Replaces the AABB collision system with a capsule-based physics controller that
/// handles stair climbing, slope limiting, floor sticking, and smooth wall sliding.
/// Works alongside FirstPersonController which provides input/camera processing.
class PhysicsCharacterController
{
public:
    PhysicsCharacterController();
    ~PhysicsCharacterController();

    // Non-copyable
    PhysicsCharacterController(const PhysicsCharacterController&) = delete;
    PhysicsCharacterController& operator=(const PhysicsCharacterController&) = delete;

    /// @brief Initializes the character with a capsule shape in the physics world.
    /// @param world Physics world (must be initialized).
    /// @param feetPosition Starting position at the character's feet.
    /// @param config Controller configuration.
    bool initialize(PhysicsWorld& world, const glm::vec3& feetPosition,
                    const PhysicsControllerConfig& config = {});

    /// @brief Destroys the character and releases resources.
    void shutdown();

    /// @brief Updates character position using physics collision resolution.
    /// @param deltaTime Frame delta time.
    /// @param desiredVelocity World-space desired velocity (XZ for walk, XYZ for fly).
    void update(float deltaTime, const glm::vec3& desiredVelocity);

    /// @brief Gets the character's feet position.
    glm::vec3 getPosition() const;

    /// @brief Teleports the character to a new feet position.
    void setPosition(const glm::vec3& feetPosition);

    /// @brief Gets the camera/eye position (feet + eye height offset).
    glm::vec3 getEyePosition() const;

    /// @brief Gets the character's current velocity.
    glm::vec3 getLinearVelocity() const;

    /// @brief Returns true if the character is standing on walkable ground.
    bool isOnGround() const;

    /// @brief Returns true if the character is on a slope too steep to climb.
    bool isOnSteepGround() const;

    /// @brief Returns true if the character is airborne.
    bool isInAir() const;

    /// @brief Toggles fly mode (no gravity, free vertical movement).
    void setFlyMode(bool fly);
    bool isFlyMode() const { return m_flyMode; }

    bool isInitialized() const { return m_initialized; }

    const PhysicsControllerConfig& getConfig() const { return m_config; }
    PhysicsControllerConfig& getConfig() { return m_config; }

private:
    JPH::Ref<JPH::CharacterVirtual> m_character;
    PhysicsWorld* m_world = nullptr;
    PhysicsControllerConfig m_config;
    bool m_initialized = false;
    bool m_flyMode = false;
};

} // namespace Vestige
