/// @file cloth_simulator.cpp
/// @brief XPBD cloth simulation implementation.
#include "physics/cloth_simulator.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

void ClothSimulator::initialize(const ClothConfig& config, uint32_t seed)
{
    m_config = config;
    m_gridW = config.width;
    m_gridH = config.height;

    // Unique RNG seed so each cloth panel has different wind timing
    m_rngState = (seed != 0) ? seed : 12345u;

    uint32_t count = m_gridW * m_gridH;
    float invMass = (config.particleMass > 0.0f) ? 1.0f / config.particleMass : 0.0f;

    // Allocate particle arrays
    m_positions.resize(count);
    m_prevPositions.resize(count);
    m_velocities.resize(count, glm::vec3(0.0f));
    m_inverseMasses.resize(count, invMass);
    m_originalInverseMasses.resize(count, invMass);
    m_normals.resize(count, glm::vec3(0.0f, 1.0f, 0.0f));
    m_texCoords.resize(count);

    // Generate grid positions and UVs
    // Grid lies in the XZ plane (Y=0), centered at origin
    float halfW = static_cast<float>(m_gridW - 1) * config.spacing * 0.5f;
    float halfH = static_cast<float>(m_gridH - 1) * config.spacing * 0.5f;

    for (uint32_t z = 0; z < m_gridH; ++z)
    {
        for (uint32_t x = 0; x < m_gridW; ++x)
        {
            uint32_t idx = z * m_gridW + x;
            m_positions[idx] = glm::vec3(
                static_cast<float>(x) * config.spacing - halfW,
                0.0f,
                static_cast<float>(z) * config.spacing - halfH
            );
            m_prevPositions[idx] = m_positions[idx];
            m_texCoords[idx] = glm::vec2(
                static_cast<float>(x) / static_cast<float>(m_gridW - 1),
                static_cast<float>(z) / static_cast<float>(m_gridH - 1)
            );
        }
    }

    // Generate triangle indices (two triangles per grid cell)
    m_indices.clear();
    m_indices.reserve(static_cast<size_t>((m_gridW - 1)) * (m_gridH - 1) * 6);
    for (uint32_t z = 0; z < m_gridH - 1; ++z)
    {
        for (uint32_t x = 0; x < m_gridW - 1; ++x)
        {
            uint32_t tl = z * m_gridW + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (z + 1) * m_gridW + x;
            uint32_t br = bl + 1;

            m_indices.push_back(tl);
            m_indices.push_back(bl);
            m_indices.push_back(tr);

            m_indices.push_back(tr);
            m_indices.push_back(bl);
            m_indices.push_back(br);
        }
    }

    // --- Build constraints ---

    auto addConstraint = [](std::vector<DistanceConstraint>& list,
                            uint32_t i0, uint32_t i1,
                            const std::vector<glm::vec3>& positions,
                            float compliance)
    {
        float rest = glm::length(positions[i0] - positions[i1]);
        list.push_back({i0, i1, rest, compliance});
    };

    m_stretchConstraints.clear();
    m_shearConstraints.clear();
    m_bendConstraints.clear();

    for (uint32_t z = 0; z < m_gridH; ++z)
    {
        for (uint32_t x = 0; x < m_gridW; ++x)
        {
            uint32_t idx = z * m_gridW + x;

            // Structural: right neighbor
            if (x + 1 < m_gridW)
            {
                addConstraint(m_stretchConstraints, idx, idx + 1,
                              m_positions, config.stretchCompliance);
            }
            // Structural: down neighbor
            if (z + 1 < m_gridH)
            {
                addConstraint(m_stretchConstraints, idx, idx + m_gridW,
                              m_positions, config.stretchCompliance);
            }
            // Shear: diagonal down-right
            if (x + 1 < m_gridW && z + 1 < m_gridH)
            {
                addConstraint(m_shearConstraints, idx, idx + m_gridW + 1,
                              m_positions, config.shearCompliance);
            }
            // Shear: diagonal down-left
            if (x > 0 && z + 1 < m_gridH)
            {
                addConstraint(m_shearConstraints, idx, idx + m_gridW - 1,
                              m_positions, config.shearCompliance);
            }
            // Bend: skip-one right
            if (x + 2 < m_gridW)
            {
                addConstraint(m_bendConstraints, idx, idx + 2,
                              m_positions, config.bendCompliance);
            }
            // Bend: skip-one down
            if (z + 2 < m_gridH)
            {
                addConstraint(m_bendConstraints, idx, idx + 2 * m_gridW,
                              m_positions, config.bendCompliance);
            }
        }
    }

    m_initialized = true;
}

