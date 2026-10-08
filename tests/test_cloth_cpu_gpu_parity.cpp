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
///   2. *A drape ~8× too soft on the GPU*, first read as constraint
///      under-convergence (Cl9 added SOR for it). The cause was the GPU
///      carrying velocity forward instead of recovering it from the solve;
///      Cl10 fixed that, and the drape test below now runs both backends on
///      their default settings.
///
/// Both backends build a byte-identical grid (same `idx = z*W + x`
/// ordering, same centred position formula, same index winding — see
/// `ClothSimulator::initialize` and `GpuClothSimulator::buildInitialGrid`),
/// so particle `i` is the same logical vertex on each and a direct
/// index-correspondent comparison is well-defined. Wind is OFF in every
/// test (its own parity is pinned by the Sh4* tests in
/// test_gpu_cloth_simulator.cpp); sleep is disabled except in the Cl10 sleep
/// test, so the backends step every tick.

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
/// backends simulate every tick.
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
// Cl1 — stiff drape parity, both backends on their default solver settings
// =============================================================================
// A taut four-corner-pinned cloth settling under gravity. The CPU settles to a
// ~0.18 m sag. The GPU once settled ~8× softer (~0.67 m), which was read as
// coloured Gauss-Seidel under-converging; Cl9 answered it with SOR
// over-relaxation. Cl10 found the real cause: the GPU carried an explicit
// velocity forward instead of recovering it from the solve, so gravity it
// should have lost to the constraints kept pulling the cloth down. With
// velocity recovery the default GPU (1 iteration, no accelerator — what the
// engine runs) matches the CPU to ~0.1 % of the diagonal.
//
// Both solvers reset lambda every iteration, so extra iterations stiffen the
// cloth: with recovery, SOR × 16 now settles too stiff (~8 %). The
// accelerator stays available; its API is pinned in test_cloth_solver_backend.
TEST_F(ClothCpuGpuParityTest, Cl1_StiffDrapeParity)
{
    const ClothConfig cfg = parityConfig();
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

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

    // (4) strict positional parity: the GPU drape matches the CPU reference
    //     within 5% of the cloth diagonal (Hausdorff).
    const float diagonal = clothDiagonal();
    const float haus = hausdorff(c, g, pc);
    EXPECT_LT(haus, 0.05f * diagonal)
        << "stiff-drape Hausdorff " << haus << " m = " << (100.0f * haus / diagonal)
        << "% of the " << diagonal << " m diagonal exceeds 5% (CPU sag " << cMinY
        << " m vs GPU sag " << gMinY << " m) — the solve or velocity recovery "
           "has drifted between the backends.";
}

// =============================================================================
// Cl10 — velocity recovery: a settled cloth released from its pins
// =============================================================================
// The CPU recovers velocity from the net position change each substep
// (`v = (pos - prevPos) / dtSub`, after the solve), so a particle the
// constraints hold still has zero velocity. A backend that instead carries an
// explicit velocity forward keeps adding gravity while the constraints pull the
// particle back, and stores a hidden downward speed — about g·dtSub·(1-d)/d
// ≈ 1.6 m/s at this config's damping. Releasing the pins exposes it: the CPU
// cloth starts falling from rest, the other one drops at that speed. On the
// pre-Cl10 velocity model (default settings) this measured 47.3 % of the
// diagonal (2026-10-08); with recovery it is ~0.5 %.
TEST_F(ClothCpuGpuParityTest, Cl10_ReleasedDrapeStartsFromRest)
{
    const ClothConfig cfg = parityConfig();
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

    pinCorners(cpu);
    pinCorners(gpu);
    run(cpu, FRAMES_2S);  // Settle: both drapes at rest.
    run(gpu, FRAMES_2S);

    const uint32_t corners[4] = {0, W - 1, (H - 1) * W, (H - 1) * W + (W - 1)};
    for (uint32_t idx : corners)
    {
        cpu.unpinParticle(idx);
        gpu.unpinParticle(idx);
    }

    constexpr int FRAMES_QUARTER_S = 15;  // 0.25 s @ 60 Hz of free fall.
    run(cpu, FRAMES_QUARTER_S);
    run(gpu, FRAMES_QUARTER_S);

    const uint32_t pc = cpu.getParticleCount();
    const glm::vec3* c = cpu.getPositions();
    const glm::vec3* g = gpu.getPositions();
    for (uint32_t i = 0; i < pc; ++i)
    {
        ASSERT_TRUE(std::isfinite(c[i].y)) << "CPU produced non-finite position";
        ASSERT_TRUE(std::isfinite(g[i].y)) << "GPU produced non-finite position";
    }

    const float diagonal = clothDiagonal();
    const float haus = hausdorff(c, g, pc);
    EXPECT_LT(haus, 0.05f * diagonal)
        << "released-drape Hausdorff " << haus << " m = " << (100.0f * haus / diagonal)
        << "% of the " << diagonal << " m diagonal exceeds 5% — the GPU's "
           "velocity no longer follows the net position change of the solve.";
}

