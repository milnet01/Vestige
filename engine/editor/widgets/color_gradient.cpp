// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file color_gradient.cpp
/// @brief ColorGradient implementation.
#include "editor/widgets/color_gradient.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Vestige
{

glm::vec4 ColorGradient::evaluate(float t) const
{
    if (stops.empty())
    {
        return glm::vec4(1.0f);
    }

    if (stops.size() == 1 || t <= stops.front().position)
    {
        return stops.front().color;
    }

    if (t >= stops.back().position)
    {
        return stops.back().color;
    }

    // Find the two stops that bracket t
    for (size_t i = 0; i + 1 < stops.size(); ++i)
    {
        const auto& a = stops[i];
        const auto& b = stops[i + 1];

        if (t >= a.position && t <= b.position)
        {
            float range = b.position - a.position;
            if (range < 0.0001f)
            {
                return a.color;
            }
            float factor = (t - a.position) / range;
            return glm::mix(a.color, b.color, factor);
        }
    }

    return stops.back().color;
}

void ColorGradient::addStop(float position, const glm::vec4& color)
{
    stops.push_back({position, color});
    sort();
}

void ColorGradient::removeStop(int index)
{
    if (index < 0 || index >= static_cast<int>(stops.size()))
    {
        return;
    }
    if (stops.size() <= 2)
    {
        return;
    }
    stops.erase(stops.begin() + index);
}

void ColorGradient::sort()
{
    std::sort(stops.begin(), stops.end(),
              [](const ColorStop& a, const ColorStop& b)
              {
                  return a.position < b.position;
              });
}

nlohmann::json ColorGradient::toJson() const
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& stop : stops)
    {
        j.push_back({
            {"pos", stop.position},
            {"r", stop.color.r},
            {"g", stop.color.g},
            {"b", stop.color.b},
            {"a", stop.color.a}
        });
    }
    return j;
}

ColorGradient ColorGradient::fromJson(const nlohmann::json& j)
{
    ColorGradient grad;
    grad.stops.clear();

    if (j.is_array())
    {
        for (const auto& item : j)
        {
            ColorStop stop;
            stop.position = item.value("pos", 0.0f);
            stop.color.r = item.value("r", 1.0f);
            stop.color.g = item.value("g", 1.0f);
            stop.color.b = item.value("b", 1.0f);
            stop.color.a = item.value("a", 1.0f);
            grad.stops.push_back(stop);
        }
    }

    if (grad.stops.size() < 2)
    {
        grad.stops = {
            {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
            {1.0f, {1.0f, 1.0f, 1.0f, 0.0f}}
        };
    }

    grad.sort();
    return grad;
}

} // namespace Vestige
