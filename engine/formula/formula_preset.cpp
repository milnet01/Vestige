// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_preset.cpp
/// @brief Formula preset implementation with built-in environment/style presets.
#include "formula/formula_preset.h"
#include "formula/formula_library.h"
#include "core/logger.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace Vestige
{

// ---------------------------------------------------------------------------
// FormulaOverride JSON
// ---------------------------------------------------------------------------

static nlohmann::json overrideToJson(const FormulaOverride& ov)
{
    nlohmann::json j;
    j["formula"] = ov.formulaName;
    j["coefficients"] = ov.coefficients;
    if (!ov.description.empty())
        j["description"] = ov.description;
    return j;
}

static bool overrideFromJson(const nlohmann::json& j, FormulaOverride& ov)
{
    if (!j.contains("formula") || !j.contains("coefficients"))
    {
        Logger::warning("FormulaPreset: override entry missing 'formula' or 'coefficients' field, skipping");
        return false;
    }
    try
    {
        ov.formulaName = j.at("formula").get<std::string>();
        ov.coefficients = j.at("coefficients").get<std::map<std::string, float>>();
        if (j.contains("description"))
            ov.description = j["description"].get<std::string>();
    }
    catch (const nlohmann::json::exception& e)
    {
        Logger::warning(std::string("FormulaPreset: malformed override entry: ") + e.what());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// FormulaPreset JSON
// ---------------------------------------------------------------------------

nlohmann::json FormulaPreset::toJson() const
{
    nlohmann::json j;
    j["name"] = name;
    j["displayName"] = displayName;
    j["category"] = category;
    j["description"] = description;
    if (!author.empty())
        j["author"] = author;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& ov : overrides)
        arr.push_back(overrideToJson(ov));
    j["overrides"] = arr;
    return j;
}

FormulaPreset FormulaPreset::fromJson(const nlohmann::json& j)
{
    FormulaPreset p;
    p.name = j.at("name").get<std::string>();
    p.displayName = j.value("displayName", p.name);
    p.category = j.value("category", "uncategorized");
    p.description = j.value("description", "");
    p.author = j.value("author", "");

    if (j.contains("overrides"))
    {
        for (const auto& item : j["overrides"])
        {
            FormulaOverride ov;
            if (overrideFromJson(item, ov))
                p.overrides.push_back(std::move(ov));
        }
    }
    return p;
}

// ---------------------------------------------------------------------------
// FormulaPresetLibrary
// ---------------------------------------------------------------------------

void FormulaPresetLibrary::registerPreset(FormulaPreset preset)
{
    std::string key = preset.name;
    m_presets[key] = std::move(preset);
}

bool FormulaPresetLibrary::removePreset(const std::string& name)
{
    return m_presets.erase(name) > 0;
}

const FormulaPreset* FormulaPresetLibrary::findByName(const std::string& name) const
{
    auto it = m_presets.find(name);
    return (it != m_presets.end()) ? &it->second : nullptr;
}

std::vector<const FormulaPreset*>
FormulaPresetLibrary::findByCategory(const std::string& category) const
{
    std::vector<const FormulaPreset*> result;
    for (const auto& [k, v] : m_presets)
    {
        if (v.category == category)
            result.push_back(&v);
    }
    return result;
}

std::vector<std::string> FormulaPresetLibrary::getCategories() const
{
    std::set<std::string> cats;
    for (const auto& [k, v] : m_presets)
        cats.insert(v.category);
    return {cats.begin(), cats.end()};
}

std::vector<const FormulaPreset*> FormulaPresetLibrary::getAll() const
{
    std::vector<const FormulaPreset*> result;
    result.reserve(m_presets.size());
    for (const auto& [k, v] : m_presets)
        result.push_back(&v);
    return result;
}

size_t FormulaPresetLibrary::count() const
{
    return m_presets.size();
}

size_t FormulaPresetLibrary::applyPreset(const FormulaPreset& preset,
                                         FormulaLibrary& library)
{
    size_t applied = 0;
    for (const auto& ov : preset.overrides)
    {
        const FormulaDefinition* def = library.findByName(ov.formulaName);
        if (!def)
            continue;

        // Clone the formula, update coefficients, re-register
        FormulaDefinition updated = def->clone();
        for (const auto& [name, val] : ov.coefficients)
            updated.coefficients[name] = val;
        updated.source = "Preset: " + preset.displayName;

        library.registerFormula(std::move(updated));
        ++applied;
    }
    return applied;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

size_t FormulaPresetLibrary::loadFromJson(const nlohmann::json& j)
{
    size_t count = 0;
    for (const auto& item : j)
    {
        try
        {
            registerPreset(FormulaPreset::fromJson(item));
            ++count;
        }
        catch (const nlohmann::json::exception& e)
        {
            Logger::warning(std::string("FormulaPresetLibrary: skipping malformed preset entry: ") + e.what());
        }
    }
    return count;
}

size_t FormulaPresetLibrary::loadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        Logger::warning("FormulaPresetLibrary: cannot open: " + path);
        return 0;
    }
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded())
    {
        Logger::warning("FormulaPresetLibrary: invalid JSON: " + path);
        return 0;
    }
    return loadFromJson(j);
}

