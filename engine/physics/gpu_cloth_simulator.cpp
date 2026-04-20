// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_cloth_simulator.cpp
/// @brief GpuClothSimulator skeleton — Phase 9B Step 2.

#include "physics/gpu_cloth_simulator.h"
#include "core/logger.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace Vestige
{

GpuClothSimulator::GpuClothSimulator() = default;

GpuClothSimulator::~GpuClothSimulator()
{
    destroyBuffers();
}

bool GpuClothSimulator::isSupported()
{
    // Probe is safe to call without a context. GLAD reports zero / nulls
    // for the version query when no context is current; treat that as
    // "not supported" rather than crashing on the GL call.
    if (glfwGetCurrentContext() == nullptr) return false;

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    // Compute shaders + SSBO are core in 4.3. We require 4.5 because the
    // engine uses glCreateBuffers / glNamedBufferStorage (DSA, 4.5 core).
    return (major > 4) || (major == 4 && minor >= 5);
}

void GpuClothSimulator::setShaderPath(const std::string& path)
{
    m_shaderPath = path;
}

void GpuClothSimulator::setSubsteps(int substeps)
{
    if (substeps < 1) substeps = 1;
    m_substeps = substeps;
    m_config.substeps = substeps;
}

void GpuClothSimulator::setParticleMass(float mass)
{
    if (mass <= 0.0f) return;
    m_config.particleMass = mass;
    // On GPU, particle mass translates to positions[i].w (inverse mass). The
    // pin buffer upload will re-pack w on next simulate via uploadPinsIfDirty.
    m_pinsDirty = true;
}

void GpuClothSimulator::setStretchCompliance(float compliance)
{
    m_config.stretchCompliance = compliance;
    m_compliancesDirty = true;
}

void GpuClothSimulator::setShearCompliance(float compliance)
{
    m_config.shearCompliance = compliance;
    m_compliancesDirty = true;
}

void GpuClothSimulator::setBendCompliance(float compliance)
{
    m_config.bendCompliance = compliance;
    m_compliancesDirty = true;
}

void GpuClothSimulator::setWind(const glm::vec3& direction, float strength)
{
    const float len = glm::length(direction);
    const glm::vec3 unit = (len > 0.0f) ? (direction / len) : glm::vec3(0.0f);
    m_windVelocity = unit * strength;
}

glm::vec3 GpuClothSimulator::getWindDirection() const
{
    const float len = glm::length(m_windVelocity);
    if (len <= 1e-7f) return glm::vec3(0.0f);
    return m_windVelocity / len;
}

float GpuClothSimulator::getWindStrength() const
{
    return glm::length(m_windVelocity);
}

void GpuClothSimulator::addSphereCollider(const glm::vec3& center, float radius)
{
    if (radius <= 0.0f) return;
    if (m_sphereColliders.size() >= static_cast<size_t>(MAX_GPU_SPHERE_COLLIDERS))
    {
        Logger::warning("[GpuClothSimulator] sphere collider cap reached ("
                        + std::to_string(MAX_GPU_SPHERE_COLLIDERS)
                        + "); silently dropping this one");
        return;
    }
    m_sphereColliders.emplace_back(center, radius);
    m_collidersDirty = true;
}

void GpuClothSimulator::clearSphereColliders()
{
    if (m_sphereColliders.empty()) return;
    m_sphereColliders.clear();
    m_collidersDirty = true;
}

bool GpuClothSimulator::addPlaneCollider(const glm::vec3& normal, float offset)
{
    const float len = glm::length(normal);
    if (len <= 1e-7f) return false;
    if (m_planeColliders.size() >= static_cast<size_t>(MAX_GPU_PLANE_COLLIDERS))
    {
        Logger::warning("[GpuClothSimulator] plane collider cap reached ("
                        + std::to_string(MAX_GPU_PLANE_COLLIDERS)
                        + "); silently dropping this one");
        return false;
    }
    m_planeColliders.emplace_back(normal / len, offset);
    m_collidersDirty = true;
    return true;
}

void GpuClothSimulator::clearPlaneColliders()
{
    if (m_planeColliders.empty()) return;
    m_planeColliders.clear();
    m_collidersDirty = true;
}

void GpuClothSimulator::setGroundPlane(float worldY)
{
    if (worldY == m_groundY) return;
    m_groundY = worldY;
    m_collidersDirty = true;
}

void GpuClothSimulator::setCollisionMargin(float margin)
{
    if (margin < 0.0f) margin = 0.0f;
    if (margin == m_collisionMargin) return;
    m_collisionMargin = margin;
    m_collidersDirty = true;
}

// Cylinder / box colliders — GPU backend doesn't implement these yet. Logged
// once per backend instance so a call site driving both CPU and GPU doesn't
// flood the log. See `docs/PHASE9B_GPU_CLOTH_DESIGN.md` § 9.
void GpuClothSimulator::addCylinderCollider(const glm::vec3& /*base*/,
                                             float /*radius*/, float /*height*/)
{
    static bool warned = false;
    if (!warned)
    {
        Logger::warning("[GpuClothSimulator] cylinder colliders are CPU-only; "
                        "GPU backend will ignore them");
        warned = true;
    }
}

void GpuClothSimulator::clearCylinderColliders() {}

void GpuClothSimulator::addBoxCollider(const glm::vec3& /*mn*/, const glm::vec3& /*mx*/)
{
    static bool warned = false;
    if (!warned)
    {
        Logger::warning("[GpuClothSimulator] box colliders are CPU-only; "
                        "GPU backend will ignore them");
        warned = true;
    }
}

void GpuClothSimulator::clearBoxColliders() {}

bool GpuClothSimulator::pinParticle(uint32_t index, const glm::vec3& worldPos)
{
    if (index >= m_particleCount) return false;
    m_positionMirror[index] = worldPos;
    if (m_invMassMirror[index] != 0.0f)
    {
        m_invMassMirror[index] = 0.0f;
        m_pinIndices.push_back(index);
    }
    m_pinsDirty = true;
    return true;
}

void GpuClothSimulator::unpinParticle(uint32_t index)
{
    if (index >= m_particleCount) return;
    if (m_invMassMirror[index] == 0.0f)
    {
        m_invMassMirror[index] = 1.0f;
        m_pinIndices.erase(
            std::remove(m_pinIndices.begin(), m_pinIndices.end(), index),
            m_pinIndices.end());
        m_pinsDirty = true;
    }
}

void GpuClothSimulator::setPinPosition(uint32_t index, const glm::vec3& worldPos)
{
    if (index >= m_particleCount) return;
    if (m_invMassMirror[index] != 0.0f) return;  // Not pinned: silently ignore.
    m_positionMirror[index] = worldPos;
    m_pinsDirty = true;
}

bool GpuClothSimulator::isParticlePinned(uint32_t index) const
{
    return index < m_invMassMirror.size() && m_invMassMirror[index] == 0.0f;
}

void GpuClothSimulator::rebuildLRA()
{
    m_lras.clear();
    if (m_pinIndices.empty())
    {
        m_lraCount = 0;
        if (m_lraSSBO) { glDeleteBuffers(1, &m_lraSSBO); m_lraSSBO = 0; }
        return;
    }

    generateLraConstraints(m_positionMirror, m_pinIndices, m_lras);
    m_lraCount = static_cast<uint32_t>(m_lras.size());
    if (m_lraCount == 0) return;

    // Re-allocate the SSBO if size changed (or first build).
    if (m_lraSSBO) { glDeleteBuffers(1, &m_lraSSBO); m_lraSSBO = 0; }
    glCreateBuffers(1, &m_lraSSBO);
    glNamedBufferStorage(
        m_lraSSBO,
        static_cast<GLsizeiptr>(m_lraCount * sizeof(GpuLraConstraint)),
        m_lras.data(),
        /*flags=*/0);
}

void GpuClothSimulator::uploadPinsIfDirty()
{
    if (!m_pinsDirty || m_positionsSSBO == 0) return;

    // Re-pack the entire positions buffer from the CPU mirror. Cheap (a few
    // hundred KB at typical sizes) and avoids tracking per-particle dirty
    // bits. Velocities for newly-pinned particles are zeroed so the integrate
    // shader's w==0 short-circuit can't be sabotaged by stale velocity.
    std::vector<glm::vec4> packed(m_particleCount);
    for (uint32_t i = 0; i < m_particleCount; ++i)
    {
        packed[i] = glm::vec4(m_positionMirror[i], m_invMassMirror[i]);
    }
    const GLsizeiptr bytes =
        static_cast<GLsizeiptr>(m_particleCount * sizeof(glm::vec4));
    glNamedBufferSubData(m_positionsSSBO,     0, bytes, packed.data());
    glNamedBufferSubData(m_prevPositionsSSBO, 0, bytes, packed.data());

    // Zero velocities for currently-pinned particles to prevent residual
    // motion before the integrate shader skips them.
    if (!m_pinIndices.empty())
    {
        std::vector<glm::vec4> velPacked(m_particleCount);
        glGetNamedBufferSubData(
            m_velocitiesSSBO, 0, bytes, velPacked.data());
        for (uint32_t p : m_pinIndices) velPacked[p] = glm::vec4(0.0f);
        glNamedBufferSubData(m_velocitiesSSBO, 0, bytes, velPacked.data());
    }

    m_pinsDirty = false;
}

void GpuClothSimulator::initialize(const ClothConfig& config, uint32_t /*seed*/)
{
    if (m_initialized) destroyBuffers();

    m_config        = config;  // Cached for live-tuning mutators + getConfig().
    m_gridW = config.width;
    m_gridH = config.height;
    m_particleCount = m_gridW * m_gridH;
    m_gravity       = config.gravity;
    m_damping       = config.damping;
    m_substeps      = (config.substeps < 1) ? 1 : config.substeps;

    if (m_particleCount == 0)
    {
        Logger::warning("[GpuClothSimulator] Refusing to initialize with zero particles");
        return;
    }

    buildInitialGrid(config);
    createBuffers();
    buildAndUploadConstraints(config);
    buildAndUploadDihedrals(config);
    loadShadersIfNeeded();

    m_initialized = true;
    Logger::info("[GpuClothSimulator] Initialized "
                 + std::to_string(m_gridW) + "x" + std::to_string(m_gridH)
                 + " grid (" + std::to_string(m_particleCount) + " particles), "
                 + (m_shadersLoaded ? "compute pipeline live"
                                    : "compute pipeline OFF (no shader path)"));
}

void GpuClothSimulator::loadShadersIfNeeded()
{
    if (m_shadersLoaded || m_shaderPath.empty()) return;

    const std::string windPath        = m_shaderPath + "/cloth_wind.comp.glsl";
    const std::string integPath       = m_shaderPath + "/cloth_integrate.comp.glsl";
    const std::string constraintsPath = m_shaderPath + "/cloth_constraints.comp.glsl";

    if (!m_windShader.loadComputeShader(windPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + windPath);
        return;
    }
    if (!m_integrateShader.loadComputeShader(integPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + integPath);
        return;
    }
    if (!m_constraintsShader.loadComputeShader(constraintsPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + constraintsPath);
        return;
    }
    const std::string dihedralPath = m_shaderPath + "/cloth_dihedral.comp.glsl";
    if (!m_dihedralShader.loadComputeShader(dihedralPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + dihedralPath);
        return;
    }
    const std::string collisionPath = m_shaderPath + "/cloth_collision.comp.glsl";
    if (!m_collisionShader.loadComputeShader(collisionPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + collisionPath);
        return;
    }
    const std::string normalsPath = m_shaderPath + "/cloth_normals.comp.glsl";
    if (!m_normalsShader.loadComputeShader(normalsPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + normalsPath);
        return;
    }
    const std::string lraPath = m_shaderPath + "/cloth_lra.comp.glsl";
    if (!m_lraShader.loadComputeShader(lraPath))
    {
        Logger::error("[GpuClothSimulator] Failed to load " + lraPath);
        return;
    }
    m_shadersLoaded = true;
}

void GpuClothSimulator::uploadCollidersIfDirty()
{
    if (!m_collidersDirty) return;

    // std140 layout — array stride for vec4 is 16, so the C++ side just needs
    // contiguous glm::vec4 arrays. Pad unused slots to zero so the shader's
    // bounded loop is harmless when sphereCount/planeCount < MAX_*.
    struct ColliderBlock
    {
        glm::vec4 spheres[MAX_GPU_SPHERE_COLLIDERS];
        glm::vec4 planes [MAX_GPU_PLANE_COLLIDERS];
        int   sphereCount;
        int   planeCount;
        float groundY;
        float collisionMargin;
    };
    static_assert(sizeof(ColliderBlock) % 16 == 0,
                  "ColliderBlock must be 16-byte aligned for std140 UBO upload");

    if (m_collidersUBO == 0)
    {
        glCreateBuffers(1, &m_collidersUBO);
        glNamedBufferStorage(m_collidersUBO, sizeof(ColliderBlock),
                              nullptr, GL_DYNAMIC_STORAGE_BIT);
    }

    ColliderBlock block{};
    const size_t sphereN = std::min(m_sphereColliders.size(),
                                     static_cast<size_t>(MAX_GPU_SPHERE_COLLIDERS));
    const size_t planeN  = std::min(m_planeColliders.size(),
                                     static_cast<size_t>(MAX_GPU_PLANE_COLLIDERS));
    for (size_t i = 0; i < sphereN; ++i) block.spheres[i] = m_sphereColliders[i];
    for (size_t i = 0; i < planeN;  ++i) block.planes[i]  = m_planeColliders[i];
    block.sphereCount     = static_cast<int>(sphereN);
    block.planeCount      = static_cast<int>(planeN);
    block.groundY         = m_groundY;
    block.collisionMargin = m_collisionMargin;

    glNamedBufferSubData(m_collidersUBO, 0, sizeof(ColliderBlock), &block);
    m_collidersDirty = false;
}

void GpuClothSimulator::buildAndUploadConstraints(const ClothConfig& config)
{
    m_constraints.clear();
    m_colourRanges.clear();
    m_constraintCount = 0;

    generateGridConstraints(m_gridW, m_gridH, m_positionMirror,
                             config.stretchCompliance,
                             config.shearCompliance,
                             config.bendCompliance,
                             m_constraints);
    if (m_constraints.empty()) return;

    m_colourRanges = colourConstraints(m_constraints, m_particleCount);
    m_constraintCount = static_cast<uint32_t>(m_constraints.size());

    glCreateBuffers(1, &m_constraintsSSBO);
    glNamedBufferStorage(
        m_constraintsSSBO,
        static_cast<GLsizeiptr>(m_constraintCount * sizeof(GpuConstraint)),
        m_constraints.data(),
        /*flags=*/0);  // Immutable for the cloth's lifetime.

    Logger::info("[GpuClothSimulator] Built " + std::to_string(m_constraintCount)
                 + " constraints in " + std::to_string(m_colourRanges.size())
                 + " colour groups");
}

void GpuClothSimulator::buildAndUploadDihedrals(const ClothConfig& config)
{
    m_dihedrals.clear();
    m_dihedralColourRanges.clear();
    m_dihedralCount = 0;

    // Use the same compliance the CPU dihedral solver defaults to
    // (`m_dihedralCompliance = 0.01f`). The runtime accessor `setDihedralBendCompliance`
    // on the CPU type covers tuning; the GPU backend will mirror that surface in
    // a later step. For Step 6 we lock to the CPU default so visual parity holds.
    constexpr float dihedralCompliance = 0.01f;
    (void)config;  // bend distance compliance is separate (config.bendCompliance is for Step 5).

    generateDihedralConstraints(m_indices, m_positionMirror,
                                 dihedralCompliance, m_dihedrals);
    if (m_dihedrals.empty()) return;

    m_dihedralColourRanges = colourDihedralConstraints(m_dihedrals, m_particleCount);
    m_dihedralCount = static_cast<uint32_t>(m_dihedrals.size());

    glCreateBuffers(1, &m_dihedralsSSBO);
    glNamedBufferStorage(
        m_dihedralsSSBO,
        static_cast<GLsizeiptr>(m_dihedralCount * sizeof(GpuDihedralConstraint)),
        m_dihedrals.data(),
        /*flags=*/0);

    Logger::info("[GpuClothSimulator] Built " + std::to_string(m_dihedralCount)
                 + " dihedrals in " + std::to_string(m_dihedralColourRanges.size())
                 + " colour groups");
}

void GpuClothSimulator::simulate(float deltaTime)
{
    if (!m_initialized || !m_shadersLoaded) return;
    if (deltaTime <= 0.0f) return;

    uploadCollidersIfDirty();
    uploadPinsIfDirty();

    const GLuint particleGroups = (m_particleCount + 63u) / 64u;
    const float  dtSub          = deltaTime / static_cast<float>(m_substeps);
    const float  dtSubSquared   = dtSub * dtSub;

    // Damping is intended as a per-frame coefficient on the CPU path; spread
    // it across substeps here so behaviour stays comparable as substep count
    // changes. Cap to [0,1] so a misconfigured damping can't flip the sign.
    const float dampingPerSub = (m_substeps > 0)
        ? std::min(1.0f, std::max(0.0f, m_damping / static_cast<float>(m_substeps)))
        : m_damping;

    for (int s = 0; s < m_substeps; ++s)
    {
        // 1. Wind / external forces → updates velocities only.
        m_windShader.use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_VELOCITIES, m_velocitiesSSBO);
        m_windShader.setUInt("u_particleCount", m_particleCount);
        m_windShader.setVec3("u_gravity",       m_gravity);
        m_windShader.setVec3("u_windVelocity",  m_windVelocity);
        m_windShader.setFloat("u_dragCoeff",    m_dragCoeff);
        m_windShader.setFloat("u_deltaTime",    dtSub);
        glDispatchCompute(particleGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // 2. Symplectic Euler integration → updates positions + prev positions.
        m_integrateShader.use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_POSITIONS,      m_positionsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_PREV_POSITIONS, m_prevPositionsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_VELOCITIES,     m_velocitiesSSBO);
        m_integrateShader.setUInt("u_particleCount", m_particleCount);
        m_integrateShader.setFloat("u_deltaTime",    dtSub);
        m_integrateShader.setFloat("u_damping",      dampingPerSub);
        glDispatchCompute(particleGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // 3. Distance-constraint solve — one Gauss-Seidel sweep through every
        //    colour. Within a colour, no two constraints share a particle, so
        //    the writes are race-free without atomics.
        if (m_constraintCount > 0)
        {
            m_constraintsShader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_POSITIONS,   m_positionsSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_CONSTRAINTS, m_constraintsSSBO);
            m_constraintsShader.setFloat("u_dtSubSquared", dtSubSquared);
            for (const auto& range : m_colourRanges)
            {
                if (range.count == 0) continue;
                m_constraintsShader.setUInt("u_colorOffset", range.offset);
                m_constraintsShader.setUInt("u_colorCount",  range.count);
                const GLuint constraintGroups = (range.count + 63u) / 64u;
                glDispatchCompute(constraintGroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }

        // 4. Dihedral bending solve — same per-colour structure, smaller
        //    workgroups (32 vs 64) because the per-thread register footprint is
        //    larger (4 particles + gradient computations).
        if (m_dihedralCount > 0)
        {
            m_dihedralShader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_POSITIONS, m_positionsSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_DIHEDRALS, m_dihedralsSSBO);
            m_dihedralShader.setFloat("u_dtSubSquared", dtSubSquared);
            for (const auto& range : m_dihedralColourRanges)
            {
                if (range.count == 0) continue;
                m_dihedralShader.setUInt("u_colorOffset", range.offset);
                m_dihedralShader.setUInt("u_colorCount",  range.count);
                const GLuint dihedralGroups = (range.count + 31u) / 32u;
                glDispatchCompute(dihedralGroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }

        // 5. Collision response — sphere + plane + ground. One thread per
        //    particle; each particle is the unique writer of its position
        //    and velocity entries so no atomics are needed even though
        //    multiple colliders may all act on the same particle within
        //    one shader invocation (the loops are serial inside the thread).
        if (m_collidersUBO != 0)
        {
            m_collisionShader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_POSITIONS,  m_positionsSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_VELOCITIES, m_velocitiesSSBO);
            glBindBufferBase(GL_UNIFORM_BUFFER,        BIND_COLLIDERS_UBO, m_collidersUBO);
            m_collisionShader.setUInt("u_particleCount", m_particleCount);
            glDispatchCompute(particleGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // 6. LRA tether solve — unilateral; each thread is the unique writer
        //    of its own particle, pin position is read-only. No colouring
        //    needed.
        if (m_lraCount > 0 && m_lraSSBO != 0)
        {
            m_lraShader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_POSITIONS, m_positionsSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_LRAS,      m_lraSSBO);
            m_lraShader.setUInt("u_lraCount", m_lraCount);
            const GLuint lraGroups = (m_lraCount + 63u) / 64u;
            glDispatchCompute(lraGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }

    // 6. Normal recomputation — runs once per frame (not per substep) since
    //    normals are for rendering, not physics. One thread per particle walks
    //    the (up to) 6 grid-adjacent triangles via the cloth_normals shader.
    {
        m_normalsShader.use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_POSITIONS, m_positionsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_NORMALS,   m_normalsSSBO);
        m_normalsShader.setUInt("u_particleCount", m_particleCount);
        m_normalsShader.setUInt("u_gridW",         m_gridW);
        m_normalsShader.setUInt("u_gridH",         m_gridH);
        glDispatchCompute(particleGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // CPU mirror is now stale — refreshed lazily on getPositions() / getNormals().
    m_positionsDirty = true;
    m_normalsDirty   = true;
}

void GpuClothSimulator::reset()
{
    if (!m_initialized) return;

    // Re-upload the initial grid into all four particle SSBOs so the GPU
    // state matches the rest pose, then mark the CPU mirror dirty so the
    // next getPositions() picks the rest pose up too. The CPU mirror was
    // built in buildInitialGrid() and is preserved across simulate() calls
    // (only its values are mutated by readback), so we can use it directly
    // as the rest-pose source.
    std::vector<glm::vec4> rest(m_particleCount);
    for (uint32_t i = 0; i < m_particleCount; ++i)
    {
        rest[i] = glm::vec4(m_positionMirror[i], 1.0f);
    }
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(m_particleCount * sizeof(glm::vec4));
    glNamedBufferSubData(m_positionsSSBO,     0, bytes, rest.data());
    glNamedBufferSubData(m_prevPositionsSSBO, 0, bytes, rest.data());

    std::vector<glm::vec4> zeros(m_particleCount, glm::vec4(0.0f));
    glNamedBufferSubData(m_velocitiesSSBO,    0, bytes, zeros.data());

    // Mirror already holds the initial grid; mark clean.
    m_positionsDirty = false;
    m_normalsDirty   = false;
}

const glm::vec3* GpuClothSimulator::getPositions() const
{
    readbackPositionsIfDirty();
    return m_positionMirror.empty() ? nullptr : m_positionMirror.data();
}

const glm::vec3* GpuClothSimulator::getNormals() const
{
    readbackNormalsIfDirty();
    return m_normalMirror.empty() ? nullptr : m_normalMirror.data();
}

void GpuClothSimulator::readbackPositionsIfDirty() const
{
    if (!m_positionsDirty || m_positionsSSBO == 0) return;

    // SSBO stores vec4 (xyz + w invMass). Stage into a local vec4 buffer,
    // then narrow into the vec3 mirror. This stalls the pipeline — Step 8
    // moves the renderer to draw directly from the SSBO so the per-frame
    // path skips this entirely.
    std::vector<glm::vec4> staging(m_particleCount);
    glGetNamedBufferSubData(
        m_positionsSSBO, 0,
        static_cast<GLsizeiptr>(m_particleCount * sizeof(glm::vec4)),
        staging.data());
    for (uint32_t i = 0; i < m_particleCount; ++i)
    {
        m_positionMirror[i] = glm::vec3(staging[i]);
    }
    m_positionsDirty = false;
}

void GpuClothSimulator::readbackNormalsIfDirty() const
{
    if (!m_normalsDirty || m_normalsSSBO == 0) return;

    std::vector<glm::vec4> staging(m_particleCount);
    glGetNamedBufferSubData(
        m_normalsSSBO, 0,
        static_cast<GLsizeiptr>(m_particleCount * sizeof(glm::vec4)),
        staging.data());
    for (uint32_t i = 0; i < m_particleCount; ++i)
    {
        m_normalMirror[i] = glm::vec3(staging[i]);
    }
    m_normalsDirty = false;
}

void GpuClothSimulator::buildInitialGrid(const ClothConfig& config)
{
    // CPU-side scaffolding mirrors what `ClothSimulator::initialize()` builds:
    // a flat XZ grid of particles centered at the origin with the cloth's
    // local-frame "up" along +Y. Topology (indices, UVs) is the same too so
    // the renderer is wholly oblivious to which backend is producing data.
    const uint32_t W = config.width;
    const uint32_t H = config.height;
    const float    s = config.spacing;

    m_positionMirror.resize(m_particleCount);
    m_normalMirror.resize(m_particleCount, glm::vec3(0.0f, 1.0f, 0.0f));
    m_texCoords.resize(m_particleCount);
    m_invMassMirror.assign(m_particleCount, 1.0f);  // All free until pin applied.

    const float wMinus1 = static_cast<float>(W - 1);
    const float hMinus1 = static_cast<float>(H - 1);
    for (uint32_t z = 0; z < H; ++z)
    {
        for (uint32_t x = 0; x < W; ++x)
        {
            const uint32_t idx = z * W + x;
            const float fx = (static_cast<float>(x) - wMinus1 * 0.5f) * s;
            const float fz = (static_cast<float>(z) - hMinus1 * 0.5f) * s;
            m_positionMirror[idx] = glm::vec3(fx, 0.0f, fz);
            m_texCoords[idx]      = glm::vec2(static_cast<float>(x) / wMinus1,
                                               static_cast<float>(z) / hMinus1);
        }
    }

    // Two triangles per quad cell, (W-1) * (H-1) cells, 3 indices per triangle.
    m_indices.clear();
    m_indices.reserve((W - 1) * (H - 1) * 6);
    for (uint32_t z = 0; z + 1 < H; ++z)
    {
        for (uint32_t x = 0; x + 1 < W; ++x)
        {
            const uint32_t i0 = z * W + x;
            const uint32_t i1 = z * W + (x + 1);
            const uint32_t i2 = (z + 1) * W + x;
            const uint32_t i3 = (z + 1) * W + (x + 1);
            m_indices.push_back(i0); m_indices.push_back(i2); m_indices.push_back(i1);
            m_indices.push_back(i1); m_indices.push_back(i2); m_indices.push_back(i3);
        }
    }
}

void GpuClothSimulator::createBuffers()
{
    // Positions, prev positions, velocities, normals all use vec4 layout
    // (xyz + w padding / inverse mass) to satisfy std430's vec3-array padding
    // semantics. Encoding an inverse-mass channel is a Step 4 concern; for
    // now the w channel is initialised to 1.0 (signalling "free particle").
    std::vector<glm::vec4> initialVec4(m_particleCount);
    for (uint32_t i = 0; i < m_particleCount; ++i)
    {
        initialVec4[i] = glm::vec4(m_positionMirror[i], m_invMassMirror[i]);
    }

    const GLsizeiptr vec4Bytes = static_cast<GLsizeiptr>(m_particleCount * sizeof(glm::vec4));

    glCreateBuffers(1, &m_positionsSSBO);
    glNamedBufferStorage(m_positionsSSBO, vec4Bytes, initialVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    glCreateBuffers(1, &m_prevPositionsSSBO);
    glNamedBufferStorage(m_prevPositionsSSBO, vec4Bytes, initialVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    // Velocities start at zero.
    std::vector<glm::vec4> zeroVec4(m_particleCount, glm::vec4(0.0f));
    glCreateBuffers(1, &m_velocitiesSSBO);
    glNamedBufferStorage(m_velocitiesSSBO, vec4Bytes, zeroVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    // Normals: +Y for the flat starting pose.
    std::vector<glm::vec4> upVec4(m_particleCount, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    glCreateBuffers(1, &m_normalsSSBO);
    glNamedBufferStorage(m_normalsSSBO, vec4Bytes, upVec4.data(),
                         GL_DYNAMIC_STORAGE_BIT);

    // Index buffer: regular triangle list, immutable for the cloth's lifetime.
    const GLsizeiptr indexBytes =
        static_cast<GLsizeiptr>(m_indices.size() * sizeof(uint32_t));
    glCreateBuffers(1, &m_indicesSSBO);
    glNamedBufferStorage(m_indicesSSBO, indexBytes, m_indices.data(),
                         /*flags=*/0);  // Immutable.
}

void GpuClothSimulator::destroyBuffers()
{
    if (m_positionsSSBO)     { glDeleteBuffers(1, &m_positionsSSBO);     m_positionsSSBO = 0; }
    if (m_prevPositionsSSBO) { glDeleteBuffers(1, &m_prevPositionsSSBO); m_prevPositionsSSBO = 0; }
    if (m_velocitiesSSBO)    { glDeleteBuffers(1, &m_velocitiesSSBO);    m_velocitiesSSBO = 0; }
    if (m_constraintsSSBO)   { glDeleteBuffers(1, &m_constraintsSSBO);   m_constraintsSSBO = 0; }
    if (m_dihedralsSSBO)     { glDeleteBuffers(1, &m_dihedralsSSBO);     m_dihedralsSSBO = 0; }
    if (m_collidersUBO)      { glDeleteBuffers(1, &m_collidersUBO);      m_collidersUBO = 0; }
    if (m_lraSSBO)           { glDeleteBuffers(1, &m_lraSSBO);           m_lraSSBO = 0; }
    if (m_normalsSSBO)       { glDeleteBuffers(1, &m_normalsSSBO);       m_normalsSSBO = 0; }
    if (m_indicesSSBO)       { glDeleteBuffers(1, &m_indicesSSBO);       m_indicesSSBO = 0; }
    m_initialized = false;
    m_particleCount = 0;
    m_gridW = m_gridH = 0;
    m_positionMirror.clear();
    m_normalMirror.clear();
    m_positionsDirty = false;
    m_normalsDirty   = false;
    m_indices.clear();
    m_texCoords.clear();
    m_constraints.clear();
    m_colourRanges.clear();
    m_constraintCount = 0;
    m_dihedrals.clear();
    m_dihedralColourRanges.clear();
    m_dihedralCount = 0;
    m_sphereColliders.clear();
    m_planeColliders.clear();
    m_collidersDirty = true;
    m_pinIndices.clear();
    m_lras.clear();
    m_lraCount = 0;
    m_pinsDirty = false;
    m_invMassMirror.clear();
    m_shadersLoaded = false;  // Shader objects retain GPU state across init cycles.
}

} // namespace Vestige
