/// @file color_grading_lut.cpp
/// @brief 3D LUT management for color grading post-processing.
#include "renderer/color_grading_lut.h"
#include "utils/cube_loader.h"
#include "core/logger.h"

#include <algorithm>

namespace Vestige
{

static const int LUT_SIZE = 32;

ColorGradingLut::ColorGradingLut() = default;

ColorGradingLut::~ColorGradingLut()
{
    for (auto& preset : m_presets)
    {
        if (preset.textureId != 0)
        {
            glDeleteTextures(1, &preset.textureId);
        }
    }
}

void ColorGradingLut::initialize()
{
    // Preset 0: Neutral (identity)
    addGeneratedPreset("Neutral", LUT_SIZE, [](const glm::vec3& c)
    {
        return c;
    });

    // Preset 1: Warm Golden (temple interiors)
    addGeneratedPreset("Warm Golden", LUT_SIZE, [](const glm::vec3& c)
    {
        glm::vec3 out;
        out.r = std::clamp(c.r * 1.05f + 0.02f, 0.0f, 1.0f);
        out.g = c.g;
        out.b = std::clamp(c.b * 0.85f - 0.02f, 0.0f, 1.0f);
        // Gentle contrast S-curve
        out = out * out * (3.0f - 2.0f * out);
        return out;
    });

    // Preset 2: Cool Blue (night/exterior)
    addGeneratedPreset("Cool Blue", LUT_SIZE, [](const glm::vec3& c)
    {
        glm::vec3 out;
        out.r = std::clamp(c.r * 0.85f, 0.0f, 1.0f);
        out.g = std::clamp(c.g * 0.95f + 0.02f, 0.0f, 1.0f);
        out.b = std::clamp(c.b * 1.1f + 0.03f, 0.0f, 1.0f);
        // Slight shadow lift
        out += glm::vec3(0.02f);
        out = glm::clamp(out, 0.0f, 1.0f);
        return out;
    });

    // Preset 3: High Contrast (S-curve)
    addGeneratedPreset("High Contrast", LUT_SIZE, [](const glm::vec3& c)
    {
        // Hermite smoothstep S-curve per channel
        glm::vec3 out = c * c * (3.0f - 2.0f * c);
        return out;
    });

    // Preset 4: Desaturated (vintage/muted)
    addGeneratedPreset("Desaturated", LUT_SIZE, [](const glm::vec3& c)
    {
        float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        glm::vec3 out = glm::mix(glm::vec3(lum), c, 0.5f);
        return out;
    });

    Logger::info("Color grading: " + std::to_string(m_presets.size()) + " LUT presets loaded");
}

bool ColorGradingLut::loadCubeFile(const std::string& filePath, const std::string& name)
{
    CubeData data = CubeLoader::load(filePath);
    if (data.size == 0)
    {
        return false;
    }

    GLuint texId = createTexture3D(data.rgbaData, data.size);
    if (texId == 0)
    {
        return false;
    }

    LutPreset preset;
    preset.name = name.empty() ? (data.title.empty() ? filePath : data.title) : name;
    preset.textureId = texId;
    preset.size = data.size;
    m_presets.push_back(preset);

    Logger::info("Color grading: loaded external LUT '" + preset.name + "'");
    return true;
}

void ColorGradingLut::bind(unsigned int unit) const
{
    if (m_presets.empty())
    {
        return;
    }

    glBindTextureUnit(unit, m_presets[static_cast<size_t>(m_currentPreset)].textureId);
}

void ColorGradingLut::nextPreset()
{
    if (m_presets.empty())
    {
        return;
    }
    m_currentPreset = (m_currentPreset + 1) % static_cast<int>(m_presets.size());
}

std::string ColorGradingLut::getCurrentPresetName() const
{
    if (m_presets.empty())
    {
        return "None";
    }
    return m_presets[static_cast<size_t>(m_currentPreset)].name;
}

int ColorGradingLut::getCurrentPresetIndex() const
{
    return m_currentPreset;
}

int ColorGradingLut::getPresetCount() const
{
    return static_cast<int>(m_presets.size());
}

float ColorGradingLut::getIntensity() const
{
    return m_intensity;
}

void ColorGradingLut::setIntensity(float intensity)
{
    m_intensity = std::clamp(intensity, 0.0f, 1.0f);
}

bool ColorGradingLut::isEnabled() const
{
    return m_enabled;
}

void ColorGradingLut::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

GLuint ColorGradingLut::createTexture3D(const std::vector<unsigned char>& data, int size)
{
    GLuint texId = 0;
    glCreateTextures(GL_TEXTURE_3D, 1, &texId);

    glTextureStorage3D(texId, 1, GL_RGBA8, size, size, size);
    glTextureSubImage3D(texId, 0, 0, 0, 0, size, size, size,
                        GL_RGBA, GL_UNSIGNED_BYTE, data.data());

    glTextureParameteri(texId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texId, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return texId;
}

void ColorGradingLut::addGeneratedPreset(const std::string& name, int size,
                                          const ColorTransform& transform)
{
    std::vector<unsigned char> data(static_cast<size_t>(size * size * size) * 4);
    float maxIdx = static_cast<float>(size - 1);

    for (int b = 0; b < size; b++)
    {
        for (int g = 0; g < size; g++)
        {
            for (int r = 0; r < size; r++)
            {
                glm::vec3 input(
                    static_cast<float>(r) / maxIdx,
                    static_cast<float>(g) / maxIdx,
                    static_cast<float>(b) / maxIdx);

                glm::vec3 output = transform(input);
                output = glm::clamp(output, 0.0f, 1.0f);

                size_t idx = static_cast<size_t>((b * size * size + g * size + r)) * 4;
                data[idx + 0] = static_cast<unsigned char>(output.r * 255.0f + 0.5f);
                data[idx + 1] = static_cast<unsigned char>(output.g * 255.0f + 0.5f);
                data[idx + 2] = static_cast<unsigned char>(output.b * 255.0f + 0.5f);
                data[idx + 3] = 255;
            }
        }
    }

    GLuint texId = createTexture3D(data, size);

    LutPreset preset;
    preset.name = name;
    preset.textureId = texId;
    preset.size = size;
    m_presets.push_back(preset);
}

} // namespace Vestige
