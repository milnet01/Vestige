// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file frame_diagnostics.cpp
/// @brief Frame capture and diagnostic report implementation.
#include "renderer/frame_diagnostics.h"
#include "core/logger.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace Vestige
{

/// @brief Generates a timestamped filename base (no extension).
static std::string generateFilenameBase(const std::string& dir)
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << dir << "/vestige_diag_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << "_" << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

/// @brief Computes brightness statistics from RGB pixel data.
struct PixelStats
{
    float avgBrightness = 0.0f;
    float minBrightness = 1.0f;
    float maxBrightness = 0.0f;
    // 5-bucket histogram: [0-0.05], (0.05-0.2], (0.2-0.5], (0.5-0.8], (0.8-1.0]
    float histogramPct[5] = {};
    float percentBlack = 0.0f;    // < 0.01
    float percentWhite = 0.0f;    // > 0.99
};

static PixelStats computePixelStats(const std::vector<unsigned char>& pixels,
                                     int width, int height)
{
    PixelStats stats;
    int totalPixels = width * height;
    if (totalPixels == 0)
    {
        return stats;
    }

    double brightnessSum = 0.0;
    int histogram[5] = {};
    int blackCount = 0;
    int whiteCount = 0;

    for (int i = 0; i < totalPixels; i++)
    {
        int idx = i * 3;
        // BT.709 luminance from sRGB values
        float r = static_cast<float>(pixels[static_cast<size_t>(idx)]) / 255.0f;
        float g = static_cast<float>(pixels[static_cast<size_t>(idx + 1)]) / 255.0f;
        float b = static_cast<float>(pixels[static_cast<size_t>(idx + 2)]) / 255.0f;
        float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;

        brightnessSum += static_cast<double>(lum);
        if (lum < stats.minBrightness) stats.minBrightness = lum;
        if (lum > stats.maxBrightness) stats.maxBrightness = lum;

        if (lum < 0.01f) blackCount++;
        if (lum > 0.99f) whiteCount++;

        if (lum <= 0.05f)       histogram[0]++;
        else if (lum <= 0.20f)  histogram[1]++;
        else if (lum <= 0.50f)  histogram[2]++;
        else if (lum <= 0.80f)  histogram[3]++;
        else                    histogram[4]++;
    }

    stats.avgBrightness = static_cast<float>(brightnessSum / totalPixels);
    stats.percentBlack = 100.0f * static_cast<float>(blackCount) / static_cast<float>(totalPixels);
    stats.percentWhite = 100.0f * static_cast<float>(whiteCount) / static_cast<float>(totalPixels);

    for (int i = 0; i < 5; i++)
    {
        stats.histogramPct[i] = 100.0f * static_cast<float>(histogram[i])
                                / static_cast<float>(totalPixels);
    }

    return stats;
}

static const char* antiAliasModeStr(AntiAliasMode mode)
{
    switch (mode)
    {
        case AntiAliasMode::NONE:    return "None";
        case AntiAliasMode::MSAA_4X: return "MSAA 4x";
        case AntiAliasMode::TAA:     return "TAA";
        case AntiAliasMode::SMAA:    return "SMAA";
    }
    return "Unknown";
}

static const char* tonemapModeStr(int mode)
{
    switch (mode)
    {
        case 0: return "Reinhard";
        case 1: return "ACES Filmic";
        case 2: return "None (linear clamp)";
    }
    return "Unknown";
}

/// @brief Shared implementation — captures framebuffer and writes PNG + report.
static std::string captureImpl(const Renderer& renderer,
                                const Camera& camera,
                                int windowWidth, int windowHeight,
                                int fps, float deltaTime,
                                const std::string& basePath)
{
    std::string pngPath = basePath + ".png";
    std::string txtPath = basePath + ".txt";

    // --- Read back the framebuffer ---
    int w = windowWidth;
    int h = windowHeight;
    std::vector<unsigned char> pixels(static_cast<size_t>(w * h * 3));

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (OpenGL reads bottom-up, PNG expects top-down)
    int rowBytes = w * 3;
    std::vector<unsigned char> rowBuf(static_cast<size_t>(rowBytes));
    for (int y = 0; y < h / 2; y++)
    {
        unsigned char* top = pixels.data() + y * rowBytes;
        unsigned char* bot = pixels.data() + (h - 1 - y) * rowBytes;
        std::memcpy(rowBuf.data(), top, static_cast<size_t>(rowBytes));
        std::memcpy(top, bot, static_cast<size_t>(rowBytes));
        std::memcpy(bot, rowBuf.data(), static_cast<size_t>(rowBytes));
    }

    // Save PNG
    if (!stbi_write_png(pngPath.c_str(), w, h, 3, pixels.data(), rowBytes))
    {
        Logger::error("FrameDiagnostics: failed to write PNG: " + pngPath);
        return "";
    }

    // --- Compute pixel statistics ---
    PixelStats pixStats = computePixelStats(pixels, w, h);

    // --- Write text report ---
    const auto& culling = renderer.getCullingStats();
    glm::vec3 camPos = camera.getPosition();
    glm::vec3 camFront = camera.getFront();

    std::ofstream report(txtPath);
    if (!report.is_open())
    {
        Logger::error("FrameDiagnostics: failed to write report: " + txtPath);
        return pngPath;
    }

    report << "=== Vestige Frame Diagnostic Report ===\n\n";

    // Timing
    report << "[Performance]\n";
    report << "  FPS:        " << fps << "\n";
    report << "  Frame time: " << std::fixed << std::setprecision(2)
           << (deltaTime * 1000.0f) << " ms\n";
    report << "  Resolution: " << w << "x" << h << "\n\n";

    // Camera
    report << "[Camera]\n";
    report << "  Position:  (" << std::fixed << std::setprecision(2)
           << camPos.x << ", " << camPos.y << ", " << camPos.z << ")\n";
    report << "  Direction: (" << std::fixed << std::setprecision(3)
           << camFront.x << ", " << camFront.y << ", " << camFront.z << ")\n";
    report << "  FOV:       " << std::fixed << std::setprecision(1)
           << camera.getFov() << " deg\n\n";

    // Render state
    report << "[Render State]\n";
    report << "  Anti-aliasing:  " << antiAliasModeStr(renderer.getAntiAliasMode()) << "\n";
    report << "  Tonemapper:     " << tonemapModeStr(renderer.getTonemapMode()) << "\n";
    report << "  Exposure:       " << std::fixed << std::setprecision(2)
           << renderer.getExposure()
           << (renderer.isAutoExposure() ? " (auto)" : " (manual)") << "\n";
    report << "  Wireframe:      " << (renderer.isWireframeMode() ? "ON" : "OFF") << "\n";
    report << "  Bloom:          " << (renderer.isBloomEnabled() ? "ON" : "OFF");
    if (renderer.isBloomEnabled())
    {
        report << " (threshold=" << std::setprecision(2) << renderer.getBloomThreshold()
               << ", intensity=" << renderer.getBloomIntensity() << ")";
    }
    report << "\n";
    report << "  SSAO:           " << (renderer.isSsaoEnabled() ? "ON" : "OFF") << "\n";
    report << "  POM:            " << (renderer.isPomEnabled() ? "ON" : "OFF");
    if (renderer.isPomEnabled())
    {
        report << " (height=" << std::setprecision(2) << renderer.getPomHeightMultiplier() << ")";
    }
    report << "\n";
    report << "  Color grading:  " << (renderer.isColorGradingEnabled()
        ? renderer.getColorGradingPresetName() : "OFF") << "\n";
    report << "  Cascade debug:  " << (renderer.isCascadeDebug() ? "ON" : "OFF") << "\n";
    report << "  HDR debug:      " << (renderer.getDebugMode() == 0 ? "OFF"
                                       : "false-color luminance") << "\n\n";

    // Lighting
    report << "[Lighting]\n";
    report << "  Point lights: " << renderer.getPointLightCount() << "\n";
    report << "  Spot lights:  " << renderer.getSpotLightCount() << "\n\n";

    // Culling & draw calls
    report << "[Rendering]\n";
    report << "  Draw calls:  " << culling.drawCalls
           << " (" << culling.instanceBatches << " instanced)\n";
    report << "  Opaque:      " << culling.culledItems << " / " << culling.totalItems
           << " visible (culled " << (culling.totalItems - culling.culledItems) << ")\n";
    report << "  Transparent: " << culling.transparentCulled << " / " << culling.transparentTotal
           << " visible\n";
    report << "  Shadow casters: " << culling.shadowCastersCulled
           << " / " << culling.shadowCastersTotal << " per cascade (avg)\n\n";

    // Pixel analysis
    report << "[Pixel Analysis] (post-tonemapped sRGB)\n";
    report << "  Avg brightness: " << std::fixed << std::setprecision(3)
           << pixStats.avgBrightness << "\n";
    report << "  Min brightness: " << pixStats.minBrightness << "\n";
    report << "  Max brightness: " << pixStats.maxBrightness << "\n";
    report << "  Black pixels (<1%):  " << std::setprecision(1)
           << pixStats.percentBlack << "%\n";
    report << "  White pixels (>99%): " << pixStats.percentWhite << "%\n";
    report << "  Histogram:\n";
    report << "    [0.00-0.05] very dark:  " << std::setprecision(1)
           << pixStats.histogramPct[0] << "%\n";
    report << "    [0.05-0.20] dark:       " << pixStats.histogramPct[1] << "%\n";
    report << "    [0.20-0.50] mid:        " << pixStats.histogramPct[2] << "%\n";
    report << "    [0.50-0.80] bright:     " << pixStats.histogramPct[3] << "%\n";
    report << "    [0.80-1.00] very bright:" << pixStats.histogramPct[4] << "%\n\n";

    report << "Screenshot: " << pngPath << "\n";
    report << "=== End Report ===\n";

    report.close();

    Logger::info("Frame diagnostic saved: " + pngPath);
    Logger::info("Frame report saved: " + txtPath);

    return pngPath;
}

std::string FrameDiagnostics::capture(const Renderer& renderer,
                                       const Camera& camera,
                                       int windowWidth, int windowHeight,
                                       int fps, float deltaTime,
                                       const std::string& outputDir)
{
    // Determine output directory
    std::string dir = outputDir;
    if (dir.empty())
    {
        const char* home = std::getenv("HOME");
        if (home)
        {
            dir = std::string(home) + "/Pictures/Screenshots";
        }
        else
        {
            dir = "/tmp";
        }
    }

    std::string basePath = generateFilenameBase(dir);
    return captureImpl(renderer, camera, windowWidth, windowHeight,
                       fps, deltaTime, basePath);
}

std::string FrameDiagnostics::captureNamed(const Renderer& renderer,
                                            const Camera& camera,
                                            int windowWidth, int windowHeight,
                                            int fps, float deltaTime,
                                            const std::string& outputDir,
                                            const std::string& basename)
{
    std::string basePath = outputDir + "/" + basename;
    return captureImpl(renderer, camera, windowWidth, windowHeight,
                       fps, deltaTime, basePath);
}

} // namespace Vestige
