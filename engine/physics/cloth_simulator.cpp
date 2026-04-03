/// @file cloth_simulator.cpp
/// @brief XPBD cloth simulation implementation.
#include "physics/cloth_simulator.h"
#include "physics/cloth_mesh_collider.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Vestige
{

/// @brief Fast inverse square root (Quake III style) with one Newton-Raphson iteration.
static float fastInvSqrt(float x)
{
    float xhalf = 0.5f * x;
    uint32_t i;
    std::memcpy(&i, &x, sizeof(uint32_t));
    i = 0x5f3759df - (i >> 1);
    std::memcpy(&x, &i, sizeof(uint32_t));
    x *= 1.5f - xhalf * x * x;  // Newton-Raphson iteration
    return x;
}

/// @brief Fast acos approximation (max error ~0.017 rad / ~1 degree).
static float fastAcos(float x)
{
    x = glm::clamp(x, -1.0f, 1.0f);
    float ax = std::abs(x);
    float result = (((-0.0187293f * ax + 0.0742610f) * ax - 0.2121144f) * ax + 1.5707288f);
    result *= std::sqrt(1.0f - ax);
    return (x < 0.0f) ? 3.14159265f - result : result;
}

void ClothSimulator::initialize(const ClothConfig& config, uint32_t seed)
{
    m_config = config;
    m_gridW = config.width;
    m_gridH = config.height;

    // Grid must be at least 2x2 to form triangles and avoid division by zero in UVs/wind
    if (m_gridW < 2 || m_gridH < 2)
    {
        m_initialized = false;
        return;
    }

    // Reject non-positive mass (negative mass would invert gravity)
    if (config.particleMass <= 0.0f)
    {
        m_initialized = false;
        return;
    }

    // Unique RNG seed so each cloth panel has different wind timing
    m_rngState = (seed != 0) ? seed : 12345u;

    uint32_t count = m_gridW * m_gridH;
    float invMass = 1.0f / config.particleMass;

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

    // Build dihedral bending constraints from the triangle mesh
    buildDihedralConstraints();

    // Snapshot initial state for reset()
    m_initialPositions = m_positions;

    // Initialize constraint ordering (will sort when pins are set)
    m_constraintsSorted = false;
    m_particleDepth.clear();

    // Start in a calm period so the curtain hangs straight before the first gust.
    // The initial timer gives gravity time to settle the cloth into its natural drape.
    m_gustCurrent = 0.0f;
    m_gustTarget = 0.0f;
    m_gustTimer = randRange(3.0f, 5.0f);  // 3-5 seconds of calm before first gust
    m_gustRampSpeed = 0.0f;
    m_windDirOffset = glm::vec3(0.0f);
    m_windDirTarget = glm::vec3(0.0f);
    m_dirTimer = randRange(2.0f, 4.0f);
    m_sleeping = false;
    m_sleepFrames = 0;

    m_initialized = true;
}

void ClothSimulator::simulate(float deltaTime)
{
    if (!m_initialized || deltaTime <= 0.0f)
    {
        return;
    }

    m_elapsed += deltaTime;

    // Update gust state even when sleeping — need to detect when wind returns
    updateGustState(deltaTime);

    // Sleep check: if cloth is sleeping and wind is calm, skip simulation
    if (m_sleeping)
    {
        if (m_gustCurrent > 0.1f)
        {
            // Wind returned — wake up
            m_sleeping = false;
            m_sleepFrames = 0;
        }
        else
        {
            return;  // Still sleeping, skip simulation
        }
    }

    // Sort constraints by depth from pins (once, after pins are set)
    if (!m_constraintsSorted && !m_pinConstraints.empty())
    {
        sortConstraintsByDepth();
    }

    int substeps = std::clamp(m_config.substeps, 1, 64);

    float dtSub = deltaTime / static_cast<float>(substeps);

    // Adaptive damping: compute average speed for this frame
    float adaptiveDamp = 0.0f;
    if (m_adaptiveDampingFactor > 0.0f)
    {
        float totalSpeed = 0.0f;
        uint32_t freeParticles = 0;
        for (size_t i = 0; i < m_velocities.size(); ++i)
        {
            if (m_inverseMasses[i] > 0.0f)
            {
                totalSpeed += glm::length(m_velocities[i]);
                ++freeParticles;
            }
        }
        float avgSpeed = (freeParticles > 0) ? totalSpeed / static_cast<float>(freeParticles) : 0.0f;
        adaptiveDamp = m_adaptiveDampingFactor * avgSpeed;
    }

    for (int s = 0; s < substeps; ++s)
    {
        uint32_t count = static_cast<uint32_t>(m_positions.size());

        // 1. Apply external forces (gravity + wind + rest spring) to velocities
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;  // Pinned particle
            }
            m_velocities[i] += m_config.gravity * dtSub;

            // (Rest pose correction is applied post-solve via position blending,
            // not here as a velocity force. See step 8 below.)
        }
        applyWind(dtSub);

        // 2. Velocity clamping: cap particle speed so it cannot travel further
        // than the collision margin in one substep. Prevents tunneling through
        // thin colliders at high velocities. (Bridson et al., Stanford)
        {
            static constexpr float MAX_TRAVEL = 0.015f;  // Match COLLISION_MARGIN
            float maxSpeed = MAX_TRAVEL / dtSub;
            float maxSpeed2 = maxSpeed * maxSpeed;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (m_inverseMasses[i] <= 0.0f) continue;
                float speed2 = glm::dot(m_velocities[i], m_velocities[i]);
                if (speed2 > maxSpeed2)
                {
                    m_velocities[i] *= maxSpeed * fastInvSqrt(speed2);
                }
            }
        }

        // 3. Predict positions from updated velocities
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }
            m_prevPositions[i] = m_positions[i];
            m_positions[i] += m_velocities[i] * dtSub;
        }

        // 3. Solve constraints (Gauss-Seidel XPBD with Rayleigh damping)
        // The dtSub is passed to the solver for XPBD Rayleigh damping (gamma term)
        for (auto& c : m_stretchConstraints)
        {
            solveDistanceConstraint(c, c.compliance, dtSub);
        }
        for (auto& c : m_shearConstraints)
        {
            solveDistanceConstraint(c, c.compliance, dtSub);
        }
        for (auto& c : m_bendConstraints)
        {
            solveDistanceConstraint(c, c.compliance, dtSub);
        }
        for (auto& c : m_dihedralConstraints)
        {
            solveDihedralConstraint(c, dtSub);
        }

        // 4. Long Range Attachment: prevent cumulative drift from pins
        solveLRAConstraints();

        // 5. Solve pin constraints
        solvePinConstraints();

        // 6. Apply collisions (first pass — after constraint solving)
        applyCollisions();

        // 5b. Re-solve pins after collision (collision may have moved pinned particles)
        solvePinConstraints();

        // 5c. Apply collisions again (second pass — constraints may have pushed
        // particles back inside colliders during step 3. Two passes catches most
        // constraint-vs-collider conflicts without the cost of iterating further.)
        applyCollisions();

        // 6. Post-solve rest pose blending: gently guide particles toward their
        // initial straight-hanging positions when wind is calm. Only active for
        // cloth with LRA constraints (hanging curtains/veils) — taut panels like
        // fence walls don't need this and it would fight their natural wind bowing.
        {
            float restBlend = 1.0f - m_gustCurrent;
            if (restBlend > 0.01f && !m_lraConstraints.empty())
            {
                // Blend factor per substep. Keep small so wind can still
                // displace the cloth noticeably. At 16 substeps/frame,
                // 0.015 per substep ≈ 21% correction toward rest per frame.
                float blend = 0.015f * restBlend;
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (m_inverseMasses[i] <= 0.0f) continue;
                    m_positions[i] = glm::mix(m_positions[i], m_initialPositions[i], blend);
                }
            }
        }

        // 7. Update velocities and apply damping (base + adaptive)
        float effectiveDamping = std::min(m_config.damping + adaptiveDamp, 0.95f);
        float dampFactor = 1.0f - effectiveDamping;
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

    // --- Sleep detection ---
    // Track average kinetic energy per particle. When it drops below the
    // threshold for several consecutive frames, freeze the cloth.
    float totalKE = 0.0f;
    uint32_t freeCount = 0;
    uint32_t count = static_cast<uint32_t>(m_positions.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        if (m_inverseMasses[i] <= 0.0f) continue;
        float speed2 = glm::dot(m_velocities[i], m_velocities[i]);
        float mass = 1.0f / m_inverseMasses[i];
        totalKE += 0.5f * mass * speed2;
        ++freeCount;
    }

    // All-pinned cloth has no free particles — skip sleep detection entirely
    if (freeCount == 0)
    {
        recomputeNormals();
        return;
    }

    float avgKE = totalKE / static_cast<float>(freeCount);
    if (avgKE < m_config.sleepThreshold && m_gustCurrent < 0.05f)
    {
        ++m_sleepFrames;
        if (m_sleepFrames >= SLEEP_FRAME_COUNT)
        {
            m_sleeping = true;
            // Zero all velocities for a clean rest state
            for (uint32_t i = 0; i < count; ++i)
            {
                m_velocities[i] = glm::vec3(0.0f);
            }
        }
    }
    else
    {
        m_sleepFrames = 0;
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

bool ClothSimulator::pinParticle(uint32_t index, const glm::vec3& worldPos)
{
    if (index >= m_positions.size())
    {
        return false;
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
            return true;
        }
    }

    m_pinConstraints.push_back({index, worldPos});
    m_constraintsSorted = false;  // Re-sort on next simulate
    return true;
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
    m_constraintsSorted = false;  // Re-sort on next simulate
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

bool ClothSimulator::addPlaneCollider(const glm::vec3& normal, float offset)
{
    float len = glm::length(normal);
    if (len < 1e-7f) return false;
    m_planeColliders.push_back({normal / len, offset});
    return true;
}

void ClothSimulator::clearPlaneColliders()
{
    m_planeColliders.clear();
}

void ClothSimulator::addCylinderCollider(const glm::vec3& base, float radius, float height)
{
    m_cylinderColliders.push_back({base, std::max(0.0f, radius), std::max(0.0f, height)});
}

void ClothSimulator::clearCylinderColliders()
{
    m_cylinderColliders.clear();
}

void ClothSimulator::addBoxCollider(const glm::vec3& min, const glm::vec3& max)
{
    m_boxColliders.push_back({glm::min(min, max), glm::max(min, max)});
}

void ClothSimulator::clearBoxColliders()
{
    m_boxColliders.clear();
}

// --- Wind ---

void ClothSimulator::setWind(const glm::vec3& direction, float strength)
{
    float len = glm::length(direction);
    m_windDirection = (len > 0.0f) ? direction / len : glm::vec3(0.0f);
    m_windStrength = strength;
}

void ClothSimulator::setWindQuality(WindQuality quality)
{
    m_windQuality = quality;
}

ClothSimulator::WindQuality ClothSimulator::getWindQuality() const
{
    return m_windQuality;
}

void ClothSimulator::setDragCoefficient(float drag)
{
    m_dragCoeff = std::max(0.0f, drag);
}

glm::vec3 ClothSimulator::getWindVelocity() const
{
    return m_windDirection * m_windStrength;
}

glm::vec3 ClothSimulator::getWindDirection() const
{
    return m_windDirection;
}

float ClothSimulator::getWindStrength() const
{
    return m_windStrength;
}

float ClothSimulator::getDragCoefficient() const
{
    return m_dragCoeff;
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

// --- Reset and live parameter updates ---

void ClothSimulator::reset()
{
    if (!m_initialized)
    {
        return;
    }

    m_positions = m_initialPositions;
    m_prevPositions = m_initialPositions;
    m_velocities.assign(m_velocities.size(), glm::vec3(0.0f));

    // Restore pin positions (pins may have been modified after initialize)
    for (const auto& pin : m_pinConstraints)
    {
        m_positions[pin.index] = pin.position;
        m_prevPositions[pin.index] = pin.position;
    }

    // Reset gust state machine
    m_gustCurrent = 0.0f;
    m_gustTarget = 0.0f;
    m_gustTimer = 0.0f;
    m_gustRampSpeed = 0.0f;
    m_windDirOffset = glm::vec3(0.0f);
    m_windDirTarget = glm::vec3(0.0f);
    m_dirTimer = 0.0f;
    m_elapsed = 0.0f;

    // Clear sleep state
    m_sleeping = false;
    m_sleepFrames = 0;

    // Re-sort constraints after reset (pins may change)
    m_constraintsSorted = false;

    recomputeNormals();
}

void ClothSimulator::captureRestPositions()
{
    m_initialPositions = m_positions;
}

void ClothSimulator::rebuildLRA()
{
    buildLRAConstraints();
}

void ClothSimulator::buildLRAConstraints()
{
    m_lraConstraints.clear();

    if (m_pinConstraints.empty())
    {
        return;
    }

    uint32_t count = static_cast<uint32_t>(m_positions.size());
    for (uint32_t i = 0; i < count; ++i)
    {
        if (m_inverseMasses[i] <= 0.0f)
        {
            continue;  // Skip pinned particles — they don't need tethers
        }

        // Find the nearest pinned particle
        float bestDist2 = std::numeric_limits<float>::max();
        uint32_t bestPin = 0;
        for (const auto& pin : m_pinConstraints)
        {
            glm::vec3 diff = m_positions[i] - m_positions[pin.index];
            float d2 = glm::dot(diff, diff);
            if (d2 < bestDist2)
            {
                bestDist2 = d2;
                bestPin = pin.index;
            }
        }

        float maxDist = std::sqrt(bestDist2);
        m_lraConstraints.push_back({i, bestPin, maxDist});
    }
}

void ClothSimulator::solveLRAConstraints()
{
    for (const auto& lra : m_lraConstraints)
    {
        const glm::vec3& pinPos = m_positions[lra.pinIndex];
        glm::vec3& pos = m_positions[lra.particleIndex];
        glm::vec3 delta = pos - pinPos;
        float dist = glm::length(delta);

        // Unilateral: only pull back if drifted too far (not if closer)
        if (dist > lra.maxDistance && dist > 1e-7f)
        {
            pos = pinPos + delta * (lra.maxDistance / dist);
        }
    }
}

void ClothSimulator::setParticleMass(float mass)
{
    if (mass <= 0.0f)
    {
        return;
    }

    m_config.particleMass = mass;
    float invMass = 1.0f / mass;

    for (size_t i = 0; i < m_inverseMasses.size(); ++i)
    {
        // Update the stored original mass for all particles
        m_originalInverseMasses[i] = invMass;
        // Only update active inverse mass for non-pinned particles
        // (pinned particles have m_inverseMasses[i] == 0)
        if (m_inverseMasses[i] > 0.0f)
        {
            m_inverseMasses[i] = invMass;
        }
    }
}

void ClothSimulator::setDamping(float damping)
{
    m_config.damping = std::max(0.0f, std::min(damping, 1.0f));
}

void ClothSimulator::setStretchCompliance(float compliance)
{
    m_config.stretchCompliance = std::max(0.0f, compliance);
    for (auto& c : m_stretchConstraints)
    {
        c.compliance = m_config.stretchCompliance;
    }
}

void ClothSimulator::setShearCompliance(float compliance)
{
    m_config.shearCompliance = std::max(0.0f, compliance);
    for (auto& c : m_shearConstraints)
    {
        c.compliance = m_config.shearCompliance;
    }
}

void ClothSimulator::setBendCompliance(float compliance)
{
    m_config.bendCompliance = std::max(0.0f, compliance);
    for (auto& c : m_bendConstraints)
    {
        c.compliance = m_config.bendCompliance;
    }
}

uint32_t ClothSimulator::getPinnedCount() const
{
    return static_cast<uint32_t>(m_pinConstraints.size());
}

uint32_t ClothSimulator::getConstraintCount() const
{
    return static_cast<uint32_t>(m_stretchConstraints.size()
                                  + m_shearConstraints.size()
                                  + m_bendConstraints.size()
                                  + m_dihedralConstraints.size());
}

uint32_t ClothSimulator::getDihedralConstraintCount() const
{
    return static_cast<uint32_t>(m_dihedralConstraints.size());
}

// --- Dihedral bending ---

void ClothSimulator::setDihedralBendCompliance(float compliance)
{
    m_dihedralCompliance = std::max(0.0f, compliance);
    for (auto& c : m_dihedralConstraints)
    {
        c.compliance = m_dihedralCompliance;
    }
}

float ClothSimulator::getDihedralBendCompliance() const
{
    return m_dihedralCompliance;
}

// --- Adaptive damping ---

void ClothSimulator::setAdaptiveDamping(float factor)
{
    m_adaptiveDampingFactor = std::max(0.0f, factor);
}

float ClothSimulator::getAdaptiveDamping() const
{
    return m_adaptiveDampingFactor;
}

// --- Friction ---

void ClothSimulator::setFriction(float staticCoeff, float kineticCoeff)
{
    m_staticFriction = std::max(0.0f, staticCoeff);
    m_kineticFriction = std::max(0.0f, kineticCoeff);
}

float ClothSimulator::getStaticFriction() const
{
    return m_staticFriction;
}

float ClothSimulator::getKineticFriction() const
{
    return m_kineticFriction;
}

// --- Thick particle model ---

void ClothSimulator::setParticleRadius(float radius)
{
    m_particleRadius = std::max(0.0f, radius);
}

float ClothSimulator::getParticleRadius() const
{
    return m_particleRadius;
}

// --- Solver internals ---

void ClothSimulator::solveDistanceConstraint(DistanceConstraint& c,
                                              float compliance, float dtSub)
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
    glm::vec3 gradient = diff / dist;

    // XPBD with Rayleigh damping (Macklin et al. 2016, Section 3.5)
    // α̃ = compliance / dt²
    float dtSub2 = dtSub * dtSub;
    float alphaTilde = compliance / dtSub2;

    // β̃ = damping_coeff * dt²  (Rayleigh damping parameter)
    // γ = α̃ * β̃ / dt  — velocity-dependent damping integrated into the constraint
    // This damps oscillations along each constraint direction, providing physically
    // motivated energy dissipation that helps cloth settle to rest naturally.
    static constexpr float RAYLEIGH_DAMPING = 3.0e-7f;
    float betaTilde = RAYLEIGH_DAMPING * dtSub2;
    float gamma = alphaTilde * betaTilde / dtSub;

    // Velocity along constraint direction (for damping term)
    glm::vec3 v0 = p0 - m_prevPositions[c.i0];
    glm::vec3 v1 = p1 - m_prevPositions[c.i1];
    float dCdt = glm::dot(gradient, v0 - v1);  // Rate of constraint change

    // XPBD correction with Rayleigh damping:
    // Δλ = -(C + α̃ * λ + γ * dC/dt) / (w0 + w1 + α̃ + γ)
    // We use λ = 0 per-substep (reset each substep in XPBD small-steps approach)
    float denom = wSum + alphaTilde + gamma;
    float deltaLambda = -(constraint + gamma * dCdt) / denom;

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
    // Collision margin: particles are pushed to surface + margin rather than
    // exactly to the surface. This prevents tunneling when discrete time steps
    // cause a particle to barely touch the surface — without margin, the next
    // frame's constraint solving can push it back through.
    static constexpr float BASE_COLLISION_MARGIN = 0.015f;  // 1.5cm
    // Thick particle model: add particle radius to collision margin
    float COLLISION_MARGIN = BASE_COLLISION_MARGIN + m_particleRadius;

    uint32_t count = static_cast<uint32_t>(m_positions.size());

    // Friction helper: apply Coulomb friction to tangential velocity at collision
    auto applyFriction = [this](glm::vec3& velocity, const glm::vec3& normal)
    {
        if (m_staticFriction <= 0.0f && m_kineticFriction <= 0.0f)
        {
            return;
        }
        float vn = glm::dot(velocity, normal);
        glm::vec3 vNormal = normal * vn;
        glm::vec3 vTangent = velocity - vNormal;
        float vtLen = glm::length(vTangent);
        if (vtLen < 1e-7f)
        {
            return;
        }
        float normalSpeed = std::abs(vn);
        if (vtLen < m_staticFriction * normalSpeed)
        {
            // Static friction: stick
            velocity = vNormal;
        }
        else
        {
            // Kinetic friction: reduce tangential velocity
            float reduction = m_kineticFriction * normalSpeed / vtLen;
            velocity = vNormal + vTangent * std::max(0.0f, 1.0f - reduction);
        }
    };

    // Ground plane collision
    float groundWithMargin = m_groundPlaneY + COLLISION_MARGIN;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (m_inverseMasses[i] <= 0.0f)
        {
            continue;
        }
        if (m_positions[i].y < groundWithMargin)
        {
            m_positions[i].y = groundWithMargin;
            glm::vec3 groundNormal(0.0f, 1.0f, 0.0f);
            if (m_velocities[i].y < 0.0f)
            {
                m_velocities[i].y = 0.0f;
            }
            applyFriction(m_velocities[i], groundNormal);
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
            float effectiveR = sphere.radius + COLLISION_MARGIN;
            if (dist < effectiveR && dist > 1e-7f)
            {
                // Push particle to surface + margin
                glm::vec3 normal = toParticle / dist;
                m_positions[i] = sphere.center + normal * effectiveR;

                // Remove inward velocity component
                float velDotN = glm::dot(m_velocities[i], normal);
                if (velDotN < 0.0f)
                {
                    m_velocities[i] -= normal * velDotN;
                }
                applyFriction(m_velocities[i], normal);
            }
        }
    }

    // Box collisions (axis-aligned) — push out along shortest penetration axis
    // Expand box by margin so particles are kept slightly outside the geometry
    for (const auto& box : m_boxColliders)
    {
        glm::vec3 bMin = box.min - glm::vec3(COLLISION_MARGIN);
        glm::vec3 bMax = box.max + glm::vec3(COLLISION_MARGIN);

        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }

            const glm::vec3& pos = m_positions[i];

            // Check if particle is inside the expanded box
            if (pos.x < bMin.x || pos.x > bMax.x ||
                pos.y < bMin.y || pos.y > bMax.y ||
                pos.z < bMin.z || pos.z > bMax.z)
            {
                continue;  // Outside — no collision
            }

            // Find the axis with smallest penetration depth and push out
            float dx_min = pos.x - bMin.x;
            float dx_max = bMax.x - pos.x;
            float dy_min = pos.y - bMin.y;
            float dy_max = bMax.y - pos.y;
            float dz_min = pos.z - bMin.z;
            float dz_max = bMax.z - pos.z;

            float minPen = dx_min;
            glm::vec3 pushDir(-1, 0, 0);

            if (dx_max < minPen) { minPen = dx_max; pushDir = glm::vec3(1, 0, 0); }
            if (dy_min < minPen) { minPen = dy_min; pushDir = glm::vec3(0, -1, 0); }
            if (dy_max < minPen) { minPen = dy_max; pushDir = glm::vec3(0, 1, 0); }
            if (dz_min < minPen) { minPen = dz_min; pushDir = glm::vec3(0, 0, -1); }
            if (dz_max < minPen) { minPen = dz_max; pushDir = glm::vec3(0, 0, 1); }

            m_positions[i] += pushDir * minPen;

            // Remove velocity component going into the box
            float velDotN = glm::dot(m_velocities[i], -pushDir);
            if (velDotN > 0.0f)
            {
                m_velocities[i] += pushDir * velDotN;
            }
            applyFriction(m_velocities[i], pushDir);
        }
    }

    // Cylinder collisions (Y-axis aligned) — 2D distance check in XZ plane
    for (const auto& cyl : m_cylinderColliders)
    {
        float cylTop = cyl.base.y + cyl.height;
        float effectiveR = cyl.radius + COLLISION_MARGIN;

        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }

            // Skip if particle is above or below the cylinder
            if (m_positions[i].y < cyl.base.y || m_positions[i].y > cylTop)
            {
                continue;
            }

            // 2D distance in XZ plane from cylinder axis
            float dx = m_positions[i].x - cyl.base.x;
            float dz = m_positions[i].z - cyl.base.z;
            float dist2D = std::sqrt(dx * dx + dz * dz);

            if (dist2D < effectiveR && dist2D > 1e-7f)
            {
                // Push particle to cylinder surface + margin
                glm::vec3 normal2D(dx / dist2D, 0.0f, dz / dist2D);
                m_positions[i] = glm::vec3(cyl.base.x, m_positions[i].y, cyl.base.z)
                                 + normal2D * effectiveR;

                // Remove inward velocity component
                float velDotN = glm::dot(m_velocities[i], normal2D);
                if (velDotN < 0.0f)
                {
                    m_velocities[i] -= normal2D * velDotN;
                }
                applyFriction(m_velocities[i], normal2D);
            }
        }
    }

    // Plane collisions — push particles to positive-normal side.
    // No margin for planes: they are abstract boundaries (bow-limiters, ceilings),
    // not physical geometry. Adding margin to planes injects energy that accumulates
    // through constraint interaction and causes panels to drift.
    for (const auto& plane : m_planeColliders)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }

            float dist = glm::dot(m_positions[i], plane.normal) - plane.offset;
            if (dist < 0.0f)
            {
                m_positions[i] -= plane.normal * dist;

                // Remove velocity component going into the plane
                float velDotN = glm::dot(m_velocities[i], plane.normal);
                if (velDotN < 0.0f)
                {
                    m_velocities[i] -= plane.normal * velDotN;
                }
                applyFriction(m_velocities[i], plane.normal);
            }
        }
    }

    // Triangle mesh collisions (BVH-accelerated)
    for (const auto* mesh : m_meshColliders)
    {
        if (mesh == nullptr || !mesh->isBuilt())
        {
            continue;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            if (m_inverseMasses[i] <= 0.0f)
            {
                continue;
            }

            glm::vec3 closestPt;
            glm::vec3 normal;
            float dist = 0.0f;

            if (mesh->queryClosest(m_positions[i], COLLISION_MARGIN, closestPt, normal, dist))
            {
                // Push particle to surface + margin along normal
                m_positions[i] = closestPt + normal * COLLISION_MARGIN;

                // Remove inward velocity component
                float velDotN = glm::dot(m_velocities[i], normal);
                if (velDotN < 0.0f)
                {
                    m_velocities[i] -= normal * velDotN;
                }
                applyFriction(m_velocities[i], normal);
            }
        }
    }

    // Self-collision (spatial hash broad phase + distance narrow phase)
    applySelfCollision();
}

