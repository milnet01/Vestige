/// @file formula.h
/// @brief Formula definition with metadata, typed inputs, quality tiers.
///
/// A FormulaDefinition represents a named mathematical formula with:
/// - Typed inputs with units and defaults
/// - Expression trees per quality tier (Full/Approximate/LUT)
/// - Fitted coefficients and provenance metadata
/// - JSON serialization for the formula library
#pragma once

#include "formula/expression.h"

#include <map>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Formula evaluation quality levels.
enum class QualityTier
{
    FULL,          ///< Original formula, highest accuracy
    APPROXIMATE,   ///< Simplified expression, ~95% accuracy, ~50% cost
    LUT            ///< Precomputed lookup table, ~90% accuracy, ~10% cost
};

/// @brief Value types for formula inputs and outputs.
enum class FormulaValueType
{
    FLOAT,   ///< Scalar float
    VEC2,    ///< 2D vector
    VEC3,    ///< 3D vector
    VEC4     ///< 4D vector
};

/// @brief A single typed input to a formula.
struct FormulaInput
{
    std::string name;                             ///< Variable name (e.g. "surfaceArea")
    FormulaValueType type = FormulaValueType::FLOAT;
    std::string unit;                             ///< Unit string (e.g. "m/s", "kg/m3")
    float defaultValue = 0.0f;                    ///< Default value if not provided
};

/// @brief The output type and unit of a formula.
struct FormulaOutput
{
    FormulaValueType type = FormulaValueType::FLOAT;
    std::string unit;                             ///< Unit string (e.g. "N", "m/s")
};

/// @brief A named formula with metadata and multiple quality tiers.
///
/// Each formula has:
/// - Unique name and category for library organization
/// - Typed inputs with units and defaults
/// - One expression tree per quality tier
/// - Named coefficients (fitted or manual)
/// - Provenance tracking (source, accuracy)
struct FormulaDefinition
{
    std::string name;                ///< Unique identifier (e.g. "aerodynamic_drag")
    std::string category;            ///< Category: wind, water, lighting, collision, material, physics
    std::string description;         ///< Human-readable description

    std::vector<FormulaInput> inputs;   ///< Typed inputs
    FormulaOutput output;               ///< Output type and unit

    /// @brief Expression tree for each quality tier.
    /// Only FULL is required; APPROXIMATE and LUT are optional.
    std::map<QualityTier, std::unique_ptr<ExprNode>> expressions;

    /// @brief Named coefficients (e.g. {"dragCoeff": 0.47}).
    /// These are available as variables during evaluation.
    std::map<std::string, float> coefficients;

    std::string source;              ///< Provenance (e.g. "fitted from simulation, 2026-04")
    float accuracy = 1.0f;          ///< R^2 accuracy vs reference data [0, 1]

    // -- Utilities ----------------------------------------------------------

    /// @brief Deep-copies this definition (including all expression trees).
    FormulaDefinition clone() const;

    /// @brief Returns true if the named quality tier has an expression.
    bool hasTier(QualityTier tier) const;

    /// @brief Returns the expression for a tier, falling back to FULL.
    const ExprNode* getExpression(QualityTier tier = QualityTier::FULL) const;

    // -- JSON serialization -------------------------------------------------

    nlohmann::json toJson() const;
    static FormulaDefinition fromJson(const nlohmann::json& j);
};

// -- Enum string conversion helpers -----------------------------------------

const char* qualityTierToString(QualityTier tier);
QualityTier qualityTierFromString(const std::string& str);

const char* formulaValueTypeToString(FormulaValueType type);
FormulaValueType formulaValueTypeFromString(const std::string& str);

} // namespace Vestige
