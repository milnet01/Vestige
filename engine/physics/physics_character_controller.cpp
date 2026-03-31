/// @file physics_character_controller.cpp
/// @brief Physics-based character controller implementation using Jolt CharacterVirtual.
#include "physics/physics_character_controller.h"
#include "physics/jolt_helpers.h"
#include "physics/physics_layers.h"
#include "core/logger.h"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Local collision filter implementations for CharacterVirtual update calls
// ---------------------------------------------------------------------------
namespace
{

/// @brief Broadphase filter: allows CHARACTER layer to collide per the world's rules.
class CharacterBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
{
public:
    explicit CharacterBroadPhaseFilter(const JPH::ObjectVsBroadPhaseLayerFilter& filter)
        : m_filter(filter) {}

    bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override
    {
        return m_filter.ShouldCollide(ObjectLayers::CHARACTER, inLayer);
    }

private:
    const JPH::ObjectVsBroadPhaseLayerFilter& m_filter;
};

/// @brief Object layer filter: allows CHARACTER layer to collide per the world's rules.
class CharacterObjectLayerFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit CharacterObjectLayerFilter(const JPH::ObjectLayerPairFilter& filter)
        : m_filter(filter) {}

    bool ShouldCollide(JPH::ObjectLayer inLayer) const override
    {
        return m_filter.ShouldCollide(ObjectLayers::CHARACTER, inLayer);
    }

private:
    const JPH::ObjectLayerPairFilter& m_filter;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// PhysicsCharacterController
// ---------------------------------------------------------------------------

PhysicsCharacterController::PhysicsCharacterController() = default;

PhysicsCharacterController::~PhysicsCharacterController()
{
    shutdown();
}

bool PhysicsCharacterController::initialize(PhysicsWorld& world,
                                             const glm::vec3& feetPosition,
                                             const PhysicsControllerConfig& config)
{
    if (m_initialized)
    {
        Logger::warning("PhysicsCharacterController already initialized");
        return true;
    }

    if (!world.isInitialized())
    {
        Logger::error("Cannot initialize PhysicsCharacterController: PhysicsWorld not initialized");
        return false;
    }

    m_world = &world;
    m_config = config;

    float halfHeight = config.capsuleHalfHeight;
    float radius = config.capsuleRadius;

    // Create a capsule shape, offset upward so GetPosition() = feet position.
    // The capsule center sits at (0, halfHeight + radius, 0) above the feet.
    JPH::CapsuleShape* capsule = new JPH::CapsuleShape(halfHeight, radius);
    JPH::RotatedTranslatedShapeSettings offsetSettings(
        JPH::Vec3(0, halfHeight + radius, 0),
        JPH::Quat::sIdentity(),
        capsule);

    auto shapeResult = offsetSettings.Create();
    if (!shapeResult.IsValid())
    {
        Logger::error("Failed to create character capsule shape");
        return false;
    }

    // Configure CharacterVirtual settings
    JPH::CharacterVirtualSettings settings;
    settings.mShape = shapeResult.Get();
    settings.mUp = JPH::Vec3::sAxisY();
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(config.maxSlopeAngle);
    settings.mMass = config.mass;
    settings.mMaxStrength = config.maxStrength;
    settings.mCharacterPadding = config.characterPadding;
    settings.mPenetrationRecoverySpeed = config.penetrationRecoverySpeed;
    settings.mPredictiveContactDistance = config.predictiveContactDistance;
    // Supporting volume: contacts below feet + radius are "ground"
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

    // Create the virtual character (kinematic — no rigid body in the physics world)
    m_character = new JPH::CharacterVirtual(
        &settings,
        toJolt(feetPosition),
        JPH::Quat::sIdentity(),
        0,
        world.getSystem());

    m_initialized = true;
    Logger::info("PhysicsCharacterController initialized (capsule: r=" +
                 std::to_string(radius) + " hh=" + std::to_string(halfHeight) +
                 " total=" + std::to_string(2.0f * (halfHeight + radius)) + "m)");
    return true;
}

void PhysicsCharacterController::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_character = nullptr;
    m_world = nullptr;
    m_initialized = false;
}

