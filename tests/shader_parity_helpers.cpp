// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <glad/gl.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace Vestige::Test
{

namespace
{

/// Pass-through vertex shader: a single fullscreen triangle generated
/// from gl_VertexID, no VBO needed.
constexpr const char* kVertexSrc = R"(#version 450 core
void main()
{
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

GLuint compileShader(GLenum stage, const std::string& src, const char* label)
{
    GLuint sh = glCreateShader(stage);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);

    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 0 ? len : 1), 0);
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        ADD_FAILURE() << label << " compile failed:\n" << log.data();
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint linkProgram(GLuint vs, GLuint fs)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 0 ? len : 1), 0);
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        ADD_FAILURE() << "link failed:\n" << log.data();
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void setUniform(GLuint prog, const std::string& name, const UniformValue& v)
{
    GLint loc = glGetUniformLocation(prog, name.c_str());
    if (loc < 0)
    {
        // Allowed: shader optimised the uniform out. Silent — the test
        // is responsible for naming uniforms its shader actually reads.
        return;
    }
    std::visit([loc](auto&& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr      (std::is_same_v<T, float>)        glUniform1f (loc, x);
        else if constexpr (std::is_same_v<T, int>)          glUniform1i (loc, x);
        else if constexpr (std::is_same_v<T, unsigned int>) glUniform1ui(loc, x);
        else if constexpr (std::is_same_v<T, glm::vec2>)    glUniform2f (loc, x.x, x.y);
        else if constexpr (std::is_same_v<T, glm::vec3>)    glUniform3f (loc, x.x, x.y, x.z);
        else if constexpr (std::is_same_v<T, glm::vec4>)    glUniform4f (loc, x.x, x.y, x.z, x.w);
    }, v);
}

}  // namespace

glm::vec4 runShaderForVec4(const std::string& fragSrc, const UniformTable& uniforms)
{
    constexpr glm::vec4 kFail(std::numeric_limits<float>::quiet_NaN());

    GLuint vs = compileShader(GL_VERTEX_SHADER,   kVertexSrc, "vertex");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc,    "fragment");
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return kFail;
    }

    GLuint prog = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) return kFail;

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        ADD_FAILURE() << "FBO incomplete";
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        glDeleteProgram(prog);
        return kFail;
    }

    glViewport(0, 0, 1, 1);
    glUseProgram(prog);
    for (const auto& [name, value] : uniforms)
    {
        setUniform(prog, name, value);
    }

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glm::vec4 pixel(std::numeric_limits<float>::quiet_NaN());
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, &pixel);

    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteProgram(prog);

    return pixel;
}

std::string readShaderFile(const std::string& basename)
{
#ifndef VESTIGE_SHADER_DIR
    ADD_FAILURE() << "VESTIGE_SHADER_DIR compile definition missing — "
                     "check tests/CMakeLists.txt";
    return {};
#else
    std::filesystem::path p = std::filesystem::path(VESTIGE_SHADER_DIR) / basename;
    std::ifstream in(p);
    if (!in.good())
    {
        ADD_FAILURE() << "could not open shader file: " << p.string();
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
#endif
}

std::string extractGlslFunction(const std::string& src,
                                 const std::string& fnName)
{
    // Find a `<word> <word> ... <fnName>(` site that's at file scope (not
    // a call site inside another function). Heuristic: scan for the name
    // followed by `(`, then walk backwards across whitespace + identifier
    // chars to find the start of the return type.
    const std::string needle = fnName + "(";
    size_t pos = 0;
    while ((pos = src.find(needle, pos)) != std::string::npos)
    {
        // Backtrack to find the start of the line containing this name.
        size_t lineStart = src.rfind('\n', pos);
        lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;

        // A definition line has the form `<returnType> <fnName>(...)` —
        // a call site has either `<fnName>(...)` (assignment / expression)
        // or starts with whitespace followed by `<fnName>(...)` AND lives
        // inside a brace block. Cheap discriminator: scan forward from `(`
        // to `)` then check if the next non-space char is `{` (definition)
        // or `;` / something else (declaration / call).
        size_t openParen  = pos + fnName.size();
        size_t closeParen = src.find(')', openParen);
        if (closeParen == std::string::npos) { ++pos; continue; }
        size_t after = src.find_first_not_of(" \t\r\n", closeParen + 1);
        if (after == std::string::npos || src[after] != '{') { ++pos; continue; }

        // Walk forward from `{` matching braces to find the body's `}`.
        int depth = 0;
        size_t end = after;
        for (; end < src.size(); ++end)
        {
            if      (src[end] == '{') ++depth;
            else if (src[end] == '}') { --depth; if (depth == 0) break; }
        }
        if (depth != 0)
        {
            ADD_FAILURE() << "unbalanced braces in function: " << fnName;
            return {};
        }

        return src.substr(lineStart, end - lineStart + 1) + "\n";
    }

    ADD_FAILURE() << "function not found in shader source: " << fnName;
    return {};
}

}  // namespace Vestige::Test
