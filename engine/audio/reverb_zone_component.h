// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reverb_zone_component.h
/// @brief AX2 R3 — a reverb zone attached to an entity for spatial reverb.
#pragma once

#include "audio/audio_reverb.h"
#include "scene/component.h"

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Marks a region of the world as a reverb zone.
///
/// The zone's *position* is the owning entity's transform — there is no
/// duplicate position field. Its shape is a sphere-with-falloff: full reverb
/// inside `coreRadius`, decaying linearly across `falloffBand`, silent beyond
/// (`computeReverbZoneWeight`). `ReverbSystem` (PostCamera, before AudioSystem)
/// picks the highest-weighted zone at the listener each frame and drives the
/// engine's single reverb aux slot from it — reverb is per-room (one slot),
/// O(1) in the number of sources.
///
/// `irPath` unifies the two backends: when it is non-empty *and* the engine
/// selected the convolution backend, the zone convolves that impulse-response
/// file; otherwise the zone falls back to the parametric `preset`.
class ReverbZoneComponent : public Component
{
public:
    ReverbZoneComponent() = default;

    /// @brief Full-weight core radius (m). Inside this the zone is at weight 1.
    float coreRadius = 5.0f;

    /// @brief Linear-falloff band thickness (m) outside the core. Zero → a hard
    ///        step from full reverb to dry at `coreRadius`.
    float falloffBand = 2.0f;

    /// @brief Parametric character used on the parametric backend (or as the
    ///        convolution fallback when `irPath` is empty).
    ReverbPreset preset = ReverbPreset::Generic;

    /// @brief Optional impulse-response path (relative, sandbox-validated on
    ///        load). Empty ⇒ parametric only.
    std::string irPath;

    /// @brief Slot wet gain [0,1] when this zone wins. Scaled by the listener's
    ///        in-zone depth so the tail fades at the zone edge, not steps.
    float wetGain = 0.30f;

    /// @brief Deep copy (config only — no engine/AL state to duplicate).
    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<ReverbZoneComponent>(*this);
    }
};

} // namespace Vestige
