// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_color_vision_filter.cpp
/// @brief Phase 10 accessibility coverage for the color-vision-deficiency
///        simulation matrices (Viénot/Brettel/Mollon 1999).

#include <gtest/gtest.h>

#include "renderer/color_vision_filter.h"

#include <glm/glm.hpp>

using namespace Vestige;

namespace
{
/// Tolerance for matrix/vector comparisons. 1e-4 is comfortably tighter
/// than the 5-decimal precision of the published Brettel coefficients.
constexpr float kEps = 1e-4f;

bool approx(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
{
    return std::abs(a.r - b.r) < eps
        && std::abs(a.g - b.g) < eps
        && std::abs(a.b - b.b) < eps;
}
}

// -- Identity / labelling --

TEST(ColorVisionFilter, NormalModeIsIdentity)
{
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Normal);
    EXPECT_EQ(m, glm::mat3(1.0f));
}

TEST(ColorVisionFilter, LabelsAreHumanReadable)
{
    EXPECT_STREQ(colorVisionModeLabel(ColorVisionMode::Normal),       "Normal");
    EXPECT_STREQ(colorVisionModeLabel(ColorVisionMode::Protanopia),   "Protanopia");
    EXPECT_STREQ(colorVisionModeLabel(ColorVisionMode::Deuteranopia), "Deuteranopia");
    EXPECT_STREQ(colorVisionModeLabel(ColorVisionMode::Tritanopia),   "Tritanopia");
}

// -- Brettel coefficients (known-good values, Table 3 of the 1999 paper) --
//
// Each dichromat matrix is pinned against the published coefficients. The
// three modes share an identical 9-entry comparison and differ only in
// data, so they collapse into one TEST_P over {mode, expected matrix}.
// glm::mat3's element constructor is column-major, so the values below are
// laid out column-by-column (m[col][row]) — the trailing comment on each
// case shows the same matrix in human-readable rows.
namespace
{
struct BrettelCase
{
    const char* name;
    ColorVisionMode mode;
    glm::mat3 expected;
};

class ColorVisionFilterCoefficients
    : public ::testing::TestWithParam<BrettelCase>
{
};
} // namespace

