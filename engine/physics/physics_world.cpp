// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file physics_world.cpp
/// @brief Jolt PhysicsSystem wrapper implementation.
#include "physics/physics_world.h"
#include "physics/jolt_helpers.h"
#include "core/logger.h"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>

namespace Vestige
{

// Jolt requires a trace function and an assertion failure handler.
// Route both through our Logger.

static void joltTraceImpl(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Logger::debug(std::string("[Jolt] ") + buffer);
}

#ifdef JPH_ENABLE_ASSERTS
static bool joltAssertFailed(const char* expression, const char* message,
                              const char* file, unsigned int line)
{
    std::string msg = "Jolt assertion failed: ";
    msg += expression;
    msg += " — ";
    msg += (message ? message : "");
    msg += " (";
    msg += file;
    msg += ":";
    msg += std::to_string(line);
    msg += ")";
    Logger::error(msg);
    return true;  // Break into debugger
}
#endif

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld()
{
    shutdown();
}

bool PhysicsWorld::initialize(const PhysicsWorldConfig& config)
{
    if (m_initialized)
    {
        Logger::warning("PhysicsWorld already initialized");
        return true;
    }

    // Register default Jolt allocator (uses standard malloc/free)
    JPH::RegisterDefaultAllocator();

    // Install trace and assert handlers
    JPH::Trace = joltTraceImpl;

#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = joltAssertFailed;
#endif

    // Create the Jolt factory (required for shape serialization/deserialization)
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all built-in Jolt types
    JPH::RegisterTypes();

    // Create temp allocator (10 MB — used for per-frame temporaries)
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    // Create job system
    int threads = config.threadCount;
    if (threads < 0)
    {
        threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    }
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

    Logger::info("Jolt Physics: " + std::to_string(threads) + " worker threads");

    // Create the physics system
    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(
        config.maxBodies,
        config.numBodyMutexes,
        config.maxBodyPairs,
        config.maxContactConstraints,
        m_broadPhaseMapping,
        m_objectVsBpFilter,
        m_objectPairFilter);

    m_fixedTimestep = config.fixedTimestep;
    m_collisionSteps = config.collisionSteps;
    m_accumulator = 0.0f;
    m_initialized = true;

    Logger::info("PhysicsWorld initialized (max bodies: " +
                 std::to_string(config.maxBodies) + ", timestep: " +
                 std::to_string(m_fixedTimestep) + "s)");
    return true;
}

void PhysicsWorld::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    // Remove all constraints before bodies
    for (auto& [idx, constraint] : m_constraints)
    {
        JPH::TwoBodyConstraint* jc = constraint.getJoltConstraint();
        if (jc)
        {
            m_physicsSystem->RemoveConstraint(jc);
        }
    }
    m_constraints.clear();

    // Remove and destroy all bodies
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyIDVector bodyIds;
    m_physicsSystem->GetBodies(bodyIds);

    for (const auto& id : bodyIds)
    {
        bodyInterface.RemoveBody(id);
        bodyInterface.DestroyBody(id);
    }

    m_physicsSystem.reset();
    m_jobSystem.reset();
    m_tempAllocator.reset();

    // Unregister types and destroy factory
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_initialized = false;
    Logger::info("PhysicsWorld shut down");
}

void PhysicsWorld::update(float deltaTime)
{
    if (!m_initialized)
    {
        return;
    }

    m_accumulator += deltaTime;

    // Clamp accumulator to prevent spiral of death (max 4 steps per frame)
    float maxAccum = m_fixedTimestep * 4.0f;
    if (m_accumulator > maxAccum)
    {
        m_accumulator = maxAccum;
    }

    while (m_accumulator >= m_fixedTimestep)
    {
        m_physicsSystem->Update(m_fixedTimestep, m_collisionSteps,
                                 m_tempAllocator.get(), m_jobSystem.get());
        m_accumulator -= m_fixedTimestep;
    }
}

JPH::BodyID PhysicsWorld::createStaticBody(const JPH::Shape* shape,
                                            const glm::vec3& position,
                                            const glm::quat& rotation)
{
    JPH::BodyCreationSettings settings(
        shape,
        toJolt(position),
        toJolt(rotation),
        JPH::EMotionType::Static,
        ObjectLayers::STATIC);

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    return bodyId;
}

JPH::BodyID PhysicsWorld::createDynamicBody(const JPH::Shape* shape,
                                             const glm::vec3& position,
                                             const glm::quat& rotation,
                                             float mass,
                                             float friction,
                                             float restitution)
{
    JPH::BodyCreationSettings settings(
        shape,
        toJolt(position),
        toJolt(rotation),
        JPH::EMotionType::Dynamic,
        ObjectLayers::DYNAMIC);

    settings.mFriction = friction;
    settings.mRestitution = restitution;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    return bodyId;
}