// ---------------------------------------------------------------------------
// Mesh collider management
// ---------------------------------------------------------------------------

void ClothSimulator::addMeshCollider(ClothMeshCollider* collider)
{
    if (collider != nullptr)
    {
        m_meshColliders.push_back(collider);
    }
}

void ClothSimulator::clearMeshColliders()
{
    m_meshColliders.clear();
}

// ---------------------------------------------------------------------------
// Self-collision
// ---------------------------------------------------------------------------

void ClothSimulator::enableSelfCollision(bool enable)
{
    m_selfCollision = enable;
}

void ClothSimulator::setSelfCollisionDistance(float distance)
{
    m_selfCollisionDist = std::max(distance, 0.001f);
}

bool ClothSimulator::isSelfCollisionEnabled() const
{
    return m_selfCollision;
}

float ClothSimulator::getSelfCollisionDistance() const
{
    return m_selfCollisionDist;
}

void ClothSimulator::applySelfCollision()
{
    if (!m_selfCollision || m_positions.size() < 4)
    {
        return;
    }

    uint32_t count = static_cast<uint32_t>(m_positions.size());

    // Build spatial hash with cell size = 2× collision distance
    m_spatialHash.build(m_positions.data(), count, m_selfCollisionDist * 2.0f);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (m_inverseMasses[i] <= 0.0f)
        {
            continue;
        }

        m_selfCollisionNeighbors.clear();
        m_spatialHash.query(m_positions[i], m_selfCollisionDist, i,
                            m_selfCollisionNeighbors);

        for (uint32_t j : m_selfCollisionNeighbors)
        {
            // Avoid duplicate pairs (only process i < j)
            if (j <= i)
            {
                continue;
            }
            if (m_inverseMasses[j] <= 0.0f)
            {
                continue;
            }
            // Skip adjacent grid neighbors (they share mesh edges/triangles)
            if (areGridAdjacent(i, j))
            {
                continue;
            }

            glm::vec3 diff = m_positions[j] - m_positions[i];
            float dist = glm::length(diff);

            if (dist < m_selfCollisionDist && dist > 1e-7f)
            {
                // Push particles apart symmetrically
                glm::vec3 normal = diff / dist;
                float correction = (m_selfCollisionDist - dist) * 0.5f;
                m_positions[i] -= normal * correction;
                m_positions[j] += normal * correction;

                // Dampen relative velocity along collision normal
                glm::vec3 relVel = m_velocities[j] - m_velocities[i];
                float relVelN = glm::dot(relVel, normal);
                if (relVelN < 0.0f)
                {
                    m_velocities[i] += normal * (relVelN * 0.5f);
                    m_velocities[j] -= normal * (relVelN * 0.5f);
                }
            }
        }
    }
}

