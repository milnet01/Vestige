/// @file shader.h
/// @brief OpenGL shader program compilation and management.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Compiles and manages an OpenGL shader program (vertex + fragment).
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

    /// @brief Activates this shader program for rendering.
    void use() const;

    /// @brief Gets the OpenGL program ID.
    GLuint getId() const;

    /// @brief Sets a boolean uniform.
    void setBool(const std::string& name, bool value) const;

    /// @brief Sets an integer uniform.
    void setInt(const std::string& name, int value) const;

    /// @brief Sets a float uniform.
    void setFloat(const std::string& name, float value) const;

    /// @brief Sets a vec2 uniform.
    void setVec2(const std::string& name, const glm::vec2& value) const;

    /// @brief Sets a vec3 uniform.
    void setVec3(const std::string& name, const glm::vec3& value) const;

    /// @brief Sets a vec4 uniform.
    void setVec4(const std::string& name, const glm::vec4& value) const;

    /// @brief Sets a mat3 uniform.
    void setMat3(const std::string& name, const glm::mat3& value) const;

    /// @brief Sets a mat4 uniform.
    void setMat4(const std::string& name, const glm::mat4& value) const;

private:
    GLuint compileShader(GLenum type, const std::string& source);
    bool linkProgram(GLuint vertexShader, GLuint fragmentShader);
    GLint getUniformLocation(const std::string& name) const;

    GLuint m_programId;
    mutable std::unordered_map<std::string, GLint> m_uniformCache;
};

} // namespace Vestige