JPH::BodyID PhysicsWorld::createKinematicBody(const JPH::Shape* shape,
                                               const glm::vec3& position,
                                               const glm::quat& rotation)
{
    JPH::BodyCreationSettings settings(
        shape,
        toJolt(position),
        toJolt(rotation),
        JPH::EMotionType::Kinematic,
        ObjectLayers::DYNAMIC);

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    return bodyId;
}

void PhysicsWorld::destroyBody(JPH::BodyID bodyId)
{
    if (!m_initialized || bodyId.IsInvalid())
    {
        return;
    }

    // Remove any constraints referencing this body before destroying it
    removeConstraintsForBody(bodyId);

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
}

glm::vec3 PhysicsWorld::getBodyPosition(JPH::BodyID bodyId) const
{
    const JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    return toGlm(bodyInterface.GetPosition(bodyId));
}

glm::quat PhysicsWorld::getBodyRotation(JPH::BodyID bodyId) const
{
    const JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    return toGlm(bodyInterface.GetRotation(bodyId));
}

void PhysicsWorld::setBodyTransform(JPH::BodyID bodyId,
                                     const glm::vec3& position,
                                     const glm::quat& rotation)
{
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.SetPositionAndRotation(bodyId, toJolt(position), toJolt(rotation),
                                          JPH::EActivation::Activate);
}

void PhysicsWorld::applyForce(JPH::BodyID bodyId, const glm::vec3& force)
{
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.AddForce(bodyId, toJolt(force));
}

void PhysicsWorld::applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse)
{
    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.AddImpulse(bodyId, toJolt(impulse));
}

unsigned int PhysicsWorld::getActiveBodyCount() const
{
    if (!m_initialized)
    {
        return 0;
    }
    return m_physicsSystem->GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

JPH::BodyInterface& PhysicsWorld::getBodyInterface()
{
    return m_physicsSystem->GetBodyInterface();
}

const JPH::BodyInterface& PhysicsWorld::getBodyInterface() const
{
    return m_physicsSystem->GetBodyInterface();
}

// ---------------------------------------------------------------------------
// Constraint helpers
// ---------------------------------------------------------------------------

glm::vec3 PhysicsWorld::computeSliderNormalAxis(const glm::vec3& slideAxis)
{
    // Hughes-Möller (1999): pick the smallest-magnitude component of
    // the unit axis and build an orthogonal vector by zeroing that
    // component and swap-negating the other two. The choice of branch
    // depends purely on the axis vector, never on a world axis, so the
    // basis is reproducible across scene rotations rather than snapping
    // at a world-Y dot-product threshold (the prior implementation).
    const glm::vec3 a = glm::normalize(slideAxis);
    const float ax = std::abs(a.x);
    const float ay = std::abs(a.y);
    const float az = std::abs(a.z);
    glm::vec3 ortho;
    if (ax <= ay && ax <= az)
    {
        ortho = glm::vec3(0.0f, -a.z, a.y);
    }
    else if (ay <= ax && ay <= az)
    {
        ortho = glm::vec3(-a.z, 0.0f, a.x);
    }
    else
    {
        ortho = glm::vec3(-a.y, a.x, 0.0f);
    }
    return glm::normalize(ortho);
}

JPH::Body* PhysicsWorld::resolveBodyA(JPH::BodyID bodyA)
{
    if (bodyA.IsInvalid())
    {
        return &JPH::Body::sFixedToWorld;
    }

    // Note: the caller must hold its own lock on bodyA, or use this pointer
    // only within the scope of the caller's lock on bodyB (Jolt allows
    // locking multiple bodies if done through BodyLockMultiWrite, but for
    // our use case — constraint creation — we lock bodyA here transiently
    // just to get its pointer).  Jolt body pointers remain stable as long as
    // the body is not destroyed, so the pointer is safe to use after unlock
    // provided the body is still alive.
    JPH::BodyLockWrite lock(m_physicsSystem->GetBodyLockInterface(), bodyA);
    if (lock.Succeeded())
    {
        return &lock.GetBody();
    }

    Logger::warning("PhysicsWorld: could not lock bodyA, using world anchor");
    return &JPH::Body::sFixedToWorld;
}

ConstraintHandle PhysicsWorld::registerConstraint(JPH::TwoBodyConstraint* constraint,
                                                   ConstraintType type,
                                                   JPH::BodyID bodyA, JPH::BodyID bodyB)
{
    ConstraintHandle handle;
    handle.index = m_nextConstraintIndex++;
    // Per-slot generation: a fresh slot starts at 1. Indices grow
    // monotonically (no reuse), so the index alone disambiguates handles;
    // generation is reserved for future slot recycling.
    handle.generation = 1;

    PhysicsConstraint pc;
    pc.m_constraint = constraint;
    pc.m_handle = handle;
    pc.m_type = type;
    pc.m_bodyA = bodyA;
    pc.m_bodyB = bodyB;

    m_physicsSystem->AddConstraint(constraint);
    m_constraints[handle.index] = std::move(pc);

    return handle;
}

// ---------------------------------------------------------------------------
// Constraint creation
// ---------------------------------------------------------------------------

ConstraintHandle PhysicsWorld::addHingeConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pivotPoint,
    const glm::vec3& hingeAxis,
    const glm::vec3& normalAxis,
    float limitsMinDeg, float limitsMaxDeg,
    float maxFrictionTorque)
{
    JPH::HingeConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = toJolt(pivotPoint);
    settings.mPoint2 = toJolt(pivotPoint);
    settings.mHingeAxis1 = toJolt(hingeAxis);
    settings.mHingeAxis2 = toJolt(hingeAxis);
    settings.mNormalAxis1 = toJolt(normalAxis);
    settings.mNormalAxis2 = toJolt(normalAxis);
    settings.mLimitsMin = JPH::DegreesToRadians(limitsMinDeg);
    settings.mLimitsMax = JPH::DegreesToRadians(limitsMaxDeg);
    settings.mMaxFrictionTorque = maxFrictionTorque;

    return withBodyPair(bodyA, bodyB, [&](JPH::Body& bA, JPH::Body& bB)
    {
        auto* constraint = static_cast<JPH::HingeConstraint*>(
            settings.Create(bA, bB));
        return registerConstraint(constraint, ConstraintType::HINGE, bodyA, bodyB);
    });
}

