// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_cpu_gpu_parity.cpp
/// @brief Phase 10.9 Slice 17 Cl1 — CPU↔GPU cloth solver parity gate
///        (CLAUDE.md Rule 7: a dual CPU-spec / GPU-runtime impl must be
///        pinned by a parity test).
///
/// Drives an identical `ClothConfig` through the pure-CPU `ClothSimulator`
/// and the GPU-compute `GpuClothSimulator` and compares the resulting
/// cloths positionally.
///
/// **Why this is NOT a bit-exact test.** The authoritative parity contract
/// is `docs/phases/phase_09b_gpu_cloth_design.md` § "Testing":
///
///   > GPU XPBD with the same seed will *not* be bit-identical to CPU
///   > because (a) constraint solve order differs (red-black graph
///   > colouring vs depth-sorted Gauss-Seidel) and (b) GPU floating-point
///   > semantics differ (FMA, vendor-specific). Tests use a Hausdorff-
///   > distance threshold (≤5% of cloth diagonal) for positional parity,
///   > not exact equality.
///
/// **What writing this harness found.** Before this gate existed the two
/// backends had silently drifted apart. Two defects surfaced:
///
///   1. *Damping convention* (FIXED in this change). `ClothConfig::damping`
///      is documented + applied by the CPU as a PER-SUBSTEP coefficient;
///      the GPU divided it by the substep count (per-frame), damping
///      ~`substeps`× less and diverging by metres in a 2 s free-fall. The
///      GPU now matches the CPU's per-substep convention. The free-fall
///      gate below pins this.
///   2. *Constraint under-convergence* (filed as a follow-up — see the
///      skip-gated drape test below). The GPU's coloured-parallel
///      Gauss-Seidel is a far weaker smoother than the CPU's sequential
///      sweep, so a stiff pinned cloth settles ~8× too soft on the GPU.
///      Closing it needs a convergence accelerator (Chebyshev/SOR); see
///      ROADMAP "Phase 10.9 … Slice 17".
///
/// Both backends build a byte-identical grid (same `idx = z*W + x`
/// ordering, same centred position formula, same index winding — see
/// `ClothSimulator::initialize` and `GpuClothSimulator::buildInitialGrid`),
/// so particle `i` is the same logical vertex on each and a direct
/// index-correspondent comparison is well-defined. Wind is OFF in both
/// tests (its own parity is pinned by the Sh4* tests in
/// test_gpu_cloth_simulator.cpp); sleep is disabled (the GPU never sleeps).

#include <gtest/gtest.h>

#include "gl_test_fixture.h"
#include "cloth_test_helpers.h"
#include "physics/cloth_simulator.h"
#include "physics/gpu_cloth_simulator.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace Vestige;

namespace
{

constexpr uint32_t W = 12, H = 12;
constexpr float    SPACING = 0.1f;
constexpr int      FRAMES_2S = 120;  // 2 s @ 60 Hz

/// Baseline parity config: a 12×12 cloth, wind-free, sleep-free so both
/// backends simulate every tick (the GPU has no sleep path).
ClothConfig parityConfig()
{
    ClothConfig cfg = Testing::clothSmallConfig(W, H);
    cfg.spacing        = SPACING;
    cfg.substeps       = 10;
    cfg.sleepThreshold = 0.0f;  // avgKE < 0 is never true → CPU never sleeps.
    return cfg;
}

/// Rest-pose diagonal of the flat grid (spans (W-1)*s by (H-1)*s in XZ).
float clothDiagonal()
{
    return glm::length(glm::vec3(float(W - 1) * SPACING, 0.0f, float(H - 1) * SPACING));
}

/// Pin the four corners of the grid at their rest positions.
void pinCorners(IClothSolverBackend& sim)
{
    const glm::vec3* p = sim.getPositions();
    const uint32_t corners[4] = {
        0, W - 1, (H - 1) * W, (H - 1) * W + (W - 1),
    };
    for (uint32_t idx : corners)
        sim.pinParticle(idx, p[idx]);
}

void run(IClothSolverBackend& sim, int frames)
{
    for (int f = 0; f < frames; ++f)
        sim.simulate(1.0f / 60.0f);
}

/// Symmetric Hausdorff distance between two equally-sized point clouds —
/// the metric the design doc fixes the 5%-of-diagonal bound on.
float hausdorff(const glm::vec3* a, const glm::vec3* b, uint32_t n)
{
    auto directed = [n](const glm::vec3* from, const glm::vec3* to) -> float
    {
        float worst = 0.0f;
        for (uint32_t i = 0; i < n; ++i)
        {
            float nearest = std::numeric_limits<float>::max();
            for (uint32_t j = 0; j < n; ++j)
                nearest = std::min(nearest, glm::length(from[i] - to[j]));
            worst = std::max(worst, nearest);
        }
        return worst;
    };
    return std::max(directed(a, b), directed(b, a));
}

/// Build a CPU + GPU cloth from @a cfg (wind off). Returns false via
/// `GTEST_SKIP` semantics by setting @a gpuReady=false when the GPU compute
/// pipeline isn't available; the caller skips.
void buildPair(const ClothConfig& cfg, ClothSimulator& cpu, GpuClothSimulator& gpu,
               bool& gpuReady)
{
    gpu.setShaderPath(VESTIGE_SHADER_DIR);
    gpu.initialize(cfg, /*seed=*/0);
    gpuReady = gpu.isInitialized() && gpu.hasShaders();
    if (!gpuReady) return;
    cpu.initialize(cfg, /*seed=*/0);
    cpu.setWindQuality(ClothWindQuality::SIMPLE);
    gpu.setWindQuality(ClothWindQuality::SIMPLE);
}

}  // namespace

