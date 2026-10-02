// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ambient_zone_component.h
/// @brief 3D_E-S0016 — an ambient-sound zone attached to an entity.
#pragma once

#include "audio/audio_ambient.h"
#include "scene/component.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Marks a region of the world as an ambient zone.
///
/// The zone's position is the owning entity's world position, as a
/// `ReverbZoneComponent`'s is. `AmbientSystem` plays `zone.clipPath` as a
/// looping bed whose volume follows the listener's distance, the time of
/// day and higher-priority zones, and scatters `oneShotClips` inside the
/// zone's core radius. An empty bed clip leaves one-shots working; an empty
/// one-shot list disables them. Design: docs/specs/3D_E-S0016-ambient-soundscapes.md.
class AmbientZoneComponent : public Component
{
public:
    AmbientZoneComponent() = default;

    /// @brief Bed clip, core radius, falloff band, maximum volume, priority.
    AmbientZone zone;

    /// @brief Time windows the zone plays in; bit `n` is the
    ///        `TimeOfDayWindow` whose value is `n`.
    std::uint8_t windows = kAllTimeOfDayWindows;

    /// @brief Clips one of which is played at random inside the zone.
    std::vector<std::string> oneShotClips;

    /// @brief One-shot volume before the time-of-day weight (0..1).
    float oneShotVolume = 1.0f;

    /// @brief Bounds of the random interval between one-shots (s).
    float minIntervalSeconds = 15.0f;
    float maxIntervalSeconds = 45.0f;

    /// @brief Deep copy (config only — no engine state to duplicate).
    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<AmbientZoneComponent>(*this);
    }
};

} // namespace Vestige