void ClothSimulator::simulate(float deltaTime)
{
    if (!m_initialized || deltaTime <= 0.0f)
    {
        return;
    }

    m_elapsed += deltaTime;

    int substeps = m_config.substeps;
    if (substeps < 1) substeps = 1;

    float dtSub = deltaTime / static_cast<float>(substeps);
    float dtSub2 = dtSub * dtSub;

    for (int s = 0; s < substeps; ++s)
    {
        uint32_t count = static_cast<uint32_t>(m_positions.size());

        // 1. Apply external forces (gravity + wind) to velocities
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;  // Pinned particle
            }
            m_velocities[i] += m_config.gravity * dtSub;
        }
        applyWind(dtSub);

        // 2. Predict positions from updated velocities
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }
            m_prevPositions[i] = m_positions[i];
            m_positions[i] += m_velocities[i] * dtSub;
        }

        // 3. Solve constraints (Gauss-Seidel XPBD)
        // Stretch constraints
        for (auto& c : m_stretchConstraints)
        {
            float alphaTilde = c.compliance / dtSub2;
            solveDistanceConstraint(c, alphaTilde);
        }
        // Shear constraints
        for (auto& c : m_shearConstraints)
        {
            float alphaTilde = c.compliance / dtSub2;
            solveDistanceConstraint(c, alphaTilde);
        }
        // Bend constraints
        for (auto& c : m_bendConstraints)
        {
            float alphaTilde = c.compliance / dtSub2;
            solveDistanceConstraint(c, alphaTilde);
        }

        // 4. Solve pin constraints
        solvePinConstraints();

        // 5. Apply collisions
        applyCollisions();

        // 6. Update velocities and apply damping
        float dampFactor = 1.0f - m_config.damping;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                m_velocities[i] = glm::vec3(0.0f);
                continue;
            }
            m_velocities[i] = (m_positions[i] - m_prevPositions[i]) / dtSub;
            m_velocities[i] *= dampFactor;
        }
    }

    // Recompute normals for rendering
    recomputeNormals();
}

uint32_t ClothSimulator::getParticleCount() const
{
    return static_cast<uint32_t>(m_positions.size());
}

const glm::vec3* ClothSimulator::getPositions() const
{
    return m_positions.empty() ? nullptr : m_positions.data();
}

const glm::vec3* ClothSimulator::getNormals() const
{
    return m_normals.empty() ? nullptr : m_normals.data();
}

uint32_t ClothSimulator::getGridWidth() const
{
    return m_gridW;
}

uint32_t ClothSimulator::getGridHeight() const
{
    return m_gridH;
}

const std::vector<uint32_t>& ClothSimulator::getIndices() const
{
    return m_indices;
}

const std::vector<glm::vec2>& ClothSimulator::getTexCoords() const
{
    return m_texCoords;
}

// --- Pin constraints ---

void ClothSimulator::pinParticle(uint32_t index, const glm::vec3& worldPos)
{
    if (index >= m_positions.size())
    {
        return;
    }

    m_inverseMasses[index] = 0.0f;
    m_positions[index] = worldPos;
    m_prevPositions[index] = worldPos;
    m_velocities[index] = glm::vec3(0.0f);

    // Check if already pinned — update position
    for (auto& pin : m_pinConstraints)
    {
        if (pin.index == index)
        {
            pin.position = worldPos;
            return;
        }
    }

    m_pinConstraints.push_back({index, worldPos});
}

void ClothSimulator::unpinParticle(uint32_t index)
{
    if (index >= m_positions.size())
    {
        return;
    }

    m_inverseMasses[index] = m_originalInverseMasses[index];

    m_pinConstraints.erase(
        std::remove_if(m_pinConstraints.begin(), m_pinConstraints.end(),
            [index](const PinConstraint& p) { return p.index == index; }),
        m_pinConstraints.end()
    );
}

void ClothSimulator::setPinPosition(uint32_t index, const glm::vec3& worldPos)
{
    for (auto& pin : m_pinConstraints)
    {
        if (pin.index == index)
        {
            pin.position = worldPos;
            m_positions[index] = worldPos;
            return;
        }
    }
}

