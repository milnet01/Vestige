// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file lut_generator.h
/// @brief Binary LUT generator for the Formula Pipeline.
///
/// Samples a formula over input ranges using the expression evaluator,
/// writes a binary lookup table in VLUT format. LUTs provide O(1) formula
/// evaluation at the cost of memory and reduced accuracy.
#pragma once

#include "formula/formula.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief VLUT binary file magic number.
constexpr uint32_t VLUT_MAGIC = 0x54554C56;  // "VLUT" in little-endian

/// @brief VLUT format version.
constexpr uint32_t VLUT_VERSION = 1;

/// @brief Flags for VLUT files.
enum class LutFlags : uint32_t
{
    NONE              = 0,
    INTERPOLATE_LINEAR = 1  ///< Use linear interpolation between samples
};

/// @brief Defines one axis of a LUT (an input variable's sample range).
struct LutAxisDef
{
    std::string variableName;   ///< Input variable to sample along this axis
    float minValue = 0.0f;      ///< Start of sampling range
    float maxValue = 1.0f;      ///< End of sampling range
    uint32_t resolution = 256;  ///< Number of samples along this axis
};

/// @brief Result of LUT generation.
struct LutGenerateResult
{
    bool success = false;
    std::vector<float> data;               ///< Sampled values (row-major)
    std::vector<LutAxisDef> axes;          ///< Axis definitions used
    float minValue = 0.0f;                 ///< Minimum value in the LUT
    float maxValue = 0.0f;                 ///< Maximum value in the LUT
    std::string errorMessage;
};

/// @brief Generates binary lookup tables from formula expression trees.
///
/// Usage:
///   LutAxisDef xAxis{"depth", 0.0f, 10.0f, 256};
///   LutAxisDef yAxis{"angle", 0.0f, 90.0f, 256};
///   auto result = LutGenerator::generate(formula, {xAxis, yAxis});
///   LutGenerator::writeToFile(result, "caustics.vlut");
class LutGenerator
{
public:
    /// @brief Generate a LUT by sampling a formula over the given axes.
    /// @param formula The formula definition to sample.
    /// @param axes 1-3 axis definitions specifying input ranges and resolution.
    /// @param tier Quality tier to evaluate.
    /// @param extraVars Additional variable values (for inputs not on an axis).
    /// @return LUT data and metadata.
    static LutGenerateResult generate(
        const FormulaDefinition& formula,
        const std::vector<LutAxisDef>& axes,
        QualityTier tier = QualityTier::FULL,
        const std::unordered_map<std::string, float>& extraVars = {});

    /// @brief Write a generated LUT to a binary VLUT file.
    /// @param result The LUT generation result.
    /// @param path Output file path.
    /// @return True if the file was written successfully.
    static bool writeToFile(const LutGenerateResult& result, const std::string& path);

    /// @brief FNV-1a hash of a string (used for axis name hashes in VLUT files).
    static uint32_t fnv1aHash(const std::string& str);
};

} // namespace Vestige
