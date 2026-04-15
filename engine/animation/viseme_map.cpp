// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file viseme_map.cpp
/// @brief Viseme-to-ARKit blend shape mapping.
///
/// Maps Rhubarb Lip Sync shapes (A-H, X) to ARKit 52 blend shape weights.
/// Mappings calibrated against Preston Blair reference charts and
/// OVR Lipsync viseme documentation.
#include "animation/viseme_map.h"

#include "animation/facial_presets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Display names
// ---------------------------------------------------------------------------

static const char* s_visemeNames[] = {
    "X (Rest)",
    "A (P/B/M)",
    "B (K/S/T/EE)",
    "C (EH/AE)",
    "D (AA)",
    "E (AO/ER)",
    "F (UW/OW/W)",
    "G (F/V)",
    "H (L)",
};

const char* visemeName(Viseme viseme)
{
    auto idx = static_cast<size_t>(viseme);
    if (idx >= static_cast<size_t>(Viseme::COUNT))
    {
        return "Unknown";
    }
    return s_visemeNames[idx];
}

// ---------------------------------------------------------------------------
// Viseme shape database
// ---------------------------------------------------------------------------

static const std::array<VisemeShape, static_cast<size_t>(Viseme::COUNT)> s_shapes = {{
    // X — Rest / silence: relaxed closed mouth (all weights zero)
    {Viseme::X, "Rest", {}},

    // A — Closed, lip pressure: P, B, M (bilabial)
    {Viseme::A, "Closed", {
        {BlendShape::MOUTH_CLOSE,        0.8f},
        {BlendShape::MOUTH_PRESS_LEFT,   0.4f},
        {BlendShape::MOUTH_PRESS_RIGHT,  0.4f},
    }},

    // B — Slightly open, clenched teeth: K, S, T, EE
    {Viseme::B, "Teeth", {
        {BlendShape::JAW_OPEN,             0.1f},
        {BlendShape::MOUTH_CLOSE,          0.3f},
        {BlendShape::MOUTH_STRETCH_LEFT,   0.3f},
        {BlendShape::MOUTH_STRETCH_RIGHT,  0.3f},
    }},

    // C — Open mouth: EH, AE vowels
    {Viseme::C, "Open", {
        {BlendShape::JAW_OPEN,                0.35f},
        {BlendShape::MOUTH_LOWER_DOWN_LEFT,   0.3f},
        {BlendShape::MOUTH_LOWER_DOWN_RIGHT,  0.3f},
    }},

    // D — Wide open: AA (as in "father")
    {Viseme::D, "Wide", {
        {BlendShape::JAW_OPEN,                0.7f},
        {BlendShape::MOUTH_LOWER_DOWN_LEFT,   0.5f},
        {BlendShape::MOUTH_LOWER_DOWN_RIGHT,  0.5f},
    }},

    // E — Slightly rounded: AO (as in "off"), ER (as in "bird")
    {Viseme::E, "Rounded", {
        {BlendShape::JAW_OPEN,       0.3f},
        {BlendShape::MOUTH_FUNNEL,   0.4f},
        {BlendShape::MOUTH_PUCKER,   0.2f},
    }},

    // F — Puckered: UW (as in "you"), OW (as in "show"), W
    {Viseme::F, "Pucker", {
        {BlendShape::MOUTH_PUCKER,   0.7f},
        {BlendShape::MOUTH_FUNNEL,   0.5f},
        {BlendShape::JAW_OPEN,       0.15f},
    }},

    // G — Upper teeth on lower lip: F, V (labiodental)
    {Viseme::G, "F/V", {
        {BlendShape::MOUTH_UPPER_UP_LEFT,   0.3f},
        {BlendShape::MOUTH_UPPER_UP_RIGHT,  0.3f},
        {BlendShape::JAW_OPEN,              0.1f},
    }},

    // H — Tongue raised: long L sounds
    {Viseme::H, "L", {
        {BlendShape::JAW_OPEN,                0.25f},
        {BlendShape::TONGUE_OUT,              0.3f},
        {BlendShape::MOUTH_LOWER_DOWN_LEFT,   0.2f},
        {BlendShape::MOUTH_LOWER_DOWN_RIGHT,  0.2f},
    }},
}};

// ---------------------------------------------------------------------------
// VisemeMap implementation
// ---------------------------------------------------------------------------

const VisemeShape& VisemeMap::get(Viseme viseme)
{
    auto idx = static_cast<size_t>(viseme);
    if (idx >= s_shapes.size())
    {
        return s_shapes[static_cast<size_t>(Viseme::X)];
    }
    return s_shapes[idx];
}

Viseme VisemeMap::fromRhubarbChar(char c)
{
    switch (c)
    {
        case 'X': case 'x': return Viseme::X;
        case 'A': case 'a': return Viseme::A;
        case 'B': case 'b': return Viseme::B;
        case 'C': case 'c': return Viseme::C;
        case 'D': case 'd': return Viseme::D;
        case 'E': case 'e': return Viseme::E;
        case 'F': case 'f': return Viseme::F;
        case 'G': case 'g': return Viseme::G;
        case 'H': case 'h': return Viseme::H;
        default:            return Viseme::X;
    }
}

char VisemeMap::toRhubarbChar(Viseme viseme)
{
    switch (viseme)
    {
        case Viseme::X: return 'X';
        case Viseme::A: return 'A';
        case Viseme::B: return 'B';
        case Viseme::C: return 'C';
        case Viseme::D: return 'D';
        case Viseme::E: return 'E';
        case Viseme::F: return 'F';
        case Viseme::G: return 'G';
        case Viseme::H: return 'H';
        default:        return 'X';
    }
}

void VisemeMap::blendWeights(Viseme a, Viseme b, float t,
                             std::unordered_map<std::string, float>& out)
{
    out.clear();
    t = std::clamp(t, 0.0f, 1.0f);

    const auto& shapeA = get(a);
    const auto& shapeB = get(b);

    // Add shape A entries weighted by (1 - t)
    for (const auto& entry : shapeA.entries)
    {
        out[entry.shapeName] = entry.weight * (1.0f - t);
    }

    // Add / blend shape B entries weighted by t
    for (const auto& entry : shapeB.entries)
    {
        out[entry.shapeName] += entry.weight * t;
    }
}

int VisemeMap::getCount()
{
    return static_cast<int>(Viseme::COUNT);
}

} // namespace Vestige
