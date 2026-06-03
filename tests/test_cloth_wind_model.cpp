// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

// Phase 10.9 Sh4b — ClothWindModel unit tests.
//
// The wind model is the shared gust state machine + per-frame FBM/turbulence
// precompute that both the CPU ClothSimulator and the GPU GpuClothSimulator
// own. These tests pin the contract the GPU FULL tier and the future Cl1
// parity harness rely on: determinism across instances (same seed + dt ⇒ same
// caches), tier-gated cache fill, the pinned-particle zero, and gust folding.

#include <gtest/gtest.h>

#include "physics/cloth_wind_model.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

using namespace Vestige;

namespace
{

struct Grid
{
    uint32_t W = 4;
    uint32_t H = 4;
    std::vector<glm::vec3> pos;
    std::vector<float>     inv;
    std::vector<uint32_t>  idx;
};

// A flat W×H grid in the XZ plane with the runtime's NW-SE diagonal triangle
// index buffer.
Grid makeGrid(uint32_t W = 4, uint32_t H = 4)
{
    Grid g;
    g.W = W;
    g.H = H;
    g.pos.resize(W * H);
    g.inv.assign(W * H, 1.0f);
    for (uint32_t z = 0; z < H; ++z)
        for (uint32_t x = 0; x < W; ++x)
            g.pos[z * W + x] = glm::vec3(float(x), 0.0f, float(z));
    for (uint32_t z = 0; z + 1 < H; ++z)
        for (uint32_t x = 0; x + 1 < W; ++x)
        {
            uint32_t i0 = z * W + x, i1 = z * W + x + 1;
            uint32_t i2 = (z + 1) * W + x, i3 = (z + 1) * W + x + 1;
            g.idx.insert(g.idx.end(), {i0, i2, i1, i1, i2, i3});
        }
    return g;
}

// Drive the model forward until it enters a gust (gustCurrent rises off zero).
// Deterministic given the seed; the opening calm period is 3-5 s so this takes
// a few hundred 1/60 s frames.
void advanceIntoGust(ClothWindModel& m)
{
    for (int f = 0; f < 4000 && m.gustCurrent() < 0.2f; ++f)
    {
        m.advance(1.0f / 60.0f);
    }
}

}  // namespace

// Two models with the same seed, driven by the same dt sequence and grid,
// produce byte-identical gust state + FBM/turbulence caches. This is the
// property that makes CPU↔GPU FULL-tier parity possible.
TEST(ClothWindModel, IdenticalSeedAndDtYieldIdenticalState_Sh4b)
{
    const Grid g = makeGrid();
    ClothWindModel a, b;
    a.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
    b.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
    a.setWindQuality(ClothWindQuality::FULL);
    b.setWindQuality(ClothWindQuality::FULL);
    a.seedAndInit(7);
    b.seedAndInit(7);

    for (int f = 0; f < 600; ++f)
    {
        a.advance(1.0f / 60.0f);
        b.advance(1.0f / 60.0f);
    }
    a.precompute(g.W, g.H, g.pos, g.inv, g.idx);
    b.precompute(g.W, g.H, g.pos, g.inv, g.idx);

    EXPECT_FLOAT_EQ(a.gustCurrent(), b.gustCurrent());
    EXPECT_FLOAT_EQ(a.flutter(), b.flutter());
    EXPECT_EQ(a.baseWindVelocity(), b.baseWindVelocity());

    ASSERT_EQ(a.particleWind().size(), b.particleWind().size());
    for (size_t i = 0; i < a.particleWind().size(); ++i)
        EXPECT_EQ(a.particleWind()[i], b.particleWind()[i]) << "particleWind " << i;

    ASSERT_EQ(a.triangleTurbulence().size(), b.triangleTurbulence().size());
    for (size_t i = 0; i < a.triangleTurbulence().size(); ++i)
        EXPECT_FLOAT_EQ(a.triangleTurbulence()[i], b.triangleTurbulence()[i]) << "turb " << i;
}

