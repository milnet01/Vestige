// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file color_grading_test_helpers.h
/// @brief Shared neutral-LUT + CPU-lookup helpers for colour-grading tests.
///
/// /test-audit 2026-05-17 Ts19-D6: `generateNeutralLut` /
/// `makeIdentityLut` and `lutLookup` / `lutLookup_cpu` were byte-identical
/// across `test_color_grading.cpp` (CPU-only) and
/// `test_color_grading_parity.cpp` (GPU parity). Centralising lets the
/// LUT-layout convention (B-major, half-texel sampling) live in one
/// place so a future change to either side automatically updates both.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Vestige::Testing
{

/// @brief Build an identity / neutral 3D LUT in `GL_RGBA8` byte layout.
///
/// Voxel (r,g,b) holds (r/(N-1), g/(N-1), b/(N-1)) as 8-bit RGBA.
/// Layout is B-major: `idx = ((b * N + g) * N + r) * 4`. Both the GPU
/// upload path and the CPU oracle index against the same B-major layout.
inline std::vector<unsigned char> makeNeutralLutBytes(int size)
{
    std::vector<unsigned char> data(static_cast<size_t>(size * size * size) * 4);
    float maxIdx = static_cast<float>(size - 1);
    for (int b = 0; b < size; ++b)
    {
        for (int g = 0; g < size; ++g)
        {
            for (int r = 0; r < size; ++r)
            {
                size_t idx = static_cast<size_t>((b * size * size + g * size + r)) * 4;
                data[idx + 0] = static_cast<unsigned char>(static_cast<float>(r) / maxIdx * 255.0f + 0.5f);
                data[idx + 1] = static_cast<unsigned char>(static_cast<float>(g) / maxIdx * 255.0f + 0.5f);
                data[idx + 2] = static_cast<unsigned char>(static_cast<float>(b) / maxIdx * 255.0f + 0.5f);
                data[idx + 3] = 255;
            }
        }
    }
    return data;
}

/// @brief CPU nearest-neighbour LUT sample with the same half-texel
///        offset the shader applies. Used as the CPU oracle by the
///        GPU↔CPU parity tests, and as the lookup table check by the
///        CPU-only neutral-LUT tests.
inline glm::vec3 lutLookupNearest(const std::vector<unsigned char>& data,
                                  int size, const glm::vec3& color)
{
    glm::vec3 c = glm::clamp(color, 0.0f, 1.0f);
    float s = static_cast<float>(size);
    glm::vec3 coord = c * ((s - 1.0f) / s) + glm::vec3(0.5f / s);

    int r = std::clamp(static_cast<int>(coord.r * static_cast<float>(size - 1) + 0.5f), 0, size - 1);
    int g = std::clamp(static_cast<int>(coord.g * static_cast<float>(size - 1) + 0.5f), 0, size - 1);
    int b = std::clamp(static_cast<int>(coord.b * static_cast<float>(size - 1) + 0.5f), 0, size - 1);

    size_t idx = static_cast<size_t>((b * size * size + g * size + r)) * 4;
    return glm::vec3(
        static_cast<float>(data[idx + 0]) / 255.0f,
        static_cast<float>(data[idx + 1]) / 255.0f,
        static_cast<float>(data[idx + 2]) / 255.0f);
}

}  // namespace Vestige::Testing