bool ClothSimulator::isParticlePinned(uint32_t index) const
{
    if (index >= m_inverseMasses.size())
    {
        return false;
    }
    return m_inverseMasses[index] <= 0.0f;
}

// --- Collision ---

void ClothSimulator::addSphereCollider(const glm::vec3& center, float radius)
{
    m_sphereColliders.push_back({center, radius});
}

void ClothSimulator::clearSphereColliders()
{
    m_sphereColliders.clear();
}

void ClothSimulator::setGroundPlane(float height)
{
    m_groundPlaneY = height;
}

float ClothSimulator::getGroundPlane() const
{
    return m_groundPlaneY;
}

// --- Wind ---

void ClothSimulator::setWind(const glm::vec3& direction, float strength)
{
    float len = glm::length(direction);
    m_windDirection = (len > 0.0f) ? direction / len : glm::vec3(0.0f);
    m_windStrength = strength;
}

void ClothSimulator::setDragCoefficient(float drag)
{
    m_dragCoeff = std::max(0.0f, drag);
}

glm::vec3 ClothSimulator::getWindVelocity() const
{
    return m_windDirection * m_windStrength;
}

// --- Config ---

void ClothSimulator::setSubsteps(int substeps)
{
    m_config.substeps = std::max(1, substeps);
}

const ClothConfig& ClothSimulator::getConfig() const
{
    return m_config;
}

bool ClothSimulator::isInitialized() const
{
    return m_initialized;
}

// --- Solver internals ---

void ClothSimulator::solveDistanceConstraint(DistanceConstraint& c, float alphaTilde)
{
    glm::vec3& p0 = m_positions[c.i0];
    glm::vec3& p1 = m_positions[c.i1];
    float w0 = m_inverseMasses[c.i0];
    float w1 = m_inverseMasses[c.i1];

    float wSum = w0 + w1;
    if (wSum <= 0.0f)
    {
        return;  // Both pinned
    }

    glm::vec3 diff = p0 - p1;
    float dist = glm::length(diff);
    if (dist < 1e-7f)
    {
        return;  // Degenerate — particles coincident
    }

    // XPBD: C = dist - restLength
    float constraint = dist - c.restLength;
    // ∇C direction
    glm::vec3 gradient = diff / dist;

    // XPBD correction: Δλ = -C / (w0 + w1 + α̃)
    float denom = wSum + alphaTilde;
    float deltaLambda = -constraint / denom;

    p0 += gradient * (w0 * deltaLambda);
    p1 -= gradient * (w1 * deltaLambda);
}

void ClothSimulator::solvePinConstraints()
{
    for (const auto& pin : m_pinConstraints)
    {
        m_positions[pin.index] = pin.position;
    }
}

void ClothSimulator::applyCollisions()
{
    uint32_t count = static_cast<uint32_t>(m_positions.size());

    // Ground plane collision
    for (uint32_t i = 0; i < count; ++i)
    {
        if (m_inverseMasses[i] <= 0.0f)
        {
            continue;
        }
        if (m_positions[i].y < m_groundPlaneY)
        {
            m_positions[i].y = m_groundPlaneY;
            // Zero out downward velocity component
            if (m_velocities[i].y < 0.0f)
            {
                m_velocities[i].y = 0.0f;
            }
        }
    }

    // Sphere collisions
    for (const auto& sphere : m_sphereColliders)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }

            glm::vec3 toParticle = m_positions[i] - sphere.center;
            float dist = glm::length(toParticle);
            if (dist < sphere.radius && dist > 1e-7f)
            {
                // Push particle to surface
                glm::vec3 normal = toParticle / dist;
                m_positions[i] = sphere.center + normal * sphere.radius;

                // Remove inward velocity component
                float velDotN = glm::dot(m_velocities[i], normal);
                if (velDotN < 0.0f)
                {
                    m_velocities[i] -= normal * velDotN;
                }
            }
        }
    }
}