// =============================================================================
// Cl10 — settle-down features run on both backends
// =============================================================================
// The CPU ClothSimulator has three settle-down features the GPU once lacked:
// adaptive damping, rest-pose blending and sleep. Each test below turns one on
// for both backends and holds the GPU to the same 5 % Hausdorff gate.

// Adaptive damping scales the per-substep damping with the RMS speed of the
// free particles, so a falling cloth slows as it speeds up. Free fall makes the
// speed, and so the effect, large: a backend without the feature falls freely
// and leaves the other behind.
TEST_F(ClothCpuGpuParityTest, Cl10_AdaptiveDampingParity)
{
    const ClothConfig cfg = parityConfig();
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

    cpu.setAdaptiveDamping(0.5f);
    gpu.setAdaptiveDamping(0.5f);
    ASSERT_FLOAT_EQ(gpu.getAdaptiveDamping(), 0.5f);

    run(cpu, FRAMES_2S);
    run(gpu, FRAMES_2S);

    const uint32_t pc = cpu.getParticleCount();
    const glm::vec3* c = cpu.getPositions();
    const glm::vec3* g = gpu.getPositions();
    for (uint32_t i = 0; i < pc; ++i)
    {
        ASSERT_TRUE(std::isfinite(c[i].y)) << "CPU produced non-finite position";
        ASSERT_TRUE(std::isfinite(g[i].y)) << "GPU produced non-finite position";
    }

    const float diagonal = clothDiagonal();
    const float haus = hausdorff(c, g, pc);
    EXPECT_LT(haus, 0.05f * diagonal)
        << "adaptive-damping Hausdorff " << haus << " m = " << (100.0f * haus / diagonal)
        << "% of the " << diagonal << " m diagonal exceeds 5% (CPU y " << c[0].y
        << " m vs GPU y " << g[0].y << " m) — the GPU's adaptive damping no "
           "longer matches the CPU's.";
}

// Rest-pose blending draws a cloth that has LRA tethers back toward its rest
// pose, 1.5 % per substep scaled by how calm the wind is. A cloth pinned along
// one edge would otherwise hang straight down; the blend holds it up toward the
// flat grid it was built as. Wind is off here, so the blend runs at full
// strength on every substep and the two outcomes are far apart.
TEST_F(ClothCpuGpuParityTest, Cl10_RestPoseBlendParity)
{
    const ClothConfig cfg = parityConfig();
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

    // Pin the whole z = 0 edge, then build the LRA tethers that switch the
    // blend on.
    for (uint32_t x = 0; x < W; ++x)
    {
        cpu.pinParticle(x, cpu.getPositions()[x]);
        gpu.pinParticle(x, gpu.getPositions()[x]);
    }
    cpu.rebuildLRA();
    gpu.rebuildLRA();
    ASSERT_FALSE(cpu.getLraConstraints().empty()) << "fixture: CPU built no LRA tethers";
    ASSERT_GT(gpu.getLraCount(), 0u) << "fixture: GPU built no LRA tethers";

    run(cpu, FRAMES_2S);
    run(gpu, FRAMES_2S);

    const uint32_t pc = cpu.getParticleCount();
    const glm::vec3* c = cpu.getPositions();
    const glm::vec3* g = gpu.getPositions();
    for (uint32_t i = 0; i < pc; ++i)
    {
        ASSERT_TRUE(std::isfinite(c[i].y)) << "CPU produced non-finite position";
        ASSERT_TRUE(std::isfinite(g[i].y)) << "GPU produced non-finite position";
    }

    const uint32_t farCorner = (H - 1) * W;  // Free edge, furthest from the pins.
    const float diagonal = clothDiagonal();
    const float haus = hausdorff(c, g, pc);
    EXPECT_LT(haus, 0.05f * diagonal)
        << "rest-pose Hausdorff " << haus << " m = " << (100.0f * haus / diagonal)
        << "% of the " << diagonal << " m diagonal exceeds 5% (free-edge y: CPU "
        << c[farCorner].y << " m vs GPU " << g[farCorner].y << " m) — the GPU "
           "no longer blends toward the rest pose as the CPU does.";
}

