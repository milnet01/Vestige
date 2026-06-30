// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file material_sound_bank.cpp
/// @brief Material sound bank: JSON load + curve/jitter-driven synthesis (AX4 S5).
#include "audio/procedural/material_sound_bank.h"

#include "audio/procedural/audio_curves.h"
#include "utils/json_size_cap.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace Vestige::Procedural
{

namespace
{

/// @brief Maps a lowercase bank-key to a SurfaceMaterial. Returns false for an
///        unknown name (the entry is skipped, bank otherwise unchanged).
bool parseMaterialName(const std::string& name, SurfaceMaterial& out)
{
    // Ordered to match the enum; the bank keys are the lowercased labels.
    static const std::array<std::pair<const char*, SurfaceMaterial>, kSurfaceMaterialCount>
        kNames{{{"default", SurfaceMaterial::Default},
                {"stone", SurfaceMaterial::Stone},
                {"wood", SurfaceMaterial::Wood},
                {"metal", SurfaceMaterial::Metal},
                {"cloth", SurfaceMaterial::Cloth},
                {"sand", SurfaceMaterial::Sand},
                {"water", SurfaceMaterial::Water},
                {"grass", SurfaceMaterial::Grass},
                {"dirt", SurfaceMaterial::Dirt},
                {"glass", SurfaceMaterial::Glass}}};
    for (const auto& [key, mat] : kNames)
    {
        if (name == key)
        {
            out = mat;
            return true;
        }
    }
    return false;
}

/// @brief Parses one material entry. Unknown keys (e.g. `pitchGlide`, not yet
///        consumed by the §5b synth core) are ignored.
MaterialSoundDef parseEntry(const nlohmann::json& j)
{
    MaterialSoundDef d;
    const std::string model = j.value("model", std::string("modal"));
    d.model = (model == "phisem") ? SynthModel::Phisem : SynthModel::Modal;
    d.durSec = j.value("durSec", 0.12f);

    if (d.model == SynthModel::Modal)
    {
        if (auto it = j.find("modes"); it != j.end() && it->is_array())
        {
            for (const auto& m : *it)
            {
                Mode mode;
                mode.freqHz = m.value("f", 0.0f);
                mode.decay  = m.value("d", 0.0f);
                mode.gain   = m.value("g", 0.0f);
                d.modes.push_back(mode);
            }
        }
        d.pitchJitterCents = j.value("pitchJitterCents", 0.0f);
        d.gainJitterDb     = j.value("gainJitterDb", 0.0f);
    }
    else
    {
        d.centreHz    = j.value("centreHz", 1500.0f);
        d.qual        = j.value("qual", 2.0f);
        d.eventRateHz = j.value("eventRateHz", 1000.0f);
        d.energyDecay = j.value("energyDecay", 18.0f);
    }
    return d;
}

/// @brief Applies a parsed `materials` object onto @a defs. Shared by the file
///        and string load paths.
bool applyDoc(const nlohmann::json& doc,
              std::array<MaterialSoundDef, kSurfaceMaterialCount>& defs)
{
    if (!doc.is_object())
    {
        return false;
    }
    auto materials = doc.find("materials");
    if (materials == doc.end() || !materials->is_object())
    {
        return false;
    }
    for (const auto& [name, entry] : materials->items())
    {
        SurfaceMaterial mat;
        if (parseMaterialName(name, mat) && entry.is_object())
        {
            defs[static_cast<std::size_t>(mat)] = parseEntry(entry);
        }
    }
    return true;
}

}  // namespace

MaterialSoundBank::MaterialSoundBank()
{
    // Built-in fallback for every material: the §6 `default` generic dull thud,
    // so synthesise() is usable before (or instead of) a loaded bank. Authored
    // entries overwrite their slot on load; omitted materials keep this thud.
    MaterialSoundDef thud;
    thud.model           = SynthModel::Modal;
    thud.modes           = {{300.0f, 40.0f, 1.0f}};
    thud.durSec          = 0.12f;
    thud.pitchJitterCents = 40.0f;
    thud.gainJitterDb    = 2.0f;
    m_defs.fill(thud);
}

bool MaterialSoundBank::loadFromFile(const std::string& path)
{
    std::optional<nlohmann::json> doc =
        JsonSizeCap::loadJsonWithSizeCap(path, "MaterialSoundBank");
    if (!doc)
    {
        return false;  // missing / oversized / malformed — bank unchanged.
    }
    return applyDoc(*doc, m_defs);
}

bool MaterialSoundBank::loadFromString(const std::string& json)
{
    nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded())
    {
        return false;
    }
    return applyDoc(doc, m_defs);
}

const MaterialSoundDef& MaterialSoundBank::defFor(SurfaceMaterial material) const
{
    const auto idx = static_cast<std::size_t>(material);
    return m_defs[std::min(idx, kSurfaceMaterialCount - 1)];
}

std::size_t MaterialSoundBank::synthesize(SurfaceMaterial material, float approachSpeed,
                                          float envelopeScale,
                                          const std::function<float()>& sample,
                                          std::vector<std::int16_t>& out) const
{
    const MaterialSoundDef& d = defFor(material);
    const float durSec = std::clamp(d.durSec * envelopeScale, 0.0f, kMaxDurationSec);

    if (d.model == SynthModel::Modal)
    {
        ModalStrike s;
        s.modes      = d.modes;
        s.durSec     = durSec;
        // Draw order is fixed (pitch then gain) so a seeded emitter replays
        // identically — the PCM-shape tests rely on it.
        s.pitchScale = jitterPitch(impactPitchScale(approachSpeed), d.pitchJitterCents, sample);
        s.energyGain = std::clamp(
            jitterGain(impactLoudnessGain(approachSpeed), d.gainJitterDb, sample), 0.0f, 1.0f);
        return synthesizeModal(s, out);
    }

    // PhISEM: grain rate scales with contact speed via the FW curve, anchored
    // so the bank's eventRateHz is the rate at the curve's nominal speed.
    // Variation is intrinsic to the Poisson RNG (§5b) — no explicit jitter.
    PhisemStrike s;
    s.centreHz    = d.centreHz;
    s.qual        = d.qual;
    s.eventRateHz = d.eventRateHz
                  * (aggregateEventRate(approachSpeed) / aggregateEventRate(kAggregateRefSpeedMps));
    s.durSec      = durSec;
    s.energyDecay = d.energyDecay;
    s.energyGain  = impactLoudnessGain(approachSpeed);
    return synthesizePhisem(s, out, sample);
}

float jitterPitch(float base, float spreadCents, const std::function<float()>& sample)
{
    const float cents = (sample() * 2.0f - 1.0f) * spreadCents;  // [-spread, +spread)
    return base * std::pow(2.0f, cents / 1200.0f);
}

float jitterGain(float base, float spreadDb, const std::function<float()>& sample)
{
    const float db = (sample() * 2.0f - 1.0f) * spreadDb;  // [-spread, +spread)
    return base * std::pow(10.0f, db / 20.0f);
}

} // namespace Vestige::Procedural
