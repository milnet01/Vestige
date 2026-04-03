/// @file ragdoll.cpp
/// @brief Ragdoll system implementation.

#include "physics/ragdoll.h"
#include "physics/jolt_helpers.h"
#include "physics/physics_layers.h"
#include "core/logger.h"

#include <string>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>


namespace Vestige
{

uint32_t Ragdoll::s_nextGroupId = 1;

Ragdoll::~Ragdoll()
{
    destroy();
}

bool Ragdoll::create(PhysicsWorld& world, const Skeleton& skeleton,
                     const RagdollPreset& preset,
                     const std::vector<glm::mat4>& boneWorldMatrices)
{
    if (m_joltRagdoll)
    {
        Logger::warning("Ragdoll already created — destroy first");
        return false;
    }

    if (!world.isInitialized() || !world.getSystem())
    {
        Logger::error("PhysicsWorld not initialized");
        return false;
    }

    m_world = &world;
    m_skeleton = &skeleton;

    int jointCount = skeleton.getJointCount();

    // Build joint-to-part mapping
    m_jointToPartIndex.assign(static_cast<size_t>(jointCount), -1);
    m_partToJointIndex.clear();

    for (const auto& jointDef : preset.joints)
    {
        int idx = skeleton.findJoint(jointDef.boneName);
        if (idx >= 0)
        {
            m_jointToPartIndex[static_cast<size_t>(idx)] =
                static_cast<int>(m_partToJointIndex.size());
            m_partToJointIndex.push_back(idx);
        }
        else
        {
            Logger::warning("Ragdoll preset bone '" + jointDef.boneName + "' not found in skeleton");
        }
    }

    if (m_partToJointIndex.empty())
    {
        Logger::error("No matching bones found between preset and skeleton");
        return false;
    }

    // Build Jolt skeleton (only for bones in the preset)
    m_joltSkeleton = new JPH::Skeleton();
    for (size_t i = 0; i < m_partToJointIndex.size(); ++i)
    {
        int vestigeIdx = m_partToJointIndex[i];
        const auto& joint = skeleton.m_joints[static_cast<size_t>(vestigeIdx)];

        // Find parent in the ragdoll part list
        int parentPartIdx = -1;
        int parentJoint = joint.parentIndex;
        while (parentJoint >= 0)
        {
            int partIdx = m_jointToPartIndex[static_cast<size_t>(parentJoint)];
            if (partIdx >= 0)
            {
                parentPartIdx = partIdx;
                break;
            }
            parentJoint = skeleton.m_joints[static_cast<size_t>(parentJoint)].parentIndex;
        }

        m_joltSkeleton->AddJoint(JPH::String(joint.name), parentPartIdx);
    }

    // Build settings
    m_settings = buildSettings(m_joltSkeleton.GetPtr(), skeleton, preset, boneWorldMatrices);
    if (!m_settings)
    {
        Logger::error("Failed to build ragdoll settings");
        return false;
    }

    // Stabilize mass distribution
    if (!m_settings->Stabilize())
    {
        Logger::warning("Ragdoll stabilization failed — may have jitter");
    }

    // Create pose matrices for collision disabling
    auto partCount = static_cast<int>(m_partToJointIndex.size());
    std::vector<JPH::Mat44> jointMatrices(static_cast<size_t>(partCount));
    for (int i = 0; i < partCount; ++i)
    {
        int vestigeIdx = m_partToJointIndex[static_cast<size_t>(i)];
        if (vestigeIdx >= 0 &&
            static_cast<size_t>(vestigeIdx) < boneWorldMatrices.size())
        {
            jointMatrices[static_cast<size_t>(i)] =
                toJolt(boneWorldMatrices[static_cast<size_t>(vestigeIdx)]);
        }
        else
        {
            jointMatrices[static_cast<size_t>(i)] = JPH::Mat44::sIdentity();
        }
    }

    m_settings->DisableParentChildCollisions(jointMatrices.data(), 0.05f);

    // Create the runtime ragdoll
    uint32_t groupId = s_nextGroupId++;
    m_joltRagdoll = m_settings->CreateRagdoll(
        groupId, 0, m_world->getSystem());

    if (!m_joltRagdoll)
    {
        Logger::error("Failed to create Jolt ragdoll (body pool exhausted?)");
        m_settings = nullptr;
        return false;
    }

    m_state = RagdollState::INACTIVE;
    Logger::info("Ragdoll created with " + std::to_string(partCount) + " parts");
    return true;
}

void Ragdoll::destroy()
{
    if (m_joltRagdoll)
    {
        if (m_state != RagdollState::INACTIVE)
        {
            m_joltRagdoll->RemoveFromPhysicsSystem();
        }
        m_joltRagdoll = nullptr;  // Release ref — Jolt ref-counting handles deletion
    }

    m_settings = nullptr;
    m_joltSkeleton = nullptr;
    m_jointToPartIndex.clear();
    m_partToJointIndex.clear();
    m_world = nullptr;
    m_skeleton = nullptr;
    m_state = RagdollState::INACTIVE;
}

void Ragdoll::activate()
{
    if (!m_joltRagdoll || m_state != RagdollState::INACTIVE)
        return;

    m_joltRagdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
    m_state = RagdollState::ACTIVE;
}

void Ragdoll::deactivate()
{
    if (!m_joltRagdoll || m_state == RagdollState::INACTIVE)
        return;

    m_joltRagdoll->RemoveFromPhysicsSystem();
    m_state = RagdollState::INACTIVE;
}

void Ragdoll::driveToPose(const std::vector<glm::mat4>& targetWorldMatrices, float deltaTime)
{
    if (!m_joltRagdoll || m_state == RagdollState::INACTIVE)
        return;

    // Build Jolt skeleton pose from target matrices
    JPH::SkeletonPose pose;
    toJoltPose(targetWorldMatrices, pose);
    pose.CalculateJointMatrices();

    if (m_state == RagdollState::KINEMATIC)
    {
        m_joltRagdoll->DriveToPoseUsingKinematics(pose, deltaTime);
    }
    else
    {
        // Use motors for powered mode
        m_joltRagdoll->DriveToPoseUsingMotors(pose);
        m_state = RagdollState::POWERED;
    }
}

void Ragdoll::setMotorStrength(float strength)
{
    m_motorStrength = glm::clamp(strength, 0.0f, 1.0f);

    if (!m_joltRagdoll)
        return;

    // Adjust motor settings on all constraints
    auto constraintCount = static_cast<int>(m_joltRagdoll->GetConstraintCount());
    for (int i = 0; i < constraintCount; ++i)
    {
        auto* constraint = m_joltRagdoll->GetConstraint(i);
        if (!constraint)
            continue;

        if (constraint->GetSubType() != JPH::EConstraintSubType::SwingTwist)
            continue;

        auto* swingTwist = static_cast<JPH::SwingTwistConstraint*>(constraint);
        if (swingTwist)
        {
            // Scale max torque by motor strength
            float baseTorque = 50.0f;
            float torque = baseTorque * m_motorStrength;
            swingTwist->GetSwingMotorSettings().mMaxTorqueLimit = torque;
            swingTwist->GetSwingMotorSettings().mMinTorqueLimit = -torque;
            swingTwist->GetTwistMotorSettings().mMaxTorqueLimit = torque;
            swingTwist->GetTwistMotorSettings().mMinTorqueLimit = -torque;

            if (m_motorStrength > 0.001f)
            {
                swingTwist->SetSwingMotorState(JPH::EMotorState::Position);
                swingTwist->SetTwistMotorState(JPH::EMotorState::Position);
            }
            else
            {
                swingTwist->SetSwingMotorState(JPH::EMotorState::Off);
                swingTwist->SetTwistMotorState(JPH::EMotorState::Off);
            }
        }
    }
}

void Ragdoll::setPartMotionType(int jointIndex, bool dynamic)
{
    if (!m_joltRagdoll || !m_world)
        return;

    if (jointIndex < 0 || static_cast<size_t>(jointIndex) >= m_jointToPartIndex.size())
        return;

    int partIdx = m_jointToPartIndex[static_cast<size_t>(jointIndex)];
    if (partIdx < 0)
        return;

    JPH::BodyID bodyId = m_joltRagdoll->GetBodyID(partIdx);
    auto& bi = m_world->getBodyInterface();

    if (dynamic)
    {
        bi.SetMotionType(bodyId, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
    }
    else
    {
        bi.SetMotionType(bodyId, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
    }
}

void Ragdoll::getBoneMatrices(std::vector<glm::mat4>& outMatrices) const
{
    if (!m_joltRagdoll || !m_skeleton)
        return;

    int jointCount = m_skeleton->getJointCount();
    outMatrices.resize(static_cast<size_t>(jointCount), glm::mat4(1.0f));

    // Get ragdoll pose
    auto partCount = static_cast<int>(m_partToJointIndex.size());
    JPH::RVec3 rootOffset;
    std::vector<JPH::Mat44> joltMatrices(static_cast<size_t>(partCount));
    m_joltRagdoll->GetPose(rootOffset, joltMatrices.data());

    // Convert ragdoll body transforms to bone matrices
    // For bones that have ragdoll bodies, use the physics transform
    // For bones without bodies, interpolate from parent
    for (int i = 0; i < jointCount; ++i)
    {
        int partIdx = m_jointToPartIndex[static_cast<size_t>(i)];
        if (partIdx >= 0 && partIdx < partCount)
        {
            // This body has a ragdoll part — use its transform
            glm::mat4 worldTransform = toGlm(joltMatrices[static_cast<size_t>(partIdx)]);

            // Apply inverse bind matrix to get the bone matrix for skinning
            const auto& joint = m_skeleton->m_joints[static_cast<size_t>(i)];
            outMatrices[static_cast<size_t>(i)] = worldTransform * joint.inverseBindMatrix;
        }
        else
        {
            // No ragdoll body — inherit from parent ragdoll body
            const auto& joint = m_skeleton->m_joints[static_cast<size_t>(i)];
            int parent = joint.parentIndex;
            if (parent >= 0)
            {
                // Use parent's world transform + local bind transform
                int parentPart = m_jointToPartIndex[static_cast<size_t>(parent)];
                if (parentPart >= 0 && parentPart < partCount)
                {
                    glm::mat4 parentWorld = toGlm(joltMatrices[static_cast<size_t>(parentPart)]);
                    glm::mat4 localBind = joint.localBindTransform;
                    glm::mat4 worldTransform = parentWorld * localBind;
                    outMatrices[static_cast<size_t>(i)] = worldTransform * joint.inverseBindMatrix;
                }
                else
                {
                    // Parent also has no body — use bind pose
                    outMatrices[static_cast<size_t>(i)] = glm::mat4(1.0f);
                }
            }
            else
            {
                outMatrices[static_cast<size_t>(i)] = glm::mat4(1.0f);
            }
        }
    }
}

glm::vec3 Ragdoll::getRootPosition() const
{
    if (!m_joltRagdoll || m_partToJointIndex.empty())
        return glm::vec3(0.0f);

    JPH::RVec3 pos;
    JPH::Quat rot;
    m_joltRagdoll->GetRootTransform(pos, rot);
    return glm::vec3(static_cast<float>(pos.GetX()),
                     static_cast<float>(pos.GetY()),
                     static_cast<float>(pos.GetZ()));
}

glm::quat Ragdoll::getRootRotation() const
{
    if (!m_joltRagdoll || m_partToJointIndex.empty())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    JPH::RVec3 pos;
    JPH::Quat rot;
    m_joltRagdoll->GetRootTransform(pos, rot);
    return toGlm(rot);
}

bool Ragdoll::isPhysicsActive() const
{
    if (!m_joltRagdoll)
        return false;
    return m_joltRagdoll->IsActive();
}

int Ragdoll::getBodyCount() const
{
    if (!m_joltRagdoll)
        return 0;
    return static_cast<int>(m_joltRagdoll->GetBodyCount());
}

JPH::BodyID Ragdoll::getBodyIdForJoint(int jointIndex) const
{
    if (!m_joltRagdoll)
        return JPH::BodyID();

    if (jointIndex < 0 || static_cast<size_t>(jointIndex) >= m_jointToPartIndex.size())
        return JPH::BodyID();

    int partIdx = m_jointToPartIndex[static_cast<size_t>(jointIndex)];
    if (partIdx < 0)
        return JPH::BodyID();

    return m_joltRagdoll->GetBodyID(partIdx);
}

void Ragdoll::applyImpulseToJoint(int jointIndex, const glm::vec3& impulse,
                                   const glm::vec3& worldPoint)
{
    if (!m_world)
        return;

    JPH::BodyID bodyId = getBodyIdForJoint(jointIndex);
    if (bodyId.IsInvalid())
        return;

    m_world->applyImpulseAtPoint(bodyId, impulse, worldPoint);
}

void Ragdoll::setInitialVelocities(const std::vector<glm::vec3>& boneVelocities)
{
    if (!m_joltRagdoll || !m_world)
        return;

    auto& bi = m_world->getBodyInterface();

    for (size_t i = 0; i < m_partToJointIndex.size(); ++i)
    {
        int vestigeIdx = m_partToJointIndex[i];
        if (vestigeIdx >= 0 && static_cast<size_t>(vestigeIdx) < boneVelocities.size())
        {
            JPH::BodyID bodyId = m_joltRagdoll->GetBodyID(static_cast<int>(i));
            bi.SetLinearVelocity(bodyId, toJolt(boneVelocities[static_cast<size_t>(vestigeIdx)]));
        }
    }
}

// --- Private helpers ---

JPH::Ref<JPH::Skeleton> Ragdoll::buildJoltSkeleton(const Skeleton& skeleton) const
{
    JPH::Ref<JPH::Skeleton> jphSkel = new JPH::Skeleton();
    for (int i = 0; i < skeleton.getJointCount(); ++i)
    {
        const auto& joint = skeleton.m_joints[static_cast<size_t>(i)];
        jphSkel->AddJoint(JPH::String(joint.name), joint.parentIndex);
    }
    return jphSkel;
}

JPH::Ref<JPH::RagdollSettings> Ragdoll::buildSettings(
    const JPH::Skeleton* jphSkeleton,
    const Skeleton& skeleton,
    const RagdollPreset& preset,
    const std::vector<glm::mat4>& boneWorldMatrices) const
{
    JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
    settings->mSkeleton = const_cast<JPH::Skeleton*>(jphSkeleton);

    auto partCount = static_cast<int>(m_partToJointIndex.size());
    settings->mParts.resize(static_cast<size_t>(partCount));

    for (int pi = 0; pi < partCount; ++pi)
    {
        int vestigeIdx = m_partToJointIndex[static_cast<size_t>(pi)];
        auto& part = settings->mParts[static_cast<size_t>(pi)];

        // Find the matching preset joint def
        const RagdollJointDef* def = nullptr;
        for (const auto& j : preset.joints)
        {
            if (j.boneName == skeleton.m_joints[static_cast<size_t>(vestigeIdx)].name)
            {
                def = &j;
                break;
            }
        }

        if (!def)
            continue;

        // Shape
        JPH::Ref<JPH::Shape> shape = createShape(*def);
        if (!shape)
            continue;

        // If there's a shape offset, wrap in RotatedTranslatedShape
        if (glm::length(def->shapeOffset) > 0.001f)
        {
            shape = new JPH::RotatedTranslatedShape(
                toJolt(def->shapeOffset), JPH::Quat::sIdentity(), shape);
        }

        part.SetShape(shape);

        // Position and rotation from bone world matrix
        if (static_cast<size_t>(vestigeIdx) < boneWorldMatrices.size())
        {
            const glm::mat4& boneWorld = boneWorldMatrices[static_cast<size_t>(vestigeIdx)];

            // Decompose matrix into position and rotation
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(boneWorld, scale, rotation, translation, skew, perspective);

            part.mPosition = JPH::RVec3(
                translation.x,
                translation.y,
                translation.z);
            part.mRotation = toJolt(rotation);
        }

        part.mMotionType = JPH::EMotionType::Dynamic;
        part.mObjectLayer = static_cast<JPH::ObjectLayer>(ObjectLayers::DYNAMIC);
        part.mMassPropertiesOverride.mMass = def->mass;
        part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;

        // Constraint to parent (SwingTwist)
        int jphParent = jphSkeleton->GetJoint(pi).mParentJointIndex;
        if (jphParent >= 0)
        {
            auto* constraint = new JPH::SwingTwistConstraintSettings();
            constraint->mSpace = JPH::EConstraintSpace::WorldSpace;

            // Constraint position at the bone's world position (the joint)
            constraint->mPosition1 = part.mPosition;
            constraint->mPosition2 = part.mPosition;

            // Twist axis along the bone direction (use bone's local X axis)
            JPH::Vec3 twistAxis = part.mRotation * JPH::Vec3::sAxisX();
            JPH::Vec3 planeAxis = part.mRotation * JPH::Vec3::sAxisY();

            constraint->mTwistAxis1 = twistAxis;
            constraint->mTwistAxis2 = twistAxis;
            constraint->mPlaneAxis1 = planeAxis;
            constraint->mPlaneAxis2 = planeAxis;

            constraint->mNormalHalfConeAngle = def->normalHalfCone;
            constraint->mPlaneHalfConeAngle = def->planeHalfCone;
            constraint->mTwistMinAngle = def->twistMin;
            constraint->mTwistMaxAngle = def->twistMax;
            constraint->mMaxFrictionTorque = def->maxFrictionTorque;

            // Motor settings for powered ragdoll
            constraint->mSwingMotorSettings.mSpringSettings.mFrequency = 2.0f;
            constraint->mSwingMotorSettings.mSpringSettings.mDamping = 1.0f;
            constraint->mSwingMotorSettings.mMinTorqueLimit = -50.0f;
            constraint->mSwingMotorSettings.mMaxTorqueLimit = 50.0f;

            constraint->mTwistMotorSettings.mSpringSettings.mFrequency = 2.0f;
            constraint->mTwistMotorSettings.mSpringSettings.mDamping = 1.0f;
            constraint->mTwistMotorSettings.mMinTorqueLimit = -50.0f;
            constraint->mTwistMotorSettings.mMaxTorqueLimit = 50.0f;

            part.mToParent = constraint;
        }
    }

    settings->CalculateBodyIndexToConstraintIndex();
    settings->CalculateConstraintIndexToBodyIdxPair();

    return settings;
}

JPH::Ref<JPH::Shape> Ragdoll::createShape(const RagdollJointDef& def) const
{
    switch (def.shapeType)
    {
        case RagdollShapeType::CAPSULE:
        {
            float radius = def.shapeSize.x;
            float halfHeight = def.shapeSize.y;
            radius = std::max(radius, 0.01f);
            halfHeight = std::max(halfHeight, 0.01f);
            return new JPH::CapsuleShape(halfHeight, radius);
        }

        case RagdollShapeType::SPHERE:
        {
            float radius = std::max(def.shapeSize.x, 0.01f);
            return new JPH::SphereShape(radius);
        }

        case RagdollShapeType::BOX:
        {
            glm::vec3 halfExtents = glm::max(def.shapeSize, glm::vec3(0.05f));
            return new JPH::BoxShape(toJolt(halfExtents));
        }
    }

    return nullptr;
}

void Ragdoll::toJoltPose(const std::vector<glm::mat4>& boneWorldMatrices,
                          JPH::SkeletonPose& outPose) const
{
    outPose.SetSkeleton(m_joltSkeleton.GetPtr());

    auto partCount = static_cast<int>(m_partToJointIndex.size());
    auto& joints = outPose.GetJoints();
    joints.resize(static_cast<size_t>(partCount));

    for (int i = 0; i < partCount; ++i)
    {
        int vestigeIdx = m_partToJointIndex[static_cast<size_t>(i)];
        if (vestigeIdx >= 0 &&
            static_cast<size_t>(vestigeIdx) < boneWorldMatrices.size())
        {
            JPH::Mat44 joltMat = toJolt(boneWorldMatrices[static_cast<size_t>(vestigeIdx)]);
            joints[static_cast<size_t>(i)].FromMatrix(joltMat);
        }
    }

    outPose.SetRootOffset(JPH::RVec3::sZero());
}

} // namespace Vestige
