/// @file visual_test_runner.h
/// @brief Automated visual testing — drives the camera through predefined
///        viewpoints and captures screenshots + diagnostic reports.
#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class Camera;
class Renderer;

/// @brief Defines a camera viewpoint for visual testing.
struct TestViewpoint
{
    std::string name;               ///< Descriptive name (used in filenames)
    glm::vec3 position;             ///< Camera world position
    float yaw = -90.0f;             ///< Horizontal angle in degrees
    float pitch = 0.0f;             ///< Vertical angle in degrees
    int rotationSteps = 8;          ///< Number of yaw rotations (8 = full 360 at 45 each)
    float rotationAngle = 45.0f;    ///< Degrees per rotation step
};

/// @brief Automated visual test runner.
///
/// Drives the camera through a sequence of viewpoints, rotating at each one,
/// and captures screenshots with diagnostic reports for analysis.
///
/// Usage:
///   1. Add viewpoints with addViewpoint()
///   2. Call start() to begin the test sequence
///   3. Call update() every frame after rendering — it positions the camera,
///      waits for the image to settle, and captures screenshots
///   4. When update() returns true, all captures are complete
///
/// Output is organised in a timestamped run directory:
///   Testing/visual_tests/run_YYYYMMDD_HHMMSS/
///     01_viewpoint_name_rot000.png   (screenshot)
///     01_viewpoint_name_rot000.txt   (diagnostic report)
///     ...
///     summary.txt                    (index of all captures)
class VisualTestRunner
{
public:
    /// @brief Adds a viewpoint to the test sequence.
    void addViewpoint(const TestViewpoint& viewpoint);

    /// @brief Starts the visual test sequence.
    /// @param outputBaseDir Base directory for test output.
    void start(const std::string& outputBaseDir = "Testing/visual_tests");

    /// @brief Called each frame after the scene has been rendered and blitted.
    ///
    /// Manages the state machine: positioning camera, waiting for the render
    /// to settle (TAA convergence, shadow updates), then capturing.
    ///
    /// @return True when all captures are complete and the engine should exit.
    bool update(Camera& camera, const Renderer& renderer,
                int windowWidth, int windowHeight,
                int fps, float deltaTime);

    /// @brief Returns true if the test is actively running.
    bool isRunning() const;

    /// @brief Returns true if all viewpoints have been captured.
    bool isComplete() const;

    /// @brief Returns the number of viewpoints registered.
    size_t viewpointCount() const { return m_viewpoints.size(); }

private:
    enum class State
    {
        IDLE,               ///< Not started
        INITIAL_SETTLE,     ///< Waiting for scene to fully load and stabilise
        MOVE_CAMERA,        ///< Set camera to current viewpoint + rotation
        SETTLE,             ///< Waiting for render to stabilise after camera move
        DONE                ///< All captures complete
    };

    struct CaptureRecord
    {
        std::string filename;
        std::string viewpointName;
        glm::vec3 position;
        float yaw;
        float pitch;
    };

    void captureAndAdvance(Camera& camera, const Renderer& renderer,
                           int windowWidth, int windowHeight,
                           int fps, float deltaTime);
    void writeSummary();
    std::string createRunDirectory(const std::string& baseDir);

    std::vector<TestViewpoint> m_viewpoints;
    std::vector<CaptureRecord> m_captures;
    std::string m_outputDir;

    State m_state = State::IDLE;
    size_t m_currentViewpoint = 0;
    int m_currentRotation = 0;
    int m_settleCounter = 0;
    int m_captureIndex = 0;

    /// Frames to wait after scene load before first capture.
    static constexpr int INITIAL_SETTLE_FRAMES = 30;
    /// Frames to wait after each camera move for TAA / shadows to settle.
    static constexpr int SETTLE_FRAMES = 5;
};

} // namespace Vestige
