/// @file character_system.h
/// @brief Domain system for character animation and physics-based movement.
#pragma once

#include "core/i_system.h"
#include "physics/physics_character_controller.h"

#include <string>

namespace Vestige
{

/// @brief Manages character movement, animation, and physics-based controller.
///
/// Owns PhysicsCharacterController. The controller update stays in
/// engine.cpp's main loop (tightly coupled with Camera and input).
/// This system handles lifecycle and will own animation subsystems
/// (skeletal, IK, motion matching) in the future.
class CharacterSystem : public ISystem
{
public:
    CharacterSystem() = default;

    // -- ISystem interface --
    const std::string& getSystemName() const override { return m_name; }
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;

    // -- Accessors --
    PhysicsCharacterController& getPhysicsCharController() { return m_physicsCharController; }
    const PhysicsCharacterController& getPhysicsCharController() const { return m_physicsCharController; }

private:
    static inline const std::string m_name = "Character";
    PhysicsCharacterController m_physicsCharController;
};

} // namespace Vestige
