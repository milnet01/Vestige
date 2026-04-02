/// @file formula_preset.h
/// @brief Named bundles of formula overrides for visual styles and environments.
///
/// A FormulaPreset groups multiple formula coefficient overrides under a
/// descriptive name (e.g. "Realistic Desert", "Anime", "Underwater"). Applying
/// a preset updates the FormulaLibrary's coefficients for each referenced
/// formula, giving a cohesive look/feel across all engine systems.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Vestige
{

class FormulaLibrary;

/// @brief Coefficient overrides for a single formula within a preset.
struct FormulaOverride
{
    std::string formulaName;                   ///< Name of the formula to override
    std::map<std::string, float> coefficients; ///< Coefficient name → value
    std::string description;                   ///< Why these values were chosen
};

/// @brief A named preset bundling multiple formula coefficient overrides.
///
/// Presets represent cohesive visual styles. Applying a preset updates
/// coefficients across multiple formulas simultaneously, giving consistent
/// behavior for wind, lighting, fog, materials, etc.
struct FormulaPreset
{
    std::string name;          ///< Unique identifier (e.g. "realistic_desert")
    std::string displayName;   ///< Human-readable name (e.g. "Realistic Desert")
    std::string category;      ///< Grouping: "environment", "stylized", "scenario"
    std::string description;   ///< Detailed description of the visual style
    std::string author;        ///< Who created this preset

    std::vector<FormulaOverride> overrides;  ///< Per-formula coefficient overrides

    // -- JSON serialization ---------------------------------------------------

    nlohmann::json toJson() const;
    static FormulaPreset fromJson(const nlohmann::json& j);
};

/// @brief Registry of formula presets with built-in defaults.
class FormulaPresetLibrary
{
public:
    /// @brief Registers a preset. Overwrites if name already exists.
    void registerPreset(FormulaPreset preset);

    /// @brief Removes a preset by name.
    bool removePreset(const std::string& name);

    /// @brief Looks up a preset by name.
    const FormulaPreset* findByName(const std::string& name) const;

    /// @brief Returns all presets in the given category.
    std::vector<const FormulaPreset*> findByCategory(const std::string& category) const;

    /// @brief Returns a sorted list of all categories.
    std::vector<std::string> getCategories() const;

    /// @brief Returns all registered presets.
    std::vector<const FormulaPreset*> getAll() const;

    /// @brief Number of registered presets.
    size_t count() const;

    /// @brief Applies a preset's coefficient overrides to a FormulaLibrary.
    /// @return Number of formulas successfully updated.
    static size_t applyPreset(const FormulaPreset& preset, FormulaLibrary& library);

    // -- Persistence ----------------------------------------------------------

    size_t loadFromJson(const nlohmann::json& j);
    size_t loadFromFile(const std::string& path);
    nlohmann::json toJson() const;
    bool saveToFile(const std::string& path) const;

    // -- Built-in presets -----------------------------------------------------

    /// @brief Registers all built-in presets.
    void registerBuiltinPresets();

private:
    std::map<std::string, FormulaPreset> m_presets;
};

} // namespace Vestige
