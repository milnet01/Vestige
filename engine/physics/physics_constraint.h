// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_constraint.h
/// @brief Constraint wrapper and handle for the Jolt physics constraint system.
#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <cstdint>

namespace Vestige
{

/// @brief Lightweight handle to a constraint managed by PhysicsWorld.
///
/// Uses index + generation to detect stale references.
struct ConstraintHandle
{
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool isValid() const { return index != UINT32_MAX; }

    bool operator==(const ConstraintHandle& other) const
    {
        return index == other.index && generation == other.generation;
    }

    bool operator!=(const ConstraintHandle& other) const
    {
        return !(*this == other);
    }
};

/// @brief Type of physics constraint.
enum class ConstraintType : uint8_t
{
    HINGE,
    FIXED,
    DISTANCE,
    POINT,
    SLIDER
};

/// @brief Wraps a Jolt TwoBodyConstraint with type information, break force,
/// and body tracking.
///
/// PhysicsWorld owns these objects. External code accesses them via
/// ConstraintHandle + PhysicsWorld::getConstraint().
class PhysicsConstraint
{
public:
    PhysicsConstraint() = default;

    /// @brief Returns the constraint type.
    ConstraintType getType() const { return m_type; }

    /// @brief Returns the handle assigned by PhysicsWorld.
    ConstraintHandle getHandle() const { return m_handle; }

    /// @brief Returns the Jolt body IDs. BodyA may be invalid (= world).
    JPH::BodyID getBodyA() const { return m_bodyA; }
    JPH::BodyID getBodyB() const { return m_bodyB; }

    /// @brief Enables or disables the constraint.
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// @brief Sets the break force threshold (Newtons). 0 = unbreakable.
    void setBreakForce(float force) { m_breakForce = force; }
    float getBreakForce() const { return m_breakForce; }

    /// @brief Returns the force magnitude applied during the last physics step.
    float getCurrentForce() const { return m_currentForce; }

    /// @brief Type-safe accessors. Returns nullptr if wrong type.
    JPH::HingeConstraint* asHinge();
    const JPH::HingeConstraint* asHinge() const;

    JPH::FixedConstraint* asFixed();
    const JPH::FixedConstraint* asFixed() const;

    JPH::DistanceConstraint* asDistance();
    const JPH::DistanceConstraint* asDistance() const;

    JPH::PointConstraint* asPoint();
    const JPH::PointConstraint* asPoint() const;

    JPH::SliderConstraint* asSlider();
    const JPH::SliderConstraint* asSlider() const;

    /// @brief Returns the raw Jolt constraint pointer.
    JPH::TwoBodyConstraint* getJoltConstraint() { return m_constraint.GetPtr(); }
    const JPH::TwoBodyConstraint* getJoltConstraint() const { return m_constraint.GetPtr(); }

private:
    JPH::Ref<JPH::TwoBodyConstraint> m_constraint;
    ConstraintHandle m_handle;
    ConstraintType m_type = ConstraintType::HINGE;
    JPH::BodyID m_bodyA;
    JPH::BodyID m_bodyB;
    float m_breakForce = 0.0f;
    float m_currentForce = 0.0f;

    friend class PhysicsWorld;
};

} // namespace Vestige
