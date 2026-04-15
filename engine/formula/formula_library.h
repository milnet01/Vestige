// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_library.h
/// @brief Registry of named formulas with JSON persistence.
///
/// The FormulaLibrary stores FormulaDefinition objects indexed by name.
/// It supports loading from JSON files/directories, saving, and lookup
/// by name or category. Built-in physics templates are registered via
/// registerBuiltinTemplates().
#pragma once

#include "formula/formula.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Central registry of formula definitions.
class FormulaLibrary
{
public:
    /// @brief Registers a formula definition. Overwrites if name already exists.
    void registerFormula(FormulaDefinition def);

    /// @brief Removes a formula by name.
    /// @return True if the formula was found and removed.
    bool removeFormula(const std::string& name);

    /// @brief Looks up a formula by name.
    /// @return Pointer to the definition, or nullptr if not found.
    const FormulaDefinition* findByName(const std::string& name) const;

    /// @brief Returns all formulas in the given category.
    std::vector<const FormulaDefinition*> findByCategory(const std::string& category) const;

    /// @brief Returns a sorted list of all categories present in the library.
    std::vector<std::string> getCategories() const;

    /// @brief Returns pointers to all registered formulas.
    std::vector<const FormulaDefinition*> getAll() const;

    /// @brief Returns the number of registered formulas.
    size_t count() const;

    /// @brief Removes all formulas from the library.
    void clear();

    // -- Persistence --------------------------------------------------------

    /// @brief Loads formulas from a JSON object (array of formula definitions).
    /// @return Number of formulas loaded.
    size_t loadFromJson(const nlohmann::json& j);

    /// @brief Loads formulas from a JSON file.
    /// @return Number of formulas loaded, or 0 on failure.
    size_t loadFromFile(const std::string& path);

    /// @brief Serializes all formulas to a JSON array.
    nlohmann::json toJson() const;

    /// @brief Saves all formulas to a JSON file.
    /// @return True on success.
    bool saveToFile(const std::string& path) const;

    // -- Built-in templates -------------------------------------------------

    /// @brief Registers all built-in physics/rendering formula templates.
    void registerBuiltinTemplates();

private:
    /// @brief Formulas indexed by name.
    std::map<std::string, FormulaDefinition> m_formulas;
};

} // namespace Vestige