class ClothCpuGpuParityTest : public ::Vestige::Test::GLTestFixture {};

// =============================================================================
// Cl1 — core-solver parity: free-falling cloth over 2 s
// =============================================================================
// A pin-free cloth under uniform gravity translates as a near-rigid sheet:
// the distance constraints barely activate (no relative motion to correct),
// so this isolates the gravity → wind → integrate → damping path plus the
// "constraints don't spuriously fire" invariant — the contract both backends
// fully share today. It is the regression guard for the damping-convention
// fix (pre-fix this diverged by ~10 m / 685% of the diagonal) and would catch
// any future drift in gravity, integration, or the no-op constraint path.
TEST_F(ClothCpuGpuParityTest, Cl1_FreeFallCoreSolverParityWithinHausdorffBound)
{
    const ClothConfig cfg = parityConfig();
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

    ASSERT_TRUE(cpu.isInitialized());
    ASSERT_EQ(cpu.getParticleCount(), gpu.getParticleCount());
    const uint32_t pc = cpu.getParticleCount();

    // No pins — free fall.
    run(cpu, FRAMES_2S);
    run(gpu, FRAMES_2S);

    const glm::vec3* c = cpu.getPositions();
    const glm::vec3* g = gpu.getPositions();
    ASSERT_NE(c, nullptr);
    ASSERT_NE(g, nullptr);

    const float diagonal = clothDiagonal();
    const float haus = hausdorff(c, g, pc);

    EXPECT_LT(haus, 0.05f * diagonal)
        << "free-fall Hausdorff " << haus << " m exceeds 5% of the "
        << diagonal << " m cloth diagonal — gravity / integrate / damping "
        << "parity has regressed";
}

// =============================================================================
// Cl1 — stiff drape parity (Cl9: closed by the SOR convergence accelerator)
// =============================================================================
// A taut four-corner-pinned cloth settling under gravity stresses the
// distance-constraint solver hard. The CPU's sequential Gauss-Seidel converges
// to a near-rigid ~0.18 m sag; the GPU's coloured-parallel sweep under-converged
// and settled ~8× softer (~0.67 m) at one sweep/substep — a genuine solver-
// quality gap, not a tolerance choice.
//
// Cl9 closes it: the GPU runs the distance-constraint solve with SOR
// over-relaxation (mode `SOR`, ω = 1.8) for `setSolverIterations(16)` outer
// iterations per substep. Over-relaxation leaves the converged solution
// (C == 0) unchanged, so the GPU settles to the SAME equilibrium the CPU does —
// just reached fast enough to be affordable. The drape now matches the CPU
// reference to ~3% Hausdorff (was ~43%), under the 5%-of-diagonal gate.
//
// The CPU stays on its default 1-iteration sequential sweep (it already
// converges over the 2 s settle); only the GPU opts into the accelerator, so
// this also exercises the Cl9 API surface.
TEST_F(ClothCpuGpuParityTest, Cl1_StiffDrapeParity)
{
    const ClothConfig cfg = parityConfig();
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

    // Cl9: opt the GPU into the SOR convergence accelerator.
    gpu.setConvergenceMode(ClothConvergenceMode::SOR);
    gpu.setSolverIterations(16);
    ASSERT_EQ(gpu.getSolverIterations(), 16);
    ASSERT_EQ(gpu.getConvergenceMode(), ClothConvergenceMode::SOR);

    const uint32_t pc = cpu.getParticleCount();
    pinCorners(cpu);
    pinCorners(gpu);

    // Corner 0 is pinned; capture its clamped position for the held-pin check.
    const glm::vec3 cpuPin0 = cpu.getPositions()[0];
    const glm::vec3 gpuPin0 = gpu.getPositions()[0];

    run(cpu, FRAMES_2S);
    run(gpu, FRAMES_2S);

    const glm::vec3* c = cpu.getPositions();
    const glm::vec3* g = gpu.getPositions();

    // Invariants that hold regardless of constraint convergence:
    // (1) pins stay clamped exactly on both backends,
    EXPECT_LT(glm::length(c[0] - cpuPin0), 1e-4f) << "CPU corner pin drifted";
    EXPECT_LT(glm::length(g[0] - gpuPin0), 1e-4f) << "GPU corner pin drifted";
    // (2) state is finite (no NaN/Inf blow-up on either backend),
    float cMinY = 1e9f, gMinY = 1e9f;
    for (uint32_t i = 0; i < pc; ++i)
    {
        ASSERT_TRUE(std::isfinite(c[i].y)) << "CPU produced non-finite position";
        ASSERT_TRUE(std::isfinite(g[i].y)) << "GPU produced non-finite position";
        cMinY = std::min(cMinY, c[i].y);
        gMinY = std::min(gMinY, g[i].y);
    }
    // (3) gravity actually pulled the unpinned interior downward.
    EXPECT_LT(cMinY, -0.01f) << "CPU cloth did not sag under gravity";
    EXPECT_LT(gMinY, -0.01f) << "GPU cloth did not sag under gravity";

    // (4) Cl9 strict positional parity: the SOR-accelerated GPU drape matches
    //     the CPU reference within 5% of the cloth diagonal (Hausdorff).
    const float diagonal = clothDiagonal();
    const float haus = hausdorff(c, g, pc);
    EXPECT_LT(haus, 0.05f * diagonal)
        << "stiff-drape Hausdorff " << haus << " m = " << (100.0f * haus / diagonal)
        << "% of the " << diagonal << " m diagonal exceeds 5% (CPU sag " << cMinY
        << " m vs GPU sag " << gMinY << " m) — the SOR convergence accelerator "
           "(Cl9) has regressed or under-converged.";
}