bool ClothSimulator::areGridAdjacent(uint32_t i, uint32_t j) const
{
    if (m_gridW == 0)
    {
        return false;
    }

    uint32_t ri = i / m_gridW;
    uint32_t ci = i % m_gridW;
    uint32_t rj = j / m_gridW;
    uint32_t cj = j % m_gridW;

    int dr = static_cast<int>(ri) - static_cast<int>(rj);
    int dc = static_cast<int>(ci) - static_cast<int>(cj);

    return (dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1);
}

// ---------------------------------------------------------------------------
// Dihedral bending constraints
// ---------------------------------------------------------------------------

void ClothSimulator::buildDihedralConstraints()
{
    m_dihedralConstraints.clear();

    if (m_indices.size() < 6)
    {
        return;  // Need at least 2 triangles
    }

    // Build edge adjacency: for each edge (sorted vertex pair), store the two
    // triangles that share it and the wing vertices opposite the edge.
    // Key: (min(v0,v1), max(v0,v1)) → (wing0, wing1, count)
    struct EdgeInfo
    {
        uint32_t wing[2];
        int count;
    };

    // Use a map keyed by packed edge (two uint32_t → one uint64_t)
    std::unordered_map<uint64_t, EdgeInfo> edgeMap;
    edgeMap.reserve(m_indices.size());

    auto packEdge = [](uint32_t a, uint32_t b) -> uint64_t
    {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
    };

    for (size_t t = 0; t + 2 < m_indices.size(); t += 3)
    {
        uint32_t tri[3] = { m_indices[t], m_indices[t + 1], m_indices[t + 2] };

        for (int e = 0; e < 3; ++e)
        {
            uint32_t e0 = tri[e];
            uint32_t e1 = tri[(e + 1) % 3];
            uint32_t wing = tri[(e + 2) % 3];  // Vertex opposite this edge

            uint64_t key = packEdge(e0, e1);
            auto it = edgeMap.find(key);
            if (it == edgeMap.end())
            {
                EdgeInfo info{};
                info.wing[0] = wing;
                info.count = 1;
                edgeMap[key] = info;
            }
            else if (it->second.count == 1)
            {
                it->second.wing[1] = wing;
                it->second.count = 2;
            }
            // count > 2: non-manifold edge, skip
        }
    }

    // Create dihedral constraints for all interior edges (shared by exactly 2 triangles)
    for (const auto& [key, info] : edgeMap)
    {
        if (info.count != 2)
        {
            continue;
        }

        uint32_t edgeV0 = static_cast<uint32_t>(key >> 32);
        uint32_t edgeV1 = static_cast<uint32_t>(key & 0xFFFFFFFF);

        // p0 = wing of triangle 0, p1 = wing of triangle 1
        // p2 = edge start, p3 = edge end
        const glm::vec3& p0 = m_positions[info.wing[0]];
        const glm::vec3& p1 = m_positions[info.wing[1]];
        const glm::vec3& p2 = m_positions[edgeV0];
        const glm::vec3& p3 = m_positions[edgeV1];

        // Compute rest angle
        glm::vec3 n1 = glm::cross(p2 - p0, p3 - p0);
        glm::vec3 n2 = glm::cross(p3 - p1, p2 - p1);
        float len1 = glm::length(n1);
        float len2 = glm::length(n2);

        float restAngle = 0.0f;  // Default flat
        if (len1 > 1e-7f && len2 > 1e-7f)
        {
            n1 /= len1;
            n2 /= len2;
            float cosAngle = glm::clamp(glm::dot(n1, n2), -1.0f, 1.0f);
            restAngle = fastAcos(cosAngle);
        }

        m_dihedralConstraints.push_back({
            info.wing[0], info.wing[1], edgeV0, edgeV1,
            restAngle, m_dihedralCompliance
        });
    }
}

