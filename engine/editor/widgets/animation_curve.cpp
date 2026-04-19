// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file animation_curve.cpp
/// @brief AnimationCurve implementation.
#include "editor/widgets/animation_curve.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Vestige
{

float AnimationCurve::evaluate(float t) const
{
    if (keyframes.empty())
    {
        return 0.0f;
    }

    if (keyframes.size() == 1 || t <= keyframes.front().time)
    {
        return keyframes.front().value;
    }

    if (t >= keyframes.back().time)
    {
        return keyframes.back().value;
    }

    // Find the two keyframes that bracket t
    for (size_t i = 0; i + 1 < keyframes.size(); ++i)
    {
        const auto& a = keyframes[i];
        const auto& b = keyframes[i + 1];

        if (t >= a.time && t <= b.time)
        {
            float range = b.time - a.time;
            if (range < 0.0001f)
            {
                return a.value;
            }
            float factor = (t - a.time) / range;
            return a.value + (b.value - a.value) * factor;
        }
    }

    return keyframes.back().value;
}

void AnimationCurve::addKeyframe(float time, float value)
{
    keyframes.push_back({time, value});
    sort();
}

void AnimationCurve::removeKeyframe(int index)
{
    if (index < 0 || index >= static_cast<int>(keyframes.size()))
    {
        return;
    }
    // Keep at least 2 keyframes
    if (keyframes.size() <= 2)
    {
        return;
    }
    keyframes.erase(keyframes.begin() + index);
}

void AnimationCurve::sort()
{
    std::sort(keyframes.begin(), keyframes.end(),
              [](const Keyframe& a, const Keyframe& b)
              {
                  return a.time < b.time;
              });
}

nlohmann::json AnimationCurve::toJson() const
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& kf : keyframes)
    {
        j.push_back({{"t", kf.time}, {"v", kf.value}});
    }
    return j;
}

AnimationCurve AnimationCurve::fromJson(const nlohmann::json& j)
{
    AnimationCurve curve;
    curve.keyframes.clear();

    // AUDIT M26: unbounded push_back in the previous revision meant a
    // malicious .scene JSON with a 10M-element curve array would allocate
    // gigabytes here. Real curves carry tens of keyframes at most — cap
    // comfortably above legitimate use.
    constexpr size_t MAX_KEYFRAMES = 65536;

    if (j.is_array())
    {
        for (const auto& item : j)
        {
            if (curve.keyframes.size() >= MAX_KEYFRAMES)
            {
                break;
            }
            Keyframe kf;
            kf.time = item.value("t", 0.0f);
            kf.value = item.value("v", 0.0f);
            curve.keyframes.push_back(kf);
        }
    }

    if (curve.keyframes.size() < 2)
    {
        curve.keyframes = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    }

    curve.sort();
    return curve;
}

} // namespace Vestige
