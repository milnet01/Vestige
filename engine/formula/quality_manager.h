// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file quality_manager.h
/// @brief Formula quality tier manager — selects Full/Approximate/LUT per category.
///
/// Provides a central system for managing formula quality tiers at runtime.
/// Each formula category (water, wind, lighting, etc.) can have its own
/// quality override, or fall back to the global tier setting.
#pragma once

#include "formula/formula.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Manages active quality tiers for formula categories.
///
/// The global tier applies to all categories unless overridden.
/// Per-category overrides allow fine-grained control (e.g. high-quality
/// water with low-quality fog on weaker hardware).
///
/// Usage:
///   manager.setGlobalTier(QualityTier::APPROXIMATE);
///   manager.setCategoryTier("water", QualityTier::FULL);
///   auto tier = manager.getEffectiveTier("water");  // FULL (override)
///   auto tier = manager.getEffectiveTier("wind");    // APPROXIMATE (global)
class FormulaQualityManager
{
public:
    FormulaQualityManager() = default;

    /// @brief Set the global quality tier (default for all categories).
    void setGlobalTier(QualityTier tier);

    /// @brief Get the global quality tier.
    QualityTier getGlobalTier() const { return m_globalTier; }

    /// @brief Override the quality tier for a specific category.
    /// @param category Category name (e.g. "water", "wind", "lighting").
    /// @param tier Quality tier to use for this category.
    void setCategoryTier(const std::string& category, QualityTier tier);

    /// @brief Get the quality tier override for a category.
    /// @return The override tier, or the global tier if no override exists.
    QualityTier getCategoryTier(const std::string& category) const;

    /// @brief Check if a category has an explicit quality override.
    bool hasCategoryOverride(const std::string& category) const;

    /// @brief Remove the quality override for a category (reverts to global).
    void clearCategoryOverride(const std::string& category);

    /// @brief Clear all category overrides (everything uses global tier).
    void clearAllOverrides();

    /// @brief Get the effective tier for a category (override if set, else global).
    QualityTier getEffectiveTier(const std::string& category) const;

    /// @brief Get all category overrides.
    const std::unordered_map<std::string, QualityTier>& getOverrides() const
    {
        return m_categoryOverrides;
    }

    // -- JSON persistence ------------------------------------------------------

    /// @brief Serialize to JSON for settings persistence.
    nlohmann::json toJson() const;

    /// @brief Load from JSON.
    void fromJson(const nlohmann::json& j);

private:
    QualityTier m_globalTier = QualityTier::FULL;
    std::unordered_map<std::string, QualityTier> m_categoryOverrides;
};

} // namespace Vestige