void ClothSimulator::solveDihedralConstraint(DihedralConstraint& c, float dtSub)
{
    glm::vec3& p0 = m_positions[c.p0];  // Wing vertex 0
    glm::vec3& p1 = m_positions[c.p1];  // Wing vertex 1
    glm::vec3& p2 = m_positions[c.p2];  // Shared edge start
    glm::vec3& p3 = m_positions[c.p3];  // Shared edge end

    float w0 = m_inverseMasses[c.p0];
    float w1 = m_inverseMasses[c.p1];
    float w2 = m_inverseMasses[c.p2];
    float w3 = m_inverseMasses[c.p3];

    float wTotal = w0 + w1 + w2 + w3;
    if (wTotal <= 0.0f)
    {
        return;  // All pinned
    }

    // Compute triangle normals
    glm::vec3 n1 = glm::cross(p2 - p0, p3 - p0);
    glm::vec3 n2 = glm::cross(p3 - p1, p2 - p1);
    float len1 = glm::length(n1);
    float len2 = glm::length(n2);

    if (len1 < 1e-7f || len2 < 1e-7f)
    {
        return;  // Degenerate triangle
    }

    n1 /= len1;
    n2 /= len2;

    // Current dihedral angle
    float cosAngle = glm::clamp(glm::dot(n1, n2), -1.0f, 1.0f);
    float phi = fastAcos(cosAngle);

    // Constraint value
    float constraint = phi - c.restAngle;
    if (std::abs(constraint) < 1e-7f)
    {
        return;  // Already at rest angle
    }

    // Shared edge vector
    glm::vec3 e = p3 - p2;
    float elen = glm::length(e);
    if (elen < 1e-7f)
    {
        return;  // Degenerate edge
    }
    float invElen = 1.0f / elen;

    // Gradients (Müller et al. 2007)
    glm::vec3 grad0 = n1 * elen;
    glm::vec3 grad1 = n2 * elen;
    glm::vec3 grad2 = n1 * (glm::dot(p0 - p3, e) * invElen)
                     + n2 * (glm::dot(p1 - p3, e) * invElen);
    glm::vec3 grad3 = n1 * (glm::dot(p2 - p0, e) * invElen)
                     + n2 * (glm::dot(p2 - p1, e) * invElen);

    // XPBD correction
    float dtSub2 = dtSub * dtSub;
    float alphaTilde = c.compliance / dtSub2;

    float wSum = w0 * glm::dot(grad0, grad0)
               + w1 * glm::dot(grad1, grad1)
               + w2 * glm::dot(grad2, grad2)
               + w3 * glm::dot(grad3, grad3);

    if (wSum + alphaTilde < 1e-10f)
    {
        return;
    }

    float lambda = -constraint / (wSum + alphaTilde);

    // Sign correction: if cross(n1, n2) points along the edge, angle is positive
    if (glm::dot(glm::cross(n1, n2), e) > 0.0f)
    {
        lambda = -lambda;
    }

    p0 += grad0 * (w0 * lambda);
    p1 += grad1 * (w1 * lambda);
    p2 += grad2 * (w2 * lambda);
    p3 += grad3 * (w3 * lambda);
}

