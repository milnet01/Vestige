// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file dismemberment_zones.h
/// @brief Dismemberment zone definitions and damage tracking.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class Skeleton;

/// @brief Defines a severable body region mapped to a skeleton bone.
struct DismembermentZone
{
    int boneIndex = -1;                ///< Skeleton bone this zone maps to
    glm::vec3 cutPlaneNormal;          ///< Cut direction (bone-perpendicular)
    float cutPlaneOffset = 0.0f;       ///< Offset along bone axis from joint
    float health = 100.0f;            ///< Current hit points
    float maxHealth = 100.0f;         ///< Threshold for severance
    float damageVisualScale = 0.0f;   ///< 0 = pristine, 1 = fully damaged
    int capMeshIndex = -1;            ///< Index into pre-modeled wound cap meshes
    int stumpMeshIndex = -1;          ///< Index into body-side stump meshes
    std::vector<int> childZones;      ///< Severing parent auto-severs children
    bool severed = false;             ///< Whether this zone has been severed
    std::string zoneName;             ///< Human-readable name (e.g., "LeftArm")
};

/// @brief Manages a set of dismemberment zones for a skinned character.
///
/// Tracks damage per zone, cascades severance to child zones,
/// and provides factory methods for standard humanoid configurations.
class DismembermentZones
{
public:
    DismembermentZones() = default;

    /// @brief Adds a dismemberment zone.
    /// @return Index of the added zone.
    int addZone(const DismembermentZone& zone);

    /// @brief Applies damage to a zone.
    /// @param zoneIndex Zone to damage.
    /// @param damage Damage amount to apply.
    /// @return True if the zone was severed by this damage.
    bool applyDamage(int zoneIndex, float damage);

    /// @brief Returns true if the zone has been severed.
    bool isZoneSevered(int zoneIndex) const;

    /// @brief Finds the zone mapped to a specific bone.
    /// @return Zone index, or -1 if no zone for this bone.
    int findZoneForBone(int boneIndex) const;

    /// @brief Gets the number of zones.
    int getZoneCount() const { return static_cast<int>(m_zones.size()); }

    /// @brief Access a zone by index.
    const DismembermentZone& getZone(int index) const;
    DismembermentZone& getZone(int index);

    /// @brief Gets all zones.
    const std::vector<DismembermentZone>& getZones() const { return m_zones; }

    /// @brief Resets all zones to full health and unsevered state.
    void reset();

    /// @brief Creates default humanoid dismemberment zones.
    /// Expects standard bone names (LeftUpperArm, RightUpperArm, LeftUpperLeg, etc.).
    /// @param skeleton The skeleton to create zones for.
    /// @return Configured DismembermentZones.
    static DismembermentZones createHumanoid(const Skeleton& skeleton);

private:
    /// @brief Recursively severs child zones.
    void severChildren(int zoneIndex);

    std::vector<DismembermentZone> m_zones;
    DismembermentZone m_dummyZone;  ///< Per-instance dummy for out-of-bounds access
};

} // namespace Vestige