// FULL fills both caches; APPROXIMATE computes flutter but leaves the per-
// particle / per-triangle caches empty; SIMPLE early-outs (not precomputed).
TEST(ClothWindModel, TierControlsCacheFill_Sh4b)
{
    const Grid g = makeGrid();

    {
        ClothWindModel m;
        m.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
        m.setWindQuality(ClothWindQuality::FULL);
        m.seedAndInit(1);
        m.precompute(g.W, g.H, g.pos, g.inv, g.idx);
        EXPECT_TRUE(m.precomputed());
        EXPECT_EQ(m.particleWind().size(), g.W * g.H);
        EXPECT_EQ(m.triangleTurbulence().size(), g.idx.size() / 3);
    }
    {
        ClothWindModel m;
        m.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
        m.setWindQuality(ClothWindQuality::APPROXIMATE);
        m.seedAndInit(1);
        m.precompute(g.W, g.H, g.pos, g.inv, g.idx);
        EXPECT_TRUE(m.precomputed());  // flutter computed → drag still runs
        EXPECT_TRUE(m.particleWind().empty());
        EXPECT_TRUE(m.triangleTurbulence().empty());
    }
    {
        ClothWindModel m;
        m.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
        m.setWindQuality(ClothWindQuality::SIMPLE);
        m.seedAndInit(1);
        m.precompute(g.W, g.H, g.pos, g.inv, g.idx);
        EXPECT_FALSE(m.precomputed());  // SIMPLE → no wind force at all
    }
}

// Zero wind strength early-outs even at FULL — matches the CPU guard.
TEST(ClothWindModel, ZeroStrengthNotPrecomputed_Sh4b)
{
    const Grid g = makeGrid();
    ClothWindModel m;
    m.setWind({1.0f, 0.0f, 0.0f}, 0.0f);
    m.setWindQuality(ClothWindQuality::FULL);
    m.seedAndInit(1);
    m.precompute(g.W, g.H, g.pos, g.inv, g.idx);
    EXPECT_FALSE(m.precomputed());
}

// A pinned particle (inverse mass 0) gets a zero FBM perturbation; free
// particles in a gust get non-zero ones.
TEST(ClothWindModel, PinnedParticleGetsZeroPerturbation_Sh4b)
{
    Grid g = makeGrid();
    g.inv[5] = 0.0f;  // pin one interior particle

    ClothWindModel m;
    m.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
    m.setWindQuality(ClothWindQuality::FULL);
    m.seedAndInit(3);
    advanceIntoGust(m);
    m.precompute(g.W, g.H, g.pos, g.inv, g.idx);

    ASSERT_EQ(m.particleWind().size(), g.W * g.H);
    EXPECT_EQ(m.particleWind()[5], glm::vec3(0.0f));

    bool anyNonZero = false;
    for (const glm::vec3& p : m.particleWind())
        if (p != glm::vec3(0.0f)) anyNonZero = true;
    EXPECT_TRUE(anyNonZero) << "expected free particles to perturb while gusting";
}

// baseWindVelocity folds in gust + flutter: it is ~zero while calm (even though
// the bare wind velocity is non-zero) and becomes non-zero once a gust ramps.
TEST(ClothWindModel, BaseWindVelocityIsGustFolded_Sh4b)
{
    const Grid g = makeGrid();
    ClothWindModel m;
    m.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
    m.setWindQuality(ClothWindQuality::FULL);
    m.seedAndInit(9);

    // Calm opening period: bare wind = (5,0,0) but the folded base wind ≈ 0.
    EXPECT_FLOAT_EQ(m.gustCurrent(), 0.0f);
    m.precompute(g.W, g.H, g.pos, g.inv, g.idx);
    EXPECT_NEAR(glm::length(m.baseWindVelocity()), 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(glm::length(m.windVelocity()), 5.0f);

    advanceIntoGust(m);
    m.precompute(g.W, g.H, g.pos, g.inv, g.idx);
    EXPECT_GT(glm::length(m.baseWindVelocity()), 0.0f);
}

// reset() zeroes gust state + elapsed without reseeding: a model advanced,
// reset, then re-advanced reproduces a freshly-seeded model's trajectory only
// if the seed was preserved (i.e. reset does not reseed).
TEST(ClothWindModel, ResetZeroesStateButKeepsSeed_Sh4b)
{
    ClothWindModel m;
    m.setWind({1.0f, 0.0f, 0.0f}, 5.0f);
    m.setWindQuality(ClothWindQuality::FULL);
    m.seedAndInit(5);
    for (int f = 0; f < 500; ++f) m.advance(1.0f / 60.0f);
    EXPECT_NE(m.elapsed(), 0.0f);

    m.reset();
    EXPECT_FLOAT_EQ(m.elapsed(), 0.0f);
    EXPECT_FLOAT_EQ(m.gustCurrent(), 0.0f);
}
