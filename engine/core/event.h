/// @file event.h
/// @brief Event base class and common engine events.
#pragma once

namespace Vestige
{

/// @brief Base class for all engine events.
struct Event
{
    virtual ~Event() = default;
};

/// @brief Fired when the window is resized.
struct WindowResizeEvent : public Event
{
    int width;
    int height;

    WindowResizeEvent(int w, int h) : width(w), height(h) {}
};

/// @brief Fired when the window close button is pressed.
struct WindowCloseEvent : public Event
{
};

/// @brief Fired when a keyboard key is pressed.
struct KeyPressedEvent : public Event
{
    int keyCode;
    bool isRepeat;

    KeyPressedEvent(int key, bool repeat) : keyCode(key), isRepeat(repeat) {}
};

/// @brief Fired when a keyboard key is released.
struct KeyReleasedEvent : public Event
{
    int keyCode;

    explicit KeyReleasedEvent(int key) : keyCode(key) {}
};

/// @brief Fired when the mouse moves.
struct MouseMovedEvent : public Event
{
    double xPosition;
    double yPosition;

    MouseMovedEvent(double x, double y) : xPosition(x), yPosition(y) {}
};

/// @brief Fired when a mouse button is pressed.
struct MouseButtonPressedEvent : public Event
{
    int button;

    explicit MouseButtonPressedEvent(int btn) : button(btn) {}
};

/// @brief Fired when a mouse button is released.
struct MouseButtonReleasedEvent : public Event
{
    int button;

    explicit MouseButtonReleasedEvent(int btn) : button(btn) {}
};

/// @brief Fired when the mouse scroll wheel is used.
struct MouseScrollEvent : public Event
{
    double xOffset;
    double yOffset;

    MouseScrollEvent(double x, double y) : xOffset(x), yOffset(y) {}
};

} // namespace Vestige