ConstraintHandle PhysicsWorld::addFixedConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB)
{
    JPH::FixedConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = true;

    return withBodyPair(bodyA, bodyB, [&](JPH::Body& bA, JPH::Body& bB)
    {
        auto* constraint = static_cast<JPH::FixedConstraint*>(
            settings.Create(bA, bB));
        return registerConstraint(constraint, ConstraintType::FIXED, bodyA, bodyB);
    });
}

ConstraintHandle PhysicsWorld::addDistanceConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pointA, const glm::vec3& pointB,
    float minDist, float maxDist,
    float springFrequency, float springDamping)
{
    JPH::DistanceConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = toJolt(pointA);
    settings.mPoint2 = toJolt(pointB);
    settings.mMinDistance = minDist;
    settings.mMaxDistance = maxDist;

    if (springFrequency > 0.0f)
    {
        settings.mLimitsSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
        settings.mLimitsSpringSettings.mFrequency = springFrequency;
        settings.mLimitsSpringSettings.mDamping = springDamping;
    }

    return withBodyPair(bodyA, bodyB, [&](JPH::Body& bA, JPH::Body& bB)
    {
        auto* constraint = static_cast<JPH::DistanceConstraint*>(
            settings.Create(bA, bB));
        return registerConstraint(constraint, ConstraintType::DISTANCE, bodyA, bodyB);
    });
}

ConstraintHandle PhysicsWorld::addPointConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& pivotPoint)
{
    JPH::PointConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = toJolt(pivotPoint);
    settings.mPoint2 = toJolt(pivotPoint);

    return withBodyPair(bodyA, bodyB, [&](JPH::Body& bA, JPH::Body& bB)
    {
        auto* constraint = static_cast<JPH::PointConstraint*>(
            settings.Create(bA, bB));
        return registerConstraint(constraint, ConstraintType::POINT, bodyA, bodyB);
    });
}

ConstraintHandle PhysicsWorld::addSliderConstraint(
    JPH::BodyID bodyA, JPH::BodyID bodyB,
    const glm::vec3& slideAxis,
    float limitsMin, float limitsMax,
    float maxFrictionForce)
{
    JPH::SliderConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mSliderAxis1 = toJolt(slideAxis);
    settings.mSliderAxis2 = toJolt(slideAxis);

    // Phase 10.9 Ph7: Hughes-Möller orthonormalize. See the static
    // helper for the rationale; in short, the basis depends only on
    // `slideAxis`, never on a world axis, so two scenes with identical
    // geometry rotated 90° produce a rotation-consistent basis instead
    // of crossing a world-Y dot-product threshold.
    const JPH::Vec3 normal = toJolt(computeSliderNormalAxis(slideAxis));
    settings.mNormalAxis1 = normal;
    settings.mNormalAxis2 = normal;

    settings.mLimitsMin = limitsMin;
    settings.mLimitsMax = limitsMax;
    settings.mMaxFrictionForce = maxFrictionForce;

    return withBodyPair(bodyA, bodyB, [&](JPH::Body& bA, JPH::Body& bB)
    {
        auto* constraint = static_cast<JPH::SliderConstraint*>(
            settings.Create(bA, bB));
        return registerConstraint(constraint, ConstraintType::SLIDER, bodyA, bodyB);
    });
}