TEST_P(ColorVisionFilterCoefficients, MatchBrettelTable3)
{
    const BrettelCase& c = GetParam();
    glm::mat3 m = colorVisionMatrix(c.mode);
    for (int col = 0; col < 3; ++col)
    {
        for (int row = 0; row < 3; ++row)
        {
            EXPECT_NEAR(m[col][row], c.expected[col][row], kEps)
                << c.name << " element m[" << col << "][" << row << "]";
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllDichromats,
    ColorVisionFilterCoefficients,
    ::testing::Values(
        BrettelCase{"Protanopia", ColorVisionMode::Protanopia,
            glm::mat3(0.56667f, 0.55833f, 0.00000f,
                      0.43333f, 0.44167f, 0.24167f,
                      0.00000f, 0.00000f, 0.75833f)},
            // rows: {0.56667 0.43333 0} {0.55833 0.44167 0} {0 0.24167 0.75833}
        BrettelCase{"Deuteranopia", ColorVisionMode::Deuteranopia,
            glm::mat3(0.625f, 0.700f, 0.000f,
                      0.375f, 0.300f, 0.300f,
                      0.000f, 0.000f, 0.700f)},
            // rows: {0.625 0.375 0} {0.700 0.300 0} {0 0.300 0.700}
        BrettelCase{"Tritanopia", ColorVisionMode::Tritanopia,
            glm::mat3(0.950f, 0.00000f, 0.00000f,
                      0.050f, 0.43333f, 0.47500f,
                      0.000f, 0.56667f, 0.52500f)}),
            // rows: {0.950 0.050 0} {0 0.43333 0.56667} {0 0.47500 0.52500}
    [](const ::testing::TestParamInfo<BrettelCase>& info)
    { return std::string(info.param.name); });

// -- Structural invariants --

TEST(ColorVisionFilter, RowsOfEveryMatrixSumToOne)
{
    // Each simulation matrix distributes the cone response across RGB such
    // that an achromatic input (equal R=G=B) projects to the same
    // achromatic output. Enforced by row-sum == 1 per channel.
    for (ColorVisionMode mode : {ColorVisionMode::Normal,
                                   ColorVisionMode::Protanopia,
                                   ColorVisionMode::Deuteranopia,
                                   ColorVisionMode::Tritanopia})
    {
        glm::mat3 m = colorVisionMatrix(mode);
        for (int row = 0; row < 3; ++row)
        {
            float sum = m[0][row] + m[1][row] + m[2][row];
            EXPECT_NEAR(sum, 1.0f, kEps)
                << "row " << row
                << " of mode " << colorVisionModeLabel(mode)
                << " sums to " << sum;
        }
    }
}

// Slice 18 Ts4: dropped `AchromaticInputPassesThroughUnchanged` — it's
// a direct consequence of the row-sum-1 property (see
// `RowsOfEveryMatrixSumToOne` above), and `WhiteStaysWhiteForEveryMode`
// below exercises the same invariant with a worked example. Three
// tests, one root invariant.

// -- Characteristic dichromat projections --

TEST(ColorVisionFilter, ProtanopeSeesPureRedAsDesaturatedYellow)
{
    // A protanope projects pure red (1,0,0) onto a roughly equal R/G pair
    // (the red and green channels both receive 0.56/0.55 of the energy),
    // with zero blue. The classic confusion: red and yellow look alike.
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Protanopia);
    glm::vec3 result = m * glm::vec3(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(result.r, 0.56667f, kEps);
    EXPECT_NEAR(result.g, 0.55833f, kEps);
    EXPECT_NEAR(result.b, 0.0f,     kEps);
    // R and G end up near-equal (|ΔR-G| ≪ 0.05) — this is why red and
    // yellow collapse into the same percept for protanopes.
    EXPECT_LT(std::abs(result.r - result.g), 0.02f);
}

TEST(ColorVisionFilter, DeuteranopeSeesPureGreenShiftedTowardRed)
{
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Deuteranopia);
    glm::vec3 result = m * glm::vec3(0.0f, 1.0f, 0.0f);
    // R receives 0.375, G keeps 0.300, B 0.300 → green pushed toward
    // a yellow-brown, with non-trivial red contribution.
    EXPECT_NEAR(result.r, 0.375f, kEps);
    EXPECT_NEAR(result.g, 0.300f, kEps);
    EXPECT_NEAR(result.b, 0.300f, kEps);
}

TEST(ColorVisionFilter, TritanopeCollapsesPureBlueIntoTheCyanBand)
{
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Tritanopia);
    glm::vec3 result = m * glm::vec3(0.0f, 0.0f, 1.0f);
    // S-cone absent: blue energy spreads into the red/green channels.
    EXPECT_NEAR(result.r, 0.0f,     kEps);
    EXPECT_NEAR(result.g, 0.56667f, kEps);
    EXPECT_NEAR(result.b, 0.52500f, kEps);
}

TEST(ColorVisionFilter, BlackStaysBlackForEveryMode)
{
    for (ColorVisionMode mode : {ColorVisionMode::Protanopia,
                                   ColorVisionMode::Deuteranopia,
                                   ColorVisionMode::Tritanopia})
    {
        glm::mat3 m = colorVisionMatrix(mode);
        EXPECT_TRUE(approx(m * glm::vec3(0.0f), glm::vec3(0.0f)))
            << "mode " << colorVisionModeLabel(mode)
            << " leaks non-zero output from pure black";
    }
}

TEST(ColorVisionFilter, WhiteStaysWhiteForEveryMode)
{
    for (ColorVisionMode mode : {ColorVisionMode::Protanopia,
                                   ColorVisionMode::Deuteranopia,
                                   ColorVisionMode::Tritanopia})
    {
        glm::mat3 m = colorVisionMatrix(mode);
        EXPECT_TRUE(approx(m * glm::vec3(1.0f), glm::vec3(1.0f)))
            << "mode " << colorVisionModeLabel(mode)
            << " does not preserve pure white";
    }
}
