/// @file visual_test_runner.cpp
/// @brief Automated visual test runner implementation.
#include "testing/visual_test_runner.h"
#include "renderer/frame_diagnostics.h"
#include "renderer/camera.h"
#include "core/logger.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace Vestige
{

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void VisualTestRunner::addViewpoint(const TestViewpoint& viewpoint)
{
    m_viewpoints.push_back(viewpoint);
}

void VisualTestRunner::start(const std::string& outputBaseDir)
{
    if (m_viewpoints.empty())
    {
        Logger::warning("VisualTestRunner: no viewpoints defined — nothing to test");
        m_state = State::DONE;
        return;
    }

    m_outputDir = createRunDirectory(outputBaseDir);
    m_currentViewpoint = 0;
    m_currentRotation = 0;
    m_captureIndex = 0;
    m_settleCounter = INITIAL_SETTLE_FRAMES;
    m_state = State::INITIAL_SETTLE;
    m_captures.clear();

    // Count total captures for the log message
    int total = 0;
    for (const auto& vp : m_viewpoints)
    {
        total += vp.rotationSteps;
    }

    Logger::info("Visual test started: " + std::to_string(m_viewpoints.size())
                 + " viewpoints, " + std::to_string(total)
                 + " captures, output: " + m_outputDir);
}

bool VisualTestRunner::update(Camera& camera, const Renderer& renderer,
                               int windowWidth, int windowHeight,
                               int fps, float deltaTime)
{
    switch (m_state)
    {
        case State::IDLE:
            return false;

        case State::DONE:
            return true;

        case State::INITIAL_SETTLE:
            m_settleCounter--;
            if (m_settleCounter <= 0)
            {
                Logger::info("Visual test: initial settle complete, starting captures");
                m_state = State::MOVE_CAMERA;
            }
            return false;

        case State::MOVE_CAMERA:
        {
            const auto& vp = m_viewpoints[m_currentViewpoint];
            float yaw = vp.yaw + static_cast<float>(m_currentRotation) * vp.rotationAngle;

            camera.setPosition(vp.position);
            camera.setYaw(yaw);
            camera.setPitch(vp.pitch);

            m_settleCounter = SETTLE_FRAMES;
            m_state = State::SETTLE;
            return false;
        }

        case State::SETTLE:
            m_settleCounter--;
            if (m_settleCounter <= 0)
            {
                captureAndAdvance(camera, renderer,
                                  windowWidth, windowHeight, fps, deltaTime);
            }
            return false;
    }

    return false;
}

bool VisualTestRunner::isRunning() const
{
    return m_state != State::IDLE && m_state != State::DONE;
}

bool VisualTestRunner::isComplete() const
{
    return m_state == State::DONE;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void VisualTestRunner::captureAndAdvance(Camera& camera, const Renderer& renderer,
                                          int windowWidth, int windowHeight,
                                          int fps, float deltaTime)
{
    const auto& vp = m_viewpoints[m_currentViewpoint];
    int rotDeg = m_currentRotation * static_cast<int>(vp.rotationAngle);

    // Build filename:  01_gate_exterior_rot045
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << (m_currentViewpoint + 1)
        << "_" << vp.name
        << "_rot" << std::setw(3) << rotDeg;
    std::string basename = oss.str();

    std::string result = FrameDiagnostics::captureNamed(
        renderer, camera, windowWidth, windowHeight,
        fps, deltaTime, m_outputDir, basename);

    if (!result.empty())
    {
        float yaw = vp.yaw + static_cast<float>(m_currentRotation) * vp.rotationAngle;
        m_captures.push_back({basename, vp.name, vp.position, yaw, vp.pitch});
        Logger::info("Visual test [" + std::to_string(m_captureIndex + 1)
                     + "]: " + basename);
    }

    m_captureIndex++;
    m_currentRotation++;

    // More rotations for this viewpoint?
    if (m_currentRotation >= vp.rotationSteps)
    {
        m_currentRotation = 0;
        m_currentViewpoint++;

        // More viewpoints?
        if (m_currentViewpoint >= m_viewpoints.size())
        {
            writeSummary();
            m_state = State::DONE;
            Logger::info("Visual test complete: " + std::to_string(m_captureIndex)
                         + " captures saved to " + m_outputDir);
            return;
        }
    }

    // Next capture
    m_state = State::MOVE_CAMERA;
}

void VisualTestRunner::writeSummary()
{
    std::string path = m_outputDir + "/summary.txt";
    std::ofstream out(path);
    if (!out.is_open())
    {
        Logger::error("VisualTestRunner: failed to write summary: " + path);
        return;
    }

    out << "=== Vestige Visual Test Summary ===\n\n";
    out << "Total captures: " << m_captures.size() << "\n";
    out << "Viewpoints:     " << m_viewpoints.size() << "\n";
    out << "Output:         " << m_outputDir << "\n\n";

    out << "--- Captures ---\n\n";
    for (const auto& cap : m_captures)
    {
        out << cap.filename << ".png\n"
            << "  Viewpoint: " << cap.viewpointName << "\n"
            << "  Position:  (" << std::fixed << std::setprecision(2)
            << cap.position.x << ", " << cap.position.y << ", "
            << cap.position.z << ")\n"
            << "  Yaw: " << std::setprecision(1) << cap.yaw
            << " deg  Pitch: " << cap.pitch << " deg\n\n";
    }

    out << "=== End Summary ===\n";
    out.close();

    Logger::info("Visual test summary written: " + path);
}

std::string VisualTestRunner::createRunDirectory(const std::string& baseDir)
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << baseDir << "/run_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
    std::string dir = oss.str();

    fs::create_directories(dir);
    return dir;
}

} // namespace Vestige