// ---------------------------------------------------------------------------
// Constraint ordering (BFS from pins)
// ---------------------------------------------------------------------------

void ClothSimulator::sortConstraintsByDepth()
{
    uint32_t count = static_cast<uint32_t>(m_positions.size());
    m_particleDepth.assign(count, UINT32_MAX);

    // BFS from all pinned particles
    std::vector<uint32_t> queue;
    queue.reserve(count);
    for (const auto& pin : m_pinConstraints)
    {
        m_particleDepth[pin.index] = 0;
        queue.push_back(pin.index);
    }

    // Build adjacency from stretch constraints (direct neighbors)
    std::vector<std::vector<uint32_t>> adj(count);
    for (const auto& c : m_stretchConstraints)
    {
        adj[c.i0].push_back(c.i1);
        adj[c.i1].push_back(c.i0);
    }

    // BFS
    size_t front = 0;
    while (front < queue.size())
    {
        uint32_t cur = queue[front++];
        uint32_t depth = m_particleDepth[cur];
        for (uint32_t neighbor : adj[cur])
        {
            if (m_particleDepth[neighbor] == UINT32_MAX)
            {
                m_particleDepth[neighbor] = depth + 1;
                queue.push_back(neighbor);
            }
        }
    }

    // Sort each constraint list by minimum depth of participating particles
    auto depthOf = [this](const DistanceConstraint& c) -> uint32_t
    {
        return std::min(m_particleDepth[c.i0], m_particleDepth[c.i1]);
    };

    std::sort(m_stretchConstraints.begin(), m_stretchConstraints.end(),
        [&](const DistanceConstraint& a, const DistanceConstraint& b)
        { return depthOf(a) < depthOf(b); });

    std::sort(m_shearConstraints.begin(), m_shearConstraints.end(),
        [&](const DistanceConstraint& a, const DistanceConstraint& b)
        { return depthOf(a) < depthOf(b); });

    std::sort(m_bendConstraints.begin(), m_bendConstraints.end(),
        [&](const DistanceConstraint& a, const DistanceConstraint& b)
        { return depthOf(a) < depthOf(b); });

    auto dihedralDepth = [this](const DihedralConstraint& c) -> uint32_t
    {
        return std::min({m_particleDepth[c.p0], m_particleDepth[c.p1],
                         m_particleDepth[c.p2], m_particleDepth[c.p3]});
    };

    std::sort(m_dihedralConstraints.begin(), m_dihedralConstraints.end(),
        [&](const DihedralConstraint& a, const DihedralConstraint& b)
        { return dihedralDepth(a) < dihedralDepth(b); });

    m_constraintsSorted = true;
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

/// @brief Fast integer-based hash noise — replaces sin-based GPU-style hash.
/// Produces same quality pseudo-random distribution as sin(x*127.1+y*311.7)*43758.5
/// but uses integer bit-mixing (~5 cycles) instead of std::sin (~30 cycles).
static float hashNoise(float x, float y)
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
            // Calm periods must be long enough for fabric to swing back to
            // its natural hanging position. A 4m curtain has a pendulum
            // period of ~4 seconds, so calm needs 3-7 seconds minimum.
            m_gustTarget = 0.0f;
            m_gustTimer = randRange(3.0f, 7.0f);   // Calm for 3-7 seconds
            m_gustRampSpeed = randRange(3.0f, 6.0f); // Wind dies off quickly
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
    if (m_windStrength <= 0.0f || dt <= 0.0f || m_windQuality == WindQuality::SIMPLE)
    {
        return;
    }

    float t = m_elapsed;

    // Gust state is updated once per frame in simulate(), not per substep here.

    // Add high-frequency flutter on top of gust envelope
    float flutter = 1.0f + 0.15f * std::sin(t * 7.3f + 1.1f)
                        + 0.08f * std::sin(t * 13.7f + 3.2f);

    float gustStrength = m_gustCurrent * flutter;

    // Effective wind direction = base + current offset
    glm::vec3 effectiveDir = m_windDirection + m_windDirOffset;
    glm::vec3 baseWindVel = effectiveDir * (m_windStrength * gustStrength);

    // --- Per-particle perturbation (FULL quality only) ---
    // Skipped in APPROXIMATE — uniform wind is applied via per-triangle drag below.
    if (m_windQuality == WindQuality::FULL && m_gridW > 0 && m_gridH > 0)
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

    // --- Per-triangle aerodynamic drag (FULL + APPROXIMATE) ---
    for (size_t ti = 0; ti + 2 < m_indices.size(); ti += 3)
    {
        uint32_t i0 = m_indices[ti];
        uint32_t i1 = m_indices[ti + 1];
        uint32_t i2 = m_indices[ti + 2];

        // Spatial turbulence from triangle centroid (FULL quality only)
        glm::vec3 windVel;
        if (m_windQuality == WindQuality::FULL)
        {
            glm::vec3 centroid = (m_positions[i0] + m_positions[i1] + m_positions[i2]) / 3.0f;
            float spatialNoise = hashNoise(centroid.x * 3.0f + t * 1.1f,
                                            centroid.y * 2.0f + t * 0.8f);
            float turb = 0.5f + spatialNoise;  // [0.5, 1.5]
            windVel = baseWindVel * turb;
        }
        else
        {
            windVel = baseWindVel;  // APPROXIMATE: uniform wind, no per-triangle noise
        }

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