void PhysicsCharacterController::update(float deltaTime, const glm::vec3& desiredVelocity)
{
    if (!m_initialized || m_character == nullptr)
    {
        return;
    }

    // Build collision filters from the world's layer rules
    CharacterBroadPhaseFilter bpFilter(m_world->getObjectVsBroadPhaseFilter());
    CharacterObjectLayerFilter objFilter(m_world->getObjectPairFilter());
    JPH::BodyFilter bodyFilter;
    JPH::ShapeFilter shapeFilter;

    if (m_flyMode)
    {
        // Fly mode: direct velocity control, no gravity, no ground logic
        m_character->SetLinearVelocity(toJolt(desiredVelocity));

        m_character->Update(deltaTime, JPH::Vec3::sZero(),
                            bpFilter, objFilter, bodyFilter, shapeFilter,
                            *m_world->getTempAllocator());
    }
    else
    {
        // Walk mode: handle gravity and ground state
        JPH::Vec3 currentVelocity = m_character->GetLinearVelocity();
        JPH::Vec3 gravity = toJolt(m_config.gravity);

        // Start with XZ from input, ignore Y from input in walk mode
        JPH::Vec3 newVelocity(desiredVelocity.x, 0.0f, desiredVelocity.z);

        JPH::CharacterVirtual::EGroundState groundState = m_character->GetGroundState();
        if (groundState == JPH::CharacterVirtual::EGroundState::OnGround)
        {
            // On ground: zero vertical velocity (no falling)
            newVelocity.SetY(0.0f);
        }
        else
        {
            // In air: preserve vertical velocity (accumulates gravity)
            newVelocity.SetY(currentVelocity.GetY());
        }

        // Apply gravity
        newVelocity += gravity * deltaTime;

        // Smooth XZ input to reduce jitter (but keep Y precise for gravity)
        float s = m_config.inputSmoothing;
        float smoothedX = (1.0f - s) * newVelocity.GetX() + s * currentVelocity.GetX();
        float smoothedZ = (1.0f - s) * newVelocity.GetZ() + s * currentVelocity.GetZ();
        newVelocity = JPH::Vec3(smoothedX, newVelocity.GetY(), smoothedZ);

        m_character->SetLinearVelocity(newVelocity);

        // Extended update: combined move + stair climbing + floor sticking
        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mStickToFloorStepDown =
            JPH::Vec3(0, -m_config.stickToFloorDistance, 0);
        updateSettings.mWalkStairsStepUp =
            JPH::Vec3(0, m_config.stairStepUp, 0);

        m_character->ExtendedUpdate(deltaTime, gravity,
                                     updateSettings,
                                     bpFilter, objFilter, bodyFilter, shapeFilter,
                                     *m_world->getTempAllocator());
    }
}

glm::vec3 PhysicsCharacterController::getPosition() const
{
    if (!m_initialized || m_character == nullptr)
    {
        return glm::vec3(0.0f);
    }
    return toGlm(m_character->GetPosition());
}

void PhysicsCharacterController::setPosition(const glm::vec3& feetPosition)
{
    if (!m_initialized || m_character == nullptr)
    {
        return;
    }
    m_character->SetPosition(toJolt(feetPosition));
}

glm::vec3 PhysicsCharacterController::getEyePosition() const
{
    return getPosition() + glm::vec3(0.0f, m_config.eyeHeight, 0.0f);
}

glm::vec3 PhysicsCharacterController::getLinearVelocity() const
{
    if (!m_initialized || m_character == nullptr)
    {
        return glm::vec3(0.0f);
    }
    return toGlm(m_character->GetLinearVelocity());
}

bool PhysicsCharacterController::isOnGround() const
{
    if (!m_initialized || m_character == nullptr)
    {
        return false;
    }
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

bool PhysicsCharacterController::isOnSteepGround() const
{
    if (!m_initialized || m_character == nullptr)
    {
        return false;
    }
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnSteepGround;
}

bool PhysicsCharacterController::isInAir() const
{
    if (!m_initialized || m_character == nullptr)
    {
        return true;
    }
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::InAir;
}

void PhysicsCharacterController::setFlyMode(bool fly)
{
    m_flyMode = fly;
}

} // namespace Vestige
