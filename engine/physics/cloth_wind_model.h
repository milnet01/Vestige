// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file cloth_wind_model.h
/// @brief Backend-agnostic cloth wind model (Phase 10.9 Sh4b).
///
/// Owns the gust state machine + per-frame FBM/turbulence precompute that used
/// to live inside the CPU `ClothSimulator`. Both the CPU backend and the GPU
/// `GpuClothSimulator` own one instance, so given the same seed and the same
/// per-frame `dt` sequence they produce **identical** wind inputs — the
/// property the Sh4b FULL tier and the future Cl1 CPU↔GPU parity harness need.
///
/// What this class does NOT do: the per-vertex aerodynamic-drag *apply*. That
/// stays in each backend's solver (CPU walks `m_indices` in order; GPU walks
/// colour-grouped triangles in a compute shader). This class only owns the
/// wind state and the cached noise arrays both solvers read.
#pragma once

#include "physics/cloth_solver_backend.h"  // ClothWindQuality
#include "utils/deterministic_lcg_rng.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Vestige
{

/// @brief Gust state machine + per-frame wind-noise precompute shared by every
///        cloth backend.
class ClothWindModel
{
public:
    // --- configuration (live-tunable) ---

    /// @brief Sets wind direction (normalised internally) + strength.
    void setWind(const glm::vec3& direction, float strength);
    void setWindQuality(ClothWindQuality quality) { m_quality = quality; }
    /// @brief Sets the aerodynamic drag coefficient. Negative clamps to zero.
    void setDragCoefficient(float drag);

    glm::vec3        windDirection()   const { return m_direction; }
    float            windStrength()    const { return m_strength; }
    /// @brief Bare direction × strength (no gust / flutter folding).
    glm::vec3        windVelocity()    const { return m_direction * m_strength; }
    float            dragCoefficient() const { return m_dragCoeff; }
    ClothWindQuality windQuality()     const { return m_quality; }

    // --- gust state machine ---

    /// @brief Seeds the RNG and initialises the gust state to the opening calm
    ///        period. Mirrors the gust block of `ClothSimulator::initialize`,
    ///        preserving the exact RNG call order so wind timing is unchanged.
    void seedAndInit(uint32_t seed);

    /// @brief Zeroes the gust / direction state + elapsed time without
    ///        reseeding the RNG. Mirrors the gust block of `ClothSimulator::reset`.
    void reset();

    /// @brief Advances elapsed time + the gust / direction state machines by dt.
    ///        Call once per frame before `precompute()`.
    void advance(float dt);

    float gustCurrent() const { return m_gustCurrent; }
    float elapsed()     const { return m_elapsed; }

    // --- per-frame precompute (call once per frame, after advance) ---

    /// @brief Recomputes the flutter scalar and, at FULL quality, the
    ///        per-particle FBM perturbation + per-triangle turbulence arrays.
    ///        Clears the precomputed flag (early-out) when wind is off or the
    ///        quality is SIMPLE.
    /// @param inverseMasses Per-particle inverse mass (0 = pinned); pinned
    ///        particles get a zero perturbation, matching the CPU contract.
    void precompute(uint32_t gridW, uint32_t gridH,
                    const std::vector<glm::vec3>& positions,
                    const std::vector<float>& inverseMasses,
                    const std::vector<uint32_t>& indices);

    bool precomputed() const { return m_precomputed; }

    /// @brief effectiveDir × (strength × gustCurrent × flutter) — the
    ///        gust/flutter/direction-offset-folded wind velocity the
    ///        per-triangle drag uses. Valid after `advance()` + `precompute()`.
    glm::vec3 baseWindVelocity() const;

    float flutter() const { return m_flutter; }
    const std::vector<glm::vec3>& particleWind()       const { return m_particleWind; }
    const std::vector<float>&     triangleTurbulence() const { return m_triangleTurb; }

private:
    void updateGustState(float dt);

    // config
    glm::vec3        m_direction = glm::vec3(0.0f);
    float            m_strength  = 0.0f;
    float            m_dragCoeff = 1.0f;
    ClothWindQuality m_quality   = ClothWindQuality::FULL;

    // gust state machine: realistic blow/calm cycles
    float     m_elapsed       = 0.0f;
    float     m_gustCurrent   = 0.0f;  ///< Current gust intensity [0,1]
    float     m_gustTarget    = 0.0f;  ///< Target gust intensity
    float     m_gustTimer     = 0.0f;  ///< Time until next target change
    float     m_gustRampSpeed = 0.0f;  ///< How fast to reach target
    glm::vec3 m_windDirOffset = glm::vec3(0.0f);  ///< Current direction offset
    glm::vec3 m_windDirTarget = glm::vec3(0.0f);  ///< Target direction offset
    float     m_dirTimer      = 0.0f;  ///< Time until next direction change
    DeterministicLcgRng m_rng{12345u};  ///< Shared LCG for deterministic randomness

    // cached per-frame outputs (recomputed in precompute, read N times per frame)
    std::vector<glm::vec3> m_particleWind;  ///< Per-particle perturbation strength (FULL)
    std::vector<float>     m_triangleTurb;  ///< Per-triangle spatial turbulence factor (FULL)
    float                  m_flutter     = 1.0f;   ///< Cached flutter value for this frame
    bool                   m_precomputed = false;  ///< True after precompute() runs
};

} // namespace Vestige