// A settled cloth sleeps: its kinetic energy per free particle stays below
// `sleepThreshold` in calm wind for CLOTH_SLEEP_FRAME_COUNT frames, and from
// the next frame `simulate()` skips the solve. Sleep is on here at the default
// threshold. Both backends must fall asleep, at about the same frame, in the
// same place, and a sleeping GPU cloth must not move.
TEST_F(ClothCpuGpuParityTest, Cl10_SettledDrapeSleepsOnBothBackends)
{
    ClothConfig cfg = parityConfig();
    cfg.sleepThreshold = ClothConfig{}.sleepThreshold;
    ASSERT_GT(cfg.sleepThreshold, 0.0f) << "fixture: default sleepThreshold disables sleep";
    ClothSimulator cpu;
    GpuClothSimulator gpu;
    bool gpuReady = false;
    buildPair(cfg, cpu, gpu, gpuReady);
    if (!gpuReady)
        GTEST_SKIP() << "GPU cloth compute pipeline not available in this environment";

    pinCorners(cpu);
    pinCorners(gpu);

    int cpuSleptAt = -1;
    int gpuSleptAt = -1;
    for (int f = 0; f < FRAMES_2S; ++f)
    {
        cpu.simulate(1.0f / 60.0f);
        gpu.simulate(1.0f / 60.0f);
        if (cpuSleptAt < 0 && cpu.isSleeping()) cpuSleptAt = f;
        if (gpuSleptAt < 0 && gpu.isSleeping()) gpuSleptAt = f;
    }
    ASSERT_GE(cpuSleptAt, 0) << "fixture: the CPU drape never slept in 2 s";
    ASSERT_GE(gpuSleptAt, 0) << "the GPU drape never slept in 2 s (the CPU slept at frame "
                             << cpuSleptAt << ")";
    // The two kinetic-energy curves match closely but are summed in a
    // different order, so allow the threshold crossing a few frames of slack.
    EXPECT_NEAR(gpuSleptAt, cpuSleptAt, 3)
        << "the GPU fell asleep at frame " << gpuSleptAt << ", the CPU at " << cpuSleptAt;

    const uint32_t pc = cpu.getParticleCount();
    const float diagonal = clothDiagonal();
    const float haus = hausdorff(cpu.getPositions(), gpu.getPositions(), pc);
    EXPECT_LT(haus, 0.05f * diagonal)
        << "sleeping-drape Hausdorff " << haus << " m = " << (100.0f * haus / diagonal)
        << "% of the " << diagonal << " m diagonal exceeds 5%";

    // Asleep means no solve: the GPU cloth holds exactly still.
    const std::vector<glm::vec3> before(gpu.getPositions(), gpu.getPositions() + pc);
    run(gpu, 10);
    EXPECT_TRUE(gpu.isSleeping()) << "the GPU cloth woke with no wind";
    const glm::vec3* after = gpu.getPositions();
    for (uint32_t i = 0; i < pc; ++i)
    {
        ASSERT_EQ(before[i], after[i]) << "sleeping GPU particle " << i << " moved";
    }
}

