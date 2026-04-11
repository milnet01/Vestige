/// @file formula_doc_generator.cpp
/// @brief Markdown documentation generator for formula templates.
#include "formula/formula_doc_generator.h"

#include <map>
#include <sstream>

namespace Vestige
{

// ---------------------------------------------------------------------------
// FormulaDocGenerator::generateMarkdown
// ---------------------------------------------------------------------------

std::string FormulaDocGenerator::generateMarkdown(
    const std::vector<FormulaDefinition>& formulas) const
{
    std::ostringstream out;

    out << "# Formula Library Reference\n\n";
    out << "Auto-generated documentation for all registered formula templates.\n\n";

    // Summary table
    out << "## Summary\n\n";
    out << generateSummaryTable(formulas);
    out << "\n";

    // Group formulas by category
    std::map<std::string, std::vector<const FormulaDefinition*>> byCategory;
    for (const auto& f : formulas)
    {
        byCategory[f.category].push_back(&f);
    }

    // Per-category sections
    for (const auto& [category, categoryFormulas] : byCategory)
    {
        out << "## Category: " << category << "\n\n";

        for (const auto* f : categoryFormulas)
        {
            out << generateFormulaDoc(*f);
            out << "\n---\n\n";
        }
    }

    // Footer
    out << "## Statistics\n\n";
    out << "- **Total formulas:** " << formulas.size() << "\n";

    // Count formulas with APPROXIMATE tier
    int approxCount = 0;
    for (const auto& f : formulas)
    {
        if (f.hasTier(QualityTier::APPROXIMATE))
        {
            ++approxCount;
        }
    }
    out << "- **With APPROXIMATE tier:** " << approxCount << "\n";

    // Count categories
    out << "- **Categories:** " << byCategory.size() << "\n";

    return out.str();
}

// ---------------------------------------------------------------------------
// FormulaDocGenerator::generateFormulaDoc
// ---------------------------------------------------------------------------

std::string FormulaDocGenerator::generateFormulaDoc(
    const FormulaDefinition& formula) const
{
    std::ostringstream out;

    out << "### " << formula.name << "\n\n";

    if (!formula.description.empty())
    {
        out << formula.description << "\n\n";
    }

    // Inputs table
    if (!formula.inputs.empty())
    {
        out << "**Inputs:**\n\n";
        out << "| Name | Type | Unit | Default |\n";
        out << "|------|------|------|---------|\n";

        for (const auto& inp : formula.inputs)
        {
            out << "| " << inp.name
                << " | " << formulaValueTypeToString(inp.type)
                << " | " << (inp.unit.empty() ? "-" : inp.unit)
                << " | " << inp.defaultValue
                << " |\n";
        }
        out << "\n";
    }

    // Coefficients table
    if (!formula.coefficients.empty())
    {
        out << "**Coefficients:**\n\n";
        out << "| Name | Value |\n";
        out << "|------|-------|\n";

        for (const auto& [name, value] : formula.coefficients)
        {
            out << "| " << name << " | " << value << " |\n";
        }
        out << "\n";
    }

    // Quality tiers
    out << "**Quality Tiers:** ";
    bool first = true;
    if (formula.hasTier(QualityTier::FULL))
    {
        out << "FULL";
        first = false;
    }
    if (formula.hasTier(QualityTier::APPROXIMATE))
    {
        if (!first) out << ", ";
        out << "APPROXIMATE";
        first = false;
    }
    if (formula.hasTier(QualityTier::LUT))
    {
        if (!first) out << ", ";
        out << "LUT";
    }
    out << "\n\n";

    // Output type and unit
    out << "**Output:** " << formulaValueTypeToString(formula.output.type);
    if (!formula.output.unit.empty())
    {
        out << " (" << formula.output.unit << ")";
    }
    out << "\n\n";

    // Source attribution
    if (!formula.source.empty())
    {
        out << "**Source:** " << formula.source << "\n\n";
    }

    return out.str();
}

// ---------------------------------------------------------------------------
// FormulaDocGenerator::generateSummaryTable
// ---------------------------------------------------------------------------

std::string FormulaDocGenerator::generateSummaryTable(
    const std::vector<FormulaDefinition>& formulas) const
{
    std::ostringstream out;

    out << "| Name | Category | FULL | APPROX | LUT | Description |\n";
    out << "|------|----------|------|--------|-----|-------------|\n";

    for (const auto& f : formulas)
    {
        out << "| " << f.name
            << " | " << f.category
            << " | " << (f.hasTier(QualityTier::FULL) ? "Y" : "-")
            << " | " << (f.hasTier(QualityTier::APPROXIMATE) ? "Y" : "-")
            << " | " << (f.hasTier(QualityTier::LUT) ? "Y" : "-")
            << " | " << f.description
            << " |\n";
    }

    return out.str();
}

} // namespace Vestige
