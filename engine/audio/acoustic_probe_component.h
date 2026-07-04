// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file acoustic_probe_component.h
/// @brief AX3 B1 — an acoustic probe: a point in the world that carries a
///        baked impulse response for reverb pre-baking.
#pragma once

#include "scene/component.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Marks a listening point in the world for acoustic pre-baking (AX3).
///
/// Like `ReverbZoneComponent`, the probe's *position* is the owning entity's
/// transform — there is no duplicate position field. Unlike a reverb zone
/// (which is authored by hand), a probe carries a **baked** impulse response:
/// the offline image-source baker (B2/B3) walks the static geometry, computes
/// the room's echo at this point, and writes a `.wav` sidecar whose path lands
/// in `bakedIrPath`. At runtime (B4) `ReverbSystem` picks the nearest probe and
/// convolves its baked IR, preferring it over any authored zone.
///
/// The probe's reach is a scalar `influenceRadius` (a deliberate simplification
/// of `LightProbe`'s influence AABB — probes are point-like), used later by the
/// runtime blend. Baking has not run until `bakedIrPath` is non-empty.
class AcousticProbeComponent : public Component
{
public:
    AcousticProbeComponent() = default;

    /// @brief Radius (m) over which this probe's baked reverb applies. Beyond
    ///        it, a nearer probe (or an authored zone) wins.
    float influenceRadius = 10.0f;

    /// @brief Baked impulse-response path (relative, sandbox-validated on load),
    ///        written by the offline baker (B3). Empty ⇒ not yet baked.
    std::string bakedIrPath;

    /// @brief Deep copy (config only — no engine/AL state to duplicate).
    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<AcousticProbeComponent>(*this);
    }
};

/// @brief Index of the probe position nearest `listener`, or -1 if the list is
///        empty. Pure and header-only so the placement tests (B1) and the
///        runtime lookup (B4) share one definition. Ties resolve to the lower
///        index, keeping probe selection deterministic across a bake.
inline int nearestAcousticProbeIndex(const std::vector<glm::vec3>& probePositions,
                                     const glm::vec3& listener)
{
    int best = -1;
    float bestDistSq = 0.0f;
    for (int i = 0; i < static_cast<int>(probePositions.size()); ++i)
    {
        const glm::vec3 d = probePositions[static_cast<size_t>(i)] - listener;
        const float distSq = glm::dot(d, d);
        if (best < 0 || distSq < bestDistSq)
        {
            best = i;
            bestDistSq = distSq;
        }
    }
    return best;
}

} // namespace Vestige
