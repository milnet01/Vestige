// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_curves_parity.cpp
/// @brief AX4 S5 parity gate: the runtime closed-form curves in
///        `audio/procedural/audio_curves.h` must agree with the Formula
///        Workbench `audio` definitions they are transcribed from
///        (`engine/formula/audio_templates.cpp`). The Workbench is the source of
///        truth (project Rule 6); this test is what stops the fast runtime form
///        from silently drifting from the fitted coefficients (project Rule 7 —
///        a parity test pins dual implementations).
#include "audio/procedural/audio_curves.h"

#include "formula/audio_templates.h"
#include "formula/expression.h"
#include "formula/expression_eval.h"
#include "formula/formula.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Vestige;

namespace
{

/// @brief Evaluates a FW definition's FULL expression at one approach speed.
float evalFw(const FormulaDefinition& def, float approachSpeed)
{
    ExpressionEvaluator eval;
    ExpressionEvaluator::VariableMap vars{{"approachSpeed", approachSpeed}};
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

// A grid spanning a graze (0) through a hard strike well past the curves' knees.
const std::vector<float> kSpeeds = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f,
                                    8.0f, 12.0f, 20.0f, 50.0f};

}  // namespace

TEST(AudioCurvesParity, LoudnessGainMatchesWorkbench)
{
    const auto all = AudioTemplates::createAll();
    const auto& def = byName(all, "impact_loudness_gain");
    for (float s : kSpeeds)
    {
        EXPECT_NEAR(Procedural::impactLoudnessGain(s), evalFw(def, s), 1e-5f)
            << "runtime audio_curves.h drifted from the FW fit at speed=" << s;
    }
}

TEST(AudioCurvesParity, PitchScaleMatchesWorkbench)
{
    const auto all = AudioTemplates::createAll();
    const auto& def = byName(all, "impact_pitch_scale");
    for (float s : kSpeeds)
    {
        EXPECT_NEAR(Procedural::impactPitchScale(s), evalFw(def, s), 1e-5f)
            << "runtime audio_curves.h drifted from the FW fit at speed=" << s;
    }
}

TEST(AudioCurvesParity, AggregateEventRateMatchesWorkbench)
{
    const auto all = AudioTemplates::createAll();
    const auto& def = byName(all, "aggregate_event_rate");
    for (float s : kSpeeds)
    {
        EXPECT_NEAR(Procedural::aggregateEventRate(s), evalFw(def, s), 1e-3f)
            << "runtime audio_curves.h drifted from the FW fit at speed=" << s;
    }
}

TEST(AudioCurvesParity, AggregateRefSpeedIsTheCurveDeclaredDefault)
{
    // The bank anchors per-material grain rates at the FW curve's declared
    // default input. If that default moves in audio_templates.cpp, the anchor
    // must move with it.
    const auto all = AudioTemplates::createAll();
    const auto& def = byName(all, "aggregate_event_rate");
    ASSERT_FALSE(def.inputs.empty());
    EXPECT_NEAR(def.inputs.front().defaultValue, Procedural::kAggregateRefSpeedMps, 1e-6f);
}
