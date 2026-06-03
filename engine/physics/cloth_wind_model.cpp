// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "physics/cloth_wind_model.h"

#include <cmath>
#include <cstring>

namespace Vestige
{

namespace
{

/// Produces same quality pseudo-random distribution as sin(x*127.1+y*311.7)*43758.5
/// but uses integer bit-mixing (~5 cycles) instead of std::sin (~30 cycles).
float hashNoise(float x, float y)
{
    // Reinterpret floats as integers for bit mixing
    uint32_t ix, iy;
    std::memcpy(&ix, &x, sizeof(uint32_t));
    std::memcpy(&iy, &y, sizeof(uint32_t));

    // Combine with golden-ratio-derived constants, then avalanche
    uint32_t h = ix * 0x45d9f3bu + iy * 0x119de1f3u;
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;

    return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;  // [0, 1)
}

}  // namespace

void ClothWindModel::setWind(const glm::vec3& direction, float strength)
{
    const float len = glm::length(direction);
    m_direction = (len > 0.0f) ? direction / len : glm::vec3(0.0f);
    m_strength = strength;
}

void ClothWindModel::setDragCoefficient(float drag)
{
    m_dragCoeff = std::max(0.0f, drag);
}

void ClothWindModel::seedAndInit(uint32_t seed)
{
    // Unique RNG seed so each cloth panel has different wind timing.
    m_rng.seed((seed != 0) ? seed : 12345u);

    // Start in a calm period so the curtain hangs straight before the first
    // gust. The initial timer gives gravity time to settle the cloth.
    m_gustCurrent = 0.0f;
    m_gustTarget = 0.0f;
    m_gustTimer = m_rng.nextRange(3.0f, 5.0f);  // 3-5 seconds of calm before first gust
    m_gustRampSpeed = 0.0f;
    m_windDirOffset = glm::vec3(0.0f);
    m_windDirTarget = glm::vec3(0.0f);
    m_dirTimer = m_rng.nextRange(2.0f, 4.0f);
}

void ClothWindModel::reset()
{
    m_gustCurrent = 0.0f;
    m_gustTarget = 0.0f;
    m_gustTimer = 0.0f;
    m_gustRampSpeed = 0.0f;
    m_windDirOffset = glm::vec3(0.0f);
    m_windDirTarget = glm::vec3(0.0f);
    m_dirTimer = 0.0f;
    m_elapsed = 0.0f;
}

void ClothWindModel::advance(float dt)
{
    m_elapsed += dt;
    updateGustState(dt);
}

void ClothWindModel::updateGustState(float dt)
{
    // --- Gust strength state machine ---
    m_gustTimer -= dt;
    if (m_gustTimer <= 0.0f)
    {
        // Pick a new target: either a gust or calm period
        if (m_gustTarget < 0.3f)
        {
            // Was calm → start a gust
            m_gustTarget = m_rng.nextRange(0.5f, 1.0f);
            m_gustTimer = m_rng.nextRange(1.5f, 4.0f);   // Blow for 1.5-4 seconds
            m_gustRampSpeed = m_rng.nextRange(1.5f, 4.0f); // Ramp up speed
        }
        else
        {
            // Was gusting → go calm (truly still)
            // Calm periods must be long enough for fabric to swing back to
            // its natural hanging position. A 4m curtain has a pendulum
            // period of ~4 seconds, so calm needs 3-7 seconds minimum.
            m_gustTarget = 0.0f;
            m_gustTimer = m_rng.nextRange(3.0f, 7.0f);   // Calm for 3-7 seconds
            m_gustRampSpeed = m_rng.nextRange(3.0f, 6.0f); // Wind dies off quickly
        }
    }

    // Smoothly interpolate toward target
    float diff = m_gustTarget - m_gustCurrent;
    float step = m_gustRampSpeed * dt;
    if (std::abs(diff) < step)
    {
        m_gustCurrent = m_gustTarget;
    }
    else
    {
        m_gustCurrent += (diff > 0.0f ? step : -step);
    }

    // --- Direction shift state machine ---
    m_dirTimer -= dt;
    if (m_dirTimer <= 0.0f)
    {
        // Pick a new direction offset — occasionally large shifts
        float shift = m_rng.nextFloat();
        if (shift < 0.15f)
        {
            // Big direction change (15% chance): partially reverse or strong side gust
            m_windDirTarget = glm::vec3(
                m_rng.nextRange(-0.8f, 0.8f),
                m_rng.nextRange(-0.3f, 0.3f),
                m_rng.nextRange(-0.5f, 0.4f)   // Can partially reverse
            );
        }
        else if (shift < 0.5f)
        {
            // Medium shift (35% chance)
            m_windDirTarget = glm::vec3(
                m_rng.nextRange(-0.4f, 0.4f),
                m_rng.nextRange(-0.15f, 0.15f),
                m_rng.nextRange(-0.1f, 0.2f)
            );
        }
        else
        {
            // Small drift or return to base (50% chance)
            m_windDirTarget = glm::vec3(
                m_rng.nextRange(-0.15f, 0.15f),
                m_rng.nextRange(-0.05f, 0.05f),
                0.0f
            );
        }
        m_dirTimer = m_rng.nextRange(1.0f, 5.0f);
    }

    // Smoothly interpolate direction offset
    glm::vec3 dirDiff = m_windDirTarget - m_windDirOffset;
    float dirLen = glm::length(dirDiff);
    float dirStep = 0.8f * dt;
    if (dirLen <= dirStep || dirLen < 1e-6f)
    {
        m_windDirOffset = m_windDirTarget;
    }
    else
    {
        m_windDirOffset += dirDiff * (dirStep / dirLen);
    }
}

void ClothWindModel::precompute(uint32_t gridW, uint32_t gridH,
                                const std::vector<glm::vec3>& positions,
                                const std::vector<float>& inverseMasses,
                                const std::vector<uint32_t>& indices)
{
    m_precomputed = false;

    if (m_strength <= 0.0f || m_quality == ClothWindQuality::SIMPLE)
    {
        return;
    }

    float t = m_elapsed;

    // Cache flutter (depends only on elapsed — constant across substeps)
    m_flutter = 1.0f + 0.15f * std::sin(t * 7.3f + 1.1f)
                     + 0.08f * std::sin(t * 13.7f + 3.2f);

    // --- Per-particle perturbation cache (FULL quality only) ---
    if (m_quality == ClothWindQuality::FULL && gridW > 0 && gridH > 0)
    {
        uint32_t count = gridW * gridH;
        m_particleWind.resize(count);

        glm::vec3 effectiveDir = m_direction + m_windDirOffset;
        float gustStrength = m_gustCurrent * m_flutter;

        for (uint32_t gz = 0; gz < gridH; ++gz)
        {
            for (uint32_t gx = 0; gx < gridW; ++gx)
            {
                uint32_t idx = gz * gridW + gx;
                if (inverseMasses[idx] <= 0.0f)
                {
                    m_particleWind[idx] = glm::vec3(0.0f);
                    continue;
                }

                float rowFrac = static_cast<float>(gz) / static_cast<float>(gridH - 1);
                float colFrac = static_cast<float>(gx) / static_cast<float>(gridW - 1);
                float edgeDist = std::abs(colFrac - 0.5f) * 2.0f;

                float baseFactor = 0.3f + 0.7f * rowFrac;
                float edgeBoost = edgeDist * edgeDist * 0.8f;
                float bottomBoost = (gz >= gridH - 2) ? 0.5f : 0.0f;
                float totalFactor = baseFactor + edgeBoost + bottomBoost;

                float px = static_cast<float>(gx);
                float py = static_cast<float>(gz);
                float n1 = hashNoise(px * 0.7f + t * 1.3f, py * 1.1f + t * 0.7f) - 0.5f;
                float n2 = hashNoise(px * 1.9f + t * 2.1f, py * 0.5f + t * 1.9f) - 0.5f;
                float n3 = hashNoise(px * 0.3f + t * 0.9f, py * 2.3f + t * 2.7f) - 0.5f;

                glm::vec3 perturbation = effectiveDir * (n1 * 2.5f)
                                       + glm::vec3(n2 * 2.0f, n3 * 1.2f, n1 * 1.0f);

                float strength = m_strength * gustStrength * totalFactor * 0.5f;

                // Store pre-multiplied perturbation (caller multiplies by dt per substep)
                m_particleWind[idx] = perturbation * (strength * inverseMasses[idx]);
            }
        }
    }

    // --- Per-triangle spatial turbulence cache (FULL quality only) ---
    if (m_quality == ClothWindQuality::FULL)
    {
        size_t triCount = indices.size() / 3;
        m_triangleTurb.resize(triCount);

        for (size_t ti = 0; ti < triCount; ++ti)
        {
            uint32_t i0 = indices[ti * 3];
            uint32_t i1 = indices[ti * 3 + 1];
            uint32_t i2 = indices[ti * 3 + 2];

            glm::vec3 centroid = (positions[i0] + positions[i1] + positions[i2]) / 3.0f;
            float spatialNoise = hashNoise(centroid.x * 3.0f + t * 1.1f,
                                            centroid.y * 2.0f + t * 0.8f);
            m_triangleTurb[ti] = 0.5f + spatialNoise;  // [0.5, 1.5]
        }
    }

    m_precomputed = true;
}

glm::vec3 ClothWindModel::baseWindVelocity() const
{
    float gustStrength = m_gustCurrent * m_flutter;
    glm::vec3 effectiveDir = m_direction + m_windDirOffset;
    return effectiveDir * (m_strength * gustStrength);
}

} // namespace Vestige
