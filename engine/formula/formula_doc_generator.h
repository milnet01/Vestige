// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_doc_generator.h
/// @brief Auto-generates markdown documentation from formula template metadata.
///
/// Produces human-readable reference documentation including summary tables,
/// input/coefficient listings, and tier availability for each formula.
#pragma once

#include "formula/formula.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Generates markdown documentation from formula definitions.
///
/// Used by the workbench export and the build system to keep formula
/// documentation in sync with the actual template code.
class FormulaDocGenerator
{
public:
    /// @brief Generate a complete markdown document for all registered formulas.
    /// @param formulas Vector of formula definitions to document.
    /// @return Complete markdown string organized by category.
    std::string generateMarkdown(
        const std::vector<FormulaDefinition>& formulas) const;

    /// @brief Generate markdown for a single formula (used in workbench export).
    /// @param formula The formula to document.
    /// @return Markdown string with inputs, coefficients, and tier info.
    std::string generateFormulaDoc(
        const FormulaDefinition& formula) const;

    /// @brief Generate a summary table showing all formulas and their tier availability.
    /// @param formulas Vector of formula definitions.
    /// @return Markdown table string.
    std::string generateSummaryTable(
        const std::vector<FormulaDefinition>& formulas) const;
};

} // namespace Vestige
