/// @file color_grading_lut.h
/// @brief 3D LUT management for color grading post-processing.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief A loaded LUT preset with its display name and GL texture.
struct LutPreset
{
    std::string name;
    GLuint textureId = 0;
    int size = 0;
};

/// @brief Manages 3D LUT textures for color grading post-processing.
class ColorGradingLut
{
public:
    ColorGradingLut();
    ~ColorGradingLut();

    ColorGradingLut(const ColorGradingLut&) = delete;
    ColorGradingLut& operator=(const ColorGradingLut&) = delete;

    ColorGradingLut(ColorGradingLut&& other) noexcept;
    ColorGradingLut& operator=(ColorGradingLut&& other) noexcept;

    /// @brief Generates the neutral LUT and built-in presets.
    void initialize();

    /// @brief Loads a .cube LUT file and adds it as a preset.
    bool loadCubeFile(const std::string& filePath, const std::string& name);

    /// @brief Binds the current LUT texture to the specified texture unit.
    void bind(unsigned int unit) const;

    /// @brief Cycles to the next LUT preset.
    void nextPreset();

    /// @brief Gets the name of the current preset.
    std::string getCurrentPresetName() const;

    /// @brief Gets the index of the current preset.
    int getCurrentPresetIndex() const;

    /// @brief Gets the total number of loaded presets.
    int getPresetCount() const;

    float getIntensity() const;
    void setIntensity(float intensity);

    bool isEnabled() const;
    void setEnabled(bool enabled);

private:
    GLuint createTexture3D(const std::vector<unsigned char>& data, int size);

    /// @brief Generates a LUT by applying a color transform to each entry.
    using ColorTransform = std::function<glm::vec3(const glm::vec3&)>;
    void addGeneratedPreset(const std::string& name, int size, const ColorTransform& transform);

    std::vector<LutPreset> m_presets;
    int m_currentPreset = 0;
    float m_intensity = 1.0f;
    bool m_enabled = false;
};

} // namespace Vestige