// =============================================================================
// Cl1 — the GPU must RESPOND to particleMass (3D_E-0630)
// =============================================================================
// Deliberately GPU-vs-GPU rather than CPU-vs-GPU, and that is the point.
//
// The two backends also differ by the Cl9 convergence gap the skip-gated drape
// test above pins, so a CPU/GPU comparison at a realistic mass cannot say which
// divergence it caught. This compares the GPU against ITSELF at two masses, so
// the convergence difference is common to both runs and cancels exactly.
//
// The invariant: XPBD weights every positional correction by inverse mass —
//
//     lambda = -C / (w0 + w1 + alphaTilde),   dp = w * lambda * n
//
// so with a non-zero compliance a 50x change in mass changes the settled shape.
// `ClothSimulator` derives `w = 1/particleMass`; the GPU mirror is only ever
// 1.0 (free) or 0.0 (pinned), so `particleMass` reaches nothing.
//
// TWO CONDITIONS THIS FIXTURE MUST HOLD, and the existing tests hold neither —
// which is why a parity harness written for exactly this rule did not catch it:
//
//   1. A NON-UNITY MASS. `clothSmallConfig` uses particleMass = 1.0, and
//      1/1.0 == 1.0 == the value the GPU hardcodes, so the two agree by
//      coincidence at the fixture's own value. A harness cannot fail at a
//      point where the bug is invisible.
//   2. A NON-ZERO COMPLIANCE AND PINS. At alphaTilde == 0 the w in the
//      numerator cancels the wSum in the denominator and mass genuinely does
//      not matter; under free fall gravity is an acceleration, so mass does
//      not matter there either. The free-fall test above is correct and blind
//      to this by construction.
//
// Masses are 1.0 (the old fixture value) against linenCurtain's shipped 0.02.
TEST_F(ClothCpuGpuParityTest, Cl1_GpuSolverRespondsToParticleMass)
{
    auto settle = [](float mass, std::vector<glm::vec3>& out, bool& ready)
    {
        ClothConfig cfg  = parityConfig();
        cfg.particleMass = mass;
        // clothSmallConfig's shear/bend compliances are non-zero, which is what
        // keeps alphaTilde from cancelling w. Assert rather than assume.
        ASSERT_GT(cfg.bendCompliance, 0.0f) << "fixture cannot detect mass at zero compliance";

        GpuClothSimulator gpu;
        gpu.setShaderPath(VESTIGE_SHADER_DIR);
        gpu.initialize(cfg, /*seed=*/0);
        ready = gpu.isInitialized() && gpu.hasShaders();
        if (!ready) return;
        gpu.setWindQuality(ClothWindQuality::SIMPLE);

        pinCorners(gpu);
        run(gpu, FRAMES_2S);

        const glm::vec3* p = gpu.getPositions();
        out.assign(p, p + gpu.getParticleCount());
    };

    std::vector<glm::vec3> heavy, light;
    bool readyHeavy = false, readyLight = false;
    settle(1.0f,  heavy, readyHeavy);
    settle(0.02f, light, readyLight);
    if (!readyHeavy || !readyLight)
        GTEST_SKIP() << "GPU compute pipeline unavailable";

    ASSERT_EQ(heavy.size(), light.size());

    const float diagonal = clothDiagonal();
    const float haus     = hausdorff(heavy.data(), light.data(), uint32_t(heavy.size()));

    // A 50x mass change must move the cloth by more than floating-point noise.
    // 1% of the diagonal is well inside the 5% parity bound, so this cannot
    // pass merely because the two runs drifted.
    const float floorDist = 0.01f * diagonal;

    EXPECT_GT(haus, floorDist)
        << "GPU settled identically at particleMass 1.0 and 0.02 (Hausdorff "
        << haus << " m vs floor " << floorDist << " m). The GPU ignores "
           "particleMass: its inverse-mass mirror is written as 1.0/0.0 and "
           "never derived from the config, so every XPBD correction is "
           "weighted w=1 while the CPU uses w=1/mass (50 at linenCurtain's "
           "0.02 kg). 3D_E-0630.";
}

