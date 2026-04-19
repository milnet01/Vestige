// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gpu_cloth_simulator.h
/// @brief GPU compute-shader backend for cloth simulation (Phase 9B skeleton).
///
/// Implements `IClothSolverBackend` using SSBOs for particle state. The
/// per-substep work (force accumulation, integration, constraint solving,
/// collision, normal recomputation) lands incrementally over Steps 3–9 of
/// the Phase 9B GPU cloth pipeline — see `docs/PHASE9B_GPU_CLOTH_DESIGN.md`.
///
/// **Step 2 scope:** SSBO allocation + teardown only. `simulate()` is a no-op.
/// `getPositions()` / `getNormals()` return a CPU mirror that holds the
/// initial grid state (the skeleton does not move particles). Future steps
/// will dispatch compute shaders and switch the mirror to a lazy GPU→CPU
/// readback per the design doc.
///
/// **Context requirement:** `initialize()` and the destructor MUST run with
/// a current OpenGL 4.5 context. `isSupported()` is the no-context-safe probe
/// for callers (it returns false when no context is current).
#pragma once

#include "physics/cloth_solver_backend.h"
#include "physics/cloth_simulator.h"  // For ClothConfig
#include "renderer/shader.h"

#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief GPU compute-shader cloth solver backend.
class GpuClothSimulator : public IClothSolverBackend
{
public:
    GpuClothSimulator();
    ~GpuClothSimulator() override;

    // Non-copyable — owns GL resources.
    GpuClothSimulator(const GpuClothSimulator&) = delete;
    GpuClothSimulator& operator=(const GpuClothSimulator&) = delete;

    // -- IClothSolverBackend --

    void initialize(const ClothConfig& config, uint32_t seed = 0) override;
    void simulate(float deltaTime) override;
    void reset() override;
    bool isInitialized() const override { return m_initialized; }
    uint32_t getParticleCount() const override { return m_particleCount; }
    const glm::vec3* getPositions() const override;
    const glm::vec3* getNormals() const override;
    const std::vector<uint32_t>& getIndices() const override { return m_indices; }
    const std::vector<glm::vec2>& getTexCoords() const override { return m_texCoords; }
    uint32_t getGridWidth() const override { return m_gridW; }
    uint32_t getGridHeight() const override { return m_gridH; }

    // -- GPU backend extras --

    /// @brief Sets the directory containing the cloth_*.comp.glsl shaders.
    /// Must be called before `initialize()` for the GPU dispatch to engage;
    /// otherwise `simulate()` falls back to a no-op (the integration /
    /// wind shaders won't load).
    void setShaderPath(const std::string& path);

    /// @brief True once the wind + integrate compute shaders have loaded.
    /// Independent of `isInitialized()`: SSBOs may exist while shaders
    /// haven't loaded (e.g. asset path not set or shader compile failure),
    /// in which case `simulate()` cannot move particles.
    bool hasShaders() const { return m_shadersLoaded; }

    /// @brief Sets per-frame wind drag coefficient (matches CPU path).
    void setDragCoefficient(float drag) { m_dragCoeff = drag; }

    /// @brief Sets uniform wind velocity (direction × strength).
    void setWindVelocity(const glm::vec3& v) { m_windVelocity = v; }

    /// @brief Sets per-step velocity damping (0 = none, 0.99 = heavy).
    void setDamping(float damping) { m_damping = damping; }

    /// @brief Probes the current GL context for compute-shader support.
    /// @return true if GL ≥ 4.3 with compute + SSBO; false otherwise (or no context).
    static bool isSupported();

    /// @brief SSBO binding indices match the contract in the design doc § 4.
    enum BufferBinding : GLuint
    {
        BIND_POSITIONS         = 0,
        BIND_PREV_POSITIONS    = 1,
        BIND_VELOCITIES        = 2,
        BIND_NORMALS           = 6,
        BIND_INDICES           = 7,
    };

    GLuint getPositionsSSBO() const { return m_positionsSSBO; }
    GLuint getPrevPositionsSSBO() const { return m_prevPositionsSSBO; }
    GLuint getVelocitiesSSBO() const { return m_velocitiesSSBO; }
    GLuint getNormalsSSBO() const { return m_normalsSSBO; }
    GLuint getIndicesSSBO() const { return m_indicesSSBO; }

private:
    void createBuffers();
    void destroyBuffers();
    void buildInitialGrid(const ClothConfig& config);
    void loadShadersIfNeeded();
    void readbackPositionsIfDirty() const;
    void readbackNormalsIfDirty() const;

    bool m_initialized = false;
    uint32_t m_particleCount = 0;
    uint32_t m_gridW = 0;
    uint32_t m_gridH = 0;

    // CPU mirror. Step 3 onward: marked dirty whenever `simulate()` runs;
    // `getPositions()` / `getNormals()` lazily refresh via glGetNamedBufferSubData
    // before returning. Once Step 8 wires the renderer to read SSBOs directly,
    // most frames will skip the readback entirely.
    mutable std::vector<glm::vec3> m_positionMirror;
    mutable std::vector<glm::vec3> m_normalMirror;
    mutable bool m_positionsDirty = false;
    mutable bool m_normalsDirty   = false;
    std::vector<uint32_t> m_indices;
    std::vector<glm::vec2> m_texCoords;

    // SSBOs (named to mirror BufferBinding enum).
    GLuint m_positionsSSBO     = 0;
    GLuint m_prevPositionsSSBO = 0;
    GLuint m_velocitiesSSBO    = 0;
    GLuint m_normalsSSBO       = 0;
    GLuint m_indicesSSBO       = 0;

    // Compute shaders.
    Shader m_windShader;
    Shader m_integrateShader;
    bool m_shadersLoaded = false;
    std::string m_shaderPath;

    // Per-frame parameters (uniforms uploaded inside simulate()).
    glm::vec3 m_gravity      = glm::vec3(0.0f, -9.81f, 0.0f);
    glm::vec3 m_windVelocity = glm::vec3(0.0f);
    float     m_dragCoeff    = 1.0f;
    float     m_damping      = 0.01f;
};

} // namespace Vestige
