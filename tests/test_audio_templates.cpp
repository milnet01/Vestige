// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_templates.cpp
/// @brief Unit tests for the Formula Workbench `audio` category (3D_E-0022):
///        the procedural-audio velocity->loudness/pitch and aggregate
///        event-rate curves consumed by the procedural-audio synthesis core.
#include "formula/audio_templates.h"
#include "formula/expression.h"
#include "formula/expression_eval.h"
#include "formula/formula.h"
#include "formula/formula_library.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace Vestige;

namespace
{

/// @brief Evaluates a definition's FULL expression at one named input value.
float evalAt(const FormulaDefinition& def, const std::string& inputName, float value)
{
    ExpressionEvaluator eval;
    ExpressionEvaluator::VariableMap vars{{inputName, value}};
    std::unordered_map<std::string, float> coeffs(def.coefficients.begin(),
                                                  def.coefficients.end());
    return eval.evaluate(*def.getExpression(QualityTier::FULL), vars, coeffs);
}

const FormulaDefinition& byName(const std::vector<FormulaDefinition>& all,
                                const std::string& name)
{
    auto it = std::find_if(all.begin(), all.end(),
                           [&](const FormulaDefinition& d) { return d.name == name; });
    EXPECT_NE(it, all.end()) << "missing audio template: " << name;
    return *it;
}

} // namespace

// --- Category registration -------------------------------------------------

TEST(AudioTemplates, CategoryIsRegisteredInTheLibrary)
{
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    auto audio = lib.findByCategory("audio");
    EXPECT_EQ(audio.size(), 3u);
    for (const auto* def : audio)
    {
        EXPECT_EQ(def->category, "audio");
    }

    auto cats = lib.getCategories();
    EXPECT_NE(std::find(cats.begin(), cats.end(), "audio"), cats.end())
        << "the `audio` category must appear alongside the 14 existing ones";

    EXPECT_NE(lib.findByName("impact_loudness_gain"), nullptr);
    EXPECT_NE(lib.findByName("impact_pitch_scale"), nullptr);
    EXPECT_NE(lib.findByName("aggregate_event_rate"), nullptr);
}

TEST(AudioTemplates, EveryCurveOnlyReferencesDeclaredInputsAndCoefficients)
{
    for (const auto& def : AudioTemplates::createAll())
    {
        std::vector<std::string> coeffNames;
        for (const auto& [k, v] : def.coefficients) coeffNames.push_back(k);

        std::string err;
        EXPECT_TRUE(ExpressionEvaluator::validate(
            *def.getExpression(QualityTier::FULL), def.inputs, coeffNames, err))
            << def.name << ": " << err;
    }
}

// --- impact_loudness_gain --------------------------------------------------

TEST(AudioTemplates, LoudnessGainIsZeroAtRestAndSaturatesTowardUnity)
{
    const auto all = AudioTemplates::createAll();
    const auto& g = byName(all, "impact_loudness_gain");

    EXPECT_NEAR(evalAt(g, "approachSpeed", 0.0f), 0.0f, 1e-4f);   // a graze is silent
    EXPECT_LT(evalAt(g, "approachSpeed", 20.0f), 1.0f);          // never exceeds unity
    EXPECT_GT(evalAt(g, "approachSpeed", 20.0f), 0.99f);         // hard strike ~ full scale
}

TEST(AudioTemplates, LoudnessGainIsMonotonicIncreasingAndBounded)
{
    const auto all = AudioTemplates::createAll();
    const auto& g = byName(all, "impact_loudness_gain");

    float prev = -1.0f;
    for (float s = 0.0f; s <= 12.0f; s += 0.5f)
    {
        float v = evalAt(g, "approachSpeed", s);
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 1.0f);
        EXPECT_GT(v, prev) << "must rise with impact speed at s=" << s;
        prev = v;
    }
}

// --- impact_pitch_scale ----------------------------------------------------

TEST(AudioTemplates, PitchScaleStartsAtBaseAndRisesWithinRange)
{
    const auto all = AudioTemplates::createAll();
    const auto& p = byName(all, "impact_pitch_scale");

    EXPECT_NEAR(evalAt(p, "approachSpeed", 0.0f), 0.85f, 1e-4f);  // base pitch at rest
    float hard = evalAt(p, "approachSpeed", 30.0f);
    EXPECT_GT(hard, 1.0f);
    EXPECT_LT(hard, 1.25f);                                       // base + range ceiling

    EXPECT_GT(evalAt(p, "approachSpeed", 4.0f),
              evalAt(p, "approachSpeed", 1.0f));                  // monotone
}

// --- aggregate_event_rate --------------------------------------------------

TEST(AudioTemplates, AggregateRateRisesWithSpeedThenClampsAtMax)
{
    const auto all = AudioTemplates::createAll();
    const auto& r = byName(all, "aggregate_event_rate");

    EXPECT_NEAR(evalAt(r, "approachSpeed", 0.0f), 400.0f, 1e-3f); // base rate floor
    EXPECT_NEAR(evalAt(r, "approachSpeed", 2.0f), 1000.0f, 1e-3f);// 400 + 300*2
    EXPECT_NEAR(evalAt(r, "approachSpeed", 50.0f), 2200.0f, 1e-3f);// clamped at maxRateHz
}
