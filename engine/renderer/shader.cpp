// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file shader.cpp
/// @brief Shader implementation.
#include "renderer/shader.h"
#include "core/logger.h"
#include "utils/json_size_cap.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

namespace Vestige
{

Shader::Shader()
    : m_programId(0)
{
}

Shader::~Shader()
{
    if (m_programId != 0)
    {
        glDeleteProgram(m_programId);
    }
}

void Shader::destroy()
{
    if (m_programId != 0)
    {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
    m_uniformCache.clear();
}

Shader::Shader(Shader&& other) noexcept
    : m_programId(other.m_programId)
    , m_uniformCache(std::move(other.m_uniformCache))
{
    other.m_programId = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other)
    {
        if (m_programId != 0)
        {
            glDeleteProgram(m_programId);
        }
        m_programId = other.m_programId;
        m_uniformCache = std::move(other.m_uniformCache);
        other.m_programId = 0;
    }
    return *this;
}

bool Shader::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath)
{
    // Clean up previous program if reloading
    if (m_programId != 0)
    {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
    m_uniformCache.clear();

    // AUDIT M26: shader sources must stay bounded; a hostile/corrupt .glsl
    // file was previously slurped via ``<< rdbuf()`` with no cap. 8 MB is
    // comfortably above any real shader (engine ships ~20 KB worst case).
    constexpr std::uintmax_t MAX_SHADER_BYTES = 8ULL * 1024ULL * 1024ULL;
    auto vertexOpt = JsonSizeCap::loadTextFileWithSizeCap(
        vertexPath, "Shader (vertex)", MAX_SHADER_BYTES);
    if (!vertexOpt)
    {
        return false;
    }
    std::string vertexSource = std::move(*vertexOpt);

    auto fragmentOpt = JsonSizeCap::loadTextFileWithSizeCap(
        fragmentPath, "Shader (fragment)", MAX_SHADER_BYTES);
    if (!fragmentOpt)
    {
        return false;
    }
    std::string fragmentSource = std::move(*fragmentOpt);

    // Compile shaders
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (vertexShader == 0)
    {
        return false;
    }

    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        return false;
    }

    // Link program
    bool success = linkProgram(vertexShader, fragmentShader);

    // Shaders are linked into the program — delete the individual shader objects
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (success)
    {
        Logger::debug("Shader loaded: " + vertexPath + " + " + fragmentPath);
    }

    return success;
}

bool Shader::loadComputeShader(const std::string& computePath)
{
    // Clean up previous program if reloading
    if (m_programId != 0)
    {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
    m_uniformCache.clear();

    // Read compute shader source (8 MB cap — see loadFromFiles).
    constexpr std::uintmax_t MAX_SHADER_BYTES = 8ULL * 1024ULL * 1024ULL;
    auto computeOpt = JsonSizeCap::loadTextFileWithSizeCap(
        computePath, "Shader (compute)", MAX_SHADER_BYTES);
    if (!computeOpt)
    {
        return false;
    }
    std::string computeSource = std::move(*computeOpt);

    // Compile compute shader
    GLuint computeShader = compileShader(GL_COMPUTE_SHADER, computeSource);
    if (computeShader == 0)
    {
        return false;
    }

    // Link program (compute-only)
    m_programId = glCreateProgram();
    glAttachShader(m_programId, computeShader);
    glLinkProgram(m_programId);

    GLint success = 0;
    glGetProgramiv(m_programId, GL_LINK_STATUS, &success);
    glDeleteShader(computeShader);

    if (!success)
    {
        GLint logLength = 0;
        glGetProgramiv(m_programId, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(std::max(logLength, 1), '\0');
        glGetProgramInfoLog(m_programId, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
        Logger::error("Compute shader program linking failed: " + infoLog);
        glDeleteProgram(m_programId);
        m_programId = 0;
        return false;
    }

    Logger::debug("Compute shader loaded: " + computePath);
    return true;
}

void Shader::use() const
{
    glUseProgram(m_programId);
}

GLuint Shader::getId() const
{
    return m_programId;
}

void Shader::setBool(std::string_view name, bool value) const
{
    glUniform1i(getUniformLocation(name), static_cast<int>(value));
}

void Shader::setInt(std::string_view name, int value) const
{
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setFloat(std::string_view name, float value) const
{
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(std::string_view name, const glm::vec2& value) const
{
    glUniform2fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec3(std::string_view name, const glm::vec3& value) const
{
    glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec4(std::string_view name, const glm::vec4& value) const
{
    glUniform4fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setMat3(std::string_view name, const glm::mat3& value) const
{
    glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(std::string_view name, const glm::mat4& value) const
{
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

/*static*/ GLuint Shader::compileShader(GLenum type, const std::string& source)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(std::max(logLength, 1), '\0');
        glGetShaderInfoLog(shader, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
        std::string typeStr = (type == GL_VERTEX_SHADER) ? "vertex"
            : (type == GL_FRAGMENT_SHADER) ? "fragment" : "compute";
        Logger::error("Shader compilation failed (" + typeStr + "): " + infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

bool Shader::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    m_programId = glCreateProgram();
    glAttachShader(m_programId, vertexShader);
    glAttachShader(m_programId, fragmentShader);
    glLinkProgram(m_programId);

    GLint success = 0;
    glGetProgramiv(m_programId, GL_LINK_STATUS, &success);
    if (!success)
    {
        GLint logLength = 0;
        glGetProgramiv(m_programId, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(std::max(logLength, 1), '\0');
        glGetProgramInfoLog(m_programId, static_cast<GLsizei>(infoLog.size()), nullptr, infoLog.data());
        Logger::error("Shader program linking failed: " + infoLog);
        glDeleteProgram(m_programId);
        m_programId = 0;
        return false;
    }

    return true;
}

GLint Shader::getUniformLocation(std::string_view name) const
{
    // Transparent find — no std::string allocation on the hot-path cache hit.
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end())
    {
        return it->second;
    }

    // Miss: allocate once to build a null-terminated C string for glGet...
    // and to key the cache for subsequent hits.
    std::string key(name);
    GLint location = glGetUniformLocation(m_programId, key.c_str());
    m_uniformCache.emplace(std::move(key), location);

    if (location == -1)
    {
        Logger::debug("Uniform not found (may be optimized out): " + std::string(name));
    }
    return location;
}

} // namespace Vestige
