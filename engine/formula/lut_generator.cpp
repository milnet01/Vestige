/// @file lut_generator.cpp
/// @brief Binary LUT generator implementation.
#include "formula/lut_generator.h"
#include "formula/expression_eval.h"
#include "core/logger.h"

#include <cstring>
#include <fstream>
#include <limits>

namespace Vestige
{

// ---------------------------------------------------------------------------
// VLUT binary file layout
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

struct VlutHeader
{
    uint32_t magic;           // "VLUT"
    uint32_t version;         // 1
    uint32_t dimensions;      // 1, 2, or 3
    uint32_t axisSizes[3];    // [W, H, D] — unused axes are 0
    uint32_t valueType;       // 0 = FLOAT32
    uint32_t flags;           // LutFlags
};

struct VlutAxisEntry
{
    uint32_t nameHash;        // FNV-1a of variable name
    float minValue;
    float maxValue;
    uint32_t padding;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

LutGenerateResult LutGenerator::generate(
    const FormulaDefinition& formula,
    const std::vector<LutAxisDef>& axes,
    QualityTier tier,
    const std::unordered_map<std::string, float>& extraVars)
{
    LutGenerateResult result;

    if (axes.empty() || axes.size() > 3)
    {
        result.errorMessage = "LUT requires 1-3 axes";
        return result;
    }

    for (const auto& axis : axes)
    {
        if (axis.resolution < 2)
        {
            result.errorMessage = "Axis '" + axis.variableName + "' resolution must be >= 2";
            return result;
        }
    }

    const ExprNode* expr = formula.getExpression(tier);
    if (!expr)
    {
        result.errorMessage = "No expression for tier";
        return result;
    }

    result.axes = axes;

    // Compute total sample count
    size_t totalSamples = 1;
    for (const auto& axis : axes)
    {
        totalSamples *= axis.resolution;
    }
    result.data.resize(totalSamples);

    // Prepare evaluator and base variable map
    ExpressionEvaluator evaluator;
    std::unordered_map<std::string, float> vars = extraVars;

    // Add coefficient defaults
    for (const auto& [name, val] : formula.coefficients)
    {
        vars[name] = val;
    }

    // Add input defaults for variables not on axes and not in extraVars
    for (const auto& input : formula.inputs)
    {
        if (vars.find(input.name) == vars.end())
        {
            vars[input.name] = input.defaultValue;
        }
    }

    result.minValue = std::numeric_limits<float>::max();
    result.maxValue = std::numeric_limits<float>::lowest();

    // Sample based on dimensionality
    uint32_t w = axes[0].resolution;
    uint32_t h = axes.size() > 1 ? axes[1].resolution : 1;
    uint32_t d = axes.size() > 2 ? axes[2].resolution : 1;

    for (uint32_t iz = 0; iz < d; ++iz)
    {
        if (axes.size() > 2)
        {
            float t = static_cast<float>(iz) / static_cast<float>(d - 1);
            vars[axes[2].variableName] = axes[2].minValue + t * (axes[2].maxValue - axes[2].minValue);
        }

        for (uint32_t iy = 0; iy < h; ++iy)
        {
            if (axes.size() > 1)
            {
                float t = static_cast<float>(iy) / static_cast<float>(h - 1);
                vars[axes[1].variableName] = axes[1].minValue + t * (axes[1].maxValue - axes[1].minValue);
            }

            for (uint32_t ix = 0; ix < w; ++ix)
            {
                float t = static_cast<float>(ix) / static_cast<float>(w - 1);
                vars[axes[0].variableName] = axes[0].minValue + t * (axes[0].maxValue - axes[0].minValue);

                size_t index = static_cast<size_t>(iz) * h * w
                             + static_cast<size_t>(iy) * w
                             + ix;

                float val = evaluator.evaluate(*expr, vars);
                result.data[index] = val;

                if (val < result.minValue) result.minValue = val;
                if (val > result.maxValue) result.maxValue = val;
            }
        }
    }

    result.success = true;
    return result;
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

bool LutGenerator::writeToFile(const LutGenerateResult& result, const std::string& path)
{
    if (!result.success || result.data.empty())
        return false;

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        Logger::warning("LutGenerator: cannot open file for writing: " + path);
        return false;
    }

    // Write header
    VlutHeader header{};
    header.magic = VLUT_MAGIC;
    header.version = VLUT_VERSION;
    header.dimensions = static_cast<uint32_t>(result.axes.size());
    for (size_t i = 0; i < result.axes.size() && i < 3; ++i)
    {
        header.axisSizes[i] = result.axes[i].resolution;
    }
    header.valueType = 0;  // FLOAT32
    header.flags = static_cast<uint32_t>(LutFlags::INTERPOLATE_LINEAR);

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write axis definitions
    for (const auto& axis : result.axes)
    {
        VlutAxisEntry entry{};
        entry.nameHash = fnv1aHash(axis.variableName);
        entry.minValue = axis.minValue;
        entry.maxValue = axis.maxValue;
        entry.padding = 0;
        file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    // Write data
    file.write(reinterpret_cast<const char*>(result.data.data()),
               static_cast<std::streamsize>(result.data.size() * sizeof(float)));

    return file.good();
}

// ---------------------------------------------------------------------------
// FNV-1a hash
// ---------------------------------------------------------------------------

uint32_t LutGenerator::fnv1aHash(const std::string& str)
{
    uint32_t hash = 2166136261u;
    for (char c : str)
    {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        hash *= 16777619u;
    }
    return hash;
}

} // namespace Vestige
