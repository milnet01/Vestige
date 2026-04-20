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

TEST(ColorVisionFilter, ProtanopiaCoefficientsMatchBrettel)
{
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Protanopia);
    // Row 0: 0.56667  0.43333  0.0
    EXPECT_NEAR(m[0][0], 0.56667f, kEps);
    EXPECT_NEAR(m[1][0], 0.43333f, kEps);
    EXPECT_NEAR(m[2][0], 0.00000f, kEps);
    // Row 1: 0.55833  0.44167  0.0
    EXPECT_NEAR(m[0][1], 0.55833f, kEps);
    EXPECT_NEAR(m[1][1], 0.44167f, kEps);
    EXPECT_NEAR(m[2][1], 0.00000f, kEps);
    // Row 2: 0.0      0.24167  0.75833
    EXPECT_NEAR(m[0][2], 0.00000f, kEps);
    EXPECT_NEAR(m[1][2], 0.24167f, kEps);
    EXPECT_NEAR(m[2][2], 0.75833f, kEps);
}

TEST(ColorVisionFilter, DeuteranopiaCoefficientsMatchBrettel)
{
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Deuteranopia);
    EXPECT_NEAR(m[0][0], 0.625f, kEps);
    EXPECT_NEAR(m[1][0], 0.375f, kEps);
    EXPECT_NEAR(m[2][0], 0.000f, kEps);
    EXPECT_NEAR(m[0][1], 0.700f, kEps);
    EXPECT_NEAR(m[1][1], 0.300f, kEps);
    EXPECT_NEAR(m[2][1], 0.000f, kEps);
    EXPECT_NEAR(m[0][2], 0.000f, kEps);
    EXPECT_NEAR(m[1][2], 0.300f, kEps);
    EXPECT_NEAR(m[2][2], 0.700f, kEps);
}

TEST(ColorVisionFilter, TritanopiaCoefficientsMatchBrettel)
{
    glm::mat3 m = colorVisionMatrix(ColorVisionMode::Tritanopia);
    EXPECT_NEAR(m[0][0], 0.950f, kEps);
    EXPECT_NEAR(m[1][0], 0.050f, kEps);
    EXPECT_NEAR(m[2][0], 0.000f, kEps);
    EXPECT_NEAR(m[0][1], 0.00000f, kEps);
    EXPECT_NEAR(m[1][1], 0.43333f, kEps);
    EXPECT_NEAR(m[2][1], 0.56667f, kEps);
    EXPECT_NEAR(m[0][2], 0.00000f, kEps);
    EXPECT_NEAR(m[1][2], 0.47500f, kEps);
    EXPECT_NEAR(m[2][2], 0.52500f, kEps);
}

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

TEST(ColorVisionFilter, AchromaticInputPassesThroughUnchanged)
{
    // Direct consequence of the row-sum-1 property, but exercise it with
    // an actual matrix-vector multiply so future regressions that only
    // permute columns (and preserve row sums) get caught.
    for (ColorVisionMode mode : {ColorVisionMode::Protanopia,
                                   ColorVisionMode::Deuteranopia,
                                   ColorVisionMode::Tritanopia})
    {
        glm::mat3 m = colorVisionMatrix(mode);
        glm::vec3 grey(0.5f, 0.5f, 0.5f);
        EXPECT_TRUE(approx(m * grey, grey))
            << "mode " << colorVisionModeLabel(mode)
            << " does not preserve achromatic input";
    }
}

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
