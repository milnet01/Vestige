// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_templates.cpp
/// @brief Built-in audio-DSP formula template implementations (`audio` category).
#include "formula/audio_templates.h"

namespace Vestige
{

// Convenience aliases (mirror physics_templates.cpp)
using E = ExprNode;
using QT = QualityTier;
using VT = FormulaValueType;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::unique_ptr<ExprNode> lit(float v) { return E::literal(v); }
static std::unique_ptr<ExprNode> var(const std::string& n) { return E::variable(n); }

static std::unique_ptr<ExprNode> binOp(const std::string& op,
                                        std::unique_ptr<ExprNode> l,
                                        std::unique_ptr<ExprNode> r)
{
    return E::binaryOp(op, std::move(l), std::move(r));
}

static std::unique_ptr<ExprNode> fn(const std::string& name,
                                     std::unique_ptr<ExprNode> arg)
{
    return E::unaryOp(name, std::move(arg));
}

// ---------------------------------------------------------------------------
// createAll
// ---------------------------------------------------------------------------

std::vector<FormulaDefinition> AudioTemplates::createAll()
{
    std::vector<FormulaDefinition> all;
    all.push_back(createImpactLoudnessGain());
    all.push_back(createImpactPitchScale());
    all.push_back(createAggregateEventRate());
    return all;
}

// ---------------------------------------------------------------------------
// Procedural-impact / footstep audio curves
// ---------------------------------------------------------------------------

FormulaDefinition AudioTemplates::createImpactLoudnessGain()
{
    // gain = saturate(1 - exp(-approachSpeed * decayPerMps))
    // Saturating loudness from impact speed: ~0 at a graze, -> 1 for a hard
    // strike. The speed->[0,1] mapping is the fitted `decayPerMps` coefficient,
    // so there is no loose runtime reference-speed constant (design §8/§9).
    FormulaDefinition def;
    def.name = "impact_loudness_gain";
    def.category = "audio";
    def.description = "Impact approach speed (m/s) -> linear loudness gain [0,1]. "
                      "Saturating; drives procedural strike amplitude.";
    def.inputs = {
        {"approachSpeed", VT::FLOAT, "m/s", 2.0f}
    };
    def.output = {VT::FLOAT, "gain"};
    def.coefficients = {{"decayPerMps", 0.4f}};

    // saturate(1 - exp(negate(approachSpeed * decayPerMps)))
    def.expressions[QT::FULL] =
        fn("saturate",
            binOp("-", lit(1.0f),
                fn("exp",
                    fn("negate",
                        binOp("*", var("approachSpeed"), var("decayPerMps"))))));

    def.source = "Procedural-audio bundle (design §9); perceptual loudness rises "
                 "with impact velocity (Lloyd et al. I3D 2011). Seed coefficient; "
                 "refine/validate in the Workbench against listening references.";
    return def;
}

FormulaDefinition AudioTemplates::createImpactPitchScale()
{
    // pitch = basePitch + pitchRange * (1 - exp(-approachSpeed * pitchDecayPerMps))
    // Harder strikes ring slightly higher/brighter. Output is a multiplier on the
    // synth sample rate / resonator tuning.
    FormulaDefinition def;
    def.name = "impact_pitch_scale";
    def.category = "audio";
    def.description = "Impact approach speed (m/s) -> pitch multiplier (~0.85..1.25). "
                      "Harder strikes ring brighter.";
    def.inputs = {
        {"approachSpeed", VT::FLOAT, "m/s", 2.0f}
    };
    def.output = {VT::FLOAT, "multiplier"};
    def.coefficients = {
        {"basePitch",        0.85f},
        {"pitchRange",       0.40f},
        {"pitchDecayPerMps", 0.30f}
    };

    // basePitch + pitchRange * (1 - exp(negate(approachSpeed * pitchDecayPerMps)))
    def.expressions[QT::FULL] =
        binOp("+", var("basePitch"),
            binOp("*", var("pitchRange"),
                binOp("-", lit(1.0f),
                    fn("exp",
                        fn("negate",
                            binOp("*", var("approachSpeed"), var("pitchDecayPerMps")))))));

    def.source = "Procedural-audio bundle (design §9). Seed coefficients; "
                 "refine/validate in the Workbench.";
    return def;
}

FormulaDefinition AudioTemplates::createAggregateEventRate()
{
    // rate = min(maxRateHz, baseRateHz + slopeHzPerMps * approachSpeed)
    // PhISEM aggregate (sand/gravel/water) grain density from contact speed —
    // faster contact spawns more micro-impacts (Cook PhISEM, design §5b).
    FormulaDefinition def;
    def.name = "aggregate_event_rate";
    def.category = "audio";
    def.description = "Aggregate footstep/impact speed (m/s) -> PhISEM grain rate (Hz). "
                      "Higher speed spawns more micro-impact grains.";
    def.inputs = {
        {"approachSpeed", VT::FLOAT, "m/s", 1.5f}
    };
    def.output = {VT::FLOAT, "Hz"};
    def.coefficients = {
        {"baseRateHz",     400.0f},
        {"slopeHzPerMps",  300.0f},
        {"maxRateHz",     2200.0f}
    };

    // min(maxRateHz, baseRateHz + slopeHzPerMps * approachSpeed)
    def.expressions[QT::FULL] =
        binOp("min", var("maxRateHz"),
            binOp("+", var("baseRateHz"),
                binOp("*", var("slopeHzPerMps"), var("approachSpeed"))));

    def.source = "Procedural-audio bundle (design §5b/§9); Cook PhISEM stochastic "
                 "event model. Seed coefficients; refine/validate in the Workbench.";
    return def;
}

} // namespace Vestige
