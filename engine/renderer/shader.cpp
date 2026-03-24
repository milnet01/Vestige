/// @file shader.cpp
/// @brief Shader implementation.
#include "renderer/shader.h"
#include "core/logger.h"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>

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

    // Read vertex shader source
    std::ifstream vertexFile(vertexPath);
    if (!vertexFile.is_open())
    {
        Logger::error("Failed to open vertex shader: " + vertexPath);
        return false;
    }
    std::stringstream vertexStream;
    vertexStream << vertexFile.rdbuf();
    std::string vertexSource = vertexStream.str();

    // Read fragment shader source
    std::ifstream fragmentFile(fragmentPath);
    if (!fragmentFile.is_open())
    {
        Logger::error("Failed to open fragment shader: " + fragmentPath);
        return false;
    }
    std::stringstream fragmentStream;
    fragmentStream << fragmentFile.rdbuf();
    std::string fragmentSource = fragmentStream.str();

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

    // Read compute shader source
    std::ifstream computeFile(computePath);
    if (!computeFile.is_open())
    {
        Logger::error("Failed to open compute shader: " + computePath);
        return false;
    }
    std::stringstream computeStream;
    computeStream << computeFile.rdbuf();
    std::string computeSource = computeStream.str();

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
        char infoLog[512];
        glGetProgramInfoLog(m_programId, sizeof(infoLog), nullptr, infoLog);
        Logger::error("Compute shader program linking failed: " + std::string(infoLog));
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

void Shader::setBool(const std::string& name, bool value) const
{
    glUniform1i(getUniformLocation(name), static_cast<int>(value));
}

void Shader::setInt(const std::string& name, int value) const
{
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) const
{
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const
{
    glUniform2fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const
{
    glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    glUniform4fv(getUniformLocation(name), 1, glm::value_ptr(value));
}

void Shader::setMat3(const std::string& name, const glm::mat3& value) const
{
    glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const
{
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
}

GLuint Shader::compileShader(GLenum type, const std::string& source)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
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
        char infoLog[512];
        glGetProgramInfoLog(m_programId, sizeof(infoLog), nullptr, infoLog);
        Logger::error("Shader program linking failed: " + std::string(infoLog));
        glDeleteProgram(m_programId);
        m_programId = 0;
        return false;
    }

    return true;
}

GLint Shader::getUniformLocation(const std::string& name) const
{
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end())
    {
        return it->second;
    }

    GLint location = glGetUniformLocation(m_programId, name.c_str());
    m_uniformCache[name] = location;

    if (location == -1)
    {
        Logger::debug("Uniform not found (may be optimized out): " + name);
    }
    return location;
}

} // namespace Vestige