nlohmann::json FormulaPresetLibrary::toJson() const
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [k, v] : m_presets)
        arr.push_back(v.toJson());
    return arr;
}

bool FormulaPresetLibrary::saveToFile(const std::string& path) const
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        Logger::warning("FormulaPresetLibrary: cannot write: " + path);
        return false;
    }
    file << toJson().dump(2);
    return file.good();
}

// ---------------------------------------------------------------------------
// Built-in presets
// ---------------------------------------------------------------------------

void FormulaPresetLibrary::registerBuiltinPresets()
{
    // Coefficient name reference (from physics_templates.cpp):
    //   aerodynamic_drag:        Cd
    //   stokes_drag:             mu
    //   fresnel_schlick:         F0
    //   beer_lambert:            alpha
    //   exponential_fog:         density
    //   inverse_square_falloff:  constant, linear, quadratic
    //   wet_darkening:           darkFactor
    //   buoyancy:                g
    //   hooke_spring:            k
    //   coulomb_friction:        mu
    //   terminal_velocity:       g, Cd
    //   gerstner_wave:           amplitude, k, omega, phase
    //   wind_deformation:        (no coefficients — all inputs)

    // ---- Environment presets ------------------------------------------------

    {
        FormulaPreset p;
        p.name = "realistic_desert";
        p.displayName = "Realistic Desert";
        p.category = "environment";
        p.description = "Hot, arid desert with strong winds, harsh sun, "
                        "minimal moisture, and sand-like particle behavior.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.55f}},
             "Higher drag for sand-laden air"},
            {"stokes_drag",
             {{"mu", 1.98e-5f}},
             "Hot air has slightly higher viscosity"},
            {"exponential_fog",
             {{"density", 0.015f}},
             "Light haze from heat shimmer and dust"},
            {"wet_darkening",
             {{"darkFactor", 0.0f}},
             "No moisture darkening in desert"},
            {"fresnel_schlick",
             {{"F0", 0.02f}},
             "Low reflectance for dry sandy surfaces"},
            {"inverse_square_falloff",
             {{"constant", 0.5f}, {"linear", 0.09f}, {"quadratic", 0.05f}},
             "Sharp light falloff in clear desert air"},
        };

        registerPreset(std::move(p));
    }

    {
        FormulaPreset p;
        p.name = "tropical_forest";
        p.displayName = "Tropical Forest";
        p.category = "environment";
        p.description = "Humid tropical jungle with dense foliage, heavy air, "
                        "frequent rain, and lush vegetation.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.5f}},
             "Standard drag in humid air"},
            {"exponential_fog",
             {{"density", 0.06f}},
             "Dense fog from humidity and canopy mist"},
            {"wet_darkening",
             {{"darkFactor", 0.7f}},
             "Persistent moisture on surfaces"},
            {"beer_lambert",
             {{"alpha", 0.4f}},
             "Murky water with organic matter"},
            {"fresnel_schlick",
             {{"F0", 0.04f}},
             "Wet surfaces have higher reflectance"},
        };

        registerPreset(std::move(p));
    }

    {
        FormulaPreset p;
        p.name = "arctic_tundra";
        p.displayName = "Arctic Tundra";
        p.category = "environment";
        p.description = "Frigid arctic landscape with strong icy winds, "
                        "blowing snow, and frozen surfaces.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.42f}},
             "Cold dense air increases drag force per unit"},
            {"stokes_drag",
             {{"mu", 1.6e-5f}},
             "Cold air has lower viscosity"},
            {"exponential_fog",
             {{"density", 0.04f}},
             "Whiteout conditions from blowing snow"},
            {"wet_darkening",
             {{"darkFactor", 0.3f}},
             "Some moisture darkening from snow melt"},
            {"fresnel_schlick",
             {{"F0", 0.08f}},
             "Ice and snow are highly reflective"},
        };

        registerPreset(std::move(p));
    }

    {
        FormulaPreset p;
        p.name = "underwater";
        p.displayName = "Underwater";
        p.category = "environment";
        p.description = "Submerged underwater environment with light absorption, "
                        "caustics, buoyancy, and fluid drag.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 1.0f}},
             "Water drag is much higher than air"},
            {"stokes_drag",
             {{"mu", 1.0e-3f}},
             "Water viscosity (~50x air)"},
            {"beer_lambert",
             {{"alpha", 0.15f}},
             "Clear ocean water absorption"},
            {"exponential_fog",
             {{"density", 0.12f}},
             "Dense underwater visibility falloff"},
            {"fresnel_schlick",
             {{"F0", 0.02f}},
             "Water-to-water internal reflections are low"},
        };

        registerPreset(std::move(p));
    }

    // ---- Stylized presets ---------------------------------------------------

    {
        FormulaPreset p;
        p.name = "anime_cel";
        p.displayName = "Anime / Cel-Shaded";
        p.category = "stylized";
        p.description = "Exaggerated, stylized look inspired by anime. "
                        "Strong wind effects on hair/cloth, vivid colors, "
                        "reduced fog for clarity.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.3f}},
             "Lower drag lets cloth/hair flow dramatically"},
            {"exponential_fog",
             {{"density", 0.005f}},
             "Minimal fog for clear anime-style backgrounds"},
            {"wet_darkening",
             {{"darkFactor", 0.9f}},
             "Dramatic wet effects when raining"},
            {"fresnel_schlick",
             {{"F0", 0.06f}},
             "Slightly boosted reflections for stylized sheen"},
            {"inverse_square_falloff",
             {{"constant", 2.0f}, {"linear", 0.02f}, {"quadratic", 0.005f}},
             "Softer light falloff for flatter shading"},
        };

        registerPreset(std::move(p));
    }

    {
        FormulaPreset p;
        p.name = "painterly";
        p.displayName = "Painterly / Oil Painting";
        p.category = "stylized";
        p.description = "Soft, dreamy atmosphere with heavy fog, muted physics, "
                        "and diffused lighting.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"exponential_fog",
             {{"density", 0.08f}},
             "Heavy atmospheric haze for painterly depth"},
            {"inverse_square_falloff",
             {{"constant", 3.0f}, {"linear", 0.01f}, {"quadratic", 0.002f}},
             "Very soft light falloff for diffused look"},
            {"fresnel_schlick",
             {{"F0", 0.03f}},
             "Subdued reflections for matte appearance"},
        };

        registerPreset(std::move(p));
    }

    // ---- Scenario presets ---------------------------------------------------

    {
        FormulaPreset p;
        p.name = "stormy_weather";
        p.displayName = "Stormy Weather";
        p.category = "scenario";
        p.description = "Intense storm with strong wind, heavy rain, "
                        "reduced visibility, and dramatic cloth movement.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.6f}},
             "Strong wind forces in storm"},
            {"exponential_fog",
             {{"density", 0.1f}},
             "Heavy rain reduces visibility"},
            {"wet_darkening",
             {{"darkFactor", 1.0f}},
             "Maximum wet darkening — everything is soaked"},
            {"fresnel_schlick",
             {{"F0", 0.05f}},
             "Wet surfaces are more reflective"},
            {"beer_lambert",
             {{"alpha", 0.3f}},
             "Turbid water from runoff"},
        };

        registerPreset(std::move(p));
    }

    {
        FormulaPreset p;
        p.name = "calm_interior";
        p.displayName = "Calm Interior";
        p.category = "scenario";
        p.description = "Indoor scene with no wind, controlled lighting, "
                        "and clean atmospheric conditions.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.47f}},
             "Standard still-air drag"},
            {"exponential_fog",
             {{"density", 0.001f}},
             "Nearly no atmospheric scattering indoors"},
            {"wet_darkening",
             {{"darkFactor", 0.0f}},
             "Dry interior surfaces"},
            {"inverse_square_falloff",
             {{"constant", 1.0f}, {"linear", 0.14f}, {"quadratic", 0.07f}},
             "Accurate point light falloff for interiors"},
        };

        registerPreset(std::move(p));
    }

    {
        FormulaPreset p;
        p.name = "biblical_tabernacle";
        p.displayName = "Biblical Tabernacle";
        p.category = "scenario";
        p.description = "Settings tuned for the Tabernacle/Tent of Meeting scene. "
                        "Gentle desert wind, warm interior lighting, fabric curtains.";
        p.author = "Vestige Engine";

        p.overrides = {
            {"aerodynamic_drag",
             {{"Cd", 0.5f}},
             "Moderate drag for tent fabric in gentle breeze"},
            {"exponential_fog",
             {{"density", 0.01f}},
             "Light desert haze around exterior"},
            {"wet_darkening",
             {{"darkFactor", 0.0f}},
             "Dry desert climate"},
            {"inverse_square_falloff",
             {{"constant", 1.0f}, {"linear", 0.22f}, {"quadratic", 0.2f}},
             "Warm oil lamp / candle lighting with faster falloff"},
            {"fresnel_schlick",
             {{"F0", 0.04f}},
             "Gold and bronze furnishing reflections"},
        };

        registerPreset(std::move(p));
    }
}

} // namespace Vestige