void ClothSimulator::recomputeNormals()
{
    uint32_t count = static_cast<uint32_t>(m_normals.size());

    // Zero all normals
    for (uint32_t i = 0; i < count; ++i)
    {
        m_normals[i] = glm::vec3(0.0f);
    }

    // Accumulate face normals (area-weighted by cross product magnitude)
    for (size_t t = 0; t + 2 < m_indices.size(); t += 3)
    {
        uint32_t i0 = m_indices[t];
        uint32_t i1 = m_indices[t + 1];
        uint32_t i2 = m_indices[t + 2];

        glm::vec3 edge1 = m_positions[i1] - m_positions[i0];
        glm::vec3 edge2 = m_positions[i2] - m_positions[i0];
        glm::vec3 faceNormal = glm::cross(edge1, edge2);

        m_normals[i0] += faceNormal;
        m_normals[i1] += faceNormal;
        m_normals[i2] += faceNormal;
    }

    // Normalize
    for (uint32_t i = 0; i < count; ++i)
    {
        float len = glm::length(m_normals[i]);
        if (len > 1e-7f)
        {
            m_normals[i] /= len;
        }
        else
        {
            m_normals[i] = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

/// @brief Simple hash for pseudo-random per-particle noise.
static float hashNoise(float x, float y)
{
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);  // [0, 1)
}

float ClothSimulator::randFloat()
{
    // LCG: fast, deterministic, no stdlib dependency
    m_rngState = m_rngState * 1664525u + 1013904223u;
    return static_cast<float>(m_rngState & 0x00FFFFFFu) / 16777216.0f;  // [0, 1)
}

float ClothSimulator::randRange(float lo, float hi)
{
    return lo + randFloat() * (hi - lo);
}

void ClothSimulator::updateGustState(float dt)
{
    // --- Gust strength state machine ---
    m_gustTimer -= dt;
    if (m_gustTimer <= 0.0f)
    {
        // Pick a new target: either a gust or calm period
        if (m_gustTarget < 0.3f)
        {
            // Was calm → start a gust
            m_gustTarget = randRange(0.5f, 1.0f);
            m_gustTimer = randRange(1.5f, 4.0f);   // Blow for 1.5-4 seconds
            m_gustRampSpeed = randRange(1.5f, 4.0f); // Ramp up speed
        }
        else
        {
            // Was gusting → go calm (truly still)
            m_gustTarget = 0.0f;
            m_gustTimer = randRange(1.0f, 3.5f);   // Calm for 1-3.5 seconds
            m_gustRampSpeed = randRange(2.0f, 5.0f); // Wind dies off fairly quickly
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
        float shift = randFloat();
        if (shift < 0.15f)
        {
            // Big direction change (15% chance): partially reverse or strong side gust
            m_windDirTarget = glm::vec3(
                randRange(-0.8f, 0.8f),
                randRange(-0.3f, 0.3f),
                randRange(-0.5f, 0.4f)   // Can partially reverse
            );
        }
        else if (shift < 0.5f)
        {
            // Medium shift (35% chance)
            m_windDirTarget = glm::vec3(
                randRange(-0.4f, 0.4f),
                randRange(-0.15f, 0.15f),
                randRange(-0.1f, 0.2f)
            );
        }
        else
        {
            // Small drift or return to base (50% chance)
            m_windDirTarget = glm::vec3(
                randRange(-0.15f, 0.15f),
                randRange(-0.05f, 0.05f),
                0.0f
            );
        }
        m_dirTimer = randRange(1.0f, 5.0f);
    }

    // Smoothly interpolate direction offset
    glm::vec3 dirDiff = m_windDirTarget - m_windDirOffset;
    float dirLen = glm::length(dirDiff);
    float dirStep = 0.8f * dt;
    if (dirLen < dirStep)
    {
        m_windDirOffset = m_windDirTarget;
    }
    else
    {
        m_windDirOffset += dirDiff * (dirStep / dirLen);
    }
}

void ClothSimulator::applyWind(float dt)
{
    if (m_windStrength <= 0.0f || dt <= 0.0f)
    {
        return;
    }

    float t = m_elapsed;

    // Update gust + direction state machines
    updateGustState(dt);

    // Add high-frequency flutter on top of gust envelope
    float flutter = 1.0f + 0.15f * std::sin(t * 7.3f + 1.1f)
                        + 0.08f * std::sin(t * 13.7f + 3.2f);

    float gustStrength = m_gustCurrent * flutter;

    // Effective wind direction = base + current offset
    glm::vec3 effectiveDir = m_windDirection + m_windDirOffset;
    glm::vec3 baseWindVel = effectiveDir * (m_windStrength * gustStrength);

    // --- Per-particle perturbation (spatial variation + edge flutter) ---
    if (m_gridW > 0 && m_gridH > 0)
    {
        for (uint32_t gz = 0; gz < m_gridH; ++gz)
        {
            for (uint32_t gx = 0; gx < m_gridW; ++gx)
            {
                uint32_t idx = gz * m_gridW + gx;
                if (m_inverseMasses[idx] <= 0.0f)
                {
                    continue;
                }

                // Row fraction: 0 at top (pinned), 1 at bottom
                float rowFrac = static_cast<float>(gz) / static_cast<float>(m_gridH - 1);
                // Column fraction: 0 at left edge, 1 at right edge
                float colFrac = static_cast<float>(gx) / static_cast<float>(m_gridW - 1);
                // Distance from center column [0=center, 1=edge]
                float edgeDist = std::abs(colFrac - 0.5f) * 2.0f;

                // Every particle gets perturbation, but edges and bottom get more
                float baseFactor = 0.3f + 0.7f * rowFrac;      // More sway lower down
                float edgeBoost = edgeDist * edgeDist * 0.8f;   // Quadratic edge boost
                float bottomBoost = (gz >= m_gridH - 2) ? 0.5f : 0.0f;
                float totalFactor = baseFactor + edgeBoost + bottomBoost;

                // Spatially varying noise: different frequencies for each axis
                float px = static_cast<float>(gx);
                float py = static_cast<float>(gz);
                float n1 = hashNoise(px * 0.7f + t * 1.3f, py * 1.1f + t * 0.7f) - 0.5f;
                float n2 = hashNoise(px * 1.9f + t * 2.1f, py * 0.5f + t * 1.9f) - 0.5f;
                float n3 = hashNoise(px * 0.3f + t * 0.9f, py * 2.3f + t * 2.7f) - 0.5f;

                // Force: along wind + lateral + vertical
                glm::vec3 perturbation = effectiveDir * (n1 * 2.5f)
                                       + glm::vec3(n2 * 2.0f, n3 * 1.2f, n1 * 1.0f);

                float strength = m_windStrength * gustStrength * totalFactor * 0.5f;
                m_velocities[idx] += perturbation * (strength * dt * m_inverseMasses[idx]);
            }
        }
    }

    // --- Per-triangle aerodynamic drag ---
    for (size_t ti = 0; ti + 2 < m_indices.size(); ti += 3)
    {
        uint32_t i0 = m_indices[ti];
        uint32_t i1 = m_indices[ti + 1];
        uint32_t i2 = m_indices[ti + 2];

        // Spatial turbulence from triangle centroid
        glm::vec3 centroid = (m_positions[i0] + m_positions[i1] + m_positions[i2]) / 3.0f;
        float spatialNoise = hashNoise(centroid.x * 3.0f + t * 1.1f,
                                        centroid.y * 2.0f + t * 0.8f);
        float turb = 0.5f + spatialNoise;  // [0.5, 1.5]

        glm::vec3 windVel = baseWindVel * turb;

        glm::vec3 vAvg = (m_velocities[i0] + m_velocities[i1] + m_velocities[i2]) / 3.0f;
        glm::vec3 vRel = windVel - vAvg;

        glm::vec3 edge1 = m_positions[i1] - m_positions[i0];
        glm::vec3 edge2 = m_positions[i2] - m_positions[i0];
        glm::vec3 crossProd = glm::cross(edge1, edge2);
        float area2 = glm::length(crossProd);

        if (area2 < 1e-7f)
        {
            continue;
        }

        glm::vec3 normal = crossProd / area2;
        float area = area2 * 0.5f;

        float vDotN = glm::dot(vRel, normal);
        glm::vec3 force = normal * (0.5f * m_dragCoeff * area * vDotN);
        glm::vec3 perVertex = force * (dt / 3.0f);

        if (m_inverseMasses[i0] > 0.0f) m_velocities[i0] += perVertex * m_inverseMasses[i0];
        if (m_inverseMasses[i1] > 0.0f) m_velocities[i1] += perVertex * m_inverseMasses[i1];
        if (m_inverseMasses[i2] > 0.0f) m_velocities[i2] += perVertex * m_inverseMasses[i2];
    }
}

} // namespace Vestige