// =============================================================================
// Cl8 — the GPU must refuse the configs the CPU refuses (3D_E-0630)
// =============================================================================
// `ClothSimulator::initialize` rejects a sub-2×2 grid, a non-positive or
// non-finite particleMass, non-positive/non-finite spacing, and non-finite
// damping or gravity. `GpuClothSimulator::initialize` checked only for a zero
// particle count, so the same ClothConfig produced an uninitialised CPU cloth
// and a live GPU one — and the engine picks the backend by size, so which you
// got depended on the grid.
TEST_F(ClothCpuGpuParityTest, Cl8_GpuRejectsTheConfigsTheCpuRejects)
{
    const float qNaN = std::numeric_limits<float>::quiet_NaN();
    const float inf  = std::numeric_limits<float>::infinity();

    struct Case { const char* what; ClothConfig cfg; };
    std::vector<Case> cases;
    auto add = [&](const char* what, auto mutate)
    {
        ClothConfig c = parityConfig();
        mutate(c);
        cases.push_back({what, c});
    };

    add("1x1 grid",            [](ClothConfig& c){ c.width = 1; c.height = 1; });
    add("2x1 grid",            [](ClothConfig& c){ c.height = 1; });
    add("zero mass",           [](ClothConfig& c){ c.particleMass = 0.0f; });
    add("negative mass",       [](ClothConfig& c){ c.particleMass = -1.0f; });
    add("NaN mass",            [&](ClothConfig& c){ c.particleMass = qNaN; });
    add("zero spacing",        [](ClothConfig& c){ c.spacing = 0.0f; });
    add("inf spacing",         [&](ClothConfig& c){ c.spacing = inf; });
    add("NaN damping",         [&](ClothConfig& c){ c.damping = qNaN; });
    add("inf gravity",         [&](ClothConfig& c){ c.gravity.y = -inf; });

    for (const Case& k : cases)
    {
        ClothSimulator cpu;
        cpu.initialize(k.cfg, /*seed=*/0);
        ASSERT_FALSE(cpu.isInitialized())
            << "fixture wrong: CPU accepted " << k.what;

        GpuClothSimulator gpu;
        gpu.setShaderPath(VESTIGE_SHADER_DIR);
        gpu.initialize(k.cfg, /*seed=*/0);
        EXPECT_FALSE(gpu.isInitialized())
            << "GPU accepted " << k.what << " where the CPU refused it. The two "
               "backends disagree about what a valid ClothConfig is, and the "
               "engine selects between them by cloth size. 3D_E-0630.";
    }
}

// A grid whose particle count overflows uint32 wraps to a small number, so the
// buffers are sized for the wrapped count while `buildInitialGrid` indexes
// `z * W + x` over the real extents — a very large out-of-bounds write.
//
// DELIBERATELY NOT PROVEN RED, and this is the one place in this change where
// the red run was skipped on purpose: proving it red means EXECUTING that
// out-of-bounds write. `testing.md` asks for a failing run first; here the
// failing run is the defect going off inside the test process, which is not a
// thing to do to find out what we already know from reading the arithmetic.
// The safe half above was proven red normally.
TEST_F(ClothCpuGpuParityTest, Cl8_GpuRejectsAGridWhoseParticleCountOverflows)
{
    ClothConfig cfg = parityConfig();
    cfg.width  = 65536;
    cfg.height = 65537;  // 65536 * 65537 wraps to 65536 in uint32.

    GpuClothSimulator gpu;
    gpu.setShaderPath(VESTIGE_SHADER_DIR);
    gpu.initialize(cfg, /*seed=*/0);

    EXPECT_FALSE(gpu.isInitialized())
        << "GPU accepted a grid whose particle count overflows uint32: "
           "width * height wrapped, so the buffers are sized for the wrapped "
           "count while the grid build indexes the real extents. 3D_E-0630.";
}
