// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file easing.cpp
/// @brief Easing function implementations (Robert Penner equations + cubic bezier).
#include "animation/easing.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Individual easing functions (Penner equations)
// All take t in [0,1] and return progress (usually [0,1]).
// ---------------------------------------------------------------------------

static constexpr float PI = 3.14159265358979323846f;

static float linear(float t) { return t; }
static float step(float t) { return (t < 1.0f) ? 0.0f : 1.0f; }

// Quadratic
static float easeInQuad(float t) { return t * t; }
static float easeOutQuad(float t) { return t * (2.0f - t); }
static float easeInOutQuad(float t)
{
    if (t < 0.5f) return 2.0f * t * t;
    return -1.0f + (4.0f - 2.0f * t) * t;
}

// Cubic
static float easeInCubic(float t) { return t * t * t; }
static float easeOutCubic(float t) { float f = t - 1.0f; return f * f * f + 1.0f; }
static float easeInOutCubic(float t)
{
    if (t < 0.5f) return 4.0f * t * t * t;
    float f = 2.0f * t - 2.0f;
    return 0.5f * f * f * f + 1.0f;
}

// Quartic
static float easeInQuart(float t) { return t * t * t * t; }
static float easeOutQuart(float t) { float f = t - 1.0f; return 1.0f - f * f * f * f; }
static float easeInOutQuart(float t)
{
    if (t < 0.5f) return 8.0f * t * t * t * t;
    float f = t - 1.0f;
    return 1.0f - 8.0f * f * f * f * f;
}

// Quintic
static float easeInQuint(float t) { return t * t * t * t * t; }
static float easeOutQuint(float t) { float f = t - 1.0f; return 1.0f + f * f * f * f * f; }
static float easeInOutQuint(float t)
{
    if (t < 0.5f) return 16.0f * t * t * t * t * t;
    float f = 2.0f * t - 2.0f;
    return 0.5f * f * f * f * f * f + 1.0f;
}

// Sine
static float easeInSine(float t) { return 1.0f - std::cos(t * PI * 0.5f); }
static float easeOutSine(float t) { return std::sin(t * PI * 0.5f); }
static float easeInOutSine(float t) { return 0.5f * (1.0f - std::cos(PI * t)); }

// Exponential
static float easeInExpo(float t)
{
    return (t == 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
}
static float easeOutExpo(float t)
{
    return (t == 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
static float easeInOutExpo(float t)
{
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    if (t < 0.5f) return 0.5f * std::pow(2.0f, 20.0f * t - 10.0f);
    return 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
}

// Circular
static float easeInCirc(float t) { return 1.0f - std::sqrt(1.0f - t * t); }
static float easeOutCirc(float t) { float f = t - 1.0f; return std::sqrt(1.0f - f * f); }
static float easeInOutCirc(float t)
{
    if (t < 0.5f) return 0.5f * (1.0f - std::sqrt(1.0f - 4.0f * t * t));
    float f = 2.0f * t - 2.0f;
    return 0.5f * (std::sqrt(1.0f - f * f) + 1.0f);
}

// Elastic
static float easeInElastic(float t)
{
    if (t == 0.0f || t == 1.0f) return t;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * (2.0f * PI / 3.0f));
}
static float easeOutElastic(float t)
{
    if (t == 0.0f || t == 1.0f) return t;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * PI / 3.0f)) + 1.0f;
}
static float easeInOutElastic(float t)
{
    if (t == 0.0f || t == 1.0f) return t;
    if (t < 0.5f)
        return -0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
               * std::sin((20.0f * t - 11.125f) * (2.0f * PI / 4.5f));
    return 0.5f * std::pow(2.0f, -20.0f * t + 10.0f)
           * std::sin((20.0f * t - 11.125f) * (2.0f * PI / 4.5f)) + 1.0f;
}

// Back (overshoot)
static constexpr float BACK_C1 = 1.70158f;
static constexpr float BACK_C2 = BACK_C1 * 1.525f;
static constexpr float BACK_C3 = BACK_C1 + 1.0f;

static float easeInBack(float t) { return BACK_C3 * t * t * t - BACK_C1 * t * t; }
static float easeOutBack(float t)
{
    float f = t - 1.0f;
    return 1.0f + BACK_C3 * f * f * f + BACK_C1 * f * f;
}
static float easeInOutBack(float t)
{
    if (t < 0.5f)
        return (std::pow(2.0f * t, 2.0f) * ((BACK_C2 + 1.0f) * 2.0f * t - BACK_C2)) * 0.5f;
    float f = 2.0f * t - 2.0f;
    return (f * f * ((BACK_C2 + 1.0f) * f + BACK_C2) + 2.0f) * 0.5f;
}

