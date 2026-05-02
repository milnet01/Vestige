// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#pragma once

/// @file shader_parity_helpers.h
/// @brief Single-pixel shader execution + readback for CPU/GPU parity tests.
///
/// Each parity test follows the same shape:
///   1. Build a fragment shader whose `main` writes ONE expression's
///      result to `outColor`.
///   2. Set scalar/vec uniforms describing the inputs.
///   3. Render a covering triangle into a 1×1 RGBA32F FBO.
///   4. Read the pixel back via `glReadPixels`.
///   5. Compare the four floats to a CPU oracle within tolerance.
///
/// `runShaderForVec4(fragSrc, uniforms)` collapses (1)-(4) into one
/// call so individual parity tests stay focused on the math.

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <variant>

namespace Vestige::Test
{

/// @brief Tagged uniform value: float / int / uint / vec2 / vec3 / vec4.
///        Avoids forcing every test to specialise a template.
using UniformValue = std::variant<
    float, int, unsigned int,
    glm::vec2, glm::vec3, glm::vec4>;

using UniformTable = std::unordered_map<std::string, UniformValue>;

/// @brief Compiled fragment shader + 1×1 RGBA32F FBO, ready to render.
///
/// Construct once per TEST so a TEST with N cases compiles GLSL once
/// (not N times). `run(uniforms)` binds the uniforms, draws a single
/// pixel, and reads back the four channels. Move-only.
///
/// On compile / link failure the constructor calls `ADD_FAILURE` and
/// leaves the program in an invalid state — `valid()` returns false
/// and `run()` returns `vec4(NaN)`. Caller should `ASSERT_TRUE(prog
/// .valid())` after construction.
class ShaderProgram
{
public:
    explicit ShaderProgram(const std::string& fragSrc);
    ~ShaderProgram();
    ShaderProgram(ShaderProgram&&) noexcept;
    ShaderProgram& operator=(ShaderProgram&&) noexcept;
    ShaderProgram(const ShaderProgram&)            = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    bool valid() const noexcept;
    glm::vec4 run(const UniformTable& uniforms);

private:
    void freeAll() noexcept;

    unsigned int m_prog = 0;  // GLuint without dragging glad into the header
    unsigned int m_fbo  = 0;
    unsigned int m_tex  = 0;
    unsigned int m_vao  = 0;
};

/// @brief One-shot convenience wrapper. Equivalent to
///        `ShaderProgram(fragSrc).run(uniforms)`. Use when a single
///        case is all that's needed (smoke tests, one-off checks).
///        For TEST blocks with multiple cases prefer `ShaderProgram`
///        directly — re-using a compiled program across cases avoids
///        a full GLSL compile+link per assertion.
glm::vec4 runShaderForVec4(
    const std::string& fragSrc,
    const UniformTable& uniforms = {});

/// @brief Read a GLSL source file from `${CMAKE_SOURCE_DIR}/assets/shaders`
///        (path resolved via the `VESTIGE_SHADER_DIR` compile definition,
///        same as `tests/test_gpu_cloth_simulator.cpp` uses for cloth
///        text-parity tests). Calls `ADD_FAILURE` and returns `""` on miss.
std::string readShaderFile(const std::string& basename);

/// @brief Extract a single top-level function (signature + body) by name
///        from a GLSL source string. Brace-counted, so nested blocks
///        inside the body don't confuse the slice. Used by parity tests
///        to copy a production helper verbatim into the test shader so
///        a drift in the production helper fails the parity assertion.
///
/// Returns the substring starting at the function's return type and
/// ending at the closing `}` (inclusive). Empty string + `ADD_FAILURE`
/// if the function isn't found. Brace-counting cost is microseconds
/// per call — not memoised because keying by `src.data()` is fragile
/// for callers passing temporaries, and keying by full source string
/// is more expensive than just re-parsing.
std::string extractGlslFunction(const std::string& src,
                                 const std::string& fnName);

}  // namespace Vestige::Test