// ---------------------------------------------------------------------------
// Constraint access and removal
// ---------------------------------------------------------------------------

PhysicsConstraint* PhysicsWorld::getConstraint(ConstraintHandle handle)
{
    if (!handle.isValid())
    {
        return nullptr;
    }

    auto it = m_constraints.find(handle.index);
    if (it == m_constraints.end() || it->second.getHandle().generation != handle.generation)
    {
        return nullptr;
    }
    return &it->second;
}

const PhysicsConstraint* PhysicsWorld::getConstraint(ConstraintHandle handle) const
{
    if (!handle.isValid())
    {
        return nullptr;
    }

    auto it = m_constraints.find(handle.index);
    if (it == m_constraints.end() || it->second.getHandle().generation != handle.generation)
    {
        return nullptr;
    }
    return &it->second;
}

void PhysicsWorld::removeConstraint(ConstraintHandle handle)
{
    if (!m_initialized || !handle.isValid())
    {
        return;
    }

    auto it = m_constraints.find(handle.index);
    if (it == m_constraints.end() || it->second.getHandle().generation != handle.generation)
    {
        return;
    }

    JPH::TwoBodyConstraint* joltConstraint = it->second.getJoltConstraint();
    if (joltConstraint)
    {
        m_physicsSystem->RemoveConstraint(joltConstraint);
    }

    m_constraints.erase(it);
}

void PhysicsWorld::removeConstraintsForBody(JPH::BodyID bodyId)
{
    if (!m_initialized || bodyId.IsInvalid())
    {
        return;
    }

    // Collect handles to remove (don't modify map during iteration)
    std::vector<ConstraintHandle> toRemove;
    for (const auto& [idx, constraint] : m_constraints)
    {
        if (constraint.getBodyA() == bodyId || constraint.getBodyB() == bodyId)
        {
            toRemove.push_back(constraint.getHandle());
        }
    }

    for (const auto& handle : toRemove)
    {
        removeConstraint(handle);
    }
}

void PhysicsWorld::checkBreakableConstraints(float deltaTime)
{
    if (!m_initialized || deltaTime <= 0.0f)
    {
        return;
    }

    for (auto& [idx, constraint] : m_constraints)
    {
        if (constraint.getBreakForce() <= 0.0f || !constraint.isEnabled())
        {
            continue;
        }

        float impulse = 0.0f;
        switch (constraint.getType())
        {
        case ConstraintType::HINGE:
            if (auto* h = constraint.asHinge())
            {
                impulse = glm::length(toGlm(h->GetTotalLambdaPosition()));
            }
            break;
        case ConstraintType::FIXED:
            if (auto* f = constraint.asFixed())
            {
                impulse = glm::length(toGlm(f->GetTotalLambdaPosition()));
            }
            break;
        case ConstraintType::DISTANCE:
            if (auto* d = constraint.asDistance())
            {
                impulse = std::abs(d->GetTotalLambdaPosition());
            }
            break;
        case ConstraintType::POINT:
            if (auto* p = constraint.asPoint())
            {
                impulse = glm::length(toGlm(p->GetTotalLambdaPosition()));
            }
            break;
        case ConstraintType::SLIDER:
            if (auto* s = constraint.asSlider())
            {
                impulse = glm::length(toGlm(JPH::Vec3(
                    s->GetTotalLambdaPosition()[0],
                    s->GetTotalLambdaPosition()[1],
                    0.0f)));
            }
            break;
        }

        float force = impulse / deltaTime;
        constraint.m_currentForce = force;

        if (force > constraint.getBreakForce())
        {
            constraint.setEnabled(false);
            Logger::info("Constraint " + std::to_string(constraint.getHandle().index) +
                         " broke (force: " + std::to_string(force) +
                         " > threshold: " + std::to_string(constraint.getBreakForce()) + ")");
        }
    }
}