// Bounce
static float easeOutBounce(float t)
{
    if (t < 1.0f / 2.75f)
    {
        return 7.5625f * t * t;
    }
    else if (t < 2.0f / 2.75f)
    {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + 0.75f;
    }
    else if (t < 2.5f / 2.75f)
    {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + 0.9375f;
    }
    else
    {
        t -= 2.625f / 2.75f;
        return 7.5625f * t * t + 0.984375f;
    }
}
static float easeInBounce(float t) { return 1.0f - easeOutBounce(1.0f - t); }
static float easeInOutBounce(float t)
{
    if (t < 0.5f) return 0.5f * easeInBounce(2.0f * t);
    return 0.5f * easeOutBounce(2.0f * t - 1.0f) + 0.5f;
}

// ---------------------------------------------------------------------------
// Lookup table
// ---------------------------------------------------------------------------

using EasingFunc = float (*)(float);

static const EasingFunc s_easingTable[static_cast<int>(EaseType::COUNT)] =
{
    linear,
    step,
    easeInQuad,    easeOutQuad,    easeInOutQuad,
    easeInCubic,   easeOutCubic,   easeInOutCubic,
    easeInQuart,   easeOutQuart,   easeInOutQuart,
    easeInQuint,   easeOutQuint,   easeInOutQuint,
    easeInSine,    easeOutSine,    easeInOutSine,
    easeInExpo,    easeOutExpo,    easeInOutExpo,
    easeInCirc,    easeOutCirc,    easeInOutCirc,
    easeInElastic, easeOutElastic, easeInOutElastic,
    easeInBack,    easeOutBack,    easeInOutBack,
    easeInBounce,  easeOutBounce,  easeInOutBounce,
};

float evaluateEasing(EaseType type, float t)
{
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(EaseType::COUNT))
    {
        return t;
    }
    return s_easingTable[idx](t);
}

// ---------------------------------------------------------------------------
// Name table
// ---------------------------------------------------------------------------

static const char* s_easeNames[static_cast<int>(EaseType::COUNT)] =
{
    "Linear",
    "Step",
    "EaseInQuad",    "EaseOutQuad",    "EaseInOutQuad",
    "EaseInCubic",   "EaseOutCubic",   "EaseInOutCubic",
    "EaseInQuart",   "EaseOutQuart",   "EaseInOutQuart",
    "EaseInQuint",   "EaseOutQuint",   "EaseInOutQuint",
    "EaseInSine",    "EaseOutSine",    "EaseInOutSine",
    "EaseInExpo",    "EaseOutExpo",    "EaseInOutExpo",
    "EaseInCirc",    "EaseOutCirc",    "EaseInOutCirc",
    "EaseInElastic", "EaseOutElastic", "EaseInOutElastic",
    "EaseInBack",    "EaseOutBack",    "EaseInOutBack",
    "EaseInBounce",  "EaseOutBounce",  "EaseInOutBounce",
};

const char* easeTypeName(EaseType type)
{
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= static_cast<int>(EaseType::COUNT))
    {
        return "Unknown";
    }
    return s_easeNames[idx];
}

// ---------------------------------------------------------------------------
// CubicBezierEase
// ---------------------------------------------------------------------------

CubicBezierEase::CubicBezierEase(float x1, float y1, float x2, float y2)
{
    // Polynomial coefficients for cubic bezier with P0=(0,0), P3=(1,1)
    m_cx = 3.0f * x1;
    m_bx = 3.0f * (x2 - x1) - m_cx;
    m_ax = 1.0f - m_cx - m_bx;

    m_cy = 3.0f * y1;
    m_by = 3.0f * (y2 - y1) - m_cy;
    m_ay = 1.0f - m_cy - m_by;
}

float CubicBezierEase::sampleX(float t) const
{
    return ((m_ax * t + m_bx) * t + m_cx) * t;
}

float CubicBezierEase::sampleY(float t) const
{
    return ((m_ay * t + m_by) * t + m_cy) * t;
}

float CubicBezierEase::sampleDerivativeX(float t) const
{
    return (3.0f * m_ax * t + 2.0f * m_bx) * t + m_cx;
}

float CubicBezierEase::solveForT(float x) const
{
    // Newton-Raphson iteration to find t where sampleX(t) = x
    float t = x;  // Initial guess
    for (int i = 0; i < 8; ++i)
    {
        float error = sampleX(t) - x;
        if (std::abs(error) < 1e-6f) break;
        float dx = sampleDerivativeX(t);
        if (std::abs(dx) < 1e-6f) break;
        t -= error / dx;
    }
    return std::clamp(t, 0.0f, 1.0f);
}

float CubicBezierEase::evaluate(float t) const
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return sampleY(solveForT(t));
}

} // namespace Vestige
