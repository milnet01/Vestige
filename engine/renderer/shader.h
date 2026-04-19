// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file shader.h
/// @brief OpenGL shader program compilation and management.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <map>
#include <string>
#include <string_view>

namespace Vestige
{

/// @brief Compiles and manages an OpenGL shader program (vertex + fragment, or compute).
class Shader
{
public:
    Shader();
    ~Shader();

    // Non-copyable (owns GPU resource)
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Movable
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    /// @brief Loads and compiles shaders from file paths.
    /// @param vertexPath Path to the vertex shader file.
    /// @param fragmentPath Path to the fragment shader file.
    /// @return True if compilation and linking succeeded.
    bool loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);

    /// @brief Loads and compiles a compute shader from a file path.
    /// @param computePath Path to the compute shader file.
    /// @return True if compilation and linking succeeded.
    bool loadComputeShader(const std::string& computePath);

    /// @brief Activates this shader program for rendering.
    void use() const;

    /// @brief Gets the OpenGL program ID.
    GLuint getId() const;

    /// @brief Releases the GL program now (safe to call before context teardown).
    void destroy();

    // Uniform setters take ``std::string_view`` so string literals and
    // ``const char*`` callers cost zero heap allocations on cache hits
    // (AUDIT H6). ``std::string`` arguments convert implicitly.

    /// @brief Sets a boolean uniform.
    void setBool(std::string_view name, bool value) const;

    /// @brief Sets an integer uniform.
    void setInt(std::string_view name, int value) const;

    /// @brief Sets a float uniform.
    void setFloat(std::string_view name, float value) const;

    /// @brief Sets a vec2 uniform.
    void setVec2(std::string_view name, const glm::vec2& value) const;

    /// @brief Sets a vec3 uniform.
    void setVec3(std::string_view name, const glm::vec3& value) const;

    /// @brief Sets a vec4 uniform.
    void setVec4(std::string_view name, const glm::vec4& value) const;

    /// @brief Sets a mat3 uniform.
    void setMat3(std::string_view name, const glm::mat3& value) const;

    /// @brief Sets a mat4 uniform.
    void setMat4(std::string_view name, const glm::mat4& value) const;

private:
    static GLuint compileShader(GLenum type, const std::string& source);
    bool linkProgram(GLuint vertexShader, GLuint fragmentShader);
    GLint getUniformLocation(std::string_view name) const;

    GLuint m_programId;
    // std::less<> is transparent so find(string_view) works without
    // constructing a std::string on every cache-hit lookup.
    mutable std::map<std::string, GLint, std::less<>> m_uniformCache;
};

} // namespace Vestige