std::vector<ConstraintHandle> PhysicsWorld::getConstraintHandles() const
{
    std::vector<ConstraintHandle> handles;
    handles.reserve(m_constraints.size());
    for (const auto& [idx, constraint] : m_constraints)
    {
        handles.push_back(constraint.getHandle());
    }
    return handles;
}

// ---------------------------------------------------------------------------
// Raycasting
// ---------------------------------------------------------------------------

bool PhysicsWorld::rayCast(const glm::vec3& origin, const glm::vec3& direction,
                            JPH::BodyID& outBodyId, float& outFraction) const
{
    if (!m_initialized)
    {
        return false;
    }

    JPH::RRayCast ray(toJolt(origin), toJolt(direction));
    JPH::RayCastResult result;

    bool hit = m_physicsSystem->GetNarrowPhaseQuery().CastRay(
        ray, result);

    if (hit)
    {
        outBodyId = result.mBodyID;
        outFraction = result.mFraction;
    }

    return hit;
}

bool PhysicsWorld::rayCast(const glm::vec3& origin, const glm::vec3& direction,
                            float maxDistance,
                            JPH::BodyID& outBodyId, float& outHitDistance,
                            JPH::BodyID ignoreBodyId) const
{
    if (!m_initialized || maxDistance <= 0.0f)
    {
        return false;
    }

    // Phase 10.9 Ph2: Construct the Jolt ray with `direction * maxDistance`
    // exactly once — caller hands us the unit direction and the range
    // separately, and we compute the world-unit hit distance from the
    // returned fraction here. Removes the `dir * range` then
    // `fraction * range` double-scaling pattern from the call site.
    const JPH::RRayCast ray(toJolt(origin), toJolt(direction * maxDistance));
    JPH::RayCastResult result;

    const JPH::NarrowPhaseQuery& query = m_physicsSystem->GetNarrowPhaseQuery();
    const bool hit = ignoreBodyId.IsInvalid()
        ? query.CastRay(ray, result)
        : query.CastRay(ray, result,
                        JPH::BroadPhaseLayerFilter{},
                        JPH::ObjectLayerFilter{},
                        JPH::IgnoreSingleBodyFilter{ignoreBodyId});

    if (hit)
    {
        outBodyId = result.mBodyID;
        outHitDistance = result.mFraction * maxDistance;
    }

    return hit;
}

bool PhysicsWorld::sphereCast(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float radius, float maxDistance,
                               JPH::BodyID& outBodyId, float& outHitDistance,
                               JPH::BodyID ignoreBodyId) const
{
    if (!m_initialized || maxDistance <= 0.0f || radius <= 0.0f)
    {
        return false;
    }

    // Phase 10.9 Slice 7 Ph3 — sweep a sphere of `radius` from `origin`
    // along `direction * maxDistance`. Mirrors the rayCast(maxDistance,
    // ignoreBody) shape so consumers don't have to translate fractions
    // back to world units. Jolt's RShapeCast::sFromWorldTransform takes
    // the direction as a translation vector, so we pre-multiply by the
    // range here (same convention as the rayCast overload above).
    JPH::Ref<JPH::Shape> sphere = new JPH::SphereShape(radius);
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        sphere,
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
        toJolt(direction * maxDistance));

    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

    const JPH::NarrowPhaseQuery& query = m_physicsSystem->GetNarrowPhaseQuery();
    if (ignoreBodyId.IsInvalid())
    {
        query.CastShape(cast, settings, JPH::RVec3::sZero(), collector);
    }
    else
    {
        query.CastShape(cast, settings, JPH::RVec3::sZero(), collector,
                        JPH::BroadPhaseLayerFilter{},
                        JPH::ObjectLayerFilter{},
                        JPH::IgnoreSingleBodyFilter{ignoreBodyId});
    }

    if (!collector.HadHit())
    {
        return false;
    }
    outBodyId = collector.mHit.mBodyID2;
    outHitDistance = collector.mHit.mFraction * maxDistance;
    return true;
}

void PhysicsWorld::applyImpulseAtPoint(JPH::BodyID bodyId,
                                        const glm::vec3& impulse,
                                        const glm::vec3& worldPoint)
{
    if (!m_initialized || bodyId.IsInvalid())
    {
        return;
    }

    JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    bodyInterface.AddImpulse(bodyId, toJolt(impulse), toJolt(worldPoint));
}

JPH::EMotionType PhysicsWorld::getBodyMotionType(JPH::BodyID bodyId) const
{
    if (!m_initialized || bodyId.IsInvalid())
    {
        return JPH::EMotionType::Static;
    }

    const JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
    return bodyInterface.GetMotionType(bodyId);
}

} // namespace Vestige
