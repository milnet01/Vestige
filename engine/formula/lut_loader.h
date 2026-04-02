/// @file lut_loader.h
/// @brief Binary LUT loader for the Formula Pipeline.
///
/// Loads VLUT binary files and provides O(1) lookup with linear
/// interpolation. Used at runtime for formulas compiled to LUT tier.
#pragma once

#include "formula/lut_generator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Axis metadata loaded from a VLUT file.
struct LutAxis
{
    uint32_t nameHash = 0;    ///< FNV-1a hash of variable name
    float minValue = 0.0f;
    float maxValue = 0.0f;
    uint32_t size = 0;        ///< Number of samples along this axis
};

/// @brief Loads and samples binary lookup tables (VLUT format).
///
/// Provides O(1) evaluation of precomputed formulas via interpolated
/// table lookup. Supports 1D, 2D, and 3D tables.
///
/// Usage:
///   LutLoader lut;
///   if (lut.loadFromFile("caustics.vlut"))
///       float val = lut.sample2D(depth, angle);
class LutLoader
{
public:
    LutLoader() = default;

    /// @brief Load a VLUT file from disk.
    /// @param path Path to the .vlut file.
    /// @return True if loaded successfully.
    bool loadFromFile(const std::string& path);

    /// @brief Load from a LutGenerateResult (in-memory, no file I/O).
    /// @param result A successfully generated LUT.
    /// @return True if the result was valid.
    bool loadFromResult(const LutGenerateResult& result);

    /// @brief Sample a 1D LUT with linear interpolation.
    /// @param x Normalized or world-space input (clamped to axis range).
    /// @return Interpolated value.
    float sample1D(float x) const;

    /// @brief Sample a 2D LUT with bilinear interpolation.
    /// @param x First axis input.
    /// @param y Second axis input.
    /// @return Interpolated value.
    float sample2D(float x, float y) const;

    /// @brief Sample a 3D LUT with trilinear interpolation.
    /// @param x First axis input.
    /// @param y Second axis input.
    /// @param z Third axis input.
    /// @return Interpolated value.
    float sample3D(float x, float y, float z) const;

    /// @brief Returns the number of dimensions (1, 2, or 3).
    int dimensions() const { return static_cast<int>(m_axes.size()); }

    /// @brief Returns true if a LUT is loaded and ready.
    bool isLoaded() const { return !m_data.empty(); }

    /// @brief Returns the axis metadata.
    const std::vector<LutAxis>& getAxes() const { return m_axes; }

private:
    /// Normalize an input value to [0, 1] within the axis range, then scale
    /// to continuous index in [0, size-1].
    float toIndex(float val, const LutAxis& axis) const;

    /// Fetch a value from the data array with bounds clamping.
    float fetch(int x, int y = 0, int z = 0) const;

    /// Linear interpolation.
    static float lerp(float a, float b, float t) { return a + t * (b - a); }

    std::vector<float> m_data;
    std::vector<LutAxis> m_axes;
};

} // namespace Vestige
