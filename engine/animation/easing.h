/// @file easing.h
/// @brief Easing functions for animation (Penner equations + cubic bezier).
#pragma once

#include <cstdint>

namespace Vestige
{

/// @brief Standard easing function types (Robert Penner equations).
enum class EaseType : uint8_t
{
    LINEAR,
    STEP,

    EASE_IN_QUAD,    EASE_OUT_QUAD,    EASE_IN_OUT_QUAD,
    EASE_IN_CUBIC,   EASE_OUT_CUBIC,   EASE_IN_OUT_CUBIC,
    EASE_IN_QUART,   EASE_OUT_QUART,   EASE_IN_OUT_QUART,
    EASE_IN_QUINT,   EASE_OUT_QUINT,   EASE_IN_OUT_QUINT,
    EASE_IN_SINE,    EASE_OUT_SINE,    EASE_IN_OUT_SINE,
    EASE_IN_EXPO,    EASE_OUT_EXPO,    EASE_IN_OUT_EXPO,
    EASE_IN_CIRC,    EASE_OUT_CIRC,    EASE_IN_OUT_CIRC,
    EASE_IN_ELASTIC, EASE_OUT_ELASTIC, EASE_IN_OUT_ELASTIC,
    EASE_IN_BACK,    EASE_OUT_BACK,    EASE_IN_OUT_BACK,
    EASE_IN_BOUNCE,  EASE_OUT_BOUNCE,  EASE_IN_OUT_BOUNCE,

    COUNT
};

/// @brief Evaluates an easing function at normalized time t [0,1].
/// @param type The easing function to use.
/// @param t Normalized time in [0,1].
/// @return Eased progress (usually [0,1], may overshoot for Elastic/Back).
float evaluateEasing(EaseType type, float t);

/// @brief Returns the display name for an EaseType.
const char* easeTypeName(EaseType type);

/// @brief Custom cubic bezier easing curve (CSS-style control points).
///
/// Defined by two control points (x1,y1) and (x2,y2), with endpoints
/// fixed at (0,0) and (1,1). Uses Newton-Raphson to solve x(t) = input.
class CubicBezierEase
{
public:
    /// @brief Constructs a cubic bezier easing curve.
    /// @param x1 X of first control point [0,1].
    /// @param y1 Y of first control point.
    /// @param x2 X of second control point [0,1].
    /// @param y2 Y of second control point.
    CubicBezierEase(float x1, float y1, float x2, float y2);

    /// @brief Evaluates the curve at normalized time t [0,1].
    float evaluate(float t) const;

private:
    float sampleX(float t) const;
    float sampleY(float t) const;
    float sampleDerivativeX(float t) const;
    float solveForT(float x) const;

    float m_cx, m_bx, m_ax;
    float m_cy, m_by, m_ay;
};

} // namespace Vestige
