/// @file dismemberment_zones.cpp
/// @brief Dismemberment zone management implementation.

#include "physics/dismemberment_zones.h"
#include "animation/skeleton.h"
#include "core/logger.h"

#include <algorithm>
#include <string>

namespace Vestige
{

int DismembermentZones::addZone(const DismembermentZone& zone)
{
    int index = static_cast<int>(m_zones.size());
    m_zones.push_back(zone);
    return index;
}

bool DismembermentZones::applyDamage(int zoneIndex, float damage)
{
    if (zoneIndex < 0 || static_cast<size_t>(zoneIndex) >= m_zones.size())
        return false;

    auto& zone = m_zones[static_cast<size_t>(zoneIndex)];
    if (zone.severed)
        return false;

    zone.health -= damage;
    zone.damageVisualScale = 1.0f - std::max(zone.health / zone.maxHealth, 0.0f);

    if (zone.health <= 0.0f)
    {
        zone.severed = true;
        zone.health = 0.0f;
        zone.damageVisualScale = 1.0f;

        // Cascade to children
        severChildren(zoneIndex);

        Logger::info("Zone '" + zone.zoneName + "' severed (bone " + std::to_string(zone.boneIndex) + ")");
        return true;
    }

    return false;
}

bool DismembermentZones::isZoneSevered(int zoneIndex) const
{
    if (zoneIndex < 0 || static_cast<size_t>(zoneIndex) >= m_zones.size())
        return false;
    return m_zones[static_cast<size_t>(zoneIndex)].severed;
}

int DismembermentZones::findZoneForBone(int boneIndex) const
{
    for (size_t i = 0; i < m_zones.size(); ++i)
    {
        if (m_zones[i].boneIndex == boneIndex)
            return static_cast<int>(i);
    }
    return -1;
}

const DismembermentZone& DismembermentZones::getZone(int index) const
{
    static const DismembermentZone s_invalidZone{};
    if (index < 0 || static_cast<size_t>(index) >= m_zones.size())
        return s_invalidZone;
    return m_zones[static_cast<size_t>(index)];
}

DismembermentZone& DismembermentZones::getZone(int index)
{
    // Return a per-instance dummy zone instead of a shared mutable static,
    // so callers that accidentally modify the return value don't corrupt
    // future "invalid zone" queries across different instances.
    m_dummyZone = DismembermentZone{};
    if (index < 0 || static_cast<size_t>(index) >= m_zones.size())
        return m_dummyZone;
    return m_zones[static_cast<size_t>(index)];
}

void DismembermentZones::reset()
{
    for (auto& zone : m_zones)
    {
        zone.health = zone.maxHealth;
        zone.damageVisualScale = 0.0f;
        zone.severed = false;
    }
}

DismembermentZones DismembermentZones::createHumanoid(const Skeleton& skeleton)
{
    DismembermentZones zones;

    // Helper to add a zone if the bone exists
    auto tryAddZone = [&](const std::string& boneName, const std::string& zoneName,
                          const glm::vec3& cutNormal, float hp,
                          const std::vector<std::string>& /*childBones*/ = {}) -> int
    {
        int boneIdx = skeleton.findJoint(boneName);
        if (boneIdx < 0)
            return -1;

        DismembermentZone zone;
        zone.boneIndex = boneIdx;
        zone.zoneName = zoneName;
        zone.cutPlaneNormal = cutNormal;
        zone.health = hp;
        zone.maxHealth = hp;
        return zones.addZone(zone);
    };

    // Standard humanoid zones: arms, legs, head
    int leftUpperArm = tryAddZone("LeftUpperArm", "LeftUpperArm",
                                   glm::vec3(1, 0, 0), 80.0f);
    int leftLowerArm = tryAddZone("LeftLowerArm", "LeftLowerArm",
                                   glm::vec3(1, 0, 0), 60.0f);
    int leftHand = tryAddZone("LeftHand", "LeftHand",
                               glm::vec3(1, 0, 0), 40.0f);

    int rightUpperArm = tryAddZone("RightUpperArm", "RightUpperArm",
                                    glm::vec3(-1, 0, 0), 80.0f);
    int rightLowerArm = tryAddZone("RightLowerArm", "RightLowerArm",
                                    glm::vec3(-1, 0, 0), 60.0f);
    int rightHand = tryAddZone("RightHand", "RightHand",
                                glm::vec3(-1, 0, 0), 40.0f);

    int leftUpperLeg = tryAddZone("LeftUpperLeg", "LeftUpperLeg",
                                   glm::vec3(0, -1, 0), 100.0f);
    int leftLowerLeg = tryAddZone("LeftLowerLeg", "LeftLowerLeg",
                                   glm::vec3(0, -1, 0), 80.0f);
    int leftFoot = tryAddZone("LeftFoot", "LeftFoot",
                               glm::vec3(0, -1, 0), 50.0f);

    int rightUpperLeg = tryAddZone("RightUpperLeg", "RightUpperLeg",
                                    glm::vec3(0, -1, 0), 100.0f);
    int rightLowerLeg = tryAddZone("RightLowerLeg", "RightLowerLeg",
                                    glm::vec3(0, -1, 0), 80.0f);
    int rightFoot = tryAddZone("RightFoot", "RightFoot",
                                glm::vec3(0, -1, 0), 50.0f);

    tryAddZone("Head", "Head", glm::vec3(0, 1, 0), 120.0f);

    // Set up child zone hierarchies
    auto setChildren = [&](int parent, const std::vector<int>& children)
    {
        if (parent < 0)
            return;
        for (int child : children)
        {
            if (child >= 0)
            {
                zones.getZone(parent).childZones.push_back(child);
            }
        }
    };

    setChildren(leftUpperArm, {leftLowerArm, leftHand});
    setChildren(leftLowerArm, {leftHand});
    setChildren(rightUpperArm, {rightLowerArm, rightHand});
    setChildren(rightLowerArm, {rightHand});
    setChildren(leftUpperLeg, {leftLowerLeg, leftFoot});
    setChildren(leftLowerLeg, {leftFoot});
    setChildren(rightUpperLeg, {rightLowerLeg, rightFoot});
    setChildren(rightLowerLeg, {rightFoot});

    return zones;
}

void DismembermentZones::severChildren(int zoneIndex)
{
    const auto& zone = m_zones[static_cast<size_t>(zoneIndex)];
    for (int childIdx : zone.childZones)
    {
        if (childIdx >= 0 && static_cast<size_t>(childIdx) < m_zones.size())
        {
            auto& child = m_zones[static_cast<size_t>(childIdx)];
            if (!child.severed)
            {
                child.severed = true;
                child.health = 0.0f;
                child.damageVisualScale = 1.0f;
                severChildren(childIdx);
            }
        }
    }
}

} // namespace Vestige
