/// @file physics_constraint.cpp
/// @brief PhysicsConstraint implementation.
#include "physics/physics_constraint.h"

namespace Vestige
{

void PhysicsConstraint::setEnabled(bool enabled)
{
    if (m_constraint)
    {
        m_constraint->SetEnabled(enabled);
    }
}

bool PhysicsConstraint::isEnabled() const
{
    return m_constraint && m_constraint->GetEnabled();
}

JPH::HingeConstraint* PhysicsConstraint::asHinge()
{
    if (m_type == ConstraintType::HINGE && m_constraint)
    {
        return static_cast<JPH::HingeConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

const JPH::HingeConstraint* PhysicsConstraint::asHinge() const
{
    if (m_type == ConstraintType::HINGE && m_constraint)
    {
        return static_cast<const JPH::HingeConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

JPH::FixedConstraint* PhysicsConstraint::asFixed()
{
    if (m_type == ConstraintType::FIXED && m_constraint)
    {
        return static_cast<JPH::FixedConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

const JPH::FixedConstraint* PhysicsConstraint::asFixed() const
{
    if (m_type == ConstraintType::FIXED && m_constraint)
    {
        return static_cast<const JPH::FixedConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

JPH::DistanceConstraint* PhysicsConstraint::asDistance()
{
    if (m_type == ConstraintType::DISTANCE && m_constraint)
    {
        return static_cast<JPH::DistanceConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

const JPH::DistanceConstraint* PhysicsConstraint::asDistance() const
{
    if (m_type == ConstraintType::DISTANCE && m_constraint)
    {
        return static_cast<const JPH::DistanceConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

JPH::PointConstraint* PhysicsConstraint::asPoint()
{
    if (m_type == ConstraintType::POINT && m_constraint)
    {
        return static_cast<JPH::PointConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

const JPH::PointConstraint* PhysicsConstraint::asPoint() const
{
    if (m_type == ConstraintType::POINT && m_constraint)
    {
        return static_cast<const JPH::PointConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

JPH::SliderConstraint* PhysicsConstraint::asSlider()
{
    if (m_type == ConstraintType::SLIDER && m_constraint)
    {
        return static_cast<JPH::SliderConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

const JPH::SliderConstraint* PhysicsConstraint::asSlider() const
{
    if (m_type == ConstraintType::SLIDER && m_constraint)
    {
        return static_cast<const JPH::SliderConstraint*>(m_constraint.GetPtr());
    }
    return nullptr;
}

} // namespace Vestige
