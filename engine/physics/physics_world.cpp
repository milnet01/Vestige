/// @file physics_world.cpp
/// @brief Jolt PhysicsSystem wrapper implementation.
#include "physics/physics_world.h"
#include "physics/jolt_helpers.h"
#include "core/logger.h"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

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

} // namespace Vestige
