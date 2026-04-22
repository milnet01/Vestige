// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_apply.h
/// @brief Apply layer between `Settings` (the persisted struct) and the
///        engine subsystems that carry user-tunable state.
///
/// Slice 13.2 introduces the **display** apply path: pushing a
/// `DisplaySettings` block at a `Window` (or anything shaped like a
/// `Window`). Later slices add audio, accessibility, input bindings,
/// and the full `Settings::applyToEngine` orchestrator that calls
/// these in the right order.
///
/// Why a sink interface instead of a direct `Window&` forwarder?
/// Tests can't construct a real `Window` without a display — GLFW
/// initialisation + GL context creation require a connected screen.
/// The `DisplayApplySink` abstract base lets `test_settings.cpp`
/// supply a recording mock and verify that `applyDisplay` invokes
/// the sink with exactly the settings values. The real engine
/// bootstrap path uses `WindowDisplaySink` which forwards to a
/// live `Window`.
#pragma once

namespace Vestige
{

struct DisplaySettings;   // core/settings.h
class Window;             // core/window.h

/// @brief Minimal interface for anything that can accept a video-mode
///        change from the Settings apply path. Implemented by
///        `WindowDisplaySink` in production; mocked by tests.
class DisplayApplySink
{
public:
    virtual ~DisplayApplySink() = default;

    /// @brief Apply the requested video mode. Implementations are
    ///        expected to normalise and propagate the change (fire a
    ///        framebuffer-resize event, call the GLFW APIs, etc.).
    virtual void setVideoMode(int width, int height,
                              bool fullscreen, bool vsync) = 0;
};

/// @brief Concrete sink that forwards to a `Window`. Thin wrapper —
///        the real work lives in `Window::setVideoMode`.
class WindowDisplaySink final : public DisplayApplySink
{
public:
    explicit WindowDisplaySink(Window& window);

    void setVideoMode(int width, int height,
                      bool fullscreen, bool vsync) override;

private:
    Window& m_window;
};

/// @brief Pushes the display block of a `Settings` onto a sink.
///
/// @param display Resolution / vsync / fullscreen to apply.
/// @param sink    Destination — usually `WindowDisplaySink`, occasionally
///                a test mock.
///
/// @note Intentionally does **not** touch the quality preset or render
///       scale — those feed the renderer and are wired in later slices
///       (render scale is a Renderer concern; quality preset governs
///       shader variants / LOD bias / shadow resolution and is consumed
///       by individual subsystems).
void applyDisplay(const DisplaySettings& display, DisplayApplySink& sink);

} // namespace Vestige
